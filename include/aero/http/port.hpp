#ifndef AERO_HTTP_PORT_HPP
#define AERO_HTTP_PORT_HPP

#include <cstdint>

namespace aero::http {

  [[maybe_unused]] constexpr inline std::uint16_t default_port = 80;
  [[maybe_unused]] constexpr inline std::uint16_t default_secure_port = 443;

} // namespace aero::http

#endif
