#include "ut.hpp"
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

#include "websocket/test_helpers.hpp"

namespace websocket = aero::websocket;

using websocket::protocol_error;
using websocket::detail::frame;
using websocket::detail::frame_encoder;
using websocket::detail::masking_key;
using websocket::detail::opcode;

using aero::tests::websocket::big_endian_bytes;
using aero::tests::websocket::starts_with;
using aero::tests::websocket::to_byte;

ut::suite websocket_frame_encoder = [] {
  "encodes unmasked length 0"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 0,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
    auto encoded = frame_encoder{}.encode(out, input);
    expect(fatal(encoded.has_value()));

    std::array<std::byte, 2> expected{to_byte(0x81U), to_byte(0x00U)};
    expect(fatal(starts_with(out, expected)));

    for (std::size_t i = input.header_size(); i < out.size(); ++i) {
      expect(out[i] == std::byte{0xAA});
    }
  };

  "encodes unmasked length 125"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 125,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
    auto encoded = frame_encoder{}.encode(out, input);
    expect(fatal(encoded.has_value()));

    std::array<std::byte, 2> expected{to_byte(0x81U), to_byte(0x7DU)};
    expect(fatal(starts_with(out, expected)));
  };

  "encodes unmasked length 126 as 16-bit extended"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 126,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
    auto encoded = frame_encoder{}.encode(out, input);
    expect(fatal(encoded.has_value()));

    auto extended = big_endian_bytes<2>(126U);
    std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x7EU), extended[0], extended[1]};
    expect(fatal(starts_with(out, expected)));
  };

  "encodes unmasked length 65535 as 16-bit extended"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 65535U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
    auto encoded = frame_encoder{}.encode(out, input);
    expect(fatal(encoded.has_value()));

    auto extended = big_endian_bytes<2>(65535U);
    std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x7EU), extended[0], extended[1]};
    expect(fatal(starts_with(out, expected)));
  };

  "encodes unmasked length 65536 as 64-bit extended"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 65536U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size(), std::byte{0xAA});
    auto encoded = frame_encoder{}.encode(out, input);
    expect(fatal(encoded.has_value()));

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
    expect(fatal(starts_with(out, expected)));
  };

  "encodes masked length 15 and copies masking key"_test = [] {
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
    expect(fatal(encoded.has_value()));

    std::vector<std::byte> expected{to_byte(0x81U), to_byte(0x8FU), key[0], key[1], key[2], key[3]};
    expect(fatal(starts_with(out, expected)));
  };

  "encodes masked length 50000 with 16-bit extended and copies masking key"_test = [] {
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
    expect(fatal(encoded.has_value()));

    auto extended = big_endian_bytes<2>(50000U);
    std::vector<std::byte> expected{to_byte(0x81U), to_byte(0xFEU), extended[0], extended[1], key[0], key[1], key[2], key[3]};
    expect(fatal(starts_with(out, expected)));
  };

  "encodes masked length 100000 with 64-bit extended and copies masking key"_test = [] {
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
    expect(fatal(encoded.has_value()));

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
    expect(fatal(starts_with(out, expected)));
  };

  "rejects output too small"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = false,
      .payload_length = 100000U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(input.header_size() - 1U);
    auto encoded = frame_encoder{}.encode(out, input);
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == aero::basic_error::not_enough_memory);
    }
  };

  "rejects masked frame without masking key"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::text,
      .masked = true,
      .payload_length = 1U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(64);
    auto encoded = frame_encoder{}.encode(out, input);
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::masking_key_missing);
    }
  };

  "rejects unmasked frame with masking key"_test = [] {
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
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::masking_flag_missing);
    }
  };

  "rejects non-final control frame"_test = [] {
    frame input{
      .fin = false,
      .opcode = opcode::ping,
      .masked = false,
      .payload_length = 0U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(64);
    auto encoded = frame_encoder{}.encode(out, input);
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::control_frame_fragmented);
    }
  };

  "rejects oversized control frame payload"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::ping,
      .masked = false,
      .payload_length = 126U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(64);
    auto encoded = frame_encoder{}.encode(out, input);
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::control_frame_payload_too_big);
    }
  };

  "rejects undersized close frame payload"_test = [] {
    frame input{
      .fin = true,
      .opcode = opcode::close,
      .masked = false,
      .payload_length = 1U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(64);
    auto encoded_valid = frame_encoder{}.encode(out, input);
    expect(encoded_valid.has_value());

    input.application_data = std::span{out.data(), 1};
    auto encoded_invalid = frame_encoder{}.encode(out, input);
    expect(fatal(not encoded_invalid.has_value()));
    expect(encoded_invalid.error() == protocol_error::close_frame_payload_too_small);
  };

  "rejects non-zero reserved bits"_test = [] {
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
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::reserved_bits_nonzero);
    }
  };

  "rejects reserved opcodes"_test = [] {
    frame input{
      .fin = true,
      .opcode = static_cast<opcode>(0x3U),
      .masked = false,
      .payload_length = 0U,
      .masking_key = std::nullopt,
    };

    std::vector<std::byte> out(64);
    auto encoded = frame_encoder{}.encode(out, input);
    expect(not encoded.has_value());
    if (!encoded) {
      expect(encoded.error() == protocol_error::opcode_reserved);
    }
  };
};

int main() {}
