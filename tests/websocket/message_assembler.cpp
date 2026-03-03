#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/message_assembler.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

#include "test_helpers.hpp"

namespace {

  using aero::websocket::close_code;
  using aero::websocket::message;
  using aero::websocket::message_kind;
  using aero::websocket::detail::message_assembler;
  using aero::websocket::detail::message_assembler_config;
  using aero::websocket::detail::opcode;
  using aero::websocket::error::message_assembler_error;
  using aero::websocket::error::protocol_error;

  using aero::tests::websocket::serialize_close_payload;
  using aero::tests::websocket::serialize_unmasked_frame;
  using aero::tests::websocket::to_bytes;
  using aero::tests::websocket::to_string;

  std::optional<message> poll_one(message_assembler& assembler) {
    return assembler.poll();
  }

  std::vector<message> poll_all(message_assembler& assembler) {
    std::vector<message> messages;
    for (;;) {
      auto next = assembler.poll();
      if (!next) {
        break;
      }
      messages.push_back(std::move(*next));
    }
    return messages;
  }

} // namespace

TEST(FrameAssembler, ProducesTextMessageForSingleUnfragmentedTextFrame) {
  message_assembler assembler;

  auto payload = to_bytes("hello");
  auto frame = serialize_unmasked_frame(opcode::text, true, payload);

  EXPECT_EQ(assembler.consume(frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());

  EXPECT_TRUE(produced->is_text());
  EXPECT_EQ(produced->text(), "hello");
  EXPECT_FALSE(assembler.closed());
}

TEST(FrameAssembler, ProducesBinaryMessageForSingleUnfragmentedBinaryFrame) {
  message_assembler assembler;

  std::vector<std::byte> payload{std::byte{0x00}, std::byte{0x01}, std::byte{0xFF}};
  auto frame = serialize_unmasked_frame(opcode::binary, true, payload);

  EXPECT_EQ(assembler.consume(frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());

  EXPECT_EQ(produced->kind, message_kind::binary);
  EXPECT_EQ(produced->payload, payload);
}

TEST(FrameAssembler, AssemblesFragmentedTextMessageAcrossContinuationFrames) {
  message_assembler assembler;

  auto first_fragment = to_bytes("hel");
  auto second_fragment = to_bytes("lo");

  auto first_frame = serialize_unmasked_frame(opcode::text, false, first_fragment);
  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, second_fragment);

  EXPECT_EQ(assembler.consume(first_frame), std::error_code{});
  EXPECT_FALSE(poll_one(assembler).has_value());

  EXPECT_EQ(assembler.consume(continuation_frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());

  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "hello");
}

TEST(FrameAssembler, RejectsUnexpectedContinuationWhenNoMessageInProgress) {
  message_assembler assembler;

  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("data"));
  EXPECT_EQ(assembler.consume(continuation_frame), message_assembler_error::unexpected_continuation);
}

TEST(FrameAssembler, RejectsInterleavedDataFramesDuringFragmentedMessage) {
  message_assembler assembler;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("part1"));
  auto interleaved_binary_frame = serialize_unmasked_frame(opcode::binary, true, to_bytes("x"));

  EXPECT_EQ(assembler.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(assembler.consume(interleaved_binary_frame), message_assembler_error::interleaved_data_frame);
}

TEST(FrameAssembler, ProducesPingBetweenFragmentsAndContinuesDataAssembly) {
  message_assembler assembler;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("he"));
  auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
  auto final_continuation = serialize_unmasked_frame(opcode::continuation, true, to_bytes("llo"));

  EXPECT_EQ(assembler.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(assembler.consume(ping_frame), std::error_code{});

  auto produced_after_ping = poll_one(assembler);
  ASSERT_TRUE(produced_after_ping.has_value());
  EXPECT_EQ(produced_after_ping->kind, message_kind::ping);
  EXPECT_EQ(to_string(produced_after_ping->payload), "p");

  EXPECT_EQ(assembler.consume(final_continuation), std::error_code{});

  auto produced_text = poll_one(assembler);
  ASSERT_TRUE(produced_text.has_value());
  EXPECT_EQ(produced_text->kind, message_kind::text);
  EXPECT_EQ(to_string(produced_text->payload), "hello");
}

TEST(FrameAssembler, RejectsFragmentedControlFrames) {
  message_assembler assembler;

  auto fragmented_ping_frame = serialize_unmasked_frame(opcode::ping, false, to_bytes("x"));
  EXPECT_EQ(assembler.consume(fragmented_ping_frame), protocol_error::control_frame_fragmented);
}

TEST(FrameAssembler, ProducesCloseMessageAndMarksClosed) {
  message_assembler assembler;

  auto close_payload = serialize_close_payload(close_code::normal, to_bytes("bye"));
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(assembler.consume(close_frame), std::error_code{});
  EXPECT_TRUE(assembler.closed());

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::close);
  EXPECT_EQ(produced->payload, close_payload);
}

TEST(FrameAssembler, RejectsDataAfterClose) {
  message_assembler assembler;

  auto close_payload = serialize_close_payload(close_code::normal, {});
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(assembler.consume(close_frame), std::error_code{});
  EXPECT_TRUE(assembler.closed());

  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("x"));
  EXPECT_EQ(assembler.consume(text_frame), message_assembler_error::data_after_close);

  std::span<const std::byte> empty_bytes{};
  EXPECT_EQ(assembler.consume(empty_bytes), message_assembler_error::data_after_close);
}

TEST(FrameAssembler, AcceptsCloseWithEmptyPayload) {
  message_assembler assembler;

  std::span<const std::byte> empty_payload{};
  auto close_frame = serialize_unmasked_frame(opcode::close, true, empty_payload);

  EXPECT_EQ(assembler.consume(close_frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::close);
  EXPECT_TRUE(produced->payload.empty());
  EXPECT_EQ(produced->close_code(), std::nullopt);
}

TEST(FrameAssembler, RejectsCloseWithPayloadSizeOne) {
  message_assembler assembler;

  std::vector<std::byte> invalid_payload{std::byte{0x03}};
  auto close_frame = serialize_unmasked_frame(opcode::close, true, invalid_payload);

  EXPECT_EQ(assembler.consume(close_frame), protocol_error::close_frame_payload_too_small);
}

TEST(FrameAssembler, RejectsCloseWithInvalidUtf8Reason) {
  message_assembler assembler;

  std::vector<std::byte> invalid_utf8_reason{std::byte{0xC3}, std::byte{0x28}};
  auto close_payload = serialize_close_payload(close_code::normal, invalid_utf8_reason);
  auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

  EXPECT_EQ(assembler.consume(close_frame), protocol_error::close_reason_invalid_utf8);
}

TEST(FrameAssembler, RejectsTextMessageWithInvalidUtf8) {
  message_assembler assembler;

  std::vector<std::byte> invalid_utf8_payload{std::byte{0xC3}, std::byte{0x28}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

  EXPECT_EQ(assembler.consume(text_frame), protocol_error::payload_text_invalid_utf8);
}

TEST(FrameAssembler, AllowsPartialUtf8AcrossNonFinalTextFrames) {
  message_assembler assembler;

  std::vector<std::byte> euro_sign_prefix{std::byte{0xE2}, std::byte{0x82}};
  std::vector<std::byte> euro_sign_suffix{std::byte{0xAC}};

  auto first_frame = serialize_unmasked_frame(opcode::text, false, euro_sign_prefix);
  auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, euro_sign_suffix);

  EXPECT_EQ(assembler.consume(first_frame), std::error_code{});
  EXPECT_FALSE(poll_one(assembler).has_value());

  EXPECT_EQ(assembler.consume(continuation_frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "€");
}

TEST(FrameAssembler, RejectsFinalTextMessageWithIncompleteUtf8Sequence) {
  message_assembler assembler;

  std::vector<std::byte> incomplete_utf8_payload{std::byte{0xE2}, std::byte{0x82}};
  auto text_frame = serialize_unmasked_frame(opcode::text, true, incomplete_utf8_payload);

  EXPECT_EQ(assembler.consume(text_frame), protocol_error::payload_text_invalid_utf8);
}

TEST(FrameAssembler, BuffersTruncatedFrameUntilComplete) {
  message_assembler assembler;

  auto full_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("Hi"));
  ASSERT_GE(full_frame.size(), 4U);

  EXPECT_EQ(assembler.consume(std::span<const std::byte>{full_frame.data(), 1}), std::error_code{});
  EXPECT_FALSE(poll_one(assembler).has_value());
  EXPECT_EQ(assembler.buffered_bytes(), 1U);

  EXPECT_EQ(assembler.consume(std::span<const std::byte>{full_frame.data() + 1, 1}), std::error_code{});
  EXPECT_FALSE(poll_one(assembler).has_value());
  EXPECT_EQ(assembler.buffered_bytes(), 2U);

  EXPECT_EQ(assembler.consume(std::span<const std::byte>{full_frame.data() + 2, full_frame.size() - 2}), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "Hi");
  EXPECT_EQ(assembler.buffered_bytes(), 0U);
}

TEST(FrameAssembler, ResetsStateAndAllowsReuse) {
  message_assembler assembler;

  auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("a"));
  EXPECT_EQ(assembler.consume(first_text_frame), std::error_code{});
  EXPECT_EQ(assembler.buffered_bytes(), 0U);

  assembler.reset();

  auto complete_text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("ok"));
  EXPECT_EQ(assembler.consume(complete_text_frame), std::error_code{});

  auto produced = poll_one(assembler);
  ASSERT_TRUE(produced.has_value());
  EXPECT_EQ(produced->kind, message_kind::text);
  EXPECT_EQ(to_string(produced->payload), "ok");
  EXPECT_FALSE(assembler.closed());
}

TEST(FrameAssembler, RejectsConsumeChunkLargerThanMaxMessageSize) {
  message_assembler assembler{message_assembler_config{.max_message_size = 5}};

  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("HiHello"));
  ASSERT_GT(text_frame.size(), 4U);

  EXPECT_EQ(assembler.consume(text_frame), message_assembler_error::message_too_big);
}

TEST(FrameAssembler, RejectsAssembledMessageExceedingMaxMessageSize) {
  message_assembler assembler{message_assembler_config{.max_message_size = 5}};

  auto first_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("abc"));
  auto second_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("def"));

  EXPECT_EQ(assembler.consume(first_frame), std::error_code{});
  EXPECT_EQ(assembler.consume(second_frame), message_assembler_error::message_too_big);
}

TEST(FrameAssembler, ProducesMessagesInArrivalOrder) {
  message_assembler assembler;

  auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
  auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("t"));
  auto pong_frame = serialize_unmasked_frame(opcode::pong, true, to_bytes("g"));

  EXPECT_EQ(assembler.consume(ping_frame), std::error_code{});
  EXPECT_EQ(assembler.consume(text_frame), std::error_code{});
  EXPECT_EQ(assembler.consume(pong_frame), std::error_code{});

  auto produced = poll_all(assembler);
  ASSERT_EQ(produced.size(), 3U);

  EXPECT_EQ(produced[0].kind, message_kind::ping);
  EXPECT_EQ(to_string(produced[0].payload), "p");

  EXPECT_EQ(produced[1].kind, message_kind::text);
  EXPECT_EQ(to_string(produced[1].payload), "t");

  EXPECT_EQ(produced[2].kind, message_kind::pong);
  EXPECT_EQ(to_string(produced[2].payload), "g");
}
