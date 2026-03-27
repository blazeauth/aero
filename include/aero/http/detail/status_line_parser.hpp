#ifndef AERO_HTTP_STATUS_LINE_PARSER_HPP
#define AERO_HTTP_STATUS_LINE_PARSER_HPP

#include <expected>
#include <system_error>

#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/version.hpp"

namespace aero::http::detail {

  struct status_line_view {
    std::string_view protocol;
    http::status_code status_code{};
    std::string_view reason_phrase;
  };

  inline std::expected<status_line_view, std::error_code> parse_status_line(std::string_view buffer) {
    constexpr auto npos = std::string_view::npos;

    // Remove CRLF status-line suffix if present
    if (buffer.ends_with(crlf)) {
      buffer.remove_suffix(crlf.size());
    }

    auto protocol_str_end = buffer.find(' ');
    if (protocol_str_end == npos) {
      return std::unexpected(http::error::protocol_error::status_line_invalid);
    }

    auto protocol = buffer.substr(0, protocol_str_end);
    if (!parse_version(protocol).has_value()) {
      return std::unexpected(http::error::protocol_error::version_invalid);
    }

    auto remaining = buffer.substr(protocol_str_end + 1);
    auto status_code_str_end = remaining.find(' ');
    auto status_code_str = remaining.substr(0, status_code_str_end);

    // Reason phrase is just whatever is left (could be empty)
    std::string_view reason_phrase;
    if (status_code_str_end != npos) {
      auto buffer_reason_phrase = remaining.substr(status_code_str_end + 1);
      if (buffer_reason_phrase.ends_with(crlf)) {
        buffer_reason_phrase.remove_suffix(crlf.size());
      }
      reason_phrase = buffer_reason_phrase;
    }

    auto status_code = aero::http::to_status_code(status_code_str);
    if (!status_code) {
      return std::unexpected(status_code.error());
    }

    return status_line_view{
      .protocol = protocol,
      .status_code = *status_code,
      .reason_phrase = reason_phrase,
    };
  }
} // namespace aero::http::detail

#endif
