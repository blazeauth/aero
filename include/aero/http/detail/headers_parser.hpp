#ifndef AERO_HTTP_DETAIL_HEADERS_PARSER_HPP
#define AERO_HTTP_DETAIL_HEADERS_PARSER_HPP

#include <algorithm>
#include <array>
#include <expected>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>

#include "aero/http/detail/common.hpp"
#include "aero/http/detail/header_fields.hpp"
#include "aero/http/error.hpp"

namespace aero::http::detail {

  struct header_line {
    std::string_view name;
    std::string_view value;

    [[nodiscard]] bool empty() const noexcept {
      return name.empty() && value.empty();
    }
  };

  constexpr std::string_view optional_whitespace_chars{" \t"};
  constexpr std::string_view tchar_symbols{"!#$%&'*+-.^_`|~"};
  constexpr std::array<std::string_view, 21> comma_separated_list_headers{
    "Accept",
    "Accept-Charset",
    "Accept-Encoding",
    "Accept-Language",
    "Accept-Ranges",
    "Allow",
    "Cache-Control",
    "Connection",
    "Content-Encoding",
    "Content-Language",
    "Expect",
    "If-Match",
    "If-None-Match",
    "Pragma",
    "Range",
    "TE",
    "Trailer",
    "Transfer-Encoding",
    "Upgrade",
    "Vary",
    "Via",
  };

  [[nodiscard]] constexpr auto to_byte(char ch) noexcept {
    return static_cast<unsigned char>(ch);
  }

  [[nodiscard]] constexpr bool is_tchar(unsigned char byte) noexcept {
    const auto lower = static_cast<unsigned char>(byte | 0x20U);
    const auto is_numeric = byte >= '0' && byte <= '9';
    const auto is_character = (lower >= 'a' && lower <= 'z');
    const auto is_valid_symbol = tchar_symbols.contains(static_cast<char>(byte));
    return is_numeric || is_character || is_valid_symbol;
  }

  [[nodiscard]] inline bool is_header_field_name_token(std::string_view name) noexcept {
    return !name.empty() && std::ranges::all_of(name, is_tchar, to_byte);
  }

  [[nodiscard]] inline bool is_valid_field_value(std::string_view value) noexcept {
    constexpr auto ascii_delete = static_cast<unsigned char>(0x7F);
    constexpr auto obs_text_first_octet = static_cast<unsigned char>(0x80);

    auto is_valid = [](unsigned char byte) noexcept {
      return byte == '\t' || byte >= obs_text_first_octet || (byte >= ' ' && byte != ascii_delete);
    };
    return std::ranges::all_of(value, is_valid, to_byte);
  }

  [[nodiscard]] inline std::string_view trim_optional_whitespace(std::string_view text) {
    const auto first = text.find_first_not_of(optional_whitespace_chars);
    if (first == std::string_view::npos) {
      return {};
    }
    const auto last = text.find_last_not_of(optional_whitespace_chars);
    return text.substr(first, last - first + 1);
  }

  inline void add_header_value(header_fields& headers, header_line line) {
    headers.emplace(std::string{line.name}, std::string{line.value});
  }

  [[nodiscard]] inline std::error_code append_obsolete_fold(header_fields& headers, std::string_view last_header_name,
    std::string_view continuation) {
    const auto [range_begin, range_end] = headers.fields_of(last_header_name);
    if (range_begin == range_end) {
      return {};
    }

    if (continuation.find_first_of(detail::header_separator) != std::string_view::npos) {
      return http::error::protocol_error::header_line_invalid;
    }

    const auto trimmed = trim_optional_whitespace(continuation);
    if (trimmed.empty()) {
      return {};
    }

    if (!is_valid_field_value(trimmed)) {
      return http::error::protocol_error::header_line_invalid;
    }

    auto& last_value = std::prev(range_end)->second;
    if (!last_value.empty()) {
      last_value.push_back(' ');
    }
    last_value.append(trimmed);

    return {};
  }

  [[nodiscard]] inline bool is_comma_separated_header(std::string_view name) noexcept {
    return std::ranges::any_of(comma_separated_list_headers,
      [&](std::string_view candidate) noexcept { return aero::detail::ascii_iequal(name, candidate); });
  }

  inline void add_comma_separated_values(header_fields& headers, header_line line) {
    std::size_t segment_start = 0;
    bool inside_quoted_string = false;
    bool quoted_escape = false;
    std::size_t comment_depth = 0;
    bool comment_escape = false;

    auto emit_segment = [&](std::size_t segment_end) {
      auto whitespace_trimmed_value = trim_optional_whitespace(line.value.substr(segment_start, segment_end - segment_start));
      if (!whitespace_trimmed_value.empty()) {
        add_header_value(headers, header_line{.name = line.name, .value = whitespace_trimmed_value});
      }
    };

    for (std::size_t i{}; i < line.value.size(); ++i) {
      const char ch = line.value[i];

      if (comment_depth > 0) {
        if (comment_escape) {
          comment_escape = false;
          continue;
        }
        if (ch == '\\') {
          comment_escape = true;
          continue;
        }
        if (ch == '(') {
          ++comment_depth;
          continue;
        }
        if (ch == ')' && comment_depth > 0) {
          --comment_depth;
          continue;
        }
        continue;
      }

      if (inside_quoted_string) {
        if (quoted_escape) {
          quoted_escape = false;
          continue;
        }
        if (ch == '\\') {
          quoted_escape = true;
          continue;
        }
        if (ch == '"') {
          inside_quoted_string = false;
          continue;
        }
        continue;
      }

      if (ch == '"') {
        inside_quoted_string = true;
        continue;
      }

      if (ch == '(') {
        comment_depth = 1;
        continue;
      }

      if (ch == ',') {
        emit_segment(i);
        segment_start = i + 1;
      }
    }

    emit_segment(line.value.size());
  }

  inline void normalize_comma_separated_headers(header_fields& headers) {
    header_fields normalized;
    for (const auto& [name, value] : headers) {
      header_line line{.name = name, .value = value};
      if (is_comma_separated_header(name)) {
        add_comma_separated_values(normalized, line);
      } else {
        add_header_value(normalized, line);
      }
    }
    headers = std::move(normalized);
  }

  inline std::expected<header_line, std::error_code> process_header_line(header_fields& headers,
    std::string_view last_header_name, std::string_view line) {
    using http::error::protocol_error;
    constexpr auto npos = std::string_view::npos;

    const auto is_header_continuation = optional_whitespace_chars.contains(line.front());
    if (is_header_continuation) {
      if (last_header_name.empty()) {
        return std::unexpected(protocol_error::obs_fold_without_previous_header);
      }
      if (auto append_ec = append_obsolete_fold(headers, last_header_name, line); append_ec) {
        return std::unexpected(append_ec);
      }
      return header_line{};
    }

    const auto colon_position = line.find(':');
    if (colon_position == npos) {
      return std::unexpected(protocol_error::header_line_invalid);
    }

    const auto raw_name = line.substr(0, colon_position);
    const auto name = trim_optional_whitespace(raw_name);

    if (name.empty() || name.size() != raw_name.size() || !is_header_field_name_token(name)) {
      return std::unexpected(protocol_error::header_name_invalid);
    }

    const auto value = trim_optional_whitespace(line.substr(colon_position + 1));
    if (!is_valid_field_value(value)) {
      return std::unexpected(protocol_error::header_line_invalid);
    }

    return header_line{.name = name, .value = value};
  }

  [[nodiscard]] inline std::expected<header_fields, std::error_code> parse_headers(std::string_view buffer) {
    using http::error::protocol_error;
    constexpr auto npos = std::string_view::npos;

    const auto headers_end_position = buffer.find(headers_end_separator);
    if (headers_end_position == npos) {
      return std::unexpected(protocol_error::headers_section_incomplete);
    }

    header_fields result;
    std::string_view last_header_name;
    const auto headers_section = buffer.substr(0, headers_end_position);

    for (auto&& line_range : headers_section | std::views::split(header_separator)) {
      std::string_view line{line_range};
      if (line.empty()) {
        break;
      }

      auto contains_separator_characters = line.find_first_of(detail::header_separator) != npos;
      if (contains_separator_characters) {
        return std::unexpected(protocol_error::header_line_invalid);
      }

      auto processed_line = process_header_line(result, last_header_name, line);
      if (!processed_line) {
        return std::unexpected(processed_line.error());
      }

      if (processed_line.value().empty()) {
        continue;
      }

      add_header_value(result, *processed_line);
      last_header_name = processed_line->name;
    }

    normalize_comma_separated_headers(result);
    return result;
  }

} // namespace aero::http::detail

#endif
