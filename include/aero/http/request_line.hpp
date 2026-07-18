#pragma once

#include <string>
#include <string_view>

#include "aero/http/detail/request_target_validator.hpp"
#include "aero/http/error.hpp"
#include "aero/http/method.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  struct request_line {
    http::method method;
    std::string target;
    http::version version;

    // The request-line string passed in must not end with CRLF
    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::string_view request_line,
      std::size_t max_method_length = 0) {
      // RFC 9112, Section 3:
      // request-line = method SP request-target SP HTTP-version

      // Rules for the error returned during parsing:
      //  - If the received string violates the format
      //    `request-line = method SP request-target SP HTTP-version`,
      //    the error returned is `request_line_invalid`.
      //  - If the string follows the request-line format but the `method`,
      //    `request-target`, or `HTTP-version` component does not conform to
      //    the RFC syntax, we return a specific error pointing at that
      //    component.
      //  - The one deliberate exception is `method_too_long`. It is decided by
      //    looking at the first token alone, so it wins even on a line whose
      //    overall format is broken. RFC 9112, Section 3 recommends a 501 for a
      //    method longer than any implemented one, and an oversized first token
      //    tells us enough on its own - there is no point in inspecting the
      //    rest of the line just to report a nicer error.

      if (max_method_length == 0) {
        max_method_length = request_line.size();
      }

      if (request_line.empty() || request_line.ends_with('\r') || request_line.ends_with('\n') || request_line.ends_with(' ')) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      std::size_t first_space = request_line.find(' ');
      if (first_space == std::string_view::npos || first_space == 0) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      // RFC 9112, 3:
      // A server that receives a method longer than any that it implements
      // SHOULD respond with a 501 (Not Implemented) status code.
      if (first_space > max_method_length) {
        return std::unexpected(protocol_error::method_too_long);
      }

      std::size_t second_space = request_line.find(' ', first_space + 1);
      if (second_space == std::string_view::npos) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      // The RFC 9112, Section 3 grammar prohibits two consecutive spaces
      bool has_extra_space_after_method = second_space == first_space + 1;
      if (has_extra_space_after_method) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      // RFC 9112 clearly states that the request-line may contain exactly 2 SP characters
      bool has_third_space = request_line.find(' ', second_space + 1) != std::string_view::npos;
      if (has_third_space) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      std::string_view method_str = request_line.substr(0, first_space);

      auto method = http::parse_method(method_str);
      if (!method.has_value()) {
        return std::unexpected(method.error());
      }

      std::string_view request_target = request_line.substr(first_space + 1, second_space - first_space - 1);
      if (!detail::is_valid_request_target(*method, request_target)) {
        return std::unexpected(protocol_error::request_target_invalid);
      }

      std::string_view version_str = request_line.substr(second_space + 1);

      auto version = http::parse_version(version_str);
      if (!version.has_value()) {
        return std::unexpected(version.error());
      }

      return http::request_line{
        .method = *method,
        .target = std::string{request_target},
        .version = *version,
      };
    }

    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::span<const std::byte> buffer,
      std::size_t max_method_length = 0) {
      return parse(std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()}, max_method_length);
    }

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
