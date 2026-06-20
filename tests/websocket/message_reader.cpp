#include "ut.hpp"
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

#include "websocket/test_helpers.hpp"

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

  expect(reader.consume(frame_prefix) == std::error_code{});
  expect(not poll_one(reader).has_value());
  expect(reader.buffered_bytes() == frame_prefix.size());

  const auto remaining_payload_length = static_cast<std::size_t>(payload_length) - first_payload_length;
  const auto remaining_payload = make_payload_bytes(remaining_payload_length);

  expect(reader.consume(remaining_payload) == std::error_code{});

  auto produced = poll_one(reader);
  expect(fatal(produced.has_value()));
  expect(produced->kind == message_kind::text);
  expect(produced->payload.size() == static_cast<std::size_t>(payload_length));
  expect(not poll_one(reader).has_value());
  expect(reader.buffered_bytes() == 0U);
}

ut::suite websocket_message_reader = [] {
  "produces text message for single unfragmented text frame"_test = [] {
    message_reader reader;

    auto payload = to_bytes("hello");
    auto frame = serialize_unmasked_frame(opcode::text, true, payload);

    expect(reader.consume(frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));

    expect(produced->is_text());
    expect(produced->text() == "hello");
    expect(not reader.closed());
  };

  "produces binary message for single unfragmented binary frame"_test = [] {
    message_reader reader;

    std::vector<std::byte> payload{std::byte{0x00}, std::byte{0x01}, std::byte{0xFF}};
    auto frame = serialize_unmasked_frame(opcode::binary, true, payload);

    expect(reader.consume(frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));

    expect(produced->kind == message_kind::binary);
    expect(produced->payload == payload);
  };

  "assembles fragmented text message across continuation frames"_test = [] {
    message_reader reader;

    auto first_fragment = to_bytes("hel");
    auto second_fragment = to_bytes("lo");

    auto first_frame = serialize_unmasked_frame(opcode::text, false, first_fragment);
    auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, second_fragment);

    expect(reader.consume(first_frame) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(continuation_frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));

    expect(produced->kind == message_kind::text);
    expect(to_string(produced->payload) == "hello");
  };

  "rejects unexpected continuation when no message in progress"_test = [] {
    message_reader reader;

    auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("data"));
    expect(reader.consume(continuation_frame) == message_reader_error::unexpected_continuation);
  };

  "rejects interleaved data frames during fragmented message"_test = [] {
    message_reader reader;

    auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("part1"));
    auto interleaved_binary_frame = serialize_unmasked_frame(opcode::binary, true, to_bytes("x"));

    expect(reader.consume(first_text_frame) == std::error_code{});
    expect(reader.consume(interleaved_binary_frame) == message_reader_error::interleaved_data_frame);
  };

  "produces ping between fragments and continues data assembly"_test = [] {
    message_reader reader;

    auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("he"));
    auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
    auto final_continuation = serialize_unmasked_frame(opcode::continuation, true, to_bytes("llo"));

    expect(reader.consume(first_text_frame) == std::error_code{});
    expect(reader.consume(ping_frame) == std::error_code{});

    auto produced_after_ping = poll_one(reader);
    expect(fatal(produced_after_ping.has_value()));
    expect(produced_after_ping->kind == message_kind::ping);
    expect(to_string(produced_after_ping->payload) == "p");

    expect(reader.consume(final_continuation) == std::error_code{});

    auto produced_text = poll_one(reader);
    expect(fatal(produced_text.has_value()));
    expect(produced_text->kind == message_kind::text);
    expect(to_string(produced_text->payload) == "hello");
  };

  "rejects fragmented control frames"_test = [] {
    message_reader reader;

    auto fragmented_ping_frame = serialize_unmasked_frame(opcode::ping, false, to_bytes("x"));
    expect(reader.consume(fragmented_ping_frame) == protocol_error::control_frame_fragmented);
  };

  "produces close message and marks closed"_test = [] {
    message_reader reader;

    auto close_payload = serialize_close_payload(close_code::normal, to_bytes("bye"));
    auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

    expect(reader.consume(close_frame) == std::error_code{});
    expect(reader.closed());

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::close);
    expect(produced->payload == close_payload);
  };

  "rejects data after close"_test = [] {
    message_reader reader;

    auto close_payload = serialize_close_payload(close_code::normal, {});
    auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

    expect(reader.consume(close_frame) == std::error_code{});
    expect(reader.closed());

    auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("x"));
    expect(reader.consume(text_frame) == message_reader_error::data_after_close);

    std::span<const std::byte> empty_bytes{};
    expect(reader.consume(empty_bytes) == message_reader_error::data_after_close);
  };

  "accepts close with empty payload"_test = [] {
    message_reader reader;

    std::span<const std::byte> empty_payload{};
    auto close_frame = serialize_unmasked_frame(opcode::close, true, empty_payload);

    expect(reader.consume(close_frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::close);
    expect(produced->payload.empty());
    expect(produced->close_code() == std::nullopt);
  };

  "rejects close with payload size of one byte"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_payload{std::byte{0x03}};
    auto close_frame = serialize_unmasked_frame(opcode::close, true, invalid_payload);

    expect(reader.consume(close_frame) == protocol_error::close_frame_payload_too_small);
  };

  "rejects close with invalid utf8 reason"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_utf8_reason{std::byte{0xC3}, std::byte{0x28}};
    auto close_payload = serialize_close_payload(close_code::normal, invalid_utf8_reason);
    auto close_frame = serialize_unmasked_frame(opcode::close, true, close_payload);

    expect(reader.consume(close_frame) == protocol_error::close_reason_invalid_utf8);
  };

  "rejects text message with invalid utf8"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_utf8_payload{std::byte{0xC3}, std::byte{0x28}};
    auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

    expect(reader.consume(text_frame) == protocol_error::payload_text_invalid_utf8);
  };

  "allows partial utf8 across non-final text frames"_test = [] {
    message_reader reader;

    std::vector<std::byte> euro_sign_prefix{std::byte{0xE2}, std::byte{0x82}};
    std::vector<std::byte> euro_sign_suffix{std::byte{0xAC}};

    auto first_frame = serialize_unmasked_frame(opcode::text, false, euro_sign_prefix);
    auto continuation_frame = serialize_unmasked_frame(opcode::continuation, true, euro_sign_suffix);

    expect(reader.consume(first_frame) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(continuation_frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::text);
    expect(to_string(produced->payload) == "€");
  };

  "rejects final text message with incomplete utf8 sequence"_test = [] {
    message_reader reader;

    std::vector<std::byte> incomplete_utf8_payload{std::byte{0xE2}, std::byte{0x82}};
    auto text_frame = serialize_unmasked_frame(opcode::text, true, incomplete_utf8_payload);

    expect(reader.consume(text_frame) == protocol_error::payload_text_invalid_utf8);
  };

  "rejects chopped single frame text as soon as utf8 becomes invalid"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_utf8_payload{std::byte{0xC3}, std::byte{0x28}};
    auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

    expect(reader.consume(std::span<const std::byte>{text_frame.data(), 3}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}) == protocol_error::payload_text_invalid_utf8);
  };

  "rejects chopped single frame text when scalar value exceeds unicode range"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_utf8_payload{
      std::byte{0xF4},
      std::byte{0x90},
      std::byte{0x80},
      std::byte{0x80},
    };
    auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

    expect(reader.consume(std::span<const std::byte>{text_frame.data(), 3}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}) == protocol_error::payload_text_invalid_utf8);
  };

  "accepts valid utf8 split across multiple consume calls within single frame"_test = [] {
    message_reader reader;

    std::vector<std::byte> euro_sign{std::byte{0xE2}, std::byte{0x82}, std::byte{0xAC}};
    auto text_frame = serialize_unmasked_frame(opcode::text, true, euro_sign);

    expect(reader.consume(std::span<const std::byte>{text_frame.data(), 3}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 4, 1}) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::text);
    expect(to_string(produced->payload) == "€");
  };

  "does not reject potentially valid utf8 prefix before contradicting byte arrives"_test = [] {
    message_reader reader;

    std::vector<std::byte> invalid_utf8_payload{std::byte{0xE2}, std::byte{0x82}, std::byte{0x41}};
    auto text_frame = serialize_unmasked_frame(opcode::text, true, invalid_utf8_payload);

    expect(reader.consume(std::span<const std::byte>{text_frame.data(), 3}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 3, 1}) == std::error_code{});
    expect(not poll_one(reader).has_value());

    expect(reader.consume(std::span<const std::byte>{text_frame.data() + 4, 1}) == protocol_error::payload_text_invalid_utf8);
  };

  "buffers truncated frame until complete"_test = [] {
    message_reader reader;

    auto full_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("Hi"));
    expect(fatal(full_frame.size() >= 4U));

    expect(reader.consume(std::span<const std::byte>{full_frame.data(), 1}) == std::error_code{});
    expect(not poll_one(reader).has_value());
    expect(reader.buffered_bytes() == 1U);

    expect(reader.consume(std::span<const std::byte>{full_frame.data() + 1, 1}) == std::error_code{});
    expect(not poll_one(reader).has_value());
    expect(reader.buffered_bytes() == 2U);

    expect(reader.consume(std::span<const std::byte>{full_frame.data() + 2, full_frame.size() - 2}) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::text);
    expect(to_string(produced->payload) == "Hi");
    expect(reader.buffered_bytes() == 0U);
  };

  "buffers truncated payload for 7-bit length until complete"_test = [] {
    expect_buffers_truncated_payload_until_complete(5U, 2U);
  };

  "buffers truncated payload for 16-bit length until complete"_test = [] {
    expect_buffers_truncated_payload_until_complete(50000U, 10U);
  };

  "buffers truncated payload for 64-bit length until complete"_test = [] {
    expect_buffers_truncated_payload_until_complete(100000U, 10U);
  };

  "resets state and allows reuse"_test = [] {
    message_reader reader;

    auto first_text_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("a"));
    expect(reader.consume(first_text_frame) == std::error_code{});
    expect(reader.buffered_bytes() == 0U);

    reader.reset();

    auto complete_text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("ok"));
    expect(reader.consume(complete_text_frame) == std::error_code{});

    auto produced = poll_one(reader);
    expect(fatal(produced.has_value()));
    expect(produced->kind == message_kind::text);
    expect(to_string(produced->payload) == "ok");
    expect(not reader.closed());
  };

  "rejects single message exceeding maximum message size"_test = [] {
    message_reader reader{message_reader_config{.max_message_size = 5}};

    auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("HiHello"));
    expect(fatal(text_frame.size() > (4U)));

    expect(reader.consume(text_frame) == message_reader_error::message_too_big);
  };

  "accepts large consume chunk when each message fits maximum message size"_test = [] {
    message_reader reader{message_reader_config{.max_message_size = 5}};

    auto first_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("abc"));
    auto second_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("de"));

    std::vector<std::byte> chunk;
    chunk.append_range(first_frame);
    chunk.append_range(second_frame);
    expect(fatal(chunk.size() > (5U)));

    expect(reader.consume(chunk) == std::error_code{});

    auto produced = poll_all(reader);
    expect(fatal(produced.size() == 2U));
    expect(to_string(produced[0].payload) == "abc");
    expect(to_string(produced[1].payload) == "de");
  };

  "rejects assembled message exceeding maximum message size"_test = [] {
    message_reader reader{message_reader_config{.max_message_size = 5}};

    auto first_frame = serialize_unmasked_frame(opcode::text, false, to_bytes("abc"));
    auto second_frame = serialize_unmasked_frame(opcode::continuation, true, to_bytes("def"));

    expect(reader.consume(first_frame) == std::error_code{});
    expect(reader.consume(second_frame) == message_reader_error::message_too_big);
  };

  "produces messages in arrival order"_test = [] {
    message_reader reader;

    auto ping_frame = serialize_unmasked_frame(opcode::ping, true, to_bytes("p"));
    auto text_frame = serialize_unmasked_frame(opcode::text, true, to_bytes("t"));
    auto pong_frame = serialize_unmasked_frame(opcode::pong, true, to_bytes("g"));

    expect(reader.consume(ping_frame) == std::error_code{});
    expect(reader.consume(text_frame) == std::error_code{});
    expect(reader.consume(pong_frame) == std::error_code{});

    auto produced = poll_all(reader);
    expect(fatal(produced.size() == 3U));

    expect(produced[0].kind == message_kind::ping);
    expect(to_string(produced[0].payload) == "p");

    expect(produced[1].kind == message_kind::text);
    expect(to_string(produced[1].payload) == "t");

    expect(produced[2].kind == message_kind::pong);
    expect(to_string(produced[2].payload) == "g");
  };
};

int main() {}
