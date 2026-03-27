#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "aero/detail/concepts.hpp"
#include "aero/detail/string.hpp"

#if AERO_USE_WOLFSSL
#if __has_include(<wolfssl/options.h>)
#include <wolfssl/options.h>
#endif
#include <wolfssl/wolfcrypt/sha256.h>
#elif AERO_USE_OPENSSL
#include <openssl/evp.h>
#endif

namespace aero::tls {

  class sha256 {
   public:
    constexpr static std::size_t digest_size = 32;

    static std::array<std::byte, digest_size> hash(std::span<const std::byte> data) {
      return sha256{}.update(data).final();
    }

    static std::array<std::byte, digest_size> hash(std::string_view str) {
      return sha256{}.update(str).final();
    }

    static std::string hash_to_hex(std::span<const std::byte> data) {
      return sha256{}.update(data).final_hex();
    }

    static std::string hash_to_hex(std::string_view str) {
      return sha256{}.update(str).final_hex();
    }

    sha256() {
      ensure_initialized();
    }

    sha256(const sha256&) = delete;
    sha256& operator=(const sha256&) = delete;

    sha256(sha256&& other) noexcept {
      move_from(other);
    }

    sha256& operator=(sha256&& other) noexcept {
      if (this != &other) {
        cleanup();
        move_from(other);
      }
      return *this;
    }

    ~sha256() noexcept {
      cleanup();
    }

    sha256& update(std::span<const std::byte> data) {
      ensure_initialized();

#if AERO_USE_OPENSSL
      if (EVP_DigestUpdate(digest_context_, data.data(), data.size()) != 1) {
        throw_error(std::errc::state_not_recoverable);
      }
#elif AERO_USE_WOLFSSL
      auto remaining = data;
      const auto max_chunk = static_cast<std::size_t>((std::numeric_limits<word32>::max)());

      while (!remaining.empty()) {
        const auto chunk_size = (std::min)(remaining.size(), max_chunk);
        const auto* chunk_ptr = reinterpret_cast<const byte*>(remaining.data());
        if (wc_Sha256Update(&wolf_context_, chunk_ptr, static_cast<word32>(chunk_size)) != 0) {
          throw_error(std::errc::state_not_recoverable);
        }
        remaining = remaining.subspan(chunk_size);
      }
#endif

      return *this;
    }

    sha256& update(std::string_view str) {
      return update(std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()});
    }

    void final(std::span<std::byte, digest_size> digest) {
      ensure_initialized();

#if AERO_USE_OPENSSL
      unsigned int output_size = 0;
      if (EVP_DigestFinal_ex(digest_context_, reinterpret_cast<unsigned char*>(digest.data()), &output_size) != 1) {
        throw_error(std::errc::state_not_recoverable);
      }
      if (output_size != digest_size) {
        throw_error(std::errc::state_not_recoverable);
      }
      cleanup();
#elif AERO_USE_WOLFSSL
      if (wc_Sha256Final(&wolf_context_, reinterpret_cast<byte*>(digest.data())) != 0) {
        throw_error(std::errc::state_not_recoverable);
      }
      cleanup();
#endif
    }

    template <aero::detail::concepts::either<std::byte, uint8_t, char> ByteT>
    void final(std::array<ByteT, digest_size>& digest) {
      final(std::span<ByteT, digest_size>(digest.data(), digest.size()));
    }

    template <aero::detail::concepts::either<uint8_t, char> ByteT>
    void final(std::span<ByteT, digest_size> digest) {
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
    static void throw_error(std::errc cond) {
      throw std::system_error(std::make_error_code(cond));
    }

    void ensure_initialized() {
      if (is_initialized_) {
        return;
      }

#if AERO_USE_OPENSSL
      digest_context_ = EVP_MD_CTX_new();
      if (digest_context_ == nullptr) {
        throw_error(std::errc::not_enough_memory);
      }
      if (EVP_DigestInit_ex(digest_context_, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(digest_context_);
        digest_context_ = nullptr;
        throw_error(std::errc::state_not_recoverable);
      }
      is_initialized_ = true;
#elif AERO_USE_WOLFSSL
      std::memset(&wolf_context_, 0, sizeof(wolf_context_));
      if (wc_InitSha256(&wolf_context_) != 0) {
        throw_error(std::errc::state_not_recoverable);
      }
      is_initialized_ = true;
#endif
    }

    void cleanup() noexcept {
#if AERO_USE_OPENSSL
      if (digest_context_ != nullptr) {
        EVP_MD_CTX_free(digest_context_);
        digest_context_ = nullptr;
      }
      is_initialized_ = false;
#elif AERO_USE_WOLFSSL
      if (is_initialized_) {
        wc_Sha256Free(&wolf_context_);
      }
      std::memset(&wolf_context_, 0, sizeof(wolf_context_));
      is_initialized_ = false;
#endif
    }

    void move_from(sha256& other) noexcept {
#if AERO_USE_OPENSSL
      digest_context_ = other.digest_context_;
      is_initialized_ = other.is_initialized_;
      other.digest_context_ = nullptr;
      other.is_initialized_ = false;
#elif AERO_USE_WOLFSSL
      wolf_context_ = other.wolf_context_;
      is_initialized_ = other.is_initialized_;
      std::memset(&other.wolf_context_, 0, sizeof(other.wolf_context_));
      other.is_initialized_ = false;
#endif
    }

#if AERO_USE_OPENSSL
    EVP_MD_CTX* digest_context_ = nullptr;
#elif AERO_USE_WOLFSSL
    wc_Sha256 wolf_context_{};
#endif

    bool is_initialized_ = false;
  };

} // namespace aero::tls
