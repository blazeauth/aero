#ifndef AERO_DETAIL_BYTES_HPP
#define AERO_DETAIL_BYTES_HPP

#include <array>
#include <concepts>
#include <span>
#include <type_traits>

namespace aero::detail {

  template <std::unsigned_integral T>
  [[nodiscard]] inline T read_big_endian(std::span<const std::byte, sizeof(T)> bytes) noexcept {
    std::uint64_t value{};
    for (std::byte byte : bytes) {
      value = (value << 8U) | std::to_integer<std::uint64_t>(byte); // NOLINT(*-magic-numbers)
    }
    return static_cast<T>(value);
  }

  template <typename T>
    requires(std::is_enum_v<T>)
  [[nodiscard]] inline T read_big_endian(std::span<const std::byte, sizeof(T)> bytes) noexcept {
    return static_cast<T>(read_big_endian<std::underlying_type_t<T>>(bytes));
  }

  template <std::size_t ByteCount>
    requires(ByteCount <= sizeof(std::uint64_t))
  inline void write_big_endian(std::span<std::byte, ByteCount> out, std::uint64_t value) noexcept {
    for (std::size_t i{}; i < ByteCount; ++i) {
      const auto shift = (ByteCount - 1U - i) * 8U;
      out[i] = static_cast<std::byte>((value >> shift) & 0xFFULL); // NOLINT(*-magic-numbers)
    }
  }

  template <std::size_t ByteCount>
    requires(ByteCount <= sizeof(std::uint64_t))
  inline std::array<std::byte, ByteCount> write_big_endian(std::uint64_t value) noexcept {
    std::array<std::byte, ByteCount> result{};
    write_big_endian(result, value);
    return result;
  }

} // namespace aero::detail

#endif
