#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace aero {

  class utf8_stream_validator {
   public:
    void reset() noexcept {
      remaining_ = 0;
      lower_bound_ = 0x80;
      upper_bound_ = 0xBF;
      valid_ = true;
    }

    bool consume(std::string_view str) noexcept {
      if (!valid_) [[unlikely]] {
        return false;
      }

      if (str.empty()) {
        return true;
      }

      const auto* it = reinterpret_cast<const std::uint8_t*>(str.data());
      const std::uint8_t* end = it + str.size();

      // Copy state to locals to avoid intermediate stores to 'this' in the hot loop.
      // When std::uint8_t == unsigned char, '*it' may alias the object representation of
      // this validator, so the compiler cannot reliably move direct member updates
      // out of the loop on its own since loop reads '*it' between writes
      std::uint32_t remaining = remaining_;
      std::uint32_t lower_bound = lower_bound_;
      std::uint32_t upper_bound = upper_bound_;
      const bool had_pending = remaining != 0;

      // First finish a codepoint that was saved from the previous .consume()
      if (remaining != 0) [[unlikely]] {
        if (!consume_pending(it, end, remaining, lower_bound, upper_bound)) {
          return fail();
        }

        if (it == end) {
          store_pending(remaining, lower_bound, upper_bound);
          return true;
        }
      }

      // Small chunks probably won't benefit much from the wider bulk loop
      if (static_cast<size_t>(end - it) <= 32) {
        if (!consume_small(it, end, remaining, lower_bound, upper_bound)) [[unlikely]] {
          return fail();
        }

        if (remaining != 0 || had_pending) {
          store_pending(remaining, lower_bound, upper_bound);
        }

        return true;
      }

      // Bulk path. Four bytes for checking any complete UTF-8 code point safely
      while (static_cast<size_t>(end - it) >= 4) {
        std::uint32_t byte = *it;

        // Avoid wide ASCII probes when already at a non-ASCII byte
        if (byte < 0x80) {
          it = skip_ascii_adaptive(it, end);
          if (static_cast<size_t>(end - it) < 4) {
            break;
          }

          byte = *it;
        }

        // Non-ASCII, keep validating full codepoints until ASCII
        // appears or the safe bulk window ends
        do {
          if (!consume_full_non_ascii(it, byte)) [[unlikely]] {
            return fail();
          }

          if (static_cast<size_t>(end - it) < 4) {
            break;
          }

          byte = *it;
        } while (byte >= 0x80);
      }

      // Finish the last 0..3 bytes and save incomplete codepoint if present
      if (!consume_tail(it, end, remaining, lower_bound, upper_bound)) [[unlikely]] {
        return fail();
      }

      if (remaining != 0 || had_pending) {
        store_pending(remaining, lower_bound, upper_bound);
      }

      return true;
    }

    bool consume(std::span<const std::byte> bytes) noexcept {
      return consume(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    }

    [[nodiscard]] bool complete() const noexcept {
      return valid_ && remaining_ == 0;
    }

   private:
    static consteval std::uint64_t repeat_byte8(std::uint8_t repeat) {
      return 0x0101010101010101ULL * repeat;
    }

    static bool is_continuation(std::uint32_t byte) noexcept {
      return (byte & 0xC0U) == 0x80;
    }

    static bool is_in_range(std::uint32_t byte, std::uint32_t lower_bound, std::uint32_t upper_bound) noexcept {
      return byte - lower_bound <= upper_bound - lower_bound;
    }

    static std::uint64_t load_u64(const std::uint8_t* ptr) noexcept {
      std::uint64_t value; // NOLINT
      std::memcpy(&value, ptr, sizeof(value));
      return value;
    }

    static size_t first_non_ascii_offset(const std::uint64_t high_bits) noexcept {
      // Offset of the first non-ASCII byte within the 8-byte word
      if constexpr (std::endian::native == std::endian::big) {
        return static_cast<std::size_t>(static_cast<unsigned int>(std::countl_zero(high_bits)) >> 3U);
      } else {
        return static_cast<std::size_t>(static_cast<unsigned int>(std::countr_zero(high_bits)) >> 3U);
      }
    }

    static const std::uint8_t* skip_ascii8(const std::uint8_t* it, const std::uint8_t* end) noexcept {
      constexpr std::uint64_t mask = repeat_byte8(0x80);

      // Cheap scanner for small buffers
      while (static_cast<size_t>(end - it) >= 8) {
        const std::uint64_t high_bits = load_u64(it) & mask;
        if (high_bits != 0) {
          return it + first_non_ascii_offset(high_bits);
        }

        it += 8;
      }

      while (it != end && *it < 0x80) {
        ++it;
      }

      return it;
    }

    static const std::uint8_t* skip_ascii_adaptive(const std::uint8_t* it, const std::uint8_t* end) noexcept {
      constexpr std::uint64_t mask = repeat_byte8(0x80);

      // Probe the first few chunks one at a time. This will make short ASCII
      // runs cheaper, which matters for mixed cases
      for (uint32_t checked_chunks = 0; checked_chunks < 4; ++checked_chunks) {
        if (static_cast<size_t>(end - it) < 8) {
          while (it != end && *it < 0x80) {
            ++it;
          }

          return it;
        }

        const std::uint64_t high_bits = load_u64(it) & mask;
        if (high_bits != 0) {
          return it + first_non_ascii_offset(high_bits);
        }

        it += 8;
      }

      // After 32 ASCII bytes, assume this is a longer ASCII run and use the
      // wider scanner to reduce loop overhead
      while (static_cast<size_t>(end - it) >= 32) {
        const std::uint64_t first_high_bits = load_u64(it) & mask;
        const std::uint64_t second_high_bits = load_u64(it + 8) & mask;
        const std::uint64_t third_high_bits = load_u64(it + 16) & mask;
        const std::uint64_t fourth_high_bits = load_u64(it + 24) & mask;

        if ((first_high_bits | second_high_bits | third_high_bits | fourth_high_bits) == 0) {
          it += 32;
          continue;
        }

        if (first_high_bits != 0) {
          return it + first_non_ascii_offset(first_high_bits);
        }

        if (second_high_bits != 0) {
          return it + 8 + first_non_ascii_offset(second_high_bits);
        }

        if (third_high_bits != 0) {
          return it + 16 + first_non_ascii_offset(third_high_bits);
        }

        return it + 24 + first_non_ascii_offset(fourth_high_bits);
      }

      return skip_ascii8(it, end);
    }

    static bool consume_full_non_ascii(const std::uint8_t*& it, const std::uint32_t byte0) noexcept {
      // Called only when at least four bytes remaining, so
      // no boundary checks are needed here

      if ((byte0 & 0xE0U) == 0xC0) {
        const std::uint32_t byte1 = it[1];

        // C0/C1 would be overlong encodings for ASCII
        if (((byte1 & 0xC0U) != 0x80) || ((byte0 & 0x1EU) == 0)) [[unlikely]] {
          return false;
        }

        it += 2;
        return true;
      }

      if ((byte0 & 0xF0U) == 0xE0) {
        const std::uint32_t byte1 = it[1];
        const std::uint32_t byte2 = it[2];

        // E0 requires A0-BF to reject overlong 3-byte sequences.
        // ED requires 80-9F to reject UTF-16 surrogate codepoints
        if (((byte1 & 0xC0U) != 0x80) || ((byte2 & 0xC0U) != 0x80) || (byte0 == 0xE0 && (byte1 & 0x20U) == 0) ||
            (byte0 == 0xED && (byte1 & 0x20U) != 0)) [[unlikely]] {
          return false;
        }

        it += 3;
        return true;
      }

      if ((byte0 & 0xF8U) == 0xF0) {
        const std::uint32_t byte1 = it[1];
        const std::uint32_t byte2 = it[2];
        const std::uint32_t byte3 = it[3];

        // F5..FF are invalid. F0 requires 90-BF to reject overlong sequences.
        // F4 requires 80-8F to keep the decoded value within U+10FFFF
        if (((byte0 & 0x07U) >= 0x05U) || ((byte1 & 0xC0U) != 0x80) || ((byte2 & 0xC0U) != 0x80) || ((byte3 & 0xC0U) != 0x80) ||
            (byte0 == 0xF0 && (byte1 & 0x30U) == 0) || (byte0 == 0xF4 && byte1 > 0x8F)) [[unlikely]] {
          return false;
        }

        it += 4;
        return true;
      }

      return false;
    }

    static bool consume_non_ascii_checked(const std::uint8_t*& it, const std::uint8_t* end, std::uint32_t& remaining,
      std::uint32_t& lower_bound, std::uint32_t& upper_bound) noexcept {
      // Boundary-safe version for small chunks and tails.
      // May leave pending state instead of failing at the buffer end

      const std::uint32_t byte0 = *it++;

      if ((byte0 & 0xE0U) == 0xC0) {
        // C0/C1 would be overlong encodings for ASCII
        if ((byte0 & 0x1EU) == 0) [[unlikely]] {
          return false;
        }

        if (it == end) {
          remaining = 1;
          lower_bound = 0x80;
          upper_bound = 0xBF;
          return true;
        }

        const std::uint32_t byte1 = *it++;

        if (!is_continuation(byte1)) [[unlikely]] {
          return false;
        }

        return true;
      }

      if ((byte0 & 0xF0U) == 0xE0) {
        // Normal 3-byte starts use 80-BF for the first continuation.
        // E0 and ED are special to preserve shortest form and reject surrogates
        const std::uint32_t first_lower_bound = byte0 == 0xE0 ? 0xA0 : 0x80;
        const std::uint32_t first_upper_bound = byte0 == 0xED ? 0x9F : 0xBF;

        // Not enough bytes to finish this codepoint in the current buffer.
        // Validate what is available and keep the remaining bounds as state
        if (static_cast<size_t>(end - it) < 2) [[unlikely]] {
          remaining = 2;
          lower_bound = first_lower_bound;
          upper_bound = first_upper_bound;
          return consume_pending(it, end, remaining, lower_bound, upper_bound);
        }

        const std::uint32_t byte1 = *it++;
        const std::uint32_t byte2 = *it++;

        if (!is_in_range(byte1, first_lower_bound, first_upper_bound) || !is_continuation(byte2)) [[unlikely]] {
          return false;
        }

        return true;
      }

      if ((byte0 & 0xF8U) == 0xF0) {
        // F5..FF are not valid UTF-8 lead bytes.
        if ((byte0 & 0x07U) >= 0x05) [[unlikely]] {
          return false;
        }

        // Normal 4-byte starts use 80-BF for the first continuation.
        // F0 rejects overlong sequences
        const std::uint32_t first_lower_bound = byte0 == 0xF0 ? 0x90 : 0x80;
        // F4 rejects values above U+10FFFF
        const std::uint32_t first_upper_bound = byte0 == 0xF4 ? 0x8F : 0xBF;

        // Same split-sequence handling as the 3-byte path but with three
        // continuation bytes in total
        if (static_cast<size_t>(end - it) < 3) [[unlikely]] {
          remaining = 3;
          lower_bound = first_lower_bound;
          upper_bound = first_upper_bound;
          return consume_pending(it, end, remaining, lower_bound, upper_bound);
        }

        const std::uint32_t byte1 = *it++;
        const std::uint32_t byte2 = *it++;
        const std::uint32_t byte3 = *it++;

        if (!is_in_range(byte1, first_lower_bound, first_upper_bound) || !is_continuation(byte2) || !is_continuation(byte3))
          [[unlikely]] {
          return false;
        }

        return true;
      }

      return false;
    }

    static bool consume_small(const std::uint8_t*& it, const std::uint8_t* end, std::uint32_t& remaining,
      std::uint32_t& lower_bound, std::uint32_t& upper_bound) noexcept {
      // Small-buffer path. Avoid the larger bulk-loop setup and still
      // use 8-byte ASCII skipping when useful
      while (it != end) {
        if (*it < 0x80) {
          it = skip_ascii8(it, end);

          if (it == end) {
            return true;
          }
        }

        if (!consume_non_ascii_checked(it, end, remaining, lower_bound, upper_bound)) [[unlikely]] {
          return false;
        }

        if (remaining != 0) {
          return true;
        }
      }

      return true;
    }

    static bool consume_tail(const std::uint8_t*& it, const std::uint8_t* end, std::uint32_t& remaining,
      std::uint32_t& lower_bound, std::uint32_t& upper_bound) noexcept {
      // Tail normally should be 0..3 bytes after bulk loop
      while (it != end) {
        if (*it < 0x80) {
          ++it;
          continue;
        }

        if (!consume_non_ascii_checked(it, end, remaining, lower_bound, upper_bound)) [[unlikely]] {
          return false;
        }

        if (remaining != 0) {
          return true;
        }
      }

      return true;
    }

    static bool consume_pending(const std::uint8_t*& it, const std::uint8_t* end, std::uint32_t& remaining,
      std::uint32_t& lower_bound, std::uint32_t& upper_bound) noexcept {
      // Continue a partially consumed codepoint.
      // Only the first continuation byte may have a tightened bound
      while (remaining != 0 && it != end) {
        const std::uint32_t byte = *it++;
        if (!is_in_range(byte, lower_bound, upper_bound)) [[unlikely]] {
          return false;
        }

        --remaining;
        lower_bound = 0x80;
        upper_bound = 0xBF;
      }

      return true;
    }

    void store_pending(const std::uint32_t remaining, const std::uint32_t lower_bound,
      const std::uint32_t upper_bound) noexcept {
      remaining_ = static_cast<uint8_t>(remaining);
      lower_bound_ = static_cast<uint8_t>(lower_bound);
      upper_bound_ = static_cast<uint8_t>(upper_bound);
    }

    bool fail() noexcept {
      valid_ = false;
      return false;
    }

    std::uint8_t remaining_{};
    std::uint8_t lower_bound_{0x80};
    std::uint8_t upper_bound_{0xBF};
    bool valid_{true};
  };

  [[nodiscard]] inline bool is_valid_utf8(std::string_view str) noexcept {
    utf8_stream_validator validator;
    return validator.consume(str) && validator.complete();
  }

  [[nodiscard]] inline bool is_valid_utf8(std::span<const std::byte> bytes) noexcept {
    return is_valid_utf8(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  }

  namespace detail {

    struct utf8_sequence_shape {
      std::uint32_t continuations; // 0 marks a byte that cannot lead a sequence
      std::uint8_t first_lower_bound;
      std::uint8_t first_upper_bound;
    };

    [[nodiscard]] constexpr utf8_sequence_shape utf8_classify_lead(std::uint8_t lead) noexcept {
      // UTF8-octets = *( UTF8-char )
      // UTF8-char   = UTF8-1 / UTF8-2 / UTF8-3 / UTF8-4
      // UTF8-1      = %x00-7F
      // UTF8-2      = %xC2-DF UTF8-tail
      // UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
      //               %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
      // UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
      //               %xF4 %x80-8F 2( UTF8-tail )
      // UTF8-tail   = %x80-BF

      // UTF8-2
      if (lead >= 0xC2 && lead <= 0xDF) {
        return {1, 0x80, 0xBF};
      }

      if (lead == 0xE0) {
        return {2, 0xA0, 0xBF};
      }
      if (lead == 0xED) {
        return {2, 0x80, 0x9F};
      }
      if (lead >= 0xE1 && lead <= 0xEF) {
        return {2, 0x80, 0xBF};
      }

      // UTF8-4
      if (lead == 0xF0) {
        return {3, 0x90, 0xBF};
      }
      if (lead == 0xF4) {
        return {3, 0x80, 0x8F};
      }
      if (lead >= 0xF1 && lead <= 0xF3) {
        return {3, 0x80, 0xBF};
      }

      return {0, 0, 0}; // 0x80..0xC1 and 0xF5..0xFF
    }

  } // namespace detail

  [[nodiscard]] inline std::string utf8_replace_ill_formed(std::string_view bytes) {
    constexpr std::string_view replacement = "\xEF\xBF\xBD"; // U+FFFD

    std::string output;
    output.reserve(bytes.size());

    std::size_t position = 0;
    while (position < bytes.size()) {
      const auto lead = static_cast<std::uint8_t>(bytes[position]);
      if (lead < 0x80) {
        output.push_back(bytes[position]);
        ++position;
        continue;
      }

      const detail::utf8_sequence_shape shape = detail::utf8_classify_lead(lead);
      if (shape.continuations == 0) {
        output.append(replacement);
        ++position;
        continue;
      }

      // Walk the continuations. Cursor stops on the first byte that does not
      // belong to this sequence
      std::size_t cursor = position + 1;
      std::uint8_t lower_bound = shape.first_lower_bound;
      std::uint8_t upper_bound = shape.first_upper_bound;
      bool well_formed = true;

      for (std::uint32_t i{}; i < shape.continuations; i++, cursor++) {
        if (cursor == bytes.size()) {
          well_formed = false; // truncated at end of input
          break;
        }

        const auto continuation = static_cast<std::uint8_t>(bytes[cursor]);
        if (continuation < lower_bound || continuation > upper_bound) {
          well_formed = false;
          break;
        }

        lower_bound = 0x80;
        upper_bound = 0xBF;
      }

      output.append(well_formed ? bytes.substr(position, cursor - position) : replacement);
      position = cursor;
    }

    return output;
  }

} // namespace aero
