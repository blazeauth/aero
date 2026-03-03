#ifndef AERO_WEBSOCKET_DETAIL_CLOSE_CODE_HPP
#define AERO_WEBSOCKET_DETAIL_CLOSE_CODE_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <system_error>

#include "aero/websocket/error.hpp"

namespace aero::websocket::detail {

  enum class opcode : std::uint8_t {
    continuation = 0x0,
    text = 0x1,
    binary = 0x2,
    close = 0x8,
    ping = 0x9,
    pong = 0xA,
  };

  [[nodiscard]] constexpr bool is_control_opcode(opcode value) noexcept {
    return value == opcode::close || value == opcode::ping || value == opcode::pong;
  }

  [[nodiscard]] constexpr bool is_reserved_opcode(std::uint8_t value) noexcept {
    auto is_in_range = [value](std::uint8_t min, std::uint8_t max) {
      return value >= min && value <= max;
    };
    return is_in_range(0x3, 0x7) || is_in_range(0xB, 0xF);
  }

  [[nodiscard]] inline std::expected<opcode, std::error_code> to_opcode(std::uint8_t value) {
    constexpr std::array opcodes{opcode::continuation, opcode::text, opcode::binary, opcode::close, opcode::ping, opcode::pong};
    if (std::ranges::contains(opcodes, static_cast<opcode>(value))) {
      return static_cast<opcode>(value);
    }

    return std::unexpected(error::protocol_error::opcode_invalid);
  }

  [[nodiscard]] inline std::expected<opcode, std::error_code> parse_opcode(std::uint8_t value) {
    if (is_reserved_opcode(value)) {
      return std::unexpected(error::protocol_error::opcode_reserved);
    }

    return to_opcode(value);
  }

} // namespace aero::websocket::detail

#endif
