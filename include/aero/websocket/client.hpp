#pragma once

#include "aero/websocket/basic_connection.hpp"
#include "aero/websocket/detail/concepts.hpp"
#include "aero/websocket/role.hpp"

namespace aero::websocket {

  using client = websocket::basic_connection<websocket::role::client>;

  static_assert(websocket::concepts::websocket_client<client>);

} // namespace aero::websocket
