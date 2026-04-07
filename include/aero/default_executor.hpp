#pragma once

#include <asio/system_executor.hpp>

namespace aero {

  [[nodiscard]] inline asio::system_executor get_default_executor() noexcept {
    return asio::system_executor{};
  }

} // namespace aero
