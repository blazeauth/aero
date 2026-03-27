#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "aero/detail/concepts.hpp"
#include "aero/detail/string.hpp"

namespace aero::detail {

  // NOLINTBEGIN(*-magic-numbers, *-signed-bitwise)

  class sha1 {
   public:
    constexpr static std::size_t digest_size = 20;

    static std::array<std::byte, digest_size> hash(std::span<const std::byte> data) {
      return sha1{}.update(data).final();
    }

    static std::array<std::byte, digest_size> hash(std::string_view str) {
      return sha1{}.update(str).final();
    }

    static std::string hash_to_hex(std::span<const std::byte> data) {
      return sha1{}.update(data).final_hex();
    }

    static std::string hash_to_hex(std::string_view str) {
      return sha1{}.update(str).final_hex();
    }

    sha1& update(std::span<const std::byte> data) {
      size_t i = 0;
      size_t j = (count_[0] >> 3) & 63;
      auto len = data.size();
      const auto* data_ptr = data.data();
      auto* buffer_ptr = buffer_.data();

      count_[0] += static_cast<uint32_t>(len << 3);
      if (count_[0] < (len << 3)) {
        count_[1]++;
      }
      count_[1] += (len >> 29);

      if ((j + len) > 63) {
        std::memcpy(&buffer_ptr[j], data_ptr, (i = 64 - j));
        process(buffer_);
        for (; i + 63 < len; i += 64) {
          const std::span data_to_process(&data_ptr[i], len - i);
          process(data_to_process.first<64>());
        }
        j = 0;
      }

      if (len > i) {
        std::memcpy(buffer_.data() + j, data_ptr + i, len - i);
      }

      return *this;
    }

    sha1& update(std::string_view str) {
      return update(std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()});
    }

    void final(std::span<std::byte, digest_size> digest) {
      std::array<std::byte, 8> finalcount{};

      for (std::size_t byte_index{}; byte_index < finalcount.size(); ++byte_index) {
        const auto count_word_index = (byte_index >= 4) ? 0 : 1;
        const auto word_byte_offset = 3 - (byte_index & 3);
        const auto shift_bits = word_byte_offset * 8;

        const auto selected_count_word = count_[count_word_index];
        const auto extracted_byte = (selected_count_word >> shift_bits) & 0xFF;

        finalcount[byte_index] = static_cast<std::byte>(extracted_byte);
      }

      update({reinterpret_cast<const std::byte*>("\200"), 1});
      while ((count_[0] & 504) != 448) {
        update({reinterpret_cast<const std::byte*>("\0"), 1});
      }

      update({finalcount.data(), 8});

      for (std::size_t i{}; i < digest.size(); i++) {
        digest[i] = static_cast<std::byte>((state_[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
      }
    }

    template <aero::detail::concepts::either<std::byte, uint8_t, char> ByteT>
    void final(std::array<std::byte, digest_size>& digest) {
      final(std::span<std::byte, digest_size>(reinterpret_cast<std::byte*>(digest.data()), digest_size));
    }

    template <aero::detail::concepts::either<uint8_t, char> ByteT>
    void final(std::span<std::byte, digest_size> digest) {
      final(std::span<std::byte, digest_size>(reinterpret_cast<std::byte*>(digest.data()), digest_size));
    }

    [[nodiscard]] std::array<std::byte, digest_size> final() {
      std::array<std::byte, digest_size> digest{};
      final(std::span<std::byte, digest_size>(digest.data(), digest_size));
      return digest;
    }

    [[nodiscard]] std::string final_hex() {
      return aero::detail::to_hex_string(final());
    }

   private:
    void process(std::span<const std::byte, 64> data) {
      std::array<uint32_t, 80> w{};
      uint32_t a{};
      uint32_t b{};
      uint32_t c{};
      uint32_t d{};
      uint32_t e{};
      uint32_t temp{};

      for (std::size_t word_index = 0; word_index < 16; ++word_index) {
        const std::size_t byte_index = word_index * 4;

        const auto [first, second, third, fourth] = std::tuple{
          std::to_integer<std::uint32_t>(data[byte_index]) << 24,
          std::to_integer<std::uint32_t>(data[byte_index + 1]) << 16,
          std::to_integer<std::uint32_t>(data[byte_index + 2]) << 8,
          std::to_integer<std::uint32_t>(data[byte_index + 3]),
        };

        w[word_index] = first | second | third | fourth;
      }

      for (std::size_t i{16}; i < w.size(); i++) {
        w[i] = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (w[i] << 1) | (w[i] >> 31);
      }

      a = state_[0];
      b = state_[1];
      c = state_[2];
      d = state_[3];
      e = state_[4];

      for (std::size_t i{}; i < w.size(); i++) {
        if (i < digest_size) {
          temp = ((a << 5) | (a >> 27)) + ((b & c) | (~b & d)) + e + w[i] + 0x5A827999;
        } else if (i < 40) {
          temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1;
        } else if (i < 60) {
          temp = ((a << 5) | (a >> 27)) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDC;
        } else {
          temp = ((a << 5) | (a >> 27)) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6;
        }

        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
      }

      state_[0] += a;
      state_[1] += b;
      state_[2] += c;
      state_[3] += d;
      state_[4] += e;
    }

    std::array<uint32_t, 5> state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::array<uint32_t, 2> count_{};
    std::array<std::byte, 64> buffer_{};
  };

  // NOLINTEND(*-magic-numbers, *-signed-bitwise)

} // namespace aero::detail
