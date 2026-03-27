#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <expected>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "aero/detail/concepts.hpp"

namespace aero::detail {

  [[nodiscard]] constexpr bool is_ascii(char c) noexcept {
    constexpr auto ascii_table_end = 0x7F; // DEL char
    return static_cast<unsigned char>(c) <= ascii_table_end;
  }

  constexpr std::string_view as_string_view(std::span<const std::byte> span) {
    return {reinterpret_cast<const char*>(span.data()), span.size()};
  }

  [[nodiscard]] inline std::string to_hex_string(std::span<const std::byte> bytes) {
    constexpr std::array lookup_table{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    auto nibbles = bytes | std::views::transform([&](std::byte b) {
      const auto hi = static_cast<std::size_t>(b >> 4U);
      const auto lo = static_cast<std::size_t>(b & static_cast<std::byte>(0x0FU));
      return std::array{lookup_table[hi], lookup_table[lo]};
    });
    return std::ranges::to<std::string>(nibbles | std::views::join);
  }

  template <std::default_initializable T>
  [[nodiscard]] inline std::expected<T, std::error_code> to_decimal(std::string_view str) {
    T value{};
    if (auto result = std::from_chars(str.data(), str.data() + str.size(), value); result.ec != std::errc{}) {
      return std::unexpected(std::make_error_code(result.ec));
    }
    return value;
  }

  template <std::default_initializable T>
    requires(std::is_enum_v<T>)
  [[nodiscard]] inline std::expected<T, std::error_code> to_decimal(std::string_view str) {
    std::underlying_type_t<T> value{};
    if (auto result = std::from_chars(str.data(), str.data() + str.size(), value); result.ec != std::errc{}) {
      return std::unexpected(std::make_error_code(result.ec));
    }
    return static_cast<T>(value);
  }

  [[nodiscard]] constexpr auto to_ascii_lower(unsigned char byte) noexcept {
    if (byte >= 'A' && byte <= 'Z') {
      return static_cast<unsigned char>(byte + ('a' - 'A'));
    }
    return byte;
  }

  [[nodiscard]] constexpr bool ascii_iequal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
      return false;
    }

    for (std::size_t i{}; i < left.size(); ++i) {
      auto left_char = to_ascii_lower(static_cast<unsigned char>(left[i]));
      auto right_char = to_ascii_lower(static_cast<unsigned char>(right[i]));

      if (left_char != right_char) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] constexpr std::string to_lowercase(std::string_view str) {
    std::string result(str.size(), '\0');
    for (std::size_t i{}; i < str.size(); i++) {
      result[i] = static_cast<char>(to_ascii_lower(static_cast<unsigned char>(str[i])));
    }
    return result;
  }

  // Replacement for std::views::join_with because it is unimplemented in too many standarts
  template <concepts::string_view_constructible_range Range>
  [[nodiscard]] inline std::string join_strings(Range&& parts, std::string_view delimiter) {
#if __cpp_lib_ranges_join_with >= 202202L && __cpp_lib_ranges_to_container >= 202202L
    return std::views::join_with(std::forward<Range>(parts), delimiter) | std::ranges::to<std::string>();
#else
    std::size_t total_size = 0;
    std::size_t parts_count = 0;

    for (auto&& part : parts) {
      std::string_view view{part};
      total_size += view.size();
      ++parts_count;
    }

    if (parts_count > 1 && !delimiter.empty()) {
      total_size += delimiter.size() * (parts_count - 1);
    }

    std::string result;
    result.reserve(total_size);

    bool is_first = true;
    for (auto&& part : parts) {
      if (!std::exchange(is_first, false) && !delimiter.empty()) {
        result.append(delimiter);
      }
      result.append(std::string_view{part});
    }

    return result;
#endif
  }

  template <concepts::string_view_constructible_range Range>
  [[nodiscard]] inline std::string join_strings(Range&& parts, char delimiter) {
    const std::array<char, 1> delimiter_storage{delimiter};
    return join_strings(std::forward<Range>(parts), std::string_view{delimiter_storage.data(), 1});
  }

} // namespace aero::detail
