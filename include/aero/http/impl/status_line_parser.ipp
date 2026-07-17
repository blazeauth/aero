#pragma once

#include <expected>
#include <system_error>

#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  inline std::expected<status_line, std::error_code> status_line::parse(std::string_view buffer) {
    // Remove CRLF status-line suffix if present
    if (buffer.ends_with("\r\n")) {
      buffer.remove_suffix(2);
    }

    auto protocol_str_end = buffer.find(' ');
    if (protocol_str_end == std::string_view::npos) {
      return std::unexpected(http::protocol_error::status_line_invalid);
    }

    auto protocol = buffer.substr(0, protocol_str_end);
    if (!parse_version(protocol).has_value()) {
      return std::unexpected(http::protocol_error::version_invalid);
    }

    auto remaining = buffer.substr(protocol_str_end + 1);
    auto status_code_str_end = remaining.find(' ');
    auto status_code_str = remaining.substr(0, status_code_str_end);

    // Reason phrase is just whatever is left (could be empty)
    std::string_view reason_phrase;
    if (status_code_str_end != std::string_view::npos) {
      reason_phrase = remaining.substr(status_code_str_end + 1);

      // For cases where the passed buffer ends with a double-CRLF, such as when
      // the status line is followed by an empty header section
      if (reason_phrase.ends_with("\r\n")) {
        reason_phrase.remove_suffix(2);
      }
    }

    auto status_code = aero::http::parse_status(status_code_str);
    if (!status_code) {
      return std::unexpected(status_code.error());
    }

    return status_line{
      .protocol = std::string{protocol},
      .status_code = *status_code,
      .reason_phrase = std::string{reason_phrase},
    };
  }

  inline std::expected<status_line, std::error_code> status_line::parse(std::span<const std::byte> buffer) {
    return parse(std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()});
  }

} // namespace aero::http
