#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string_view>
#include <system_error>

#include "aero/http/error.hpp"

namespace aero::http {

  enum class version : std::uint8_t {
    http1_0 = 1,
    http1_1,
  };

  inline std::expected<version, std::error_code> parse_version(std::string_view version_str) noexcept {
    if (version_str == "HTTP/1.0") {
      return version::http1_0;
    }
    if (version_str == "HTTP/1.1") {
      return version::http1_1;
    }
    return std::unexpected(http::error::protocol_error::version_invalid);
  }

  constexpr std::string_view to_string(version version) noexcept {
    switch (version) {
    case version::http1_0:
      return "HTTP/1.0";
    case version::http1_1:
      return "HTTP/1.1";
    }
    return {};
  }

} // namespace aero::http

template <>
struct std::formatter<aero::http::version> : std::formatter<std::string_view> {
  auto format(const aero::http::version& version, std::format_context& ctx) const {
    constexpr std::string_view unknown_version = "unknown_version";
    auto version_str = aero::http::to_string(version);
    return std::formatter<std::string_view>{}.format(version_str.empty() ? unknown_version : version_str, ctx);
  }
};
