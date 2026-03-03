#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "aero/error.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_encoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"

#include "test_helpers.hpp"

namespace websocket = aero::websocket;

namespace {

  using websocket::detail::frame;
  using websocket::detail::frame_encoder;
  using websocket::detail::masking_key;
  using websocket::detail::opcode;
  using websocket::error::protocol_error;

  using aero::tests::websocket::big_endian_bytes;
  using aero::tests::websocket::starts_with;
  using aero::tests::websocket::to_byte;

} // namespace

TEST(WebsocketFrameEncoder, EncodesUnmaskedLength0) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 0,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  std::array<std::byte, 2> expected{to_byte(0x81U), to_byte(0x00U)};
  ASSERT_TRUE(starts_with(out, expected));

  for (std::size_t i = input.header_size(); i < out.size(); ++i) {
    EXPECT_EQ(out[i], std::byte{0xAA});
  }
}

TEST(WebsocketFrameEncoder, EncodesUnmaskedLength125) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 125,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  std::array<std::byte, 2> expected{to_byte(0x81U), to_byte(0x7DU)};
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesUnmaskedLength126As16bitExtended) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 126,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  auto extended = big_endian_bytes<2>(126U);
  std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x7EU), extended[0], extended[1]};
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesUnmaskedLength65535As16bitExtended) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 65535U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  auto extended = big_endian_bytes<2>(65535U);
  std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x7EU), extended[0], extended[1]};
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesUnmaskedLength65536As64bitExtended) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 65536U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  auto extended = big_endian_bytes<8>(65536U);
  std::vector<std::byte> expected{
    to_byte(0x81U),
    to_byte(0x7FU),
    extended[0],
    extended[1],
    extended[2],
    extended[3],
    extended[4],
    extended[5],
    extended[6],
    extended[7],
  };
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesMaskedLength15AndCopiesMaskingKey) {
  masking_key key{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = true,
    .payload_length = 15U,
    .masking_key = key,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x8FU), key[0], key[1], key[2], key[3]};
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesMaskedLength50000With16bitExtendedAndCopiesMaskingKey) {
  masking_key key{std::byte{10}, std::byte{11}, std::byte{12}, std::byte{13}};
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = true,
    .payload_length = 50000U,
    .masking_key = key,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  auto extended = big_endian_bytes<2>(50000U);
  std::vector<std::byte> expected{to_byte(0x81U), to_byte(0xFEU), extended[0], extended[1], key[0], key[1], key[2], key[3]};
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, EncodesMaskedLength100000With64bitExtendedAndCopiesMaskingKey) {
  masking_key key{std::byte{20}, std::byte{21}, std::byte{22}, std::byte{23}};
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = true,
    .payload_length = 100000U,
    .masking_key = key,
  };

  std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
  auto encoded = frame_encoder{}.encode(out, input);
  ASSERT_TRUE(encoded);

  auto extended = big_endian_bytes<8>(100000U);
  std::vector<std::byte> expected{
    to_byte(0x81U),
    to_byte(0xFFU),
    extended[0],
    extended[1],
    extended[2],
    extended[3],
    extended[4],
    extended[5],
    extended[6],
    extended[7],
    key[0],
    key[1],
    key[2],
    key[3],
  };
  ASSERT_TRUE(starts_with(out, expected));
}

TEST(WebsocketFrameEncoder, RejectsOutputTooSmall) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 100000U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(input.header_size() - 1U);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), aero::error::basic_error::not_enough_memory);
  }
}

TEST(WebsocketFrameEncoder, RejectsMaskedFrameWithoutMaskingKey) {
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = true,
    .payload_length = 1U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::masking_key_missing);
  }
}

TEST(WebsocketFrameEncoder, RejectsUnmaskedFrameWithMaskingKey) {
  masking_key key{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};
  frame input{
    .fin = true,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 1U,
    .masking_key = key,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::masking_flag_missing);
  }
}

TEST(WebsocketFrameEncoder, RejectsControlFrameNotFinal) {
  frame input{
    .fin = false,
    .opcode = opcode::ping,
    .masked = false,
    .payload_length = 0U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::control_frame_fragmented);
  }
}

TEST(WebsocketFrameEncoder, RejectsControlFramePayloadTooBig) {
  frame input{
    .fin = true,
    .opcode = opcode::ping,
    .masked = false,
    .payload_length = 126U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::control_frame_payload_too_big);
  }
}

TEST(WebsocketFrameEncoder, RejectsCloseFramePayloadTooSmall) {
  frame input{
    .fin = true,
    .opcode = opcode::close,
    .masked = false,
    .payload_length = 1U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded_valid = frame_encoder{}.encode(out, input);
  EXPECT_TRUE(encoded_valid);

  input.application_data = std::span{out.data(), 1};
  auto encoded_invalid = frame_encoder{}.encode(out, input);
  ASSERT_FALSE(encoded_invalid);
  EXPECT_EQ(encoded_invalid.error(), protocol_error::close_frame_payload_too_small);
}

TEST(WebsocketFrameEncoder, RejectsReservedBitsNonZero) {
  frame input{
    .fin = true,
    .rsv1 = true,
    .rsv2 = false,
    .rsv3 = false,
    .opcode = opcode::text,
    .masked = false,
    .payload_length = 0U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::reserved_bits_nonzero);
  }
}

TEST(WebsocketFrameEncoder, RejectsReservedOpcodes) {
  frame input{
    .fin = true,
    .opcode = static_cast<opcode>(0x3U),
    .masked = false,
    .payload_length = 0U,
    .masking_key = std::nullopt,
  };

  std::vector<std::byte> out(64);
  auto encoded = frame_encoder{}.encode(out, input);
  EXPECT_FALSE(encoded);
  if (!encoded) {
    EXPECT_EQ(encoded.error(), protocol_error::opcode_reserved);
  }
}
