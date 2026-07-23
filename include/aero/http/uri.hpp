#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
    using uri_error = http::uri_error;

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
        return uri_error::scheme_invalid;
      }

      if (host.empty()) {
        return uri_error::host_empty;
      }

      if (has_forbidden_character(host)) {
        return uri_error::character_invalid;
      }

      if (host.contains('@')) {
        return uri_error::userinfo_not_allowed;
      }

      if (host.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      if (host.front() == '[' || host.back() == ']') {
        if (!(host.starts_with('[') && host.ends_with(']'))) {
          return uri_error::host_invalid;
        }

        auto inside_brackets = host.substr(1, host.size() - 2);
        if (inside_brackets.empty()) {
          return uri_error::ipv6_literal_invalid;
        }

        if (contains_invalid_ipv6_token(inside_brackets)) {
          return uri_error::ipv6_literal_invalid;
        }
      } else {
        if (host.contains('[') || host.contains(']') || host.contains(':')) {
          return uri_error::host_invalid;
        }
      }

      if (port_value.has_value() && *port_value == 0) {
        return uri_error::port_invalid;
      }

      if (has_forbidden_character(path)) {
        return uri_error::character_invalid;
      }

      if (path.contains('?')) {
        return uri_error::path_invalid;
      }

      if (path.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      if (has_forbidden_character(query)) {
        return uri_error::character_invalid;
      }

      if (query.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      return {};
    }

    [[nodiscard]] static std::expected<uri, std::error_code> parse(std::string_view uri_text);

   private:
    [[nodiscard]] static std::expected<std::uint16_t, std::error_code> parse_port(std::string_view port_view);

    [[nodiscard]] static bool contains_invalid_ipv6_token(std::string_view address);

    uri_parts parts_;
  };

} // namespace aero::http

#include "aero/http/impl/uri_parser.ipp"
