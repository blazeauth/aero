#pragma once

#include <cstdint>

namespace aero::websocket {

  enum class role : std::uint8_t {
    client,
    server,
  };

} // namespace aero::websocket
