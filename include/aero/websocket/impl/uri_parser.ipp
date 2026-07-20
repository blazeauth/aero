#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/util/string.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/uri.hpp"

namespace aero::websocket {

  inline std::expected<uri, std::error_code> uri::parse(std::string_view uri_text) {
    // \todo: We will need to refactor this

    if (uri_text.empty()) {
      return std::unexpected(uri_error::scheme_delimiter_missing);
    }

    auto is_forbidden_character = [](unsigned char character) {
      constexpr auto valid_ascii_start{0x20};
      constexpr auto ascii_del_code{0x7F};
      return character <= valid_ascii_start || character == ascii_del_code;
    };

    if (std::ranges::any_of(uri_text, is_forbidden_character)) {
      return std::unexpected(uri_error::character_invalid);
    }

    constexpr std::string_view scheme_delimiter = "://";
    std::size_t scheme_delimiter_position = uri_text.find(scheme_delimiter);
    if (scheme_delimiter_position == std::string_view::npos || scheme_delimiter_position == 0) {
      return std::unexpected(uri_error::scheme_delimiter_missing);
    }

    std::string_view scheme_view = uri_text.substr(0, scheme_delimiter_position);
    std::string normalized_scheme;
    normalized_scheme.reserve(scheme_view.size());
    for (char c : scheme_view) {
      normalized_scheme.push_back(aero::ascii_tolower(c));
    }

    if (normalized_scheme != "ws" && normalized_scheme != "wss") {
      return std::unexpected(uri_error::scheme_invalid);
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
      return std::unexpected(uri_error::authority_empty);
    }

    if (authority_view.contains('@')) {
      return std::unexpected(uri_error::userinfo_not_allowed);
    }

    auto is_forbidden_in_authority = [](char character) {
      return character == '/' || character == '?' || character == '#';
    };

    if (std::ranges::any_of(authority_view,
          [&](unsigned char character) { return is_forbidden_in_authority(static_cast<char>(character)); })) {
      return std::unexpected(uri_error::authority_invalid);
    }

    std::string_view host_view;
    std::optional<std::uint16_t> parsed_port;

    if (!authority_view.empty() && authority_view.front() == '[') {
      std::size_t closing_bracket_position = authority_view.find(']');
      if (closing_bracket_position == std::string_view::npos) {
        return std::unexpected(uri_error::ipv6_literal_invalid);
      }
      if (closing_bracket_position == 1) {
        return std::unexpected(uri_error::ipv6_literal_invalid);
      }

      host_view = authority_view.substr(0, closing_bracket_position + 1);

      std::string_view inside_brackets = authority_view.substr(1, closing_bracket_position - 1);

      if (contains_invalid_ipv6_token(inside_brackets)) {
        return std::unexpected(uri_error::ipv6_literal_invalid);
      }

      std::string_view remainder = authority_view.substr(closing_bracket_position + 1);
      if (!remainder.empty()) {
        if (remainder.front() != ':') {
          return std::unexpected(uri_error::authority_invalid);
        }

        std::string_view port_view = remainder.substr(1);
        if (port_view.empty()) {
          return std::unexpected(uri_error::port_empty);
        }

        std::uint32_t port_number = 0;
        auto parse_result = std::from_chars(port_view.data(), port_view.data() + port_view.size(), port_number);
        if (parse_result.ec != std::errc{} || parse_result.ptr != port_view.data() + port_view.size()) {
          return std::unexpected(uri_error::port_invalid);
        }
        if (port_number == 0) {
          return std::unexpected(uri_error::port_invalid);
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
          return std::unexpected(uri_error::authority_invalid);
        }

        host_view = authority_view.substr(0, first_colon_position);
        std::string_view port_view = authority_view.substr(first_colon_position + 1);

        if (port_view.empty()) {
          return std::unexpected(uri_error::port_empty);
        }

        std::uint32_t port_number = 0;
        auto parse_result = std::from_chars(port_view.data(), port_view.data() + port_view.size(), port_number);
        if (parse_result.ec != std::errc{} || parse_result.ptr != port_view.data() + port_view.size()) {
          return std::unexpected(uri_error::port_invalid);
        }
        if (port_number == 0) {
          return std::unexpected(uri_error::port_invalid);
        }
        if (port_number > (std::numeric_limits<std::uint16_t>::max)()) {
          return std::unexpected(uri_error::port_out_of_range);
        }

        parsed_port = static_cast<std::uint16_t>(port_number);
      } else {
        host_view = authority_view;
      }

      if (host_view.empty()) {
        return std::unexpected(uri_error::host_empty);
      }

      auto is_forbidden_in_host = [](unsigned char character) {
        char c = static_cast<char>(character);
        return c == '@' || c == '/' || c == '?' || c == '#';
      };

      if (std::ranges::any_of(host_view, [&](unsigned char character) { return is_forbidden_in_host(character); })) {
        return std::unexpected(uri_error::host_invalid);
      }

      if (host_view.contains('[') || host_view.contains(']')) {
        return std::unexpected(uri_error::host_invalid);
      }
    }

    std::size_t query_position = uri_text.find('?', authority_end);
    std::size_t path_end = (query_position == std::string_view::npos) ? uri_text.size() : query_position;

    auto path_view = uri_text.substr(authority_end, path_end - authority_end);
    std::string parsed_path;

    if (!path_view.empty()) {
      if (path_view.front() != '/') {
        return std::unexpected(uri_error::path_invalid);
      }
      if (path_view.size() > 1) {
        parsed_path.assign(path_view.substr(1));
      }
    }

    std::vector<std::pair<std::string, std::string>> parsed_query;
    if (query_position != std::string_view::npos) {
      auto query_view = uri_text.substr(query_position + 1);

      if (std::ranges::any_of(query_view, is_forbidden_character)) {
        return std::unexpected(uri_error::character_invalid);
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

    if (auto ec = result.validate()) {
      return std::unexpected(ec);
    }

    return result;
  }

} // namespace aero::websocket
