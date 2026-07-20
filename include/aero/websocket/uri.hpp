#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/websocket/error.hpp"

namespace aero::websocket {

  struct uri_parts {
    std::string scheme;
    std::string host;
    std::optional<std::uint16_t> port;
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;

    [[nodiscard]] bool is_ws() const noexcept {
      return scheme == "ws";
    }

    [[nodiscard]] bool is_wss() const noexcept {
      return scheme == "wss";
    }
  };

  class uri {
    using uri_error = websocket::uri_error;

   public:
    uri() = default;

    explicit uri(uri_parts parts): parts_(std::move(parts)) {}

    [[nodiscard]] std::uint16_t default_port() const noexcept {
      if (parts_.is_ws()) {
        return 80; // NOLINT
      }
      if (parts_.is_wss()) {
        return 443; // NOLINT
      }
      return 0;
    }

    [[nodiscard]] uri_parts& parts() noexcept {
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

    [[nodiscard]] std::span<const std::pair<std::string, std::string>> query() const noexcept {
      return parts_.query;
    }

    [[nodiscard]] std::string to_string() const {
      // \todo: We will need to refactor this

      std::string uri_text;
      uri_text.reserve(parts_.scheme.size() + parts_.host.size() + parts_.path.size() + parts_.query.size());

      uri_text.append(parts_.scheme).append("://").append(parts_.host);

      if (parts_.port.has_value()) {
        uri_text.push_back(':');
        uri_text.append(std::to_string(*parts_.port));
      }

      if (parts_.path.empty()) {
        uri_text.push_back('/');
      } else {
        if (parts_.path.front() != '/') {
          uri_text.push_back('/');
          uri_text.append(parts_.path);
        } else {
          uri_text.append(parts_.path);
        }
      }

      if (!parts_.query.empty()) {
        uri_text.push_back('?');
        for (std::size_t i{}; i < parts_.query.size(); ++i) {
          if (i != 0) {
            uri_text.push_back('&');
          }
          uri_text.append(parts_.query[i].first);
          if (!parts_.query[i].second.empty()) {
            uri_text.push_back('=');
            uri_text.append(parts_.query[i].second);
          }
        }
      }

      return uri_text;
    }

    [[nodiscard]] std::error_code validate() const noexcept {
      // \todo: We will need to refactor this

      auto has_forbidden_character = [](std::string_view text) {
        constexpr auto valid_ascii_start{0x20};
        constexpr auto ascii_del_code{0x7F};
        return std::ranges::any_of(text,
          [](unsigned char character) { return character <= valid_ascii_start || character == ascii_del_code; });
      };

      std::string_view scheme{parts_.scheme};
      std::string_view host{parts_.host};
      auto port{parts_.port};
      std::string_view path{parts_.path};
      std::span query{parts_.query};

      if (scheme != "ws" && scheme != "wss") {
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

      if (port.has_value()) {
        if (*port == 0) {
          return uri_error::port_invalid;
        }
      }

      if (has_forbidden_character(path)) {
        return uri_error::character_invalid;
      }

      if (!path.empty() && path.front() == '?') {
        return uri_error::path_invalid;
      }

      if (path.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      for (const auto& [key, value] : query) {
        if (has_forbidden_character(key) || has_forbidden_character(value)) {
          return uri_error::character_invalid;
        }
        if (key.contains('#') || value.contains('#')) {
          return uri_error::fragment_not_allowed;
        }
      }

      return {};
    }

    [[nodiscard]] static std::expected<uri, std::error_code> parse(std::string_view uri_text);

   private:
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

} // namespace aero::websocket

#include "aero/websocket/impl/uri_parser.ipp"
