#ifndef AERO_WEBSOCKET_DETAIL_FRAME_DECODER_HPP
#define AERO_WEBSOCKET_DETAIL_FRAME_DECODER_HPP

#include <cassert>
#include <expected>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/detail/bytes.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/detail/role.hpp"
#include "aero/websocket/error.hpp"

namespace aero::websocket::detail {

  template <websocket::detail::role ReceiverRole>
  class frame_decoder {
   public:
    [[nodiscard]] std::expected<frame, std::error_code> decode(std::span<const std::byte> buf) {
      using aero::websocket::error::protocol_error;

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
      if (buf.size() < frame.header_size()) {
        return std::unexpected(protocol_error::buffer_truncated);
      }
      if (frame.masked && receiver_role_ == role::client) {
        return std::unexpected(protocol_error::masked_frame_from_server);
      }

      const auto payload_length_offset = frame.payload_length_offset();
      if (frame.payload_length == 126) {
        auto payload_length = aero::detail::read_big_endian<uint16_t>(buf.subspan(payload_length_offset).first<2>());
        if (payload_length <= 125) {
          // Protocol violation, no extended length should've been passed
          return std::unexpected(protocol_error::payload_length_invalid);
        }
        frame.payload_length = payload_length;
      } else if (frame.payload_length == 127) {
        auto payload_length = aero::detail::read_big_endian<uint64_t>(buf.subspan(payload_length_offset).first<8>());
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
          return std::unexpected(protocol_error::masking_key_missing);
        }
        frame.masking_key = read_masking_key(buf.subspan(frame.masking_key_offset()).first<4>());
      }

      if (buf.size() < header_size + frame.payload_length) {
        return std::unexpected(protocol_error::buffer_truncated);
      }

      frame.payload_data = buf.subspan(header_size, frame.payload_length);
      frame.application_data = frame.payload_data;

      if (auto ec = frame.validate(); ec) {
        return std::unexpected(ec);
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
        return aero::detail::read_big_endian<std::uint16_t>(bytes);
      } else if constexpr (BytesCount == 8) {
        return aero::detail::read_big_endian<std::uint64_t>(bytes);
      }
    }

    [[nodiscard]] std::array<std::byte, 4> read_masking_key(std::span<const std::byte, 4> bytes) const noexcept {
      return {bytes[0], bytes[1], bytes[2], bytes[3]};
    }

    websocket::detail::role receiver_role_{ReceiverRole};
  };

} // namespace aero::websocket::detail

#endif
