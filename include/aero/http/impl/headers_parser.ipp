#ifndef AERO_HTTP_IMPL_HEADER_PARSER_IPP
#define AERO_HTTP_IMPL_HEADER_PARSER_IPP

#pragma once

#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

namespace aero::http {

  namespace {

    using http::error::protocol_error;

    struct field_view {
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

    inline void add_header(headers& headers, field_view line) {
      headers.add(std::string{line.name}, std::string{line.value});
    }

    [[nodiscard]] inline std::error_code append_obsolete_fold(headers& headers, std::string_view last_header_name,
      std::string_view continuation) {
      auto fields = headers.fields(last_header_name);
      if (fields.empty()) {
        return {};
      }

      if (continuation.find_first_of(detail::crlf) != std::string_view::npos) {
        return protocol_error::header_line_invalid;
      }

      const auto trimmed = trim_optional_whitespace(continuation);
      if (trimmed.empty()) {
        return {};
      }

      if (!is_valid_field_value(trimmed)) {
        return protocol_error::header_line_invalid;
      }

      auto it = fields.begin();
      auto last_it = it; // In case range contains only one element
      for (++it; it != fields.end(); ++it) {
        last_it = it;
      }

      std::string& last_value = last_it->value;

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

    inline void add_comma_separated_values(headers& headers, field_view field) {
      std::size_t segment_start = 0;
      bool inside_quoted_string = false;
      bool quoted_escape = false;
      std::size_t comment_depth = 0;
      bool comment_escape = false;

      auto emit_segment = [&](std::size_t segment_end) {
        auto whitespace_trimmed_value =
          trim_optional_whitespace(field.value.substr(segment_start, segment_end - segment_start));
        if (!whitespace_trimmed_value.empty()) {
          headers.add(std::string{field.name}, std::string{whitespace_trimmed_value});
        }
      };

      for (std::size_t i{}; i < field.value.size(); ++i) {
        const char ch = field.value[i];

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

      emit_segment(field.value.size());
    }

    inline void normalize_comma_separated_headers(headers& headers) {
      http::headers normalized;
      for (const auto& [name, value] : headers) {
        field_view field{.name = name, .value = value};
        if (is_comma_separated_header(name)) {
          add_comma_separated_values(normalized, field);
        } else {
          normalized.add(std::string{field.name}, std::string{field.value});
        }
      }
      headers = std::move(normalized);
    }

    inline std::expected<field_view, std::error_code> process_header_field(headers& headers, std::string_view last_header_name,
      std::string_view line) {
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
        return field_view{};
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

      return field_view{.name = name, .value = value};
    }

    [[nodiscard]] inline std::expected<headers, std::error_code> parse_headers(std::string_view buffer) {
      constexpr auto npos = std::string_view::npos;

      const auto headers_end_position = buffer.find(detail::double_crlf);
      if (headers_end_position == npos) {
        return std::unexpected(protocol_error::headers_section_incomplete);
      }

      headers result;
      std::string_view last_header_name;
      std::string_view headers_section = buffer.substr(0, headers_end_position);

      for (auto&& line_range : headers_section | std::views::split(detail::crlf)) {
        std::string_view line{line_range};
        if (line.empty()) {
          break;
        }

        auto contains_separator_characters = line.find_first_of(detail::crlf) != npos;
        if (contains_separator_characters) {
          return std::unexpected(protocol_error::header_line_invalid);
        }

        auto field = process_header_field(result, last_header_name, line);
        if (!field) {
          return std::unexpected(field.error());
        }

        if (field->empty()) {
          continue;
        }

        result.add(std::string{field->name}, std::string{field->value});
        last_header_name = field->name;
      }

      normalize_comma_separated_headers(result);
      return result;
    }

  } // namespace

  inline std::expected<headers, std::error_code> headers::parse(std::string_view buffer) {
    return parse_headers(buffer);
  }

  inline std::expected<headers, std::error_code> headers::parse(std::span<const std::byte> buffer) {
    return parse_headers(std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()});
  }

} // namespace aero::http

#endif
