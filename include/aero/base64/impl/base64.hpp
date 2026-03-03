#ifndef AERO_BASE64_IMPL_BASE64_HPP
#define AERO_BASE64_IMPL_BASE64_HPP

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace aero::detail {

  // NOLINTBEGIN(*-magic-numbers, *-signed-bitwise)

  constexpr inline std::string_view base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  constexpr std::size_t base64_encoded_size(std::size_t input_size) {
    return 4 * ((input_size + 2) / 3);
  }

  constexpr inline auto base64_decode_table = [] {
    std::array<std::int16_t, 256> table{};
    table.fill(-1);
    for (std::size_t i{}; i < base64_chars.size(); ++i) {
      table[static_cast<unsigned char>(base64_chars[i])] = static_cast<std::int16_t>(i);
    }
    return table;
  }();

  [[nodiscard]] inline std::string base64_encode(std::span<const std::byte> input) {
    std::string decoded_bytes;
    decoded_bytes.reserve(base64_encoded_size(input.size()));

    constexpr std::uint32_t sextet_mask = 0x3FU;
    std::uint32_t bit_buffer = 0;
    int buffered_bits = 0;

    for (std::byte byte : input) {
      bit_buffer = (bit_buffer << 8) | std::to_underlying(byte);
      buffered_bits += 8;

      while (buffered_bits >= 6) {
        buffered_bits -= 6;
        decoded_bytes.push_back(base64_chars[(bit_buffer >> buffered_bits) & sextet_mask]);
      }
    }

    if (buffered_bits != 0) {
      bit_buffer <<= (6 - buffered_bits);
      decoded_bytes.push_back(base64_chars[bit_buffer & sextet_mask]);
    }

    while ((decoded_bytes.size() % 4) != 0U) {
      decoded_bytes.push_back('=');
    }

    return decoded_bytes;
  }

  [[nodiscard]] inline std::string base64_encode(std::string_view input) {
    return base64_encode(std::span{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  }

  [[nodiscard]] inline std::string base64_decode(std::span<const std::byte> encoded_input) {
    std::string decoded_bytes;
    decoded_bytes.reserve((encoded_input.size() * 3) / 4);

    constexpr std::uint32_t byte_mask = 0xFFU;
    std::uint32_t bit_buffer = 0;
    int buffered_bits = 0;

    for (std::byte encoded_byte : encoded_input) {
      const int table_value = base64_decode_table[std::to_underlying(encoded_byte)];
      if (table_value < 0) {
        break;
      }

      bit_buffer = (bit_buffer << 6) | static_cast<std::uint32_t>(table_value);
      buffered_bits += 6;

      if (buffered_bits >= 8) {
        buffered_bits -= 8;
        decoded_bytes.push_back(static_cast<char>((bit_buffer >> buffered_bits) & byte_mask));
      }
    }

    return decoded_bytes;
  }

  [[nodiscard]] inline std::string base64_decode(std::string_view input) {
    return base64_decode(std::span{reinterpret_cast<const std::byte*>(input.data()), input.size()});
  }

  // NOLINTEND(*-magic-numbers, *-signed-bitwise)

} // namespace aero::detail

#endif
