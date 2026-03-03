#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_decoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/detail/role.hpp"
#include "aero/websocket/error.hpp"

#include "test_helpers.hpp"

namespace {

  constexpr std::uint8_t opcode_text_value = 0x1U;
  constexpr std::uint8_t opcode_close_value = 0x8U;
  constexpr std::uint8_t opcode_ping_value = 0x9U;
  constexpr std::uint8_t reserved_noncontrol_opcode_value = 0x3U;
  constexpr std::uint8_t reserved_control_opcode_value = 0xBU;
  constexpr std::uint8_t payload_len_16_indicator = 126U;
  constexpr std::uint8_t payload_len_64_indicator = 127U;

  using aero::websocket::detail::frame;
  using aero::websocket::detail::masking_key;
  using aero::websocket::detail::opcode;
  using aero::websocket::detail::role;
  using aero::websocket::error::protocol_error;

  using client_frame_decoder = aero::websocket::detail::frame_decoder<role::client>;
  using server_frame_decoder = aero::websocket::detail::frame_decoder<role::server>;

  using aero::tests::websocket::big_endian_bytes;
  using aero::tests::websocket::build_frame_bytes_canonical;
  using aero::tests::websocket::build_frame_bytes_explicit;
  using aero::tests::websocket::make_first_byte;
  using aero::tests::websocket::make_payload_bytes;
  using aero::tests::websocket::make_second_byte;

  bool frame_headers_equal(frame first, frame second) {
    return first.fin == second.fin && first.rsv1 == second.rsv1 && first.rsv2 == second.rsv2 && first.rsv3 == second.rsv3 &&
           first.opcode == second.opcode && first.masked == second.masked && first.payload_length == second.payload_length &&
           first.masking_key == second.masking_key;
  }

  template <typename Decoder>
  void expect_decodes_to(std::span<const std::byte> bytes, const frame& expected) {
    auto decoded = Decoder{}.decode(bytes);
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(frame_headers_equal(expected, decoded.value()));
  }

  template <typename Decoder>
  void expect_rejected(std::span<const std::byte> bytes) {
    auto decoded = Decoder{}.decode(bytes);
    EXPECT_FALSE(decoded);
  }

  template <typename Decoder, typename ErrorEnum>
  void expect_rejected_with(std::span<const std::byte> bytes, ErrorEnum expected_error) {
    auto decoded = Decoder{}.decode(bytes);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error(), expected_error);
  }

  frame expected_text_frame_unmasked(std::uint64_t payload_length) {
    return {
      .fin = true,
      .rsv1 = false,
      .rsv2 = false,
      .rsv3 = false,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = payload_length,
      .masking_key = std::nullopt,
    };
  }

  frame expected_text_frame_masked(std::uint64_t payload_length, masking_key key) {
    return {
      .fin = true,
      .rsv1 = false,
      .rsv2 = false,
      .rsv3 = false,
      .opcode = opcode::text,
      .masked = true,
      .payload_length = payload_length,
      .masking_key = key,
    };
  }

} // namespace

class WebsocketFrameDecoderUnmaskedLengths : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(WebsocketFrameDecoderUnmaskedLengths, DecodesUnmaskedFrames) {
  const auto payload_length = GetParam();
  auto payload = make_payload_bytes(static_cast<std::size_t>(payload_length));
  auto bytes =
    build_frame_bytes_canonical(true, false, false, false, opcode_text_value, false, payload_length, std::nullopt, payload);
  expect_decodes_to<client_frame_decoder>(bytes, expected_text_frame_unmasked(payload_length));
}

INSTANTIATE_TEST_SUITE_P(BoundaryAndTypical, WebsocketFrameDecoderUnmaskedLengths,
  ::testing::Values<std::uint64_t>(0U, 125U, 126U, 65535U, 65536U, 100000U));

class WebsocketFrameDecoderMaskedKeys : public ::testing::TestWithParam<std::pair<std::uint64_t, masking_key>> {};

TEST_P(WebsocketFrameDecoderMaskedKeys, DecodesMaskedFramesAndParsesMaskingKey) {
  const auto [payload_length, key] = GetParam();
  auto payload = make_payload_bytes(static_cast<std::size_t>(payload_length));
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, true, payload_length, key, payload);
  expect_decodes_to<server_frame_decoder>(bytes, expected_text_frame_masked(payload_length, key));
}

INSTANTIATE_TEST_SUITE_P(KeyAcrossLengthEncodings, WebsocketFrameDecoderMaskedKeys,
  ::testing::Values(std::pair{15U, masking_key{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}}},
    std::pair{50000U, masking_key{std::byte{10}, std::byte{11}, std::byte{12}, std::byte{13}}},
    std::pair{100000U, masking_key{std::byte{20}, std::byte{21}, std::byte{22}, std::byte{23}}}));

TEST(WebsocketFrameDecoder, RejectsNonZeroRsvBits) {
  auto payload = make_payload_bytes(0);
  auto bytes = build_frame_bytes_canonical(true, true, false, false, opcode_text_value, false, 0U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::reserved_bits_nonzero);
}

TEST(WebsocketFrameDecoder, RejectsReservedOpcodesNonControlRange) {
  auto payload = make_payload_bytes(0);
  auto bytes =
    build_frame_bytes_canonical(true, false, false, false, reserved_noncontrol_opcode_value, false, 0U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::opcode_reserved);
}

TEST(WebsocketFrameDecoder, RejectsReservedOpcodesControlRange) {
  auto payload = make_payload_bytes(0);
  auto bytes =
    build_frame_bytes_canonical(true, false, false, false, reserved_control_opcode_value, false, 0U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::opcode_reserved);
}

TEST(WebsocketFrameDecoder, RejectsControlFrameNotFinal) {
  auto payload = make_payload_bytes(0);
  auto bytes = build_frame_bytes_canonical(false, false, false, false, opcode_ping_value, false, 0U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::control_frame_fragmented);
}

TEST(WebsocketFrameDecoder, RejectsControlFramePayloadTooBig) {
  auto payload = make_payload_bytes(126);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_ping_value, false, 126U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::control_frame_payload_too_big);
}

TEST(WebsocketFrameDecoder, RejectsCloseFramePayloadTooSmall) {
  auto payload = make_payload_bytes(1);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_close_value, false, 1U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::close_frame_payload_too_small);
}

TEST(WebsocketFrameDecoder, RejectsMaskedFrameWithoutMaskingKeyEvenIfPayloadLengthIsZero) {
  std::array<std::byte, 2> bytes{
    make_first_byte(true, false, false, false, opcode_text_value),
    make_second_byte(true, 0U),
  };
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsTruncatedPayloadFor7bitLength) {
  auto payload = make_payload_bytes(2);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, false, 5U, std::nullopt, payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsTruncatedPayloadFor16bitLength) {
  auto payload = make_payload_bytes(10);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, false, 50000U, std::nullopt, payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsTruncatedPayloadFor64bitLength) {
  auto payload = make_payload_bytes(10);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, false, 100000U, std::nullopt, payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsNonCanonicalLengthEncoding126ForLength125) {
  auto payload = make_payload_bytes(125);
  auto extended = big_endian_bytes<2>(125U);
  auto bytes = build_frame_bytes_explicit(true,
    false,
    false,
    false,
    opcode_text_value,
    false,
    payload_len_16_indicator,
    extended,
    std::nullopt,
    payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsNonCanonicalLengthEncoding127ForLength1) {
  auto payload = make_payload_bytes(1);
  auto extended = big_endian_bytes<8>(1U);
  auto bytes = build_frame_bytes_explicit(true,
    false,
    false,
    false,
    opcode_text_value,
    false,
    payload_len_64_indicator,
    extended,
    std::nullopt,
    payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsNonCanonicalLengthEncoding127ForLength65535) {
  auto payload = make_payload_bytes(65535);
  auto extended = big_endian_bytes<8>(65535U);
  auto bytes = build_frame_bytes_explicit(true,
    false,
    false,
    false,
    opcode_text_value,
    false,
    payload_len_64_indicator,
    extended,
    std::nullopt,
    payload);
  expect_rejected<client_frame_decoder>(bytes);
}

TEST(WebsocketFrameDecoder, RejectsPayloadLengthTooBig) {
  auto too_big_payload_length = frame::max_allowed_payload_length + 1U;
  auto extended = big_endian_bytes<8>(too_big_payload_length);
  std::array<std::byte, 10> header_only{};
  header_only[0] = make_first_byte(true, false, false, false, opcode_text_value);
  header_only[1] = make_second_byte(false, payload_len_64_indicator);
  for (std::size_t i{}; i < 8U; ++i) {
    header_only[2U + i] = extended[i];
  }

  auto decoded = client_frame_decoder{}.decode(header_only);
  if (!decoded) {
    EXPECT_EQ(decoded.error(), protocol_error::payload_length_too_big);
  } else {
    FAIL();
  }
}

TEST(WebsocketFrameDecoder, ClientRejectsMaskedFrames) {
  auto payload = make_payload_bytes(1);
  auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, true, 1U, std::nullopt, payload);
  expect_rejected_with<client_frame_decoder>(bytes, protocol_error::masked_frame_from_server);
}
