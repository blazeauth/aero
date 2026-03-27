#pragma once

#include "aero/net/tcp_transport.hpp"
#include "aero/websocket/basic_client.hpp"
#include "aero/websocket/concepts/websocket_client.hpp"

namespace aero::websocket {

  using client = websocket::basic_client<aero::net::tcp_transport<>>;

  static_assert(websocket::concepts::websocket_client<client>);

} // namespace aero::websocket
