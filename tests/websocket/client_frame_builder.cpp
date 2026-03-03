#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/client_frame_builder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"

#include "test_helpers.hpp"

namespace {

  using aero::websocket::close_code;
  using aero::websocket::detail::client_frame_builder;
  using aero::websocket::detail::masking_key;
  using aero::websocket::detail::opcode;

  using aero::tests::websocket::big_endian_bytes;
  using aero::tests::websocket::build_frame_bytes_canonical;
  using aero::tests::websocket::make_masking_key;
  using aero::tests::websocket::make_payload_bytes;
  using aero::tests::websocket::starts_with;
  using aero::tests::websocket::to_bytes;
  using aero::tests::websocket::to_string;

  using protocol_error = aero::websocket::error::protocol_error;

  masking_key extract_masking_key(std::span<const std::byte> frame_bytes) {
    const auto second = std::to_integer<std::uint8_t>(frame_bytes[1]);
    const std::uint8_t length_indicator = second & 0x7FU;

    std::size_t extended_length_size = 0;
    if (length_indicator == 126U) {
      extended_length_size = 2;
    } else if (length_indicator == 127U) {
      extended_length_size = 8;
    }

    const std::size_t key_offset = 2U + extended_length_size;

    return masking_key{
      frame_bytes[key_offset + 0U],
      frame_bytes[key_offset + 1U],
      frame_bytes[key_offset + 2U],
      frame_bytes[key_offset + 3U],
    };
  }

  std::span<const std::byte> payload_bytes(std::span<const std::byte> frame_bytes) {
    const auto second = std::to_integer<std::uint8_t>(frame_bytes[1]);
    const std::uint8_t length_indicator = second & 0x7FU;

    std::size_t payload_length = 0;
    std::size_t extended_length_size = 0;

    if (length_indicator <= 125U) {
      payload_length = length_indicator;
    } else if (length_indicator == 126U) {
      extended_length_size = 2;
      payload_length = (static_cast<std::size_t>(std::to_integer<std::uint8_t>(frame_bytes[2])) << 8U) |
                       static_cast<std::size_t>(std::to_integer<std::uint8_t>(frame_bytes[3]));
    } else {
      extended_length_size = 8;
      std::uint64_t value = 0;
      for (std::size_t i{}; i < 8U; ++i) {
        value = (value << 8U) | std::to_integer<std::uint8_t>(frame_bytes[2U + i]);
      }
      payload_length = static_cast<std::size_t>(value);
    }

    const std::size_t payload_offset = 2U + extended_length_size + 4U;
    return frame_bytes.subspan(payload_offset, payload_length);
  }

  std::vector<std::byte> unmask_payload(std::span<const std::byte> masked_payload, masking_key key) {
    std::vector<std::byte> unmasked;
    unmasked.resize(masked_payload.size());
    for (std::size_t i{}; i < masked_payload.size(); ++i) {
      const auto masked_value = std::to_integer<std::uint8_t>(masked_payload[i]);
      const auto key_value = std::to_integer<std::uint8_t>(key[i % 4U]);
      unmasked[i] = std::byte{static_cast<std::uint8_t>(masked_value ^ key_value)};
    }
    return unmasked;
  }

  std::vector<std::byte> mask_payload(std::span<const std::byte> unmasked_payload, masking_key key) {
    std::vector<std::byte> masked;
    masked.resize(unmasked_payload.size());
    for (std::size_t i{}; i < unmasked_payload.size(); ++i) {
      const auto value = std::to_integer<std::uint8_t>(unmasked_payload[i]);
      const auto key_value = std::to_integer<std::uint8_t>(key[i % 4U]);
      masked[i] = std::byte{static_cast<std::uint8_t>(value ^ key_value)};
    }
    return masked;
  }

  std::vector<std::byte> expected_masked_header_prefix(opcode frame_opcode, std::size_t payload_length, masking_key key) {
    const auto opcode_value = static_cast<std::uint8_t>(frame_opcode) & 0x0FU;
    std::span<const std::byte> empty_payload{};
    auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_value, true, payload_length, key, empty_payload);
    return bytes;
  }

  struct fixed_masking_key_source {
    masking_key key{};

    std::expected<masking_key, std::error_code> next() {
      return key;
    }
  };

  struct failing_masking_key_source {
    std::error_code error;

    std::expected<masking_key, std::error_code> next() {
      return std::unexpected(error);
    }
  };

  struct sequence_masking_key_source {
    std::vector<masking_key> keys;
    std::size_t index{0};

    std::expected<masking_key, std::error_code> next() {
      if (index >= keys.size()) {
        return std::unexpected(protocol_error::masking_key_generation_failed);
      }
      return keys[index++];
    }
  };

} // namespace

TEST(WebsocketClientFrameBuilder, BuildsMaskedFinalTextFrameWithRsvBitsZero) {
  const masking_key key = make_masking_key(0x00, 0x01, 0x02, 0x03);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto built = builder.build_text_frame("Hi");
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::text, 2U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));

  ASSERT_EQ(built->size(), expected_prefix.size() + 2U);

  const auto first = std::to_integer<std::uint8_t>((*built)[0]);
  EXPECT_EQ(first & 0x80U, 0x80U);
  EXPECT_EQ(first & 0x70U, 0x00U);
  EXPECT_EQ(first & 0x0FU, static_cast<std::uint8_t>(opcode::text) & 0x0FU);

  const auto second = std::to_integer<std::uint8_t>((*built)[1]);
  EXPECT_EQ(second & 0x80U, 0x80U);
}

TEST(WebsocketClientFrameBuilder, MasksTextPayloadUsingProvidedMaskingKey) {
  const masking_key key = make_masking_key(0x12, 0x34, 0x56, 0x78);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  const auto payload = to_bytes("abcd");
  auto built = builder.build_text_frame("abcd");
  ASSERT_TRUE(built);

  const auto header_prefix = expected_masked_header_prefix(opcode::text, payload.size(), key);
  ASSERT_TRUE(starts_with(*built, header_prefix));

  const auto extracted_key = extract_masking_key(*built);
  EXPECT_EQ(extracted_key, key);

  const auto masked_payload = payload_bytes(*built);
  const auto expected_masked = mask_payload(payload, key);
  ASSERT_EQ(masked_payload.size(), expected_masked.size());

  for (std::size_t i{}; i < expected_masked.size(); ++i) {
    EXPECT_EQ(masked_payload[i], expected_masked[i]);
  }
}

TEST(WebsocketClientFrameBuilder, EncodesMaskedPayloadLengthUsing7BitEncodingForSmallPayloads) {
  const masking_key key = make_masking_key(0xAA, 0xBB, 0xCC, 0xDD);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string payload(125, 'x');
  auto built = builder.build_text_frame(payload);
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::text, 125U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 125U);

  const auto second = std::to_integer<std::uint8_t>((*built)[1]);
  EXPECT_EQ(second, static_cast<std::uint8_t>(0x80U | 125U));
}

TEST(WebsocketClientFrameBuilder, EncodesMaskedPayloadLengthUsing16BitExtendedEncodingFrom126To65535) {
  const masking_key key = make_masking_key(0x00, 0x00, 0x00, 0x01);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string payload(126, 'y');
  auto built = builder.build_text_frame(payload);
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::text, 126U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 126U);

  const auto second = std::to_integer<std::uint8_t>((*built)[1]);
  EXPECT_EQ(second, static_cast<std::uint8_t>(0x80U | 126U));

  const auto extended = big_endian_bytes<2>(126U);
  EXPECT_EQ((*built)[2], extended[0]);
  EXPECT_EQ((*built)[3], extended[1]);
}

TEST(WebsocketClientFrameBuilder, EncodesMaskedPayloadLengthUsing64BitExtendedEncodingFrom65536Upwards) {
  const masking_key key = make_masking_key(0x10, 0x11, 0x12, 0x13);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string payload(65536, 'z');
  auto built = builder.build_text_frame(payload);
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::text, 65536U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 65536U);

  const auto second = std::to_integer<std::uint8_t>((*built)[1]);
  EXPECT_EQ(second, static_cast<std::uint8_t>(0x80U | 127U));

  const auto extended = big_endian_bytes<8>(65536U);
  for (std::size_t i{}; i < 8U; ++i) {
    EXPECT_EQ((*built)[2U + i], extended[i]);
  }
}

TEST(WebsocketClientFrameBuilder, BuildsMaskedBinaryFrameAndMasksPayloadBytes) {
  const masking_key key = make_masking_key(0x01, 0x02, 0x03, 0x04);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::vector<std::byte> payload{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  auto built = builder.build_binary_frame(payload);
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::binary, payload.size(), key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + payload.size());

  const auto masked = payload_bytes(*built);
  const auto expected_masked = mask_payload(payload, key);

  for (std::size_t i{}; i < payload.size(); ++i) {
    EXPECT_EQ(masked[i], expected_masked[i]);
  }
}

TEST(WebsocketClientFrameBuilder, RequestsNewMaskingKeyForEachFrame) {
  sequence_masking_key_source source;
  source.keys.push_back(make_masking_key(0x00, 0x00, 0x00, 0x00));
  source.keys.push_back(make_masking_key(0x01, 0x01, 0x01, 0x01));
  client_frame_builder<sequence_masking_key_source> builder{source};

  auto first = builder.build_text_frame("a");
  ASSERT_TRUE(first);

  auto second = builder.build_text_frame("b");
  ASSERT_TRUE(second);

  const auto first_key = extract_masking_key(*first);
  const auto second_key = extract_masking_key(*second);

  EXPECT_NE(first_key, second_key);
}

TEST(WebsocketClientFrameBuilder, PropagatesMaskingKeySourceFailure) {
  failing_masking_key_source source;
  source.error = protocol_error::masking_key_generation_failed;

  client_frame_builder<failing_masking_key_source> builder{source};
  auto built = builder.build_text_frame("x");

  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::masking_key_generation_failed);
}

TEST(WebsocketClientFrameBuilder, BuildsPingFrameAsFinalMaskedControlFrame) {
  const masking_key key = make_masking_key(0xAB, 0xCD, 0xEF, 0x01);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto payload = to_bytes("p");
  auto built = builder.build_ping_frame(std::span<const std::byte>{payload});
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::ping, 1U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 1U);

  const auto first = std::to_integer<std::uint8_t>((*built)[0]);
  EXPECT_EQ(first & 0x80U, 0x80U);
  EXPECT_EQ(first & 0x70U, 0x00U);
  EXPECT_EQ(first & 0x0FU, static_cast<std::uint8_t>(opcode::ping) & 0x0FU);
}

TEST(WebsocketClientFrameBuilder, RejectsPingPayloadLongerThan125Bytes) {
  const masking_key key = make_masking_key(0x00, 0x01, 0x02, 0x03);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto payload = make_payload_bytes(126, std::byte{0x11});
  auto built = builder.build_ping_frame(std::span<const std::byte>{payload});

  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::control_frame_payload_too_big);
}

TEST(WebsocketClientFrameBuilder, RejectsPongPayloadLongerThan125BytesWithControlFrameError) {
  const masking_key key = make_masking_key(0x00, 0x01, 0x02, 0x03);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto payload = make_payload_bytes(126, std::byte{0x22});
  auto built = builder.build_pong_frame(std::span<const std::byte>{payload});

  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::control_frame_payload_too_big);
}

TEST(WebsocketClientFrameBuilder, BuildsPongFrameAsFinalMaskedControlFrame) {
  const masking_key key = make_masking_key(0x10, 0x20, 0x30, 0x40);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto payload = make_payload_bytes(125, std::byte{0x33});
  auto built = builder.build_pong_frame(std::span<const std::byte>{payload});
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::pong, 125U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 125U);

  const auto first = std::to_integer<std::uint8_t>((*built)[0]);
  EXPECT_EQ(first & 0x80U, 0x80U);
  EXPECT_EQ(first & 0x0FU, static_cast<std::uint8_t>(opcode::pong) & 0x0FU);
}

TEST(WebsocketClientFrameBuilder, BuildsCloseFrameWithStatusCodeInNetworkByteOrder) {
  const masking_key key = make_masking_key(0x55, 0x66, 0x77, 0x88);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  auto built = builder.build_close_frame(close_code::normal, std::nullopt);
  ASSERT_TRUE(built);

  const auto expected_prefix = expected_masked_header_prefix(opcode::close, 2U, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + 2U);

  const auto masked = payload_bytes(*built);
  const auto unmasked = unmask_payload(masked, key);

  const auto expected_code = big_endian_bytes<2>(std::to_underlying(close_code::normal));
  ASSERT_EQ(unmasked.size(), 2U);
  EXPECT_EQ(unmasked[0], expected_code[0]);
  EXPECT_EQ(unmasked[1], expected_code[1]);
}

TEST(WebsocketClientFrameBuilder, BuildsCloseFrameWithReasonWithoutPaddingOrDuplication) {
  const masking_key key = make_masking_key(0x01, 0x23, 0x45, 0x67);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  constexpr std::string_view reason = "bye";
  auto built = builder.build_close_frame(close_code::normal, reason);
  ASSERT_TRUE(built);

  constexpr auto payload_length = 2U + reason.size();
  const auto expected_prefix = expected_masked_header_prefix(opcode::close, payload_length, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + payload_length);

  const auto unmasked = unmask_payload(payload_bytes(*built), key);
  ASSERT_EQ(unmasked.size(), payload_length);

  const auto expected_code = big_endian_bytes<2>(std::to_underlying(close_code::normal));
  EXPECT_EQ(unmasked[0], expected_code[0]);
  EXPECT_EQ(unmasked[1], expected_code[1]);

  const std::string recovered_reason = to_string(std::span<const std::byte>{unmasked}.subspan(2));
  EXPECT_EQ(recovered_reason, reason);
}

TEST(WebsocketClientFrameBuilder, RejectsCloseReasonThatIsNotValidUtf8) {
  const masking_key key = make_masking_key(0x99, 0x88, 0x77, 0x66);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string invalid_utf8;
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8.push_back(static_cast<char>(0x28));

  auto built = builder.build_close_frame(close_code::normal, invalid_utf8);
  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::close_reason_invalid_utf8);
}

TEST(WebsocketClientFrameBuilder, RejectsTextPayloadThatIsNotValidUtf8) {
  const masking_key key = make_masking_key(0x01, 0x02, 0x03, 0x04);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string invalid_utf8;
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8.push_back(static_cast<char>(0x28));

  auto built = builder.build_text_frame(invalid_utf8);
  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::payload_text_invalid_utf8);
}

TEST(WebsocketClientFrameBuilder, IgnoresInvalidUtf8WhenValidationIsDisabledViaConfig) {
  const masking_key key = make_masking_key(0x99, 0x88, 0x77, 0x66);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}, {.validate_utf8 = false}};

  std::string invalid_utf8;
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8.push_back(static_cast<char>(0x28));

  auto built = builder.build_close_frame(close_code::normal, invalid_utf8);
  ASSERT_TRUE(built);

  built = builder.build_text_frame(invalid_utf8);
  ASSERT_TRUE(built);
}

TEST(WebsocketClientFrameBuilder, AcceptsMaximumAllowedCloseReasonLengthForControlFramePayloadLimit) {
  const masking_key key = make_masking_key(0x01, 0x02, 0x03, 0x04);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string reason(123, 'r');
  auto built = builder.build_close_frame(close_code::normal, reason);
  ASSERT_TRUE(built);

  const std::size_t payload_length = 125U;
  const auto expected_prefix = expected_masked_header_prefix(opcode::close, payload_length, key);
  ASSERT_TRUE(starts_with(*built, expected_prefix));
  EXPECT_EQ(built->size(), expected_prefix.size() + payload_length);
}

TEST(WebsocketClientFrameBuilder, RejectsCloseReasonThatWouldExceedControlFramePayloadLimit) {
  const masking_key key = make_masking_key(0x01, 0x02, 0x03, 0x04);
  client_frame_builder<fixed_masking_key_source> builder{fixed_masking_key_source{key}};

  std::string reason(124, 'r');
  auto built = builder.build_close_frame(close_code::normal, reason);
  ASSERT_FALSE(built);
  EXPECT_EQ(built.error(), protocol_error::control_frame_payload_too_big);
}
