#ifndef AERO_BASE64_BASE64_HPP
#define AERO_BASE64_BASE64_HPP

#include <span>
#include <string>

#include "aero/base64/impl/base64.hpp"

#ifdef AERO_USE_TLS
#include "aero/tls/detail/base64.hpp"
#endif

namespace aero {

  [[nodiscard]] inline std::string base64_encode(std::span<const std::byte> input) {
#ifdef AERO_USE_TLS
    return aero::tls::detail::base64_encode(input);
#else
    return aero::detail::base64_encode(input);
#endif
  }

  [[nodiscard]] inline std::string base64_encode(std::string_view input) {
#ifdef AERO_USE_TLS
    return aero::tls::detail::base64_encode(input);
#else
    return aero::detail::base64_encode(input);
#endif
  }

  [[nodiscard]] inline std::string base64_decode(std::span<const std::byte> input) {
    return aero::detail::base64_decode(input);
  }

  [[nodiscard]] inline std::string base64_decode(std::string_view input) {
    return aero::detail::base64_decode(input);
  }

} // namespace aero

#endif
