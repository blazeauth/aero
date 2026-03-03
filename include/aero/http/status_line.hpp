#ifndef AERO_HTTP_STATUS_LINE_HPP
#define AERO_HTTP_STATUS_LINE_HPP

#include <string>
#include <utility>

#include "aero/http/detail/status_line_parser.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/version.hpp"

#include "aero/detail/string.hpp"

namespace aero::http {

  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Messages#http_responses
  struct status_line {
    std::string protocol;
    http::status_code status_code{};
    std::string reason_phrase;

    static std::expected<status_line, std::error_code> parse(std::string_view buffer) {
      auto parsed_status_line = http::detail::parse_status_line(buffer);
      if (!parsed_status_line) {
        return std::unexpected(parsed_status_line.error());
      }
      return http::status_line{
        .protocol = std::string{parsed_status_line->protocol},
        .status_code = parsed_status_line->status_code,
        .reason_phrase = std::string{parsed_status_line->reason_phrase},
      };
    }

    static std::expected<status_line, std::error_code> parse(std::span<const std::byte> buffer) {
      auto buffer_str = std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()};
      auto parsed_status_line = http::detail::parse_status_line(buffer_str);
      if (!parsed_status_line) {
        return std::unexpected(parsed_status_line.error());
      }
      return http::status_line{
        .protocol = std::string{parsed_status_line->protocol},
        .status_code = parsed_status_line->status_code,
        .reason_phrase = std::string{parsed_status_line->reason_phrase},
      };
    }

    [[nodiscard]] bool empty() const noexcept {
      return protocol.empty() && status_code == http::status_code{} && reason_phrase.empty();
    }

    [[nodiscard]] std::string to_string() const {
      auto status_code_str = std::to_string(std::to_underlying(status_code));
      if (status_code_str.empty()) {
        return {};
      }
      return aero::detail::join_strings(std::array{protocol, status_code_str, reason_phrase}, ' ');
    }

    [[nodiscard]] http::version version() const noexcept {
      return parse_version().value_or(http::version{});
    }

    [[nodiscard]] std::expected<http::version, std::error_code> parse_version() const noexcept {
      return http::parse_version(protocol);
    }

    [[nodiscard]] bool has_reason_phrase() const noexcept {
      return !reason_phrase.empty();
    }

    [[nodiscard]] bool operator==(const status_line& other) const noexcept = default;
    [[nodiscard]] bool operator==(const detail::status_line_view& other) const noexcept {
      return status_code == other.status_code && protocol == other.protocol && reason_phrase == other.reason_phrase;
    }
  };

} // namespace aero::http

#endif
