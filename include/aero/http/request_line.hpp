#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "aero/http/method.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  struct request_line {
    http::method method;
    std::string target;
    http::version version;

    // The request-line string passed in must not end with CRLF
    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::string_view request_line,
      std::size_t max_method_length = 0);

    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::span<const std::byte> buffer,
      std::size_t max_method_length = 0);

    [[nodiscard]] std::string serialize() const {
      auto method_str = http::to_string(method);
      if (method_str.empty()) {
        return {};
      }

      auto version_str = http::to_string(version);
      if (version_str.empty()) {
        return {};
      }

      std::string request_line_str;
      // +4 for 2 SP and CRLF
      request_line_str.reserve(method_str.size() + target.size() + version_str.size() + 4);

      // RFC 9112, Section 3: request-line = method SP request-target SP HTTP-version
      request_line_str.append(method_str);
      request_line_str.push_back(' ');
      request_line_str.append(target);
      request_line_str.push_back(' ');
      request_line_str.append(version_str);
      request_line_str.append("\r\n");

      return request_line_str;
    }

    [[nodiscard]] bool operator==(const request_line& other) const = default;
  };

} // namespace aero::http

#include "aero/http/impl/request_line_parser.ipp"
