#pragma once

#include "aero/websocket/client_handshaker.hpp"

namespace aero::websocket {

  constexpr inline std::size_t default_max_message_size = 16ZU * 1024 * 1024;
  constexpr inline std::size_t default_read_buffer_size = 32ZU * 1024;

  struct connection_options {
    std::size_t max_message_size{default_max_message_size};
    std::size_t read_buffer_size{default_read_buffer_size};
    bool validate_outgoing_utf8{true};
    websocket::client_handshaker client_handshaker;
  };

} // namespace aero::websocket
