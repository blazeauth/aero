#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/message_reader.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

#include "test_helpers.hpp"

namespace {

  using aero::websocket::close_code;
  using aero::websocket::message;
  using aero::websocket::message_kind;
  using aero::websocket::message_reader_error;
  using aero::websocket::protocol_error;
  using aero::websocket::detail::message_reader;
  using aero::websocket::detail::message_reader_config;
  using aero::websocket::detail::opcode;

  using aero::tests::websocket::build_frame_bytes_canonical;
  using aero::tests::websocket::make_payload_bytes;
  using aero::tests::websocket::serialize_close_payload;
  using aero::tests::websocket::serialize_unmasked_frame;
  using aero::tests::websocket::to_bytes;
  using aero::tests::websocket::to_string;

  std::optional<message> poll_one(message_reader& reader) {
    return reader.poll();
  }

  std::vector<message> poll_all(message_reader& reader) {
    std::vector<message> messages;
    for (;;) {
      auto next = reader.poll();
      if (!next) {
        break;
      }
      messages.push_back(std::move(*next));
    }
    return messages;
  }

  void expect_buffers_truncated_payload_until_complete(std::uint64_t payload_length, std::size_t first_payload_length) {
    message_reader reader;

    const auto first_payload = make_payload_bytes(first_payload_length);
    const auto frame_prefix = build_frame_bytes_canonical(true,
      false,
      false,
      false,
      static_cast<std::uint8_t>(opcode::text),
      false,
      payload_length,
      std::nullopt,
      first_payload);

    EXPECT_EQ(reader.consume(frame_prefix), std::error_code{});
    EXPECT_FALSE(poll_one(reader).has_value());
    EXPECT_EQ(reader.buffered_bytes(), frame_prefix.size());

    const auto remaining_payload_length = static_cast<std::size_t>(payload_length) - first_payload_length;
    const auto remaining_payload = make_payload_bytes(remaining_payload_length);

    EXPECT_EQ(reader.consume(remaining_payload), std::error_code{});

    auto produced = poll_one(reader);
    ASSERT_TRUE(produced.has_value());
    EXPECT_EQ(produced->kind, message_kind::text);
    EXPECT_EQ(produced->payload.size(), static_cast<std::size_t>(payload_length));
    EXPECT_FALSE(poll_one(reader).has_value());
    EXPECT_EQ(reader.buffered_bytes(), 0U);
  }

} // namespace

TEST(MessageReader, ProducesTextMessageForSingleUnfragmentedTextFrame) {
  message_reader reader;

  auto payload = to_bytes("hello");
  auto frame = serialize_unmasked_frame(opcode::text, true, payload);

  EXPECT_EQ(reader.consume(frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());

  EXPECT_TRUE(produced->is_text());
  EXPECT_EQ(produced->text(), "hello");
  EXPECT_FALSE(reader.closed());
}

TEST(MessageReader, ProducesBinaryMessageForSingleUnfragmentedBinaryFrame) {
  message_reader reader;

  std::vector<std::byte> payload{std::byte{0x00}, std::byte{0x01}, std::byte{0xFF}};
  auto frame = serialize_unmasked_frame(opcode::binary, true, payload);

  EXPECT_EQ(reader.consume(frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());

  EXPECT_EQ(produced->kind, message_kind::binary);
  EXPECT_EQ(produced->payload, payload);
}

TEST(MessageReader, AssemblesFragmentedTextMessageAcrossContinuationFrames) {
  message_reader reader;

  auto first_fragment = to_bytes("hel");
  auto second_fragment = to_bytes("lo");

  auto first_frame = serialize_unmasked_frame(opcode::text, false, first_fragment);
  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, second_fragment);

  EXPECT_EQ(reader.consume(first_frame), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(continuation_frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());

  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "hello");
}

TEST(MessageReader, RejectsUnexpectedContinuationWhenNoMessageInProgress) {
  message_reader reader;

  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("data"));
  EXPECT_EQ(reader.consume(continuation_frame), message_reader_error::unexpected_continuation);
}

TEST(MessageReader, RejectsInterleavedDataFramesDuringFragmentedMessage) {
  message_reader reader;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("part1"));
  auto interleaved_binary_frame = serialize_unmasked_frame(opcode::binary, true, to_bytes("x"));

  EXPECT_EQ(reader.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(reader.consume(interleaved_binary_frame), message_reader_error::interleaved_data_frame);
}

TEST(MessageReader, ProducesPingBetweenFragmentsAndContinuesDataAssembly) {
  message_reader reader;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("he"));
  auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
  auto final_continuation = serialize_unmasked_frame(opcode::continuation, true, to_bytes("llo"));

  EXPECT_EQ(reader.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(reader.consume(ping_frame), std::error_code{});

  auto produced_after_ping = poll_one(reader);
  ASSERT_TRUE(produced_after_ping.has_value());
  EXPECT_EQ(produced_after_ping->kind, message_kind::ping);
  EXPECT_EQ(to_string(produced_after_ping->payload), "p");

  EXPECT_EQ(reader.consume(final_continuation), std::error_code{});

  auto produced_text = poll_one(reader);
  ASSERT_TRUE(produced_text.has_value());
  EXPECT_EQ(produced_text->kind, message_kind::text);
  EXPECT_EQ(to_string(produced_text->payload), "hello");
}

TEST(MessageReader, RejectsFragmentedControlFrames) {
  message_reader reader;

  auto fragmented_ping_frame = serialize_unmasked_frame(opcode::ping, false, to_bytes("x"));
  EXPECT_EQ(reader.consume(fragmented_ping_frame), protocol_error::control_frame_fragmented);
}

TEST(MessageReader, ProducesCloseMessageAndMarksClosed) {
  message_reader reader;

  auto close_payload = serialize_close_payload(close_code::normal, to_bytes("bye"));
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(reader.consume(close_frame), std::error_code{});
  EXPECT_TRUE(reader.closed());

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::close);
  EXPECT_EQ(produced->payload, close_payload);
}

TEST(MessageReader, RejectsDataAfterClose) {
  message_reader reader;

  auto close_payload = serialize_close_payload(close_code::normal, {});
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(reader.consume(close_frame), std::error_code{});
  EXPECT_TRUE(reader.closed());

  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("x"));
  EXPECT_EQ(reader.consume(text_frame), message_reader_error::data_after_close);

  std::span<const std::byte> empty_bytes{};
  EXPECT_EQ(reader.consume(empty_bytes), message_reader_error::data_after_close);
}

TEST(MessageReader, AcceptsCloseWithEmptyPayload) {
  message_reader reader;

  std::span<const std::byte> empty_payload{};
  auto close_frame = serialize_unmasked_frame(opcode::close, true, empty_payload);

  EXPECT_EQ(reader.consume(close_frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::close);
  EXPECT_TRUE(produced->payload.empty());
  EXPECT_EQ(produced->close_code(), std::nullopt);
}

TEST(MessageReader, RejectsCloseWithPayloadSizeOne) {
  message_reader reader;

  std::vector<std::byte> invalid_payload{std::byte{0x03}};
  auto close_frame = serialize_unmasked_frame(opcode::close, true, invalid_payload);

  EXPECT_EQ(reader.consume(close_frame), protocol_error::close_frame_payload_too_small);
}

TEST(MessageReader, RejectsCloseWithInvalidUtf8Reason) {
  message_reader reader;

  std::vector<std::byte> invalid_utf8_reason{std::byte{0xC3}, std::byte{0x28}};
  auto close_payload = serialize_close_payload(close_code::normal, invalid_utf8_reason);
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(reader.consume(close_frame), protocol_error::close_reason_invalid_utf8);
}

TEST(MessageReader, RejectsTextMessageWithInvalidUtf8) {
  message_reader reader;

  std::vector<std::byte> invalid_utf8_payload{std::byte{0xC3}, std::byte{0x28}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

  EXPECT_EQ(reader.consume(text_frame), protocol_error::payload_text_invalid_utf8);
}

TEST(MessageReader, AllowsPartialUtf8AcrossNonFinalTextFrames) {
  message_reader reader;

  std::vector<std::byte> euro_sign_prefix{std::byte{0xE2}, std::byte{0x82}};
  std::vector<std::byte> euro_sign_suffix{std::byte{0xAC}};

  auto first_frame = serialize_unmasked_frame(opcode::text, false, euro_sign_prefix);
  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, euro_sign_suffix);

  EXPECT_EQ(reader.consume(first_frame), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(continuation_frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "€");
}

TEST(MessageReader, RejectsFinalTextMessageWithIncompleteUtf8Sequence) {
  message_reader reader;

  std::vector<std::byte> incomplete_utf8_payload{std::byte{0xE2}, std::byte{0x82}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, incomplete_utf8_payload);

  EXPECT_EQ(reader.consume(text_frame), protocol_error::payload_text_invalid_utf8);
}

TEST(MessageReader, RejectsChoppedSingleFrameTextAsSoonAsUtf8BecomesInvalid) {
  message_reader reader;

  std::vector<std::byte> invalid_utf8_payload{std::byte{0xC3}, std::byte{0x28}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data(), 3}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}), protocol_error::payload_text_invalid_utf8);
}

TEST(MessageReader, RejectsChoppedSingleFrameTextWhenScalarValueExceedsUnicodeRange) {
  message_reader reader;

  std::vector<std::byte> invalid_utf8_payload{
    std::byte{0xF4},
    std::byte{0x90},
    std::byte{0x80},
    std::byte{0x80},
  };
  auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data(), 3}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}), protocol_error::payload_text_invalid_utf8);
}

TEST(MessageReader, AcceptsValidUtf8SplitAcrossMultipleConsumeCallsWithinSingleFrame) {
  message_reader reader;

  std::vector<std::byte> euro_sign{std::byte{0xE2}, std::byte{0x82}, std::byte{0xAC}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, euro_sign);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data(), 3}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 4, 1}), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "€");
}

TEST(MessageReader, DoesNotRejectPotentiallyValidUtf8PrefixBeforeContradictingByteArrives) {
  message_reader reader;

  std::vector<std::byte> invalid_utf8_payload{std::byte{0xE2}, std::byte{0x82}, std::byte{0x41}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data(), 3}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());

  EXPECT_EQ(reader.consume(std::span<const std::byte>{text_frame.data() + 4, 1}), protocol_error::payload_text_invalid_utf8);
}

TEST(MessageReader, BuffersTruncatedFrameUntilComplete) {
  message_reader reader;

  auto full_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("Hi"));
  ASSERT_GE(full_frame.size(), 4U);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{full_frame.data(), 1}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());
  EXPECT_EQ(reader.buffered_bytes(), 1U);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{full_frame.data() + 1, 1}), std::error_code{});
  EXPECT_FALSE(poll_one(reader).has_value());
  EXPECT_EQ(reader.buffered_bytes(), 2U);

  EXPECT_EQ(reader.consume(std::span<const std::byte>{full_frame.data() + 2, full_frame.size() - 2}), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "Hi");
  EXPECT_EQ(reader.buffered_bytes(), 0U);
}

TEST(MessageReader, BuffersTruncatedPayloadFor7bitLengthUntilComplete) {
  expect_buffers_truncated_payload_until_complete(5U, 2U);
}

TEST(MessageReader, BuffersTruncatedPayloadFor16bitLengthUntilComplete) {
  expect_buffers_truncated_payload_until_complete(50000U, 10U);
}

TEST(MessageReader, BuffersTruncatedPayloadFor64bitLengthUntilComplete) {
  expect_buffers_truncated_payload_until_complete(100000U, 10U);
}

TEST(MessageReader, ResetsStateAndAllowsReuse) {
  message_reader reader;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("a"));
  EXPECT_EQ(reader.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(reader.buffered_bytes(), 0U);

  reader.reset();

  auto complete_text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("ok"));
  EXPECT_EQ(reader.consume(complete_text_frame), std::error_code{});

  auto produced = poll_one(reader);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "ok");
  EXPECT_FALSE(reader.closed());
}

TEST(MessageReader, RejectsSingleMessageExceedingMaxMessageSize) {
  message_reader reader{message_reader_config{.max_message_size = 5}};

  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("HiHello"));
  ASSERT_GT(text_frame.size(), 4U);

  EXPECT_EQ(reader.consume(text_frame), message_reader_error::message_too_big);
}

TEST(MessageReader, AcceptsLargeConsumeChunkWhenEachMessageFitsMaxMessageSize) {
  message_reader reader{message_reader_config{.max_message_size = 5}};

  auto first_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("abc"));
  auto second_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("de"));

  std::vector<std::byte> chunk;
  chunk.append_range(first_frame);
  chunk.append_range(second_frame);
  ASSERT_GT(chunk.size(), 5U);

  EXPECT_EQ(reader.consume(chunk), std::error_code{});

  auto produced = poll_all(reader);
  ASSERT_EQ(produced.size(), 2U);
  EXPECT_EQ(to_string(produced[0].payload), "abc");
  EXPECT_EQ(to_string(produced[1].payload), "de");
}

TEST(MessageReader, RejectsAssembledMessageExceedingMaxMessageSize) {
  message_reader reader{message_reader_config{.max_message_size = 5}};

  auto first_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("abc"));
  auto second_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("def"));

  EXPECT_EQ(reader.consume(first_frame), std::error_code{});
  EXPECT_EQ(reader.consume(second_frame), message_reader_error::message_too_big);
}

TEST(MessageReader, ProducesMessagesInArrivalOrder) {
  message_reader reader;

  auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("t"));
  auto pong_frame = serialize_unmasked_frame(opcode::pong, true, to_bytes("g"));

  EXPECT_EQ(reader.consume(ping_frame), std::error_code{});
  EXPECT_EQ(reader.consume(text_frame), std::error_code{});
  EXPECT_EQ(reader.consume(pong_frame), std::error_code{});

  auto produced = poll_all(reader);
  ASSERT_EQ(produced.size(), 3U);

  EXPECT_EQ(produced[0].kind, message_kind::ping);
  EXPECT_EQ(to_string(produced[0].payload), "p");

  EXPECT_EQ(produced[1].kind, message_kind::text);
  EXPECT_EQ(to_string(produced[1].payload), "t");

  EXPECT_EQ(produced[2].kind, message_kind::pong);
  EXPECT_EQ(to_string(produced[2].payload), "g");
}
