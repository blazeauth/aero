#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string_view>
#include <system_error>

#include "aero/http/error.hpp"
#include "aero/util/string.hpp"

namespace aero::http {

  enum class version : std::uint8_t {
    http1_0 = 1,
    http1_1,
  };

  inline std::expected<version, std::error_code> parse_version(std::string_view http_version) noexcept {
    // RFC 9112, Section 2.3:
    // HTTP-version  = HTTP-name "/" DIGIT "." DIGIT
    // HTTP-name     = %s"HTTP"

    if (http_version.size() != 8) {
      return std::unexpected(protocol_error::version_invalid);
    }

    constexpr std::string_view prefix = "HTTP/";
    if (!http_version.starts_with(prefix)) {
      return std::unexpected(protocol_error::version_invalid);
    }

    http_version.remove_prefix(prefix.size());

    // Everything after "HTTP/" should be: DIGIT "." DIGIT
    if (http_version.size() != 3) {
      return std::unexpected(protocol_error::version_invalid);
    }

    // RFC 9112, Section 2.3:
    // HTTP-version = HTTP-name "/" DIGIT "." DIGIT
    char major_version = http_version[0];
    char version_separator = http_version[1];
    char minor_version = http_version[2];
    if (!aero::is_digit(major_version) || version_separator != '.' || !aero::is_digit(minor_version)) {
      return std::unexpected(protocol_error::version_invalid);
    }

    if (major_version == '1' && minor_version == '0') {
      return version::http1_0;
    }

    // RFC 9110, Section 6.2:
    // A recipient that receives a message with a major version number that it
    // implements and a minor version number higher than what it implements
    // SHOULD process the message as if it were in the highest minor version
    // within that major version to which the recipient is conformant.
    if (major_version == '1' && minor_version >= '1') {
      return version::http1_1;
    }

    // The syntax is correct according to the RFC 9112 Section 2.3,
    // but the version itself is not supported
    return std::unexpected(protocol_error::version_unsupported);
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
