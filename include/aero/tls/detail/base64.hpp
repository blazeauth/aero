#pragma once

#include <span>
#include <string>

#if AERO_USE_OPENSSL
#include <openssl/evp.h>
#elif AERO_USE_WOLFSSL
#if __has_include(<wolfssl/options.h>)
#include <wolfssl/options.h>
#endif
#include <wolfssl/wolfcrypt/coding.h>
#endif

namespace aero::tls::detail {

  [[nodiscard]] inline std::string base64_encode(std::span<const std::byte> input) {
    if (input.empty()) {
      return {};
    }

    const std::size_t expected_base64_size = 4 * ((input.size() + 2) / 3);
    std::string base64_bytes;
    base64_bytes.resize(expected_base64_size + 1);

    const auto* input_ptr = reinterpret_cast<const uint8_t*>(input.data());
    auto* output_ptr = reinterpret_cast<uint8_t*>(base64_bytes.data());
    uint32_t encoded_length = 0;

#ifdef AERO_USE_WOLFSSL
    encoded_length = static_cast<std::uint32_t>(expected_base64_size);
    const int result = Base64_Encode_NoNl(input_ptr, input.size(), output_ptr, &encoded_length);
    if (result != 0) {
      return {};
    }
    if (encoded_length > base64_bytes.size()) {
      return {};
    }
#elif defined(AERO_USE_OPENSSL)
    encoded_length = EVP_EncodeBlock(output_ptr, input_ptr, input.size());
    if (encoded_length < 0) {
      return {};
    }
#endif
    base64_bytes.resize(static_cast<std::size_t>(encoded_length));

    return base64_bytes;
  }

  [[nodiscard]] inline std::string base64_encode(std::string_view input) {
    return base64_encode(std::span{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  }

} // namespace aero::tls::detail
