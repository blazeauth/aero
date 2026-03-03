#ifndef AERO_WEBSOCKET_DETAIL_ROLE_HPP
#define AERO_WEBSOCKET_DETAIL_ROLE_HPP

#include <cstdint>

namespace aero::websocket::detail {

  enum class role : std::uint8_t {
    client,
    server,
  };

} // namespace aero::websocket::detail

#endif
