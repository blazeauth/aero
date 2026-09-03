#pragma once

#include <cstdint>
#include <string>
#include <system_error>

namespace aero::websocket {

  enum class protocol_error : std::uint8_t {
    connection_closed = 1,
    masking_key_generation_failed,
    masking_key_missing,
    masking_flag_missing,
    control_frame_payload_too_big,
    control_frame_fragmented,
    close_frame_payload_too_small,
    opcode_reserved,
    opcode_invalid,
    close_code_reserved,
    close_code_invalid,
    close_code_missing,
    close_code_server_only,
    close_reason_invalid_utf8,
    reserved_bits_nonzero,
    payload_length_too_big,
    payload_length_invalid,
    payload_text_invalid_utf8,
    payload_fragment_invalid_utf8,
    buffer_truncated,
    frame_too_small,
    masked_frame_from_server,
    already_closing,
    already_reading,
    connection_not_closed,
    unexpected_continuation,
    interleaved_data_frame,
    message_too_big,
    data_after_close,
  };

  enum class handshake_error : std::uint8_t {
    accept_header_invalid = 1,
    connection_header_invalid,
    upgrade_header_invalid,
    status_code_invalid,
    accept_challenge_failed,
    header_name_reserved,
  };

  namespace detail {
    class protocol_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.websocket.protocol_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::websocket::protocol_error;
        switch (static_cast<protocol_error>(value)) {
        case protocol_error::connection_closed:
          return "connection closed";
        case protocol_error::masking_key_generation_failed:
          return "masking key generation failed";
        case protocol_error::masking_key_missing:
          return "mask bit is set but masking key is missing";
        case protocol_error::masking_flag_missing:
          return "masking key is present but mask bit is not set";
        case protocol_error::control_frame_payload_too_big:
          return "control frame payload length must be 125 bytes or less";
        case protocol_error::control_frame_fragmented:
          return "control frame must not be fragmented (control frames cannot be continued)";
        case protocol_error::close_frame_payload_too_small:
          return "close frame payload must be either empty or at least 2 bytes";
        case protocol_error::opcode_reserved:
          return "received reserved opcode";
        case protocol_error::opcode_invalid:
          return "received invalid opcode";
        case protocol_error::close_code_reserved:
          return "close code is reserved and must not be used";
        case protocol_error::close_code_invalid:
          return "close code is invalid";
        case protocol_error::close_code_missing:
          return "close code missing in close frame payload";
        case protocol_error::close_code_server_only:
          return "close code is server-only and must not be sent by a client application";
        case protocol_error::close_reason_invalid_utf8:
          return "close reason is not a valid utf-8";
        case protocol_error::reserved_bits_nonzero:
          return "rsv1/rsv2/rsv3 must be 0";
        case protocol_error::payload_length_too_big:
          return "payload length exceeds the maximum allowed value";
        case protocol_error::payload_length_invalid:
          return "payload length field is invalid";
        case protocol_error::payload_text_invalid_utf8:
          return "payload text is not a valid utf-8";
        case protocol_error::payload_fragment_invalid_utf8:
          return "payload fragment payload is not a valid utf-8";
        case protocol_error::buffer_truncated:
          return "buffer does not contain the full payload declared by the length field";
        case protocol_error::frame_too_small:
          return "frame too small";
        case protocol_error::masked_frame_from_server:
          return "client received masked frame from server";
        case protocol_error::already_closing:
          return "connection is already in a closing state";
        case protocol_error::already_reading:
          return "already reading";
        case protocol_error::connection_not_closed:
          return "connect requires a closed connection";
        case protocol_error::unexpected_continuation:
          return "unexpected continuation frame";
        case protocol_error::interleaved_data_frame:
          return "data frame interleaved into fragmented message";
        case protocol_error::message_too_big:
          return "message too big";
        case protocol_error::data_after_close:
          return "data after close";
        default:
          return "unknown websocket protocol error";
        }
      }
    };

    class handshake_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.websocket.handshake_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::websocket::handshake_error;
        switch (static_cast<handshake_error>(value)) {
        case handshake_error::accept_header_invalid:
          return "header Sec-WebSocket-Accept is missing or invalid";
        case handshake_error::connection_header_invalid:
          return "connection header value is not Upgrade";
        case handshake_error::upgrade_header_invalid:
          return "upgrade header value is not websocket";
        case handshake_error::status_code_invalid:
          return "http status code is not 101";
        case handshake_error::accept_challenge_failed:
          return "challenge verification for Sec-WebSocket-Accept failed";
        case handshake_error::header_name_reserved:
          return "header name is reserved";
        default:
          return "unknown websocket handshake error";
        }
      }
    };
  } // namespace detail

  const inline std::error_category& protocol_error_category() noexcept {
    static const detail::protocol_error_category category{};
    return category;
  }

  const inline std::error_category& handshake_error_category() noexcept {
    static const detail::handshake_error_category category{};
    return category;
  }

  inline std::error_code make_error_code(protocol_error value) {
    return {static_cast<int>(value), websocket::protocol_error_category()};
  }

  inline std::error_code make_error_code(handshake_error value) {
    return {static_cast<int>(value), websocket::handshake_error_category()};
  }

  [[nodiscard]] inline bool is_protocol_violation(const std::error_code& ec) {
    return ec.category() == protocol_error_category();
  }

  [[nodiscard]] inline bool is_invalid_payload(const std::error_code& ec) {
    if (ec.category() != protocol_error_category()) {
      return false;
    }

    return ec == make_error_code(protocol_error::close_reason_invalid_utf8) ||
           ec == make_error_code(protocol_error::payload_fragment_invalid_utf8) ||
           ec == make_error_code(protocol_error::payload_text_invalid_utf8);
  }

} // namespace aero::websocket

template <>
struct std::is_error_code_enum<aero::websocket::protocol_error> : std::true_type {};
template <>
struct std::is_error_code_enum<aero::websocket::handshake_error> : std::true_type {};
