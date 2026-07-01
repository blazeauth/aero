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
    // RFC 9112, Section 2.3:
    // HTTP-version  = HTTP-name "/" DIGIT "." DIGIT
    // HTTP-name     = %s"HTTP"

    constexpr std::string_view prefix = "HTTP/";
    if (!version_str.starts_with(prefix)) {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    version_str.remove_prefix(prefix.size());

    // Everything after "HTTP/" should be: DIGIT "." DIGIT
    if (version_str.size() > 3) {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    char major_version = version_str[0];
    bool is_major_digit = major_version >= '0' && major_version <= '9';
    if (!is_major_digit) {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    char version_separator = version_str[1];
    if (version_separator != '.') {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    char minor_version = version_str[2];
    bool is_minor_digit = minor_version >= '0' && minor_version <= '9';
    if (!is_minor_digit) {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    if (major_version == '1' && minor_version == '0') {
      return version::http1_0;
    }

    if (major_version == '1' && minor_version == '1') {
      return version::http1_1;
    }

    // The syntax is correct according to the RFC 9112 Section 2.3,
    // but the version itself is not supported
    return std::unexpected(http::protocol_error::version_unsupported);
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
