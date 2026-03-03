#ifndef AERO_WEBSOCKET_URI_HPP
#define AERO_WEBSOCKET_URI_HPP

#include <algorithm>
#include <cctype>
#include <charconv>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/detail/string.hpp"
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
    using uri_error = websocket::error::uri_error;

   public:
    uri() = default;
    explicit uri(std::string_view uri_text) {
      if (auto result = uri::parse(uri_text); result) {
        *this = *result;
      }
    }
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

      if (port.has_value()) {
        if (*port == 0) {
          return uri_error::invalid_port;
        }
      }

      if (has_forbidden_character(path)) {
        return uri_error::invalid_character;
      }

      if (!path.empty() && path.front() == '?') {
        return uri_error::invalid_path;
      }

      if (path.contains('#')) {
        return uri_error::fragment_not_allowed;
      }

      for (const auto& [key, value] : query) {
        if (has_forbidden_character(key) || has_forbidden_character(value)) {
          return uri_error::invalid_character;
        }
        if (key.contains('#') || value.contains('#')) {
          return uri_error::fragment_not_allowed;
        }
      }

      return {};
    }

    [[nodiscard]] static std::expected<uri, std::error_code> parse(std::string_view uri_text) {
      // \todo: We will need to refactor this

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

      if (normalized_scheme != "ws" && normalized_scheme != "wss") {
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
        if (closing_bracket_position == std::string_view::npos) {
          return std::unexpected(uri_error::invalid_ipv6_literal);
        }
        if (closing_bracket_position == 1) {
          return std::unexpected(uri_error::invalid_ipv6_literal);
        }

        host_view = authority_view.substr(0, closing_bracket_position + 1);

        std::string_view inside_brackets = authority_view.substr(1, closing_bracket_position - 1);

        if (!is_valid_ipv6_address(inside_brackets)) {
          return std::unexpected(uri_error::invalid_ipv6_literal);
        }

        std::string_view remainder = authority_view.substr(closing_bracket_position + 1);
        if (!remainder.empty()) {
          if (remainder.front() != ':') {
            return std::unexpected(uri_error::invalid_authority);
          }

          std::string_view port_view = remainder.substr(1);
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

          parsed_port = static_cast<std::uint16_t>(port_number);
        }
      } else {
        std::size_t first_colon_position = authority_view.find(':');
        if (first_colon_position != std::string_view::npos) {
          if (authority_view.find(':', first_colon_position + 1) != std::string_view::npos) {
            return std::unexpected(uri_error::invalid_authority);
          }

          host_view = authority_view.substr(0, first_colon_position);
          std::string_view port_view = authority_view.substr(first_colon_position + 1);

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

          parsed_port = static_cast<std::uint16_t>(port_number);
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
      std::size_t path_end = (query_position == std::string_view::npos) ? uri_text.size() : query_position;

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

      std::vector<std::pair<std::string, std::string>> parsed_query;
      if (query_position != std::string_view::npos) {
        auto query_view = uri_text.substr(query_position + 1);

        if (std::ranges::any_of(query_view, is_forbidden_character)) {
          return std::unexpected(uri_error::invalid_character);
        }

        std::size_t segment_start = 0;
        while (segment_start <= query_view.size()) {
          std::size_t ampersand_position = query_view.find('&', segment_start);
          if (ampersand_position == std::string_view::npos) {
            ampersand_position = query_view.size();
          }

          std::string_view segment = query_view.substr(segment_start, ampersand_position - segment_start);
          if (!segment.empty()) {
            std::size_t equals_position = segment.find('=');
            if (equals_position == std::string_view::npos) {
              parsed_query.emplace_back(std::string(segment), std::string());
            } else {
              parsed_query.emplace_back(std::string(segment.substr(0, equals_position)),
                std::string(segment.substr(equals_position + 1)));
            }
          }

          if (ampersand_position == query_view.size()) {
            break;
          }
          segment_start = ampersand_position + 1;
        }
      }

      websocket::uri result{{
        .scheme = std::move(normalized_scheme),
        .host = std::string{host_view},
        .port = parsed_port,
        .path = std::move(parsed_path),
        .query = std::move(parsed_query),
      }};

      if (auto validation_ec = result.validate()) {
        return std::unexpected(validation_ec);
      }

      return result;
    }

   private:
    [[nodiscard]] static bool is_valid_ipv6_address(std::string_view address) {
      return std::ranges::any_of(address, [](char ch) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
          return true;
        }
        constexpr std::string_view allowed_ipv6_chars = ":-._~!$&'()*+,;=%";
        auto is_invalid_ipv6_character = !allowed_ipv6_chars.contains(ch);
        return is_invalid_ipv6_character;
      });
    }

    uri_parts parts_;
  };

} // namespace aero::websocket

#endif
