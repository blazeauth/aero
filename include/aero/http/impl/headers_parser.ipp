#pragma once

#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

namespace aero::http {

  namespace {

    using http::error::header_error;

    struct field_view {
      std::string_view name;
      std::string_view value;
    };

    constexpr inline std::string_view optional_whitespace_chars{" \t"};
    constexpr inline std::string_view tchar_symbols{"!#$%&'*+-.^_`|~"};

    [[nodiscard]] constexpr auto to_byte(char ch) noexcept {
      return static_cast<unsigned char>(ch);
    }

    [[nodiscard]] constexpr bool is_tchar(unsigned char byte) noexcept {
      const auto lower = static_cast<unsigned char>(byte | 0x20U);
      bool is_numeric = byte >= '0' && byte <= '9';
      bool is_character = (lower >= 'a' && lower <= 'z');
      bool is_valid_symbol = tchar_symbols.contains(static_cast<char>(byte));
      return is_numeric || is_character || is_valid_symbol;
    }

    [[nodiscard]] inline bool is_header_field_name_token(std::string_view name) noexcept {
      return !name.empty() && std::ranges::all_of(name, is_tchar, to_byte);
    }

    [[nodiscard]] inline bool is_valid_field_value(std::string_view value) noexcept {
      constexpr auto ascii_delete = static_cast<unsigned char>(0x7F);
      constexpr auto obs_text_first_octet = static_cast<unsigned char>(0x80);

      auto is_valid_byte = [](unsigned char byte) noexcept {
        return byte == '\t' || byte >= obs_text_first_octet || (byte >= ' ' && byte != ascii_delete);
      };
      return std::ranges::all_of(value, is_valid_byte, to_byte);
    }

    [[nodiscard]] inline std::string_view trim_optional_whitespace(std::string_view text) {
      std::size_t first_non_whitespace_pos = text.find_first_not_of(optional_whitespace_chars);
      if (first_non_whitespace_pos == std::string_view::npos) {
        return {};
      }
      std::size_t last_non_whitespace_pos = text.find_last_not_of(optional_whitespace_chars);
      return text.substr(first_non_whitespace_pos, last_non_whitespace_pos - first_non_whitespace_pos + 1);
    }

    [[nodiscard]] inline std::expected<field_view, std::error_code> parse_header_field(std::string_view last_header_name,
      std::string_view line) {
      bool is_obs_fold_continuation = optional_whitespace_chars.contains(line.front());
      if (is_obs_fold_continuation && !last_header_name.empty()) {
        return std::unexpected(header_error::obs_fold_not_supported);
      }

      std::size_t colon_position = line.find(':');
      if (colon_position == std::string_view::npos) {
        return std::unexpected(header_error::field_invalid);
      }

      std::string_view raw_name = line.substr(0, colon_position);
      std::string_view name = trim_optional_whitespace(raw_name);

      if (name.empty() || name.size() != raw_name.size() || !is_header_field_name_token(name)) {
        return std::unexpected(header_error::name_invalid);
      }

      std::string_view value = trim_optional_whitespace(line.substr(colon_position + 1));
      if (!is_valid_field_value(value)) {
        return std::unexpected(header_error::field_invalid);
      }

      return field_view{.name = name, .value = value};
    }

    [[nodiscard]] inline std::expected<headers, std::error_code> parse_headers(std::string_view buffer) {
      if (buffer == http::detail::crlf) {
        return headers{};
      }

      std::size_t headers_end_pos = buffer.find(http::detail::double_crlf);
      if (headers_end_pos == std::string_view::npos) {
        if (buffer.ends_with(http::detail::double_lf)) {
          return std::unexpected(header_error::lf_field_endings_not_supported);
        }

        return std::unexpected(header_error::section_incomplete);
      }

      http::headers headers;
      std::string_view last_header_name;
      std::string_view headers_section = buffer.substr(0, headers_end_pos);

      for (auto&& field_subrange : headers_section | std::views::split(http::detail::crlf)) {
        std::string_view field_line{field_subrange};
        if (field_line.empty()) {
          break;
        }

        bool field_line_contains_crlf = field_line.find_first_of(http::detail::crlf) != std::string_view::npos;
        if (field_line_contains_crlf) {
          return std::unexpected(header_error::field_invalid);
        }

        auto field_view = parse_header_field(last_header_name, field_line);
        if (!field_view) {
          return std::unexpected(field_view.error());
        }

        headers.add(std::string{field_view->name}, std::string{field_view->value});
        last_header_name = field_view->name;
      }

      return headers;
    }

  } // namespace

  inline std::string_view headers::trim_optional_whitespace(std::string_view text) {
    return aero::http::trim_optional_whitespace(text);
  }

  inline std::expected<headers, std::error_code> headers::parse(std::string_view buffer) {
    return parse_headers(buffer);
  }

  inline std::expected<headers, std::error_code> headers::parse(std::span<const std::byte> buffer) {
    return parse_headers(std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()});
  }

} // namespace aero::http
