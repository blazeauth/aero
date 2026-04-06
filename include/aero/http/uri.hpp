#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "aero/detail/string.hpp"
#include "aero/http/error.hpp"
#include "aero/http/port.hpp"

namespace aero::http {

  struct uri_parts {
    std::string scheme;
    std::string host;
    std::optional<std::uint16_t> port;
    std::string path;
    std::string query;
    bool has_query{false};

    [[nodiscard]] bool is_http() const noexcept {
      return scheme == "http";
    }

    [[nodiscard]] bool is_https() const noexcept {
      return scheme == "https";
    }
  };

  // \todo: We'll need to create a common interface with utility
  // functions for parsing URIs, since parsing WebSocket and
  // HTTP URIs is very, very similar process

  class uri {
    constexpr static std::size_t scheme_delimiter_length = 3;
    constexpr static std::size_t max_port_text_length = 5;
    using uri_error = http::error::uri_error;

   public:
    uri() = default;

    explicit uri(uri_parts parts): parts_(std::move(parts)) {}

    [[nodiscard]] std::uint16_t default_port() const noexcept {
      if (parts_.is_http()) {
        return http::default_port;
      }
      if (parts_.is_https()) {
        return http::default_secure_port;
      }
      return 0;
    }

    [[nodiscard]] uri_parts& parts() noexcept {
      return parts_;
    }

    [[nodiscard]] const uri_parts& parts() const noexcept {
      return parts_;
    }

    [[nodiscard]] std::string_view scheme() const noexcept {
      return parts_.scheme;
    }

    [[nodiscard]] std::string_view host() const noexcept {
      return parts_.host;
    }

    [[nodiscard]] std::uint16_t port() const noexcept {
      if (parts_.port.has_value()) {
        return *parts_.port;
      }
      return default_port();
    }

    [[nodiscard]] std::string_view path() const noexcept {
      return parts_.path;
    }

    [[nodiscard]] std::string_view query() const noexcept {
      return parts_.query;
    }

    [[nodiscard]] bool has_query() const noexcept {
      return parts_.has_query || !parts_.query.empty();
    }

    [[nodiscard]] bool is_http() const noexcept {
      return parts_.is_http();
    }

    [[nodiscard]] bool is_https() const noexcept {
      return parts_.is_https();
    }

    [[nodiscard]] std::string target() const {
      std::string request_target;
      request_target.reserve(parts_.path.size() + parts_.query.size() + 2);

      if (parts_.path.empty()) {
        request_target.push_back('/');
      } else if (parts_.path.front() != '/') {
        request_target.push_back('/');
        request_target.append(parts_.path);
      } else {
        request_target.append(parts_.path);
      }

      if (has_query()) {
        request_target.push_back('?');
        request_target.append(parts_.query);
      }

      return request_target;
    }

    [[nodiscard]] std::string to_string() const {
      std::string uri_text;
      uri_text.reserve(parts_.scheme.size() + scheme_delimiter_length + parts_.host.size() + parts_.path.size() +
                       parts_.query.size() + max_port_text_length);

      uri_text.append(parts_.scheme).append("://").append(parts_.host);

      if (parts_.port.has_value()) {
        uri_text.push_back(':');
        uri_text.append(std::to_string(*parts_.port));
      }

      uri_text.append(target());
      return uri_text;
    }

    [[nodiscard]] std::error_code validate() const {
      auto has_forbidden_character = [](std::string_view text) {
        constexpr auto valid_ascii_start{0x20};
        constexpr auto ascii_del_code{0x7F};
        return std::ranges::any_of(text,
          [](unsigned char character) { return character <= valid_ascii_start || character == ascii_del_code; });
      };

      std::string_view scheme{parts_.scheme};
      std::string_view host{parts_.host};
      auto port_value{parts_.port};
      std::string_view path{parts_.path};
      std::string_view query{parts_.query};

      if (scheme != "http" && scheme != "https") {
        return uri_error::invalid_scheme;
      }

      if (host.empty()) {
        return uri_error::empty_host;
      }

      if (has_forbidden_character(host)) {
        return uri_error::invalid_character;
      }

      if (host.contains('@')) {
        return uri_error::userinfo_not_allowed;
      }

      if (host.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      if (host.front() == '[' || host.back() == ']') {
        if (!(host.starts_with('[') && host.ends_with(']'))) {
          return uri_error::invalid_host;
        }

        auto inside_brackets = host.substr(1, host.size() - 2);
        if (inside_brackets.empty()) {
          return uri_error::invalid_ipv6_literal;
        }

        if (contains_invalid_ipv6_token(inside_brackets)) {
          return uri_error::invalid_ipv6_literal;
        }
      } else {
        if (host.contains('[') || host.contains(']') || host.contains(':')) {
          return uri_error::invalid_host;
        }
      }

      if (port_value.has_value() && *port_value == 0) {
        return uri_error::invalid_port;
      }

      if (has_forbidden_character(path)) {
        return uri_error::invalid_character;
      }

      if (path.contains('?')) {
        return uri_error::invalid_path;
      }

      if (path.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      if (has_forbidden_character(query)) {
        return uri_error::invalid_character;
      }

      if (query.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      return {};
    }

    [[nodiscard]] static std::expected<uri, std::error_code> parse(std::string_view uri_text) {
      if (uri_text.empty()) {
        return std::unexpected(uri_error::missing_scheme_delimiter);
      }

      auto is_forbidden_character = [](unsigned char character) {
        constexpr auto valid_ascii_start{0x20};
        constexpr auto ascii_del_code{0x7F};
        return character <= valid_ascii_start || character == ascii_del_code;
      };

      if (std::ranges::any_of(uri_text, is_forbidden_character)) {
        return std::unexpected(uri_error::invalid_character);
      }

      constexpr std::string_view scheme_delimiter = "://";
      std::size_t scheme_delimiter_position = uri_text.find(scheme_delimiter);
      if (scheme_delimiter_position == std::string_view::npos || scheme_delimiter_position == 0) {
        return std::unexpected(uri_error::missing_scheme_delimiter);
      }

      std::string_view scheme_view = uri_text.substr(0, scheme_delimiter_position);
      std::string normalized_scheme;
      normalized_scheme.reserve(scheme_view.size());
      for (unsigned char character : scheme_view) {
        normalized_scheme.push_back(static_cast<char>(aero::detail::to_ascii_lower(character)));
      }

      if (normalized_scheme != "http" && normalized_scheme != "https") {
        return std::unexpected(uri_error::invalid_scheme);
      }

      std::size_t after_scheme = scheme_delimiter_position + scheme_delimiter.size();

      if (uri_text.find('#', after_scheme) != std::string_view::npos) {
        return std::unexpected(uri_error::fragment_not_allowed);
      }

      std::size_t authority_end = uri_text.find_first_of("/?", after_scheme);
      if (authority_end == std::string_view::npos) {
        authority_end = uri_text.size();
      }

      std::string_view authority_view = uri_text.substr(after_scheme, authority_end - after_scheme);
      if (authority_view.empty()) {
        return std::unexpected(uri_error::empty_authority);
      }

      if (authority_view.contains('@')) {
        return std::unexpected(uri_error::userinfo_not_allowed);
      }

      auto is_forbidden_in_authority = [](char character) {
        return character == '/' || character == '?' || character == '#';
      };

      if (std::ranges::any_of(authority_view,
            [&](unsigned char character) { return is_forbidden_in_authority(static_cast<char>(character)); })) {
        return std::unexpected(uri_error::invalid_authority);
      }

      std::string_view host_view;
      std::optional<std::uint16_t> parsed_port;

      if (!authority_view.empty() && authority_view.front() == '[') {
        std::size_t closing_bracket_position = authority_view.find(']');
        if (closing_bracket_position == std::string_view::npos || closing_bracket_position == 1) {
          return std::unexpected(uri_error::invalid_ipv6_literal);
        }

        host_view = authority_view.substr(0, closing_bracket_position + 1);

        std::string_view inside_brackets = authority_view.substr(1, closing_bracket_position - 1);
        if (contains_invalid_ipv6_token(inside_brackets)) {
          return std::unexpected(uri_error::invalid_ipv6_literal);
        }

        std::string_view remainder = authority_view.substr(closing_bracket_position + 1);
        if (!remainder.empty()) {
          if (remainder.front() != ':') {
            return std::unexpected(uri_error::invalid_authority);
          }

          auto parsed_port_result = parse_port(remainder.substr(1));
          if (!parsed_port_result.has_value()) {
            return std::unexpected(parsed_port_result.error());
          }

          parsed_port = *parsed_port_result;
        }
      } else {
        std::size_t first_colon_position = authority_view.find(':');
        if (first_colon_position != std::string_view::npos) {
          if (authority_view.find(':', first_colon_position + 1) != std::string_view::npos) {
            return std::unexpected(uri_error::invalid_authority);
          }

          host_view = authority_view.substr(0, first_colon_position);

          auto parsed_port_result = parse_port(authority_view.substr(first_colon_position + 1));
          if (!parsed_port_result.has_value()) {
            return std::unexpected(parsed_port_result.error());
          }

          parsed_port = *parsed_port_result;
        } else {
          host_view = authority_view;
        }

        if (host_view.empty()) {
          return std::unexpected(uri_error::empty_host);
        }

        auto is_forbidden_in_host = [](unsigned char character) {
          char c = static_cast<char>(character);
          return c == '@' || c == '/' || c == '?' || c == '#';
        };

        if (std::ranges::any_of(host_view, [&](unsigned char character) { return is_forbidden_in_host(character); })) {
          return std::unexpected(uri_error::invalid_host);
        }

        if (host_view.contains('[') || host_view.contains(']')) {
          return std::unexpected(uri_error::invalid_host);
        }
      }

      std::size_t query_position = uri_text.find('?', authority_end);
      std::size_t path_end = query_position == std::string_view::npos ? uri_text.size() : query_position;

      auto path_view = uri_text.substr(authority_end, path_end - authority_end);
      std::string parsed_path;

      if (!path_view.empty()) {
        if (path_view.front() != '/') {
          return std::unexpected(uri_error::invalid_path);
        }
        if (path_view.size() > 1) {
          parsed_path.assign(path_view.substr(1));
        }
      }

      std::string parsed_query;
      bool has_query = false;
      if (query_position != std::string_view::npos) {
        parsed_query.assign(uri_text.substr(query_position + 1));
        has_query = true;

        if (std::ranges::any_of(parsed_query, is_forbidden_character)) {
          return std::unexpected(uri_error::invalid_character);
        }
      }

      http::uri result{{
        .scheme = std::move(normalized_scheme),
        .host = std::string{host_view},
        .port = parsed_port,
        .path = std::move(parsed_path),
        .query = std::move(parsed_query),
        .has_query = has_query,
      }};

      if (auto ec = result.validate()) {
        return std::unexpected(ec);
      }

      return result;
    }

   private:
    [[nodiscard]] static std::expected<std::uint16_t, std::error_code> parse_port(std::string_view port_view) {
      if (port_view.empty()) {
        return std::unexpected(uri_error::empty_port);
      }

      std::uint32_t port_number = 0;
      auto parse_result = std::from_chars(port_view.data(), port_view.data() + port_view.size(), port_number);
      if (parse_result.ec != std::errc{} || parse_result.ptr != port_view.data() + port_view.size()) {
        return std::unexpected(uri_error::invalid_port);
      }
      if (port_number == 0) {
        return std::unexpected(uri_error::invalid_port);
      }
      if (port_number > (std::numeric_limits<std::uint16_t>::max)()) {
        return std::unexpected(uri_error::port_out_of_range);
      }

      return static_cast<std::uint16_t>(port_number);
    }

    [[nodiscard]] static bool contains_invalid_ipv6_token(std::string_view address) {
      return std::ranges::any_of(address, [](char character) {
        constexpr std::string_view allowed_ipv6_chars = ":-._~!$&'()*+,;=%";

        bool is_alphanum = std::isalnum(static_cast<unsigned char>(character));
        bool is_allowed_non_alphanum = allowed_ipv6_chars.contains(character);

        return !is_alphanum && !is_allowed_non_alphanum;
      });
    }

    uri_parts parts_;
  };

} // namespace aero::http
