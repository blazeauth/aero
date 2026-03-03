#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/error.hpp"

#include "test_helpers.hpp"

namespace {

  using aero::websocket::close_code;
  using aero::websocket::detail::frame;
  using aero::websocket::detail::get_payload_length_size;
  using aero::websocket::detail::masking_key;
  using aero::websocket::detail::opcode;
  using aero::websocket::error::protocol_error;

  using aero::tests::websocket::make_masking_key;
  using aero::tests::websocket::make_minimal_frame;
  using aero::tests::websocket::make_network_u16;

} // namespace

TEST(WebsocketDataFraming, ReturnsPayloadLengthSizeForBoundaryValues) {
  EXPECT_EQ(get_payload_length_size(0), 0);
  EXPECT_EQ(get_payload_length_size(1), 0);
  EXPECT_EQ(get_payload_length_size(125), 0);
  EXPECT_EQ(get_payload_length_size(126), 2);
  EXPECT_EQ(get_payload_length_size(65535), 2);
  EXPECT_EQ(get_payload_length_size(65536), 8);
}

TEST(WebsocketDataFraming, UnmaskedHeaderSizeMatchesRfcBaseFields) {
  frame frame = make_minimal_frame(opcode::text);

  frame.payload_length = 0;
  EXPECT_EQ(frame.header_size(), 2);

  frame.payload_length = 125;
  EXPECT_EQ(frame.header_size(), 2);

  frame.payload_length = 126;
  EXPECT_EQ(frame.header_size(), 4);

  frame.payload_length = 65535;
  EXPECT_EQ(frame.header_size(), 4);

  frame.payload_length = 65536;
  EXPECT_EQ(frame.header_size(), 10);
}

TEST(WebsocketDataFraming, MaskedHeaderSizeIncludesMaskingKey) {
  frame frame = make_minimal_frame(opcode::binary);
  frame.masked = true;
  frame.masking_key = make_masking_key(0x12, 0x34, 0x56, 0x78);

  frame.payload_length = 0;
  EXPECT_EQ(frame.header_size(), 6);

  frame.payload_length = 125;
  EXPECT_EQ(frame.header_size(), 6);

  frame.payload_length = 126;
  EXPECT_EQ(frame.header_size(), 8);

  frame.payload_length = 65536;
  EXPECT_EQ(frame.header_size(), 14);
}

TEST(WebsocketDataFraming, OffsetsMatchWireLayoutForGivenPayloadLengthEncoding) {
  frame frame = make_minimal_frame(opcode::binary);

  frame.payload_length = 0;
  EXPECT_EQ(frame.payload_length_offset(), 2);
  EXPECT_EQ(frame.masking_key_offset(), 2);

  frame.payload_length = 126;
  EXPECT_EQ(frame.payload_length_offset(), 2);
  EXPECT_EQ(frame.masking_key_offset(), 4);

  frame.payload_length = 65536;
  EXPECT_EQ(frame.payload_length_offset(), 2);
  EXPECT_EQ(frame.masking_key_offset(), 10);
}

TEST(WebsocketDataFraming, MaskFlagAndMaskingKeyMustBeConsistent) {
  frame frame = make_minimal_frame(opcode::text);

  frame.masked = true;
  frame.masking_key.reset();
  EXPECT_EQ(frame.validate(), protocol_error::masking_key_missing);

  frame.masked = false;
  frame.masking_key = make_masking_key(0x00, 0x00, 0x00, 0x00);
  EXPECT_EQ(frame.validate(), protocol_error::masking_flag_missing);

  frame.masked = true;
  frame.masking_key = make_masking_key(0xAA, 0xBB, 0xCC, 0xDD);
  EXPECT_EQ(frame.validate(), std::error_code{});
}

TEST(WebsocketDataFraming, ControlFramesMustBeFinal) {
  frame frame = make_minimal_frame(opcode::ping);
  frame.fin = false;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_fragmented);

  frame = make_minimal_frame(opcode::pong);
  frame.fin = false;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_fragmented);

  frame = make_minimal_frame(opcode::close);
  frame.fin = false;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_fragmented);
}

TEST(WebsocketDataFraming, ControlFramePayloadLengthMustNotExceed125) {
  frame frame = make_minimal_frame(opcode::ping);
  frame.payload_length = 126;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_payload_too_big);

  frame = make_minimal_frame(opcode::pong);
  frame.payload_length = 126;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_payload_too_big);

  frame = make_minimal_frame(opcode::close);
  frame.payload_length = 126;
  EXPECT_EQ(frame.validate(), protocol_error::control_frame_payload_too_big);
}

TEST(WebsocketDataFraming, CloseFramePayloadMustBeEmptyOrAtLeast2Bytes) {
  frame frame = make_minimal_frame(opcode::close);

  frame.payload_length = 0;
  frame.payload_data = std::span<const std::byte>{};
  frame.application_data = std::span<const std::byte>{};
  EXPECT_EQ(frame.validate(), std::error_code{});

  std::array<std::byte, 1> single_byte_payload{
    static_cast<std::byte>(0x03),
  };
  frame.payload_length = single_byte_payload.size();
  frame.payload_data = std::span<const std::byte>{single_byte_payload.data(), single_byte_payload.size()};
  frame.application_data = frame.payload_data;
  EXPECT_EQ(frame.validate(), protocol_error::close_frame_payload_too_small);

  auto normal_close_code_payload = make_network_u16(std::to_underlying(close_code::normal));
  frame.payload_length = normal_close_code_payload.size();
  frame.payload_data = std::span<const std::byte>{normal_close_code_payload.data(), normal_close_code_payload.size()};
  frame.application_data = frame.payload_data;
  EXPECT_EQ(frame.validate(), std::error_code{});
}

TEST(WebsocketDataFraming, RejectsReservedOpcodes) {
  frame frame = make_minimal_frame(static_cast<opcode>(0x3));
  EXPECT_EQ(frame.validate(), protocol_error::opcode_reserved);

  frame = make_minimal_frame(static_cast<opcode>(0x7));
  EXPECT_EQ(frame.validate(), protocol_error::opcode_reserved);

  frame = make_minimal_frame(static_cast<opcode>(0xB));
  EXPECT_EQ(frame.validate(), protocol_error::opcode_reserved);

  frame = make_minimal_frame(static_cast<opcode>(0xF));
  EXPECT_EQ(frame.validate(), protocol_error::opcode_reserved);
}

TEST(WebsocketDataFraming, ReservedBitsMustBeZeroWithoutExtensions) {
  frame frame = make_minimal_frame(opcode::binary);

  frame.rsv1 = true;
  EXPECT_EQ(frame.validate(), protocol_error::reserved_bits_nonzero);

  frame = make_minimal_frame(opcode::binary);
  frame.rsv2 = true;
  EXPECT_EQ(frame.validate(), protocol_error::reserved_bits_nonzero);

  frame = make_minimal_frame(opcode::binary);
  frame.rsv3 = true;
  EXPECT_EQ(frame.validate(), protocol_error::reserved_bits_nonzero);
}

TEST(WebsocketDataFraming, PayloadLengthMustNotExceed63Bits) {
  frame frame = make_minimal_frame(opcode::binary);
  frame.payload_length = frame::max_allowed_payload_length;
  EXPECT_EQ(frame.validate(), std::error_code{});

  frame.payload_length = frame::max_allowed_payload_length + 1ULL;
  EXPECT_EQ(frame.validate(), protocol_error::payload_length_too_big);
}

TEST(WebsocketDataFraming, RejectsCloseFramesWithInvalidStatusCodes) {
  std::array<std::uint16_t, 6> invalid_status_codes{
    static_cast<std::uint16_t>(999),
    static_cast<std::uint16_t>(1004),
    static_cast<std::uint16_t>(1005),
    static_cast<std::uint16_t>(1006),
    static_cast<std::uint16_t>(1015),
    static_cast<std::uint16_t>(5000),
  };

  for (std::uint16_t status_code : invalid_status_codes) {
    SCOPED_TRACE(::testing::Message() << "status_code: " << status_code);

    std::array<std::byte, 2> payload = make_network_u16(status_code);

    frame frame = make_minimal_frame(opcode::close);
    frame.payload_length = payload.size();
    frame.payload_data = std::span<const std::byte>{payload.data(), payload.size()};
    frame.application_data = frame.payload_data;

    std::error_code validation_ec = frame.validate();
    EXPECT_NE(validation_ec, std::error_code{});
  }
}
