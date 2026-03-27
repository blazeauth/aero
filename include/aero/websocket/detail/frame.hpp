#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/concepts/masking_key_source.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"

namespace aero::websocket::detail {

  [[nodiscard]] constexpr std::uint32_t get_payload_length_size(std::uint64_t payload_length) {
    if (payload_length <= 125) {
      return 0;
    }
    if (payload_length >= 126 && payload_length <= 65535) {
      return 2;
    }
    if (payload_length > 65535) [[unlikely]] {
      return 8;
    }
    std::unreachable();
  }

  [[maybe_unused]] constexpr inline auto max_frame_header_size = 14ZU;

  using masking_key = concepts::masking_key;

  class frame {
   public:
    // FIN: 1 bit
    bool fin{false};
    // RSV1, RSV2, RSV3: 1 bit each
    bool rsv1{false};
    bool rsv2{false};
    bool rsv3{false};
    // Opcode: 4 bits
    detail::opcode opcode{};
    // Mask: 1 bit
    // Defines whether the "Payload data" is masked.
    bool masked{false};
    // Payload length: 7 bits, 7+16 bits, or 7+64 bits
    // Stores 7, 16 or 64 bits
    std::uint64_t payload_length{0};
    // Masking-key: 0 or 4 bytes
    std::optional<detail::masking_key> masking_key;

    // Payload data: (x+y) bytes
    std::span<const std::byte> payload_data;
    // Extension data: x bytes
    std::span<const std::byte> extension_data;
    // Application data: y bytes
    std::span<const std::byte> application_data;

    [[nodiscard]] bool is_continuation() const noexcept {
      return opcode == opcode::continuation;
    }

    [[nodiscard]] bool is_text() const noexcept {
      return opcode == opcode::text;
    }

    [[nodiscard]] bool is_binary() const noexcept {
      return opcode == opcode::binary;
    }

    [[nodiscard]] bool is_close() const noexcept {
      return opcode == opcode::close;
    }

    [[nodiscard]] bool is_ping() const noexcept {
      return opcode == opcode::ping;
    }

    [[nodiscard]] bool is_pong() const noexcept {
      return opcode == opcode::pong;
    }

    [[nodiscard]] bool is_control() const noexcept {
      return is_control_opcode(opcode);
    }

    // Frame is valid by RFC6455 (without extensions)
    [[nodiscard]] std::error_code validate() const noexcept {
      using websocket::error::protocol_error;

      if (masked && !masking_key.has_value()) [[unlikely]] {
        return protocol_error::masking_key_missing;
      }
      if (!masked && masking_key.has_value()) [[unlikely]] {
        return protocol_error::masking_flag_missing;
      }

      const auto is_control_frame = opcode == opcode::close || opcode == opcode::ping || opcode == opcode::pong;
      if (is_control_frame) {
        if (!fin) [[unlikely]] {
          return protocol_error::control_frame_fragmented;
        }
        if (payload_length > 125) [[unlikely]] {
          return protocol_error::control_frame_payload_too_big;
        }
      }

      if (auto close_code_ec = validate_close_code(); close_code_ec) {
        if (close_code_ec != protocol_error::close_code_missing) {
          return close_code_ec;
        }
      }

      if (auto opcode_ec = validate_opcode(); opcode_ec) {
        return opcode_ec;
      }

      if (rsv1 || rsv2 || rsv3) [[unlikely]] {
        return protocol_error::reserved_bits_nonzero;
      }
      if (payload_length > max_allowed_payload_length) [[unlikely]] {
        return protocol_error::payload_length_too_big;
      }

      return {};
    }

    [[nodiscard]] std::size_t header_size() const noexcept {
      return 2U + payload_length_size() + (masked ? 4U : 0U);
    }

    [[nodiscard]] std::ptrdiff_t masking_key_offset() const noexcept {
      return 2U + payload_length_size();
    }

    [[nodiscard]] std::uint32_t payload_length_size() const {
      return get_payload_length_size(payload_length);
    }

    [[nodiscard]] constexpr std::ptrdiff_t payload_length_offset() const noexcept {
      return 2U;
    }

    // Masking-aware close-code parser
    [[nodiscard]] std::expected<websocket::close_code, std::error_code> parse_close_code() const noexcept {
      using websocket::error::protocol_error;

      if (application_data.empty()) {
        return std::unexpected(protocol_error::close_code_missing);
      }
      if (application_data.size() < 2) {
        return std::unexpected(protocol_error::close_frame_payload_too_small);
      }

      auto read_octet = [&](std::size_t index) -> std::uint8_t {
        auto value = static_cast<std::uint8_t>(application_data[index]);
        if (masked) {
          auto key_octet = static_cast<std::uint8_t>((*masking_key)[index & 3U]);
          value = static_cast<std::uint8_t>(value ^ key_octet);
        }
        return value;
      };

      const auto high = static_cast<std::uint16_t>(read_octet(0));
      const auto low = static_cast<std::uint16_t>(read_octet(1));
      auto close_code_value = static_cast<std::uint16_t>(static_cast<std::uint32_t>(high << 8U) | low);
      return websocket::parse_close_code(close_code_value);
    }

    constexpr static std::uint64_t max_allowed_payload_length{0x7FFF'FFFF'FFFF'FFFFULL};

   private:
    [[nodiscard]] std::error_code validate_close_code() const noexcept {
      if (opcode != opcode::close) {
        return {};
      }
      return parse_close_code().error_or(std::error_code{});
    }

    [[nodiscard]] std::error_code validate_opcode() const noexcept {
      return parse_opcode(std::to_underlying(opcode)).error_or(std::error_code{});
    }
  };

} // namespace aero::websocket::detail
