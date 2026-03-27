#ifndef AERO_HTTP_REQUEST_LINE_HPP
#define AERO_HTTP_REQUEST_LINE_HPP

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

    [[nodiscard]] bool is_get() const noexcept {
      return method == http::method::get;
    }

    [[nodiscard]] bool is_post() const noexcept {
      return method == http::method::post;
    }

    [[nodiscard]] bool is_put() const noexcept {
      return method == http::method::put;
    }

    [[nodiscard]] bool is_patch() const noexcept {
      return method == http::method::patch;
    }

    [[nodiscard]] bool is_delete() const noexcept {
      return method == http::method::delete_;
    }

    [[nodiscard]] bool is_head() const noexcept {
      return method == http::method::head;
    }

    [[nodiscard]] bool is_options() const noexcept {
      return method == http::method::options;
    }

    [[nodiscard]] static std::expected<request_line, std::error_code> parse(std::string_view line) {
      using http::error::protocol_error;
      constexpr auto npos = std::string_view::npos;

      if (line.ends_with(detail::crlf)) {
        line.remove_suffix(detail::crlf.size());
      }

      if (line.empty()) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      if (line.find('\r') != npos || line.find('\n') != npos) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto first_space = line.find(' ');
      if (first_space == npos || first_space == 0) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto second_space = line.find(' ', first_space + 1);
      if (second_space == npos || second_space == first_space + 1) {
        return std::unexpected(protocol_error::request_line_invalid);
      }

      auto has_third_space = line.find(' ', second_space + 1) != npos;
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

    [[nodiscard]] std::string to_string() const {
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

#endif
