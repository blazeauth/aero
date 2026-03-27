#pragma once

#include "aero/websocket/client_handshaker.hpp"

namespace aero::websocket {

  constexpr inline std::size_t default_max_message_size = 32ZU * 1024;

  struct client_options {
    std::size_t max_message_size{default_max_message_size};
    bool validate_outcoming_utf8{true};
    websocket::client_handshaker client_handshaker;
  };

} // namespace aero::websocket
