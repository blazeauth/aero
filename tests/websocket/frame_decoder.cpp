#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <ut/ut.hpp>

#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_decoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/detail/role.hpp"
#include "aero/websocket/error.hpp"

#include "websocket/test_helpers.hpp"

using namespace ut;

constexpr std::uint8_t opcode_text_value = 0x1U;
constexpr std::uint8_t opcode_close_value = 0x8U;
constexpr std::uint8_t opcode_ping_value = 0x9U;
constexpr std::uint8_t reserved_noncontrol_opcode_value = 0x3U;
constexpr std::uint8_t reserved_control_opcode_value = 0xBU;
constexpr std::uint8_t payload_len_16_indicator = 126U;
constexpr std::uint8_t payload_len_64_indicator = 127U;

using aero::websocket::protocol_error;
using aero::websocket::detail::frame;
using aero::websocket::detail::masking_key;
using aero::websocket::detail::opcode;
using aero::websocket::detail::role;

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
void expect_header_decodes_to(std::span<const std::byte> bytes, const frame& expected) {
  auto decoded = Decoder{}.decode_header(bytes);
  expect[decoded.has_value()];
  expect(frame_headers_equal(expected, decoded.value()));
  expect(decoded->payload_data.empty());
  expect(decoded->application_data.empty());
}

void expect_rejected(std::span<const std::byte> bytes) {
  auto decoded = client_frame_decoder{}.decode_header(bytes);
  expect(not decoded.has_value());
}

template <typename ErrorEnum>
void expect_rejected_with(std::span<const std::byte> bytes, ErrorEnum expected_error) {
  auto decoded = client_frame_decoder{}.decode_header(bytes);
  expect[not decoded.has_value()];
  expect(decoded.error() == expected_error);
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

int main() {
  suite websocket_frame_decoder = [] {
    "decodes unmasked frames"_test = [] {
      for (std::uint64_t payload_length : std::to_array<std::uint64_t>({0U, 125U, 126U, 65535U, 65536U, 100000U})) {
        auto payload = make_payload_bytes(static_cast<std::size_t>(payload_length));
        auto bytes = build_frame_bytes_canonical(true,
          false,
          false,
          false,
          opcode_text_value,
          false,
          payload_length,
          std::nullopt,
          payload);
        expect_header_decodes_to<client_frame_decoder>(bytes, expected_text_frame_unmasked(payload_length));
      }
    };

    "decodes masked frames and parses masking key"_test = [] {
      std::array<std::pair<std::uint64_t, masking_key>, 3> cases{
        std::pair<std::uint64_t, masking_key>{15U, masking_key{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}}},
        std::pair<std::uint64_t, masking_key>{50000U, masking_key{std::byte{10}, std::byte{11}, std::byte{12}, std::byte{13}}},
        std::pair<std::uint64_t, masking_key>{100000U, masking_key{std::byte{20}, std::byte{21}, std::byte{22}, std::byte{23}}},
      };

      for (const auto& [payload_length, key] : cases) {
        auto payload = make_payload_bytes(static_cast<std::size_t>(payload_length));
        auto bytes =
          build_frame_bytes_canonical(true, false, false, false, opcode_text_value, true, payload_length, key, payload);
        expect_header_decodes_to<server_frame_decoder>(bytes, expected_text_frame_masked(payload_length, key));
      }
    };

    "rejects non-zero rsv bits"_test = [] {
      auto payload = make_payload_bytes(0);
      auto bytes = build_frame_bytes_canonical(true, true, false, false, opcode_text_value, false, 0U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::reserved_bits_nonzero);
    };

    "rejects reserved opcodes non-control range"_test = [] {
      auto payload = make_payload_bytes(0);
      auto bytes = build_frame_bytes_canonical(true,
        false,
        false,
        false,
        reserved_noncontrol_opcode_value,
        false,
        0U,
        std::nullopt,
        payload);
      expect_rejected_with(bytes, protocol_error::opcode_reserved);
    };

    "rejects reserved opcodes control range"_test = [] {
      auto payload = make_payload_bytes(0);
      auto bytes =
        build_frame_bytes_canonical(true, false, false, false, reserved_control_opcode_value, false, 0U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::opcode_reserved);
    };

    "rejects non-final control frame"_test = [] {
      auto payload = make_payload_bytes(0);
      auto bytes = build_frame_bytes_canonical(false, false, false, false, opcode_ping_value, false, 0U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::control_frame_fragmented);
    };

    "rejects oversized control frame payload"_test = [] {
      auto payload = make_payload_bytes(126);
      auto bytes =
        build_frame_bytes_canonical(true, false, false, false, opcode_ping_value, false, 126U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::control_frame_payload_too_big);
    };

    "rejects undersized close frame payload"_test = [] {
      auto payload = make_payload_bytes(1);
      auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_close_value, false, 1U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::close_frame_payload_too_small);
    };

    "rejects masked frame without masking key even if payload length is zero"_test = [] {
      std::array<std::byte, 2> bytes{
        make_first_byte(true, false, false, false, opcode_text_value),
        make_second_byte(true, 0U),
      };
      expect_rejected(bytes);
    };

    "decodes header before full payload arrives"_test = [] {
      auto payload = make_payload_bytes(2);
      auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, false, 5U, std::nullopt, payload);
      expect_header_decodes_to<client_frame_decoder>(std::span<const std::byte>{bytes.data(), 2},
        expected_text_frame_unmasked(5U));
    };

    "rejects non-canonical length encoding 126 for length 125"_test = [] {
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
      expect_rejected(bytes);
    };

    "rejects non-canonical length encoding 127 for length 1"_test = [] {
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
      expect_rejected(bytes);
    };

    "rejects non-canonical length encoding 127 for length 65535"_test = [] {
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
      expect_rejected(bytes);
    };

    "rejects oversized payload length"_test = [] {
      auto too_big_payload_length = frame::max_allowed_payload_length + 1U;
      auto extended = big_endian_bytes<8>(too_big_payload_length);
      std::array<std::byte, 10> header_only{};
      header_only[0] = make_first_byte(true, false, false, false, opcode_text_value);
      header_only[1] = make_second_byte(false, payload_len_64_indicator);
      for (std::size_t i{}; i < 8U; ++i) {
        header_only[2U + i] = extended[i];
      }

      auto decoded = client_frame_decoder{}.decode_header(header_only);
      if (!decoded) {
        expect(decoded.error() == protocol_error::payload_length_too_big);
      } else {
        expect[false];
      }
    };

    "client rejects masked frames"_test = [] {
      auto payload = make_payload_bytes(1);
      auto bytes = build_frame_bytes_canonical(true, false, false, false, opcode_text_value, true, 1U, std::nullopt, payload);
      expect_rejected_with(bytes, protocol_error::masked_frame_from_server);
    };
  };
}
