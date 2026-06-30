#pragma once

#include <array>
#include <string>
#include <string_view>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/method.hpp"
#include "aero/http/version.hpp"

namespace aero::http {

  struct request_line {
    http::method method;
    std::string target;
    http::version version;

    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::string_view line,
      std::size_t max_method_length = 0) {
      using http::protocol_error;
      constexpr auto npos = std::string_view::npos;

      if (max_method_length == 0) {
        max_method_length = line.size();
      }

      if (line.ends_with(detail::crlf)) {
        line.remove_suffix(detail::crlf.size());
      }

      if (line.empty()) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      if (line.find('\r') != npos || line.find('\n') != npos) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      std::size_t first_space = line.find(' ');
      if (first_space == npos) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      if (first_space == 0) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      // RFC 9112, 3:
      // A server that receives a method longer than any that it implements
      // SHOULD respond with a 501 (Not Implemented) status code.
      if (first_space > max_method_length) {
        return std::unexpected(protocol_error::method_too_long);
      }

      std::size_t second_space = line.find(' ', first_space + 1);
      if (second_space == npos || second_space == first_space + 1) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      bool has_third_space = line.find(' ', second_space + 1) != npos;
      if (has_third_space) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto method_token = line.substr(0, first_space);
      auto target_token = line.substr(first_space + 1, second_space - (first_space + 1));
      auto version_token = line.substr(second_space + 1);

      if (target_token.empty() || version_token.empty()) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto parsed_method = http::parse_method(method_token);
      if (!parsed_method.has_value()) {
        return std::unexpected(parsed_method.error());
      }

      auto parsed_version = http::parse_version(version_token);
      if (!parsed_version.has_value()) {
        return std::unexpected(parsed_version.error());
      }

      return request_line{
        .method = *parsed_method,
        .target = std::string{target_token},
        .version = *parsed_version,
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

      auto request_line_str = aero::detail::join_strings(std::array{method_str, std::string_view{target}, version_str}, ' ');
      request_line_str.append(detail::crlf);
      return request_line_str;
    }

    [[nodiscard]] bool operator==(const request_line& other) const = default;
  };

} // namespace aero::http
