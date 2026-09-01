#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <system_error>

#include "aero/urls/error.hpp"
#include "aero/urls/url.hpp"
#include "aero/util/string.hpp"

namespace aero::websocket {

  [[maybe_unused]] constexpr inline std::uint16_t default_port = 80;
  [[maybe_unused]] constexpr inline std::uint16_t default_secure_port = 443;

  [[nodiscard]] constexpr std::optional<std::uint16_t> get_default_port(std::string_view scheme) noexcept {
    if (scheme.empty()) {
      return std::nullopt;
    }

    if (aero::striequal(scheme, "ws")) {
      return default_port;
    }
    if (aero::striequal(scheme, "wss")) {
      return default_secure_port;
    }

    return std::nullopt;
  }

  [[nodiscard]] inline std::expected<std::uint16_t, std::error_code> get_port_for_scheme(const urls::url& url) noexcept {
    std::string_view scheme = url.scheme();
    if (scheme != "ws" && scheme != "wss") {
      return std::unexpected(urls::url_error::scheme_invalid);
    }

    std::uint16_t port{};
    if (url.has_port()) {
      auto url_port = url.port_number();
      if (!url_port) {
        return std::unexpected(urls::url_error::port_invalid);
      }
      port = *url_port;
    } else {
      port = *get_default_port(scheme);
    }

    return port;
  }

} // namespace aero::websocket
