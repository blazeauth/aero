#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <string_view>
#include <system_error>

#include "aero/http/error.hpp"

// To deconflict Windows.h
#ifdef DELETE
#undef DELETE
#endif

namespace aero::http {

  enum class method : std::uint8_t {
    GET = 1,
    POST,
    PUT,
    PATCH,
    DELETE,
    CONNECT,
    TRACE,
    HEAD,
    OPTIONS,
  };

  [[nodiscard]] inline std::expected<http::method, std::error_code> parse_method(std::string_view method_str) {
    using method_entry = std::pair<std::string_view, http::method>;
    constexpr auto methods = std::to_array<method_entry>({
      {"GET", method::GET},
      {"POST", method::POST},
      {"PUT", method::PUT},
      {"PATCH", method::PATCH},
      {"DELETE", method::DELETE},
      {"CONNECT", method::CONNECT},
      {"TRACE", method::TRACE},
      {"HEAD", method::HEAD},
      {"OPTIONS", method::OPTIONS},
    });

    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find(methods, method_str, &method_entry::first);
    if (it == methods.end()) {
      return std::unexpected(http::protocol_error::method_invalid);
    }

    return it->second;
  }

  [[nodiscard]] constexpr std::string_view to_string(http::method method) {
    switch (method) {
    case http::method::GET:
      return "GET";
    case http::method::POST:
      return "POST";
    case http::method::PUT:
      return "PUT";
    case http::method::PATCH:
      return "PATCH";
    case http::method::DELETE:
      return "DELETE";
    case http::method::CONNECT:
      return "CONNECT";
    case http::method::TRACE:
      return "TRACE";
    case http::method::HEAD:
      return "HEAD";
    case http::method::OPTIONS:
      return "OPTIONS";
    }
    return {};
  }

  [[maybe_unused]] constexpr inline std::array methods{
    http::method::GET,
    http::method::POST,
    http::method::PUT,
    http::method::PATCH,
    http::method::DELETE,
    http::method::CONNECT,
    http::method::TRACE,
    http::method::HEAD,
    http::method::OPTIONS,
  };

} // namespace aero::http

template <>
struct std::formatter<aero::http::method> : std::formatter<std::string_view> {
  auto format(const aero::http::method& method, std::format_context& ctx) const {
    constexpr std::string_view unknown_method = "unknown_method";
    auto method_str = aero::http::to_string(method);
    return std::formatter<std::string_view>{}.format(method_str.empty() ? unknown_method : method_str, ctx);
  }
};
