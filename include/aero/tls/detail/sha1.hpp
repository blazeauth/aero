#pragma once

#include <array>
#include <span>

#if AERO_USE_WOLFSSL
#include <wolfssl/options.h>
#endif

#include <openssl/sha.h>

namespace aero::tls::detail {

  constexpr inline auto sha1_digest_size = 20;

  [[nodiscard]] inline std::array<std::byte, sha1_digest_size> sha1(std::span<const std::byte> input) {
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

} // namespace aero::tls::detail
