#pragma once

#include <string_view>

namespace aero::http::detail {

  [[nodiscard]] constexpr bool is_tchar(char ch) noexcept {
    // RFC 9110, Section 5.6.2:
    // token = 1*tchar
    // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*"
    //       / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
    //       / DIGIT / ALPHA
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '!' || ch == '#' ||
           ch == '$' || ch == '%' || ch == '&' || ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.' || ch == '^' ||
           ch == '_' || ch == '`' || ch == '|' || ch == '~';
  }

  [[nodiscard]] constexpr bool is_pct_encoded(std::string_view str) noexcept {
    // RFC 3986, Appendix A:
    // pct-encoded = "%" HEXDIG HEXDIG

    if (str.size() != 3) {
      return false;
    }

    auto is_pct_encoded_hex_digit = [](char ch) -> bool {
      return ('0' <= ch && ch <= '9') || ('A' <= ch && ch <= 'F') || ('a' <= ch && ch <= 'f');
    };

    return str.starts_with('%') && is_pct_encoded_hex_digit(str[1]) && is_pct_encoded_hex_digit(str[2]);
  }

  [[nodiscard]] constexpr bool is_unreserved(char value) noexcept {
    // RFC 3986, Appendix A:
    // unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
    return ('A' <= value && value <= 'Z') || ('a' <= value && value <= 'z') || ('0' <= value && value <= '9') || value == '-' ||
           value == '.' || value == '_' || value == '~';
  }

  [[nodiscard]] constexpr bool is_sub_delim(char value) noexcept {
    // RFC 3986, Appendix A:
    // sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
    return value == '!' || value == '$' || value == '&' || value == '\'' || value == '(' || value == ')' || value == '*' ||
           value == '+' || value == ',' || value == ';' || value == '=';
  }

} // namespace aero::http::detail
