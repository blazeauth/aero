#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#ifdef AERO_USE_TLS
#if AERO_USE_WOLFSSL
#include <wolfssl/options.h>
#endif

#include <openssl/sha.h>
#endif

#include "aero/util/string.hpp"

namespace aero {

  constexpr inline int sha1_digest_size = 20;

  namespace detail {

#ifdef AERO_USE_TLS
    [[nodiscard]] inline std::array<std::byte, sha1_digest_size> tls_sha1(std::span<const std::byte> input) {
      std::array<std::byte, sha1_digest_size> result{};
      SHA_CTX context{};

      if (SHA1_Init(&context) != 1) {
        return {};
      }

      if (SHA1_Update(&context, reinterpret_cast<const unsigned char*>(input.data()), input.size()) != 1) {
        return {};
      }

      if (SHA1_Final(reinterpret_cast<unsigned char*>(result.data()), &context) != 1) {
        return {};
      }

      return result;
    }
#else
    class sha1 {
     public:
      static std::array<std::byte, sha1_digest_size> hash(std::span<const std::byte> data) noexcept {
        return sha1{}.update(data).final();
      }

      static std::array<std::byte, sha1_digest_size> hash(std::string_view str) noexcept {
        return sha1{}.update(str).final();
      }

      static std::string hash_to_hex(std::span<const std::byte> data) {
        return sha1{}.update(data).final_hex();
      }

      static std::string hash_to_hex(std::string_view str) {
        return sha1{}.update(str).final_hex();
      }

      sha1& update(std::span<const std::byte> data) noexcept {
        size_t i = 0;
        size_t j = (count_[0] >> 3U) & 63U;
        auto len = data.size();
        const auto* data_ptr = data.data();
        auto* buffer_ptr = buffer_.data();

        count_[0] += static_cast<std::uint32_t>(len << 3U);
        if (count_[0] < (len << 3U)) {
          count_[1]++;
        }
        count_[1] += (len >> 29U);

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

      sha1& update(std::string_view str) noexcept {
        return update(std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()});
      }

      void final(std::span<std::byte, sha1_digest_size> digest) noexcept {
        std::array<std::byte, 8> finalcount{};

        for (std::size_t byte_index{}; byte_index < finalcount.size(); ++byte_index) {
          const auto count_word_index = (byte_index >= 4) ? 0 : 1;
          const auto word_byte_offset = 3 - (byte_index & 3U);
          const auto shift_bits = word_byte_offset * 8;

          const auto selected_count_word = count_[count_word_index];
          const auto extracted_byte = (selected_count_word >> shift_bits) & 0xFFU;

          finalcount[byte_index] = static_cast<std::byte>(extracted_byte);
        }

        update({reinterpret_cast<const std::byte*>("\200"), 1});
        while ((count_[0] & 504U) != 448) {
          update({reinterpret_cast<const std::byte*>("\0"), 1});
        }

        update({finalcount.data(), 8});

        for (std::size_t i{}; i < digest.size(); i++) {
          digest[i] = static_cast<std::byte>((state_[i >> 2U] >> ((3 - (i & 3U)) * 8)) & 255U);
        }
      }

      [[nodiscard]] std::array<std::byte, sha1_digest_size> final() noexcept {
        std::array<std::byte, sha1_digest_size> digest{};
        final(std::span<std::byte, sha1_digest_size>(digest.data(), sha1_digest_size));
        return digest;
      }

      [[nodiscard]] std::string final_hex() {
        return aero::to_hex_string(final());
      }

     private:
      void process(std::span<const std::byte, 64> data) noexcept {
        std::array<std::uint32_t, 80> w{};
        std::uint32_t a{};
        std::uint32_t b{};
        std::uint32_t c{};
        std::uint32_t d{};
        std::uint32_t e{};
        std::uint32_t temp{};

        for (std::size_t word_index = 0; word_index < 16; ++word_index) {
          const std::size_t byte_index = word_index * 4;

          const auto [first, second, third, fourth] = std::tuple{
            std::to_integer<std::uint32_t>(data[byte_index]) << 24U,
            std::to_integer<std::uint32_t>(data[byte_index + 1]) << 16U,
            std::to_integer<std::uint32_t>(data[byte_index + 2]) << 8U,
            std::to_integer<std::uint32_t>(data[byte_index + 3]),
          };

          w[word_index] = first | second | third | fourth;
        }

        for (std::size_t i{16}; i < w.size(); i++) {
          w[i] = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
          w[i] = (w[i] << 1U) | (w[i] >> 31U);
        }

        a = state_[0];
        b = state_[1];
        c = state_[2];
        d = state_[3];
        e = state_[4];

        for (std::size_t i{}; i < w.size(); i++) {
          if (i < sha1_digest_size) {
            temp = ((a << 5U) | (a >> 27U)) + ((b & c) | (~b & d)) + e + w[i] + 0x5A827999U;
          } else if (i < 40) {
            temp = ((a << 5U) | (a >> 27U)) + (b ^ c ^ d) + e + w[i] + 0x6ED9EBA1U;
          } else if (i < 60) {
            temp = ((a << 5U) | (a >> 27U)) + ((b & c) | (b & d) | (c & d)) + e + w[i] + 0x8F1BBCDCU;
          } else {
            temp = ((a << 5U) | (a >> 27U)) + (b ^ c ^ d) + e + w[i] + 0xCA62C1D6U;
          }

          e = d;
          d = c;
          c = (b << 30U) | (b >> 2U);
          b = a;
          a = temp;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
      }

      std::array<std::uint32_t, 5> state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
      std::array<std::uint32_t, 2> count_{};
      std::array<std::byte, 64> buffer_{};
    };
#endif

  } // namespace detail

  inline std::array<std::byte, aero::sha1_digest_size> sha1(std::span<const std::byte> input) {
#ifdef AERO_USE_TLS
    return aero::detail::tls_sha1(input);
#else
    return aero::detail::sha1::hash(input);
#endif
  }

  inline std::array<std::byte, aero::sha1_digest_size> sha1(std::string_view input) {
    return sha1(std::span{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  }

} // namespace aero
