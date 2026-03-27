#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <string_view>
#include <system_error>

#include "aero/http/error.hpp"

namespace aero::http {

  enum class method : std::uint8_t {
    get,
    post,
    put,
    patch,
    delete_,
    head,
    options,
  };

  [[nodiscard]] inline std::expected<http::method, std::error_code> parse_method(std::string_view method_str) {
    using method_entry = std::pair<std::string_view, http::method>;
    constexpr auto methods = std::to_array<method_entry>({
      {"GET", method::get},
      {"POST", method::post},
      {"PUT", method::put},
      {"PATCH", method::patch},
      {"DELETE", method::delete_},
      {"HEAD", method::head},
      {"OPTIONS", method::options},
    });

    // NOLINTNEXTLINE(readability-qualified-auto)
    const auto it = std::ranges::find(methods, method_str, &method_entry::first);
    if (it == methods.end()) {
      return std::unexpected(http::error::protocol_error::method_invalid);
    }
    return it->second;
  }

  [[nodiscard]] inline std::string_view to_string(http::method method) {
    switch (method) {
    case http::method::get:
      return "GET";
    case http::method::post:
      return "POST";
    case http::method::put:
      return "PUT";
    case http::method::patch:
      return "PATCH";
    case http::method::delete_:
      return "DELETE";
    case http::method::head:
      return "HEAD";
    case http::method::options:
      return "OPTIONS";
    }
    return {};
  }

  [[maybe_unused]] constexpr inline std::array methods{
    http::method::get,
    http::method::post,
    http::method::put,
    http::method::patch,
    http::method::delete_,
    http::method::head,
    http::method::options,
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
