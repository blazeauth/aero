#pragma once

#include <cstring>
#include <expected>
#include <span>
#include <system_error>

#include "aero/detail/bytes.hpp"
#include "aero/error.hpp"
#include "aero/websocket/detail/frame.hpp"

namespace aero::websocket::detail {

  class frame_encoder {
   public:
    constexpr frame_encoder() = default;

    [[nodiscard]] std::expected<std::size_t, std::error_code> encode(std::span<std::byte> out,
      const frame& frame) const noexcept {
      const auto header_size = frame.header_size();
      if (auto ec = frame.validate(); ec) {
        return std::unexpected(ec);
      }
      if (out.size() < header_size) [[unlikely]] {
        return std::unexpected(aero::error::basic_error::not_enough_memory);
      }

      out[0] = make_first_byte(frame);
      out[1] = make_second_byte(frame);

      auto extended_length_offset = frame.payload_length_offset();

      if (frame.payload_length >= 126 && frame.payload_length <= 65535) {
        aero::detail::write_big_endian<2>(out.subspan(extended_length_offset).first<2>(), frame.payload_length);
      } else if (frame.payload_length > 65535) {
        aero::detail::write_big_endian<8>(out.subspan(extended_length_offset).first<8>(), frame.payload_length);
      }

      if (frame.masked && frame.masking_key) {
        const auto offset = frame.masking_key_offset();
        std::memcpy(out.data() + offset, frame.masking_key->data(), frame.masking_key->size());
      }

      return header_size;
    }

    [[nodiscard]] std::error_code mask(std::span<std::byte> out, std::span<const std::byte> payload,
      detail::masking_key masking_key) const noexcept {
      if (out.size() < payload.size()) [[unlikely]] {
        return aero::error::basic_error::not_enough_memory;
      }

      for (std::size_t i{}; i < payload.size(); ++i) {
        const auto key_octet = masking_key[i & 3U];
        const auto payload_octet = payload[i];
        out[i] = payload_octet ^ key_octet;
      }

      return {};
    }

   private:
    [[nodiscard]] constexpr std::byte make_first_byte(const frame& frame) const noexcept {
      const auto fin_bit = frame.fin ? 0x80U : 0x00U;
      const auto rsv1_bit = frame.rsv1 ? 0x40U : 0x00U;
      const auto rsv2_bit = frame.rsv2 ? 0x20U : 0x00U;
      const auto rsv3_bit = frame.rsv3 ? 0x10U : 0x00U;
      const auto opcode_bits = std::to_underlying(frame.opcode) & 0x0FU;
      const auto first_byte = fin_bit | rsv1_bit | rsv2_bit | rsv3_bit | opcode_bits;
      return static_cast<std::byte>(first_byte);
    }

    [[nodiscard]] constexpr std::byte make_second_byte(const frame& frame) const noexcept {
      // RFC 6455 5.2 - Payload length:
      // If 0-125, that is the payload length.
      // If 126, the following 2 bytes interpreted as a
      // 16-bit unsigned integer are the payload length
      // If 127, the following 8 bytes interpreted as a
      // 64-bit unsigned integer are the payload length.

      // In plain words, if payload length is <= 125, first 7 bits
      // should be the payload length. If length is > 126, first
      // 7 bits should contain not the payload length but it's
      // "indicator", that will be 126 or 127.

      std::uint64_t payload_length_indicator{0};
      if (frame.payload_length <= 125) {
        payload_length_indicator = frame.payload_length;
      } else if (frame.payload_length >= 126 && frame.payload_length <= 65535) {
        payload_length_indicator = 126;
      } else if (frame.payload_length > 65535) {
        payload_length_indicator = 127;
      }

      const std::uint8_t mask_bit = frame.masked ? 0x80U : 0x00U;
      return static_cast<std::byte>(mask_bit | (payload_length_indicator & 0x7FU));
    }
  };

} // namespace aero::websocket::detail
