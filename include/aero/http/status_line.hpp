#pragma once

#include <string>
#include <utility>

#include "aero/detail/string.hpp"
#include "aero/http/status_code.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Messages#http_responses
  struct status_line {
    std::string protocol;
    http::status_code status_code{};
    std::string reason_phrase;

    static std::expected<status_line, std::error_code> parse(std::string_view buffer);
    static std::expected<status_line, std::error_code> parse(std::span<const std::byte> buffer);

    [[nodiscard]] bool empty() const noexcept {
      return protocol.empty() && status_code == http::status_code{} && reason_phrase.empty();
    }

    [[nodiscard]] std::string serialize() const {
      if (empty()) {
        return {};
      }

      auto status_code_str = std::to_string(std::to_underlying(status_code));
      return aero::detail::join_strings(std::array{protocol, status_code_str, reason_phrase}, ' ');
    }

    [[nodiscard]] http::version version() const noexcept {
      return http::parse_version(protocol).value_or(http::version{});
    }

    [[nodiscard]] bool has_reason_phrase() const noexcept {
      return !reason_phrase.empty();
    }

    [[nodiscard]] bool operator==(const status_line& other) const noexcept = default;
  };

} // namespace aero::http

#include "aero/http/impl/status_line_parser.ipp"
