#include "ut.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/error.hpp"

#include "websocket/test_helpers.hpp"

using aero::websocket::close_code;
using aero::websocket::protocol_error;
using aero::websocket::detail::frame;
using aero::websocket::detail::get_payload_length_size;
using aero::websocket::detail::masking_key;
using aero::websocket::detail::opcode;

using aero::tests::websocket::make_masking_key;
using aero::tests::websocket::make_minimal_frame;
using aero::tests::websocket::make_network_u16;

ut::suite websocket_frame = [] {
  "returns payload length size for boundary values"_test = [] {
    expect(get_payload_length_size(0) == 0);
    expect(get_payload_length_size(1) == 0);
    expect(get_payload_length_size(125) == 0);
    expect(get_payload_length_size(126) == 2);
    expect(get_payload_length_size(65535) == 2);
    expect(get_payload_length_size(65536) == 8);
  };

  "unmasked header size matches rfc base fields"_test = [] {
    frame frame = make_minimal_frame(opcode::text);

    frame.payload_length = 0;
    expect(frame.header_size() == 2);

    frame.payload_length = 125;
    expect(frame.header_size() == 2);

    frame.payload_length = 126;
    expect(frame.header_size() == 4);

    frame.payload_length = 65535;
    expect(frame.header_size() == 4);

    frame.payload_length = 65536;
    expect(frame.header_size() == 10);
  };

  "masked header size includes masking key"_test = [] {
    frame frame = make_minimal_frame(opcode::binary);
    frame.masked = true;
    frame.masking_key = make_masking_key(0x12, 0x34, 0x56, 0x78);

    frame.payload_length = 0;
    expect(frame.header_size() == 6);

    frame.payload_length = 125;
    expect(frame.header_size() == 6);

    frame.payload_length = 126;
    expect(frame.header_size() == 8);

    frame.payload_length = 65536;
    expect(frame.header_size() == 14);
  };

  "offsets match wire layout for given payload length encoding"_test = [] {
    frame frame = make_minimal_frame(opcode::binary);

    frame.payload_length = 0;
    expect(frame.payload_length_offset() == 2);
    expect(frame.masking_key_offset() == 2);

    frame.payload_length = 126;
    expect(frame.payload_length_offset() == 2);
    expect(frame.masking_key_offset() == 4);

    frame.payload_length = 65536;
    expect(frame.payload_length_offset() == 2);
    expect(frame.masking_key_offset() == 10);
  };

  "mask flag and masking key must be consistent"_test = [] {
    frame frame = make_minimal_frame(opcode::text);

    frame.masked = true;
    frame.masking_key.reset();
    expect(frame.validate() == protocol_error::masking_key_missing);

    frame.masked = false;
    frame.masking_key = make_masking_key(0x00, 0x00, 0x00, 0x00);
    expect(frame.validate() == protocol_error::masking_flag_missing);

    frame.masked = true;
    frame.masking_key = make_masking_key(0xAA, 0xBB, 0xCC, 0xDD);
    expect(frame.validate() == std::error_code{});
  };

  "control frames must be final"_test = [] {
    frame frame = make_minimal_frame(opcode::ping);
    frame.fin = false;
    expect(frame.validate() == protocol_error::control_frame_fragmented);

    frame = make_minimal_frame(opcode::pong);
    frame.fin = false;
    expect(frame.validate() == protocol_error::control_frame_fragmented);

    frame = make_minimal_frame(opcode::close);
    frame.fin = false;
    expect(frame.validate() == protocol_error::control_frame_fragmented);
  };

  "control frame payload length must not exceed 125"_test = [] {
    frame frame = make_minimal_frame(opcode::ping);
    frame.payload_length = 126;
    expect(frame.validate() == protocol_error::control_frame_payload_too_big);

    frame = make_minimal_frame(opcode::pong);
    frame.payload_length = 126;
    expect(frame.validate() == protocol_error::control_frame_payload_too_big);

    frame = make_minimal_frame(opcode::close);
    frame.payload_length = 126;
    expect(frame.validate() == protocol_error::control_frame_payload_too_big);
  };

  "close frame payload must be empty or at least 2 bytes"_test = [] {
    frame frame = make_minimal_frame(opcode::close);

    frame.payload_length = 0;
    frame.payload_data = std::span<const std::byte>{};
    frame.application_data = std::span<const std::byte>{};
    expect(frame.validate() == std::error_code{});

    std::array<std::byte, 1> single_byte_payload{
      static_cast<std::byte>(0x03),
    };
    frame.payload_length = single_byte_payload.size();
    frame.payload_data = std::span<const std::byte>{single_byte_payload.data(), single_byte_payload.size()};
    frame.application_data = frame.payload_data;
    expect(frame.validate() == protocol_error::close_frame_payload_too_small);

    auto normal_close_code_payload = make_network_u16(std::to_underlying(close_code::normal));
    frame.payload_length = normal_close_code_payload.size();
    frame.payload_data = std::span<const std::byte>{normal_close_code_payload.data(), normal_close_code_payload.size()};
    frame.application_data = frame.payload_data;
    expect(frame.validate() == std::error_code{});
  };

  "rejects reserved opcodes"_test = [] {
    frame frame = make_minimal_frame(static_cast<opcode>(0x3));
    expect(frame.validate() == protocol_error::opcode_reserved);

    frame = make_minimal_frame(static_cast<opcode>(0x7));
    expect(frame.validate() == protocol_error::opcode_reserved);

    frame = make_minimal_frame(static_cast<opcode>(0xB));
    expect(frame.validate() == protocol_error::opcode_reserved);

    frame = make_minimal_frame(static_cast<opcode>(0xF));
    expect(frame.validate() == protocol_error::opcode_reserved);
  };

  "reserved bits must be zero without extensions"_test = [] {
    frame frame = make_minimal_frame(opcode::binary);

    frame.rsv1 = true;
    expect(frame.validate() == protocol_error::reserved_bits_nonzero);

    frame = make_minimal_frame(opcode::binary);
    frame.rsv2 = true;
    expect(frame.validate() == protocol_error::reserved_bits_nonzero);

    frame = make_minimal_frame(opcode::binary);
    frame.rsv3 = true;
    expect(frame.validate() == protocol_error::reserved_bits_nonzero);
  };

  "payload length must not exceed 63 bits"_test = [] {
    frame frame = make_minimal_frame(opcode::binary);
    frame.payload_length = frame::max_allowed_payload_length;
    expect(frame.validate() == std::error_code{});

    frame.payload_length = frame::max_allowed_payload_length + 1ULL;
    expect(frame.validate() == protocol_error::payload_length_too_big);
  };

  "rejects close frames with invalid status codes"_test = [] {
    std::array<std::uint16_t, 6> invalid_status_codes{
      static_cast<std::uint16_t>(999),
      static_cast<std::uint16_t>(1004),
      static_cast<std::uint16_t>(1005),
      static_cast<std::uint16_t>(1006),
      static_cast<std::uint16_t>(1015),
      static_cast<std::uint16_t>(5000),
    };

    for (std::uint16_t status_code : invalid_status_codes) {

      std::array<std::byte, 2> payload = make_network_u16(status_code);

      frame frame = make_minimal_frame(opcode::close);
      frame.payload_length = payload.size();
      frame.payload_data = std::span<const std::byte>{payload.data(), payload.size()};
      frame.application_data = frame.payload_data;

      std::error_code validation_ec = frame.validate();
      expect(validation_ec != std::error_code{});
    }
  };
};

int main() {}
