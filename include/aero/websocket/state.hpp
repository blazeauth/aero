#pragma once

#include <cstdint>

namespace aero::websocket {

  enum class state : std::uint8_t {
    // Socket has been created. The connection is not yet open
    connecting = 0,
    // The connection is open and ready to communicate
    open = 1,
    // The connection is in the process of closing
    closing = 2,
    // The connection is closed or couldn't be opened
    closed = 3,
  };

} // namespace aero::websocket
