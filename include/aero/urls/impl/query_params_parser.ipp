#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "aero/detail/rfc_grammar.hpp"
#include "aero/urls/query_params.hpp"
#include "aero/util/string.hpp"
#include "aero/util/utf8.hpp"

namespace aero::urls {

  namespace detail {

    [[nodiscard]] constexpr unsigned char hex_digit_value(char c) noexcept {
      if (aero::is_digit(c)) {
        return static_cast<unsigned char>(c - '0');
      }
      return static_cast<unsigned char>(aero::ascii_tolower(c) - 'a' + 10);
    }

    // https://url.spec.whatwg.org/#percent-decode
    //
    // 0x25 (%) followed by two ASCII hex digits becomes the byte those digits
    // spell; every other byte, including a malformed %, is copied through
    [[nodiscard]] inline std::string percent_decode(std::string_view input) {
      std::string output;
      output.reserve(input.size());

      for (std::size_t i{}; i < input.size(); ++i) {
        if (aero::detail::is_pct_encoded(input.substr(i, 3))) {
          output.push_back(static_cast<char>((hex_digit_value(input[i + 1]) * 16) + hex_digit_value(input[i + 2])));
          i += 2;
        } else {
          output.push_back(input[i]);
        }
      }

      return output;
    }

    inline query_params parse_query_params(std::string_view str) {
      if (str.empty()) {
        return {};
      }

      query_params params;

      // https://url.spec.whatwg.org/#urlencoded-parsing
      for (auto&& bytes : str | std::views::split('&')) {
        std::string_view candidate{bytes};
        if (candidate.empty()) {
          // 1. If bytes is the empty byte sequence, then continue.
          continue;
        }

        std::size_t separator_pos = candidate.find('=');
        bool has_value = separator_pos != std::string_view::npos;

        std::string name;
        std::string value;

        // 2. If bytes contains a 0x3D (=), then let name be the bytes from the
        //    start of bytes up to but excluding its first 0x3D (=), and let
        //    value be the bytes, if any, after the first 0x3D (=) up to the end
        //    of bytes. If 0x3D (=) is the first byte, then name will be the
        //    empty byte sequence. If it is the last, then value will be the
        //    empty byte sequence.
        if (has_value) {
          name = candidate.substr(0, separator_pos);
          value = candidate.substr(separator_pos + 1);
        }
        // 3. Otherwise, let name have the value of bytes and let value be the
        //    empty byte sequence.
        else {
          name = candidate;
        }

        // 4. Replace any 0x2B (+) in name and value with 0x20 (SP).
        std::ranges::replace(name, '+', ' ');
        std::ranges::replace(value, '+', ' ');

        // 5. Let nameString and valueString be the result of running UTF-8
        //    decode without BOM on the percent-decoding of name and value,
        //    respectively.
        name = percent_decode(name);
        if (!aero::is_valid_utf8(name)) {
          name = aero::utf8_replace_ill_formed(name);
        }

        if (has_value) {
          value = percent_decode(value);
          if (!aero::is_valid_utf8(value)) {
            value = aero::utf8_replace_ill_formed(value);
          }
          params.add(std::move(name), std::move(value));
        } else {
          params.add(std::move(name));
        }
      }

      return params;
    }

  } // namespace detail

  inline std::expected<query_params, std::error_code> query_params::parse(std::string_view str) {
    return detail::parse_query_params(str);
  }

  inline std::expected<query_params, std::error_code> query_params::parse(std::span<const std::byte> bytes) {
    return detail::parse_query_params(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
  }

} // namespace aero::urls
