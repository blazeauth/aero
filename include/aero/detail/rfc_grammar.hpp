#pragma once

#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "aero/util/string.hpp"

namespace aero::detail {

  [[nodiscard]] constexpr bool is_tchar(char c) noexcept {
    // RFC 9110, Section 5.6.2:
    // token = 1*tchar
    // tchar = "!" / "#" / "$" / "%" / "&" / "'" / "*"
    //       / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
    //       / DIGIT / ALPHA
    return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' ||
           c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~' || aero::is_digit(c) || aero::is_alpha(c);
  }

  [[nodiscard]] constexpr bool is_pct_encoded(std::string_view str) noexcept {
    // RFC 3986, Appendix A:
    // pct-encoded = "%" HEXDIG HEXDIG

    if (str.size() != 3) {
      return false;
    }

    auto is_pct_encoded_hex_digit = [](char c) -> bool {
      return aero::is_digit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
    };

    return str.starts_with('%') && is_pct_encoded_hex_digit(str[1]) && is_pct_encoded_hex_digit(str[2]);
  }

  [[nodiscard]] constexpr bool is_unreserved(char c) noexcept {
    // RFC 3986, Appendix A:
    // unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
    return aero::is_alpha(c) || aero::is_digit(c) || c == '-' || c == '.' || c == '_' || c == '~';
  }

  [[nodiscard]] constexpr bool is_sub_delim(char c) noexcept {
    // RFC 3986, Appendix A:
    // sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
    return c == '!' || c == '$' || c == '&' || c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' || c == ',' ||
           c == ';' || c == '=';
  }

  [[nodiscard]] constexpr bool is_unencoded_pchar(char c) noexcept {
    // RFC 3986, Appendix A:
    // pchar = unreserved / pct-encoded / sub-delims / ":" / "@"
    return is_unreserved(c) || is_sub_delim(c) || c == ':' || c == '@';
  }

  template <std::predicate<char> Pred>
  [[nodiscard]] constexpr bool is_pct_encoded_sequence(std::string_view str, Pred is_allowed) noexcept(
    std::is_nothrow_invocable_v<Pred, char>) {
    for (std::size_t i{}; i < str.size(); i++) {
      char c = str[i];
      if (c == '%') {
        if (!is_pct_encoded(str.substr(i, 3))) {
          return false;
        }

        i += 2;
        continue;
      }

      if (!is_allowed(c)) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] constexpr bool is_pchar_sequence(std::string_view str) noexcept {
    // RFC 3986, Appendix A:
    // segment = *pchar
    return is_pct_encoded_sequence(str, is_unencoded_pchar);
  }

  [[nodiscard]] constexpr bool is_reg_name(std::string_view str) noexcept {
    // RFC 3986, Appendix A:
    // reg-name = *( unreserved / pct-encoded / sub-delims )
    return is_pct_encoded_sequence(str, [](char c) { return detail::is_unreserved(c) || detail::is_sub_delim(c); });
  }

} // namespace aero::detail
