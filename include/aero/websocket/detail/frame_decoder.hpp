#pragma once

#include <cassert>
#include <expected>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/util/bytes.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/detail/role.hpp"
#include "aero/websocket/error.hpp"

namespace aero::websocket::detail {

  template <websocket::detail::role ReceiverRole>
  class frame_decoder {
   public:
    [[nodiscard]] std::expected<frame, std::error_code> decode_header(std::span<const std::byte> buf) const {
      using aero::websocket::protocol_error;

      if (buf.size() < 2) {
        return std::unexpected(protocol_error::frame_too_small);
      }

      frame frame{};

      std::tie(frame.fin, frame.rsv1, frame.rsv2, frame.rsv3, frame.opcode) = parse_fin_rsv_and_opcode(buf[0]);
      const auto opcode = parse_opcode(std::to_underlying(frame.opcode));
      if (!opcode) {
        return std::unexpected(opcode.error());
      }

      std::tie(frame.masked, frame.payload_length) = parse_mask_and_payload_length(buf[1]);
      if (frame.masked && ReceiverRole == role::client) {
        return std::unexpected(protocol_error::masked_frame_from_server);
      }

      const auto payload_length_offset = frame.payload_length_offset();
      if (frame.payload_length == 126) {
        if (buf.size() < payload_length_offset + 2U) {
          return std::unexpected(protocol_error::buffer_truncated);
        }
        auto payload_length = aero::read_big_endian<uint16_t>(buf.subspan(payload_length_offset).first<2>());
        if (payload_length <= 125) {
          // Protocol violation, no extended length should've been passed
          return std::unexpected(protocol_error::payload_length_invalid);
        }
        frame.payload_length = payload_length;
      } else if (frame.payload_length == 127) {
        if (buf.size() < payload_length_offset + 8U) {
          return std::unexpected(protocol_error::buffer_truncated);
        }
        auto payload_length = aero::read_big_endian<uint64_t>(buf.subspan(payload_length_offset).first<8>());
        if (payload_length <= 65535) {
          // Protocol violation, payload_length should've been < 127
          return std::unexpected(protocol_error::payload_length_invalid);
        }
        frame.payload_length = payload_length;
      }

      if (frame.payload_length > frame::max_allowed_payload_length) [[unlikely]] {
        return std::unexpected(protocol_error::payload_length_too_big);
      }

      const auto header_size = frame.header_size();

      if (frame.masked) {
        if (buf.size() < header_size) {
          return std::unexpected(protocol_error::buffer_truncated);
        }
        frame.masking_key = read_masking_key(buf.subspan(frame.masking_key_offset()).first<4>());
      }

      if (frame.rsv1 || frame.rsv2 || frame.rsv3) [[unlikely]] {
        return std::unexpected(protocol_error::reserved_bits_nonzero);
      }
      if (frame.is_control()) {
        if (!frame.fin) [[unlikely]] {
          return std::unexpected(protocol_error::control_frame_fragmented);
        }
        if (frame.payload_length > 125) [[unlikely]] {
          return std::unexpected(protocol_error::control_frame_payload_too_big);
        }
      }
      if (frame.is_close() && frame.payload_length == 1U) {
        return std::unexpected(protocol_error::close_frame_payload_too_small);
      }

      return frame;
    }

    [[nodiscard]] std::vector<std::byte> unmask(std::span<const std::byte> masked_payload,
      detail::masking_key masking_key) const {
      std::vector<std::byte> result(masked_payload.size());

      for (std::size_t i{}; i < masked_payload.size(); ++i) {
        const auto key_octet = masking_key[i & 3U];
        const auto payload_octet = masked_payload[i];
        result[i] = payload_octet ^ key_octet;
      }

      return result;
    }

   private:
    using fin_rsv_opcode = std::tuple<bool, bool, bool, bool, opcode>;
    using mask_payload = std::tuple<bool, std::uint8_t>;

    [[nodiscard]] fin_rsv_opcode parse_fin_rsv_and_opcode(std::byte byte) const noexcept {
      const bool fin = (static_cast<std::uint8_t>(byte >> 7U) & 1U) != 0;
      const bool rsv1 = (static_cast<std::uint8_t>(byte >> 6U) & 1U) != 0;
      const bool rsv2 = (static_cast<std::uint8_t>(byte >> 5U) & 1U) != 0;
      const bool rsv3 = (static_cast<std::uint8_t>(byte >> 4U) & 1U) != 0;
      const std::uint8_t opcode = static_cast<std::uint8_t>(byte) & 0x0FU;
      return {fin, rsv1, rsv2, rsv3, static_cast<detail::opcode>(opcode)};
    }

    [[nodiscard]] mask_payload parse_mask_and_payload_length(std::byte byte) const noexcept {
      const bool masked = (static_cast<std::uint8_t>(byte >> 7U) & 1U) != 0;
      const auto payload = static_cast<std::uint8_t>(byte) & 0x7FU;
      return {masked, payload};
    }

    template <std::size_t BytesCount>
      requires(BytesCount == 2 || BytesCount == 8)
    [[nodiscard]] std::uint64_t read_extended_length(std::span<const std::byte, BytesCount> bytes) const noexcept {
      if constexpr (BytesCount == 2) {
        return aero::read_big_endian<std::uint16_t>(bytes);
      } else if constexpr (BytesCount == 8) {
        return aero::read_big_endian<std::uint64_t>(bytes);
      }
    }

    [[nodiscard]] std::array<std::byte, 4> read_masking_key(std::span<const std::byte, 4> bytes) const noexcept {
      return {bytes[0], bytes[1], bytes[2], bytes[3]};
    }
  };

} // namespace aero::websocket::detail
