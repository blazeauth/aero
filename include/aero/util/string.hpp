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

namespace aero {

  namespace detail {

    // Lowercases 8 packed bytes at once. Only 'A'..'Z' are changed.
    // The high-bit mask keeps per-byte additions from carrying into neighbors
    // and excludes non-ASCII bytes from classification
    constexpr std::uint64_t ascii_tolower_u64(const std::uint64_t v) noexcept {
      constexpr std::uint64_t ones = 0x0101010101010101ULL;
      constexpr std::uint64_t high = 0x8080808080808080ULL;
      const std::uint64_t seven = v & ~high;
      const std::uint64_t ge_A = seven + ((0x80 - 'A') * ones);
      const std::uint64_t gt_Z = seven + ((0x80 - ('Z' + 1)) * ones);
      const std::uint64_t is_upper = ge_A & ~gt_Z & ~v & high;
      return v | (is_upper >> 2U);
    }

  } // namespace detail

  [[nodiscard]] constexpr char ascii_tolower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }

  [[nodiscard]] constexpr bool is_upper(char c) noexcept {
    return c >= 'A' && c <= 'Z';
  }

  [[nodiscard]] constexpr bool is_lower(char c) noexcept {
    return c >= 'a' && c <= 'z';
  }

  [[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
  }

  [[nodiscard]] constexpr bool is_alpha(char c) noexcept {
    return is_upper(c) || is_lower(c);
  }

  [[nodiscard]] constexpr bool is_alnum(char c) noexcept {
    return is_alpha(c) || is_digit(c);
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
  [[nodiscard]] constexpr std::expected<T, std::error_code> to_decimal(std::string_view str, int base = 10) noexcept {
    T value{};
    if (auto result = std::from_chars(str.data(), str.data() + str.size(), value, base); result.ec != std::errc{}) {
      return std::unexpected(std::make_error_code(result.ec));
    }
    return value;
  }

  [[nodiscard]] constexpr std::string to_lowercase(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (char ch : str) {
      result.push_back(ascii_tolower(ch));
    }
    return result;
  }

  // ASCII case-insensitive equality, checks sizes
  [[nodiscard]] constexpr bool striequal(std::string_view lhs, std::string_view rhs) noexcept {
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
        if (detail::ascii_tolower_u64(a) != detail::ascii_tolower_u64(b)) {
          return false;
        }
      }

      if (count != 0U) {
        // Zero padding is safe, both sides pad identically and 0 is not 'A'..'Z'
        std::uint64_t a{0}, b{0};
        std::memcpy(&a, l, count);
        std::memcpy(&b, r, count);
        return detail::ascii_tolower_u64(a) == detail::ascii_tolower_u64(b);
      }

      return true;
    }
  }

} // namespace aero
