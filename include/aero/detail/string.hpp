#pragma once

#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <expected>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

#include "aero/detail/concepts.hpp"

namespace aero::detail {

  [[maybe_unused]] constexpr inline int decimal_base = 10;
  [[maybe_unused]] constexpr inline int hexadecimal_base = 16;

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
    return nibbles | std::views::join | std::ranges::to<std::string>();
  }

  template <std::default_initializable T>
  [[nodiscard]] inline std::expected<T, std::error_code> to_decimal(std::string_view str, int base = decimal_base) noexcept {
    T value{};
    if (auto result = std::from_chars(str.data(), str.data() + str.size(), value, base); result.ec != std::errc{}) {
      return std::unexpected(std::make_error_code(result.ec));
    }
    return value;
  }

  template <std::default_initializable T>
    requires(std::is_enum_v<T>)
  [[nodiscard]] inline std::expected<T, std::error_code> to_decimal(std::string_view str, int base = decimal_base) noexcept {
    std::underlying_type_t<T> value{};
    if (auto result = std::from_chars(str.data(), str.data() + str.size(), value, base); result.ec != std::errc{}) {
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

  [[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
  }

  // Lowercases 8 packed bytes at once. Only 'A'..'Z' are changed.
  // The high-bit mask keeps per-byte additions from carrying into neighbors
  // and excludes non-ASCII bytes from classification.
  constexpr std::uint64_t ascii_tolower_u64(const uint64_t v) noexcept {
    constexpr std::uint64_t ones = 0x0101010101010101ULL;
    constexpr std::uint64_t high = 0x8080808080808080ULL;
    const std::uint64_t seven = v & ~high;
    const std::uint64_t ge_A = seven + ((0x80 - 'A') * ones);
    const std::uint64_t gt_Z = seven + ((0x80 - ('Z' + 1)) * ones);
    const std::uint64_t is_upper = ge_A & ~gt_Z & ~v & high;
    return v | (is_upper >> 2U);
  }

  // ASCII case-insensitive equality, checks sizes
  constexpr bool striequal(const std::string_view lhs, const std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) {
      return false;
    }

    if consteval {
      for (size_t i = 0; i < lhs.size(); ++i) {
        const auto lower = [](char c) {
          return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        };
        if (lower(lhs[i]) != lower(rhs[i])) {
          return false;
        }
      }
      return true;
    } else {
      const char* l = lhs.data();
      const char* r = rhs.data();
      std::uint64_t count = lhs.size();

      for (; count >= 8; l += 8, r += 8, count -= 8) {
        std::uint64_t a, b; // NOLINT
        std::memcpy(&a, l, 8);
        std::memcpy(&b, r, 8);
        if (ascii_tolower_u64(a) != ascii_tolower_u64(b)) {
          return false;
        }
      }

      if (count != 0U) {
        // Zero padding is safe, both sides pad identically and 0 is not 'A'..'Z'
        std::uint64_t a{}, b{};
        std::memcpy(&a, l, count);
        std::memcpy(&b, r, count);
        return ascii_tolower_u64(a) == ascii_tolower_u64(b);
      }

      return true;
    }
  }

} // namespace aero::detail
