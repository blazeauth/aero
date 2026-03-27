#pragma once

#include <cstddef>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/detail/utf8.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_decoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

namespace aero::websocket::detail {

  struct message_assembler_config {
    std::optional<std::size_t> max_message_size;
    bool validate_message_utf8{true};
  };

  // Assembles multiple websocket frames to a single message
  // Fail-fast on invalid UTF-8 fragments is not implemented (NON-STRICT behavior)
  // This class does not checks for fragments UTF-8 validity at all

  class message_assembler {
    using message_assembler_error = websocket::error::message_assembler_error;
    using protocol_error = websocket::error::protocol_error;

   public:
    explicit message_assembler(message_assembler_config config = {}): config_(config) {}

    [[nodiscard]] std::error_code consume(std::span<const std::byte> bytes) {
      if (received_close_) {
        return message_assembler_error::data_after_close;
      }

      if (config_.max_message_size.has_value() && bytes.size() > *config_.max_message_size) {
        return message_assembler_error::message_too_big;
      }

      receive_buffer_.append_range(bytes);

      for (;;) {
        auto parse_result = try_parse_one();
        if (!parse_result) {
          return parse_result.error();
        }

        auto parse_succeded = parse_result.value();
        if (!parse_succeded) {
          break;
        }
      }
      return {};
    }

    [[nodiscard]] std::optional<websocket::message> poll() {
      if (produced_messages_.empty()) {
        return std::nullopt;
      }
      websocket::message next = std::move(produced_messages_.front());
      produced_messages_.pop_front();
      return next;
    }

    [[nodiscard]] std::size_t buffered_bytes() const noexcept {
      return receive_buffer_.size() - receive_offset_;
    }

    [[nodiscard]] bool closed() const noexcept {
      return received_close_;
    }

    void reset() noexcept {
      receive_buffer_.clear();
      receive_offset_ = 0;
      produced_messages_.clear();
      data_message_opcode_.reset();
      assembled_payload_.clear();
      received_close_ = false;
    }

   private:
    [[nodiscard]] static message_kind to_message_kind(opcode value) noexcept {
      switch (value) {
      case opcode::text:
        return message_kind::text;
      case opcode::binary:
        return message_kind::binary;
      case opcode::ping:
        return message_kind::ping;
      case opcode::pong:
        return message_kind::pong;
      case opcode::close:
        return message_kind::close;
      default:
        return {};
      }
    }

    [[nodiscard]] std::error_code append_payload(std::span<const std::byte> payload) {
      if (config_.max_message_size.has_value() && assembled_payload_.size() + payload.size() > *config_.max_message_size) {
        return message_assembler_error::message_too_big;
      }
      assembled_payload_.append_range(payload);
      return {};
    }

    [[nodiscard]] std::expected<bool, std::error_code> try_parse_one() {
      if (receive_offset_ >= receive_buffer_.size()) {
        receive_offset_ = 0;
        receive_buffer_.clear();
        return false;
      }

      std::span available_bytes{receive_buffer_.data() + receive_offset_, receive_buffer_.size() - receive_offset_};

      auto decoded = decoder_.decode(available_bytes);
      if (!decoded) {
        if (decoded.error() == protocol_error::frame_too_small || decoded.error() == protocol_error::buffer_truncated) {
          clear_receive_buffer_if_needed();
          return false;
        }
        return std::unexpected(decoded.error());
      }

      const auto frame_size = decoded->header_size() + decoded->payload_data.size();
      receive_offset_ += frame_size;

      if (auto handle_ec = handle_frame(*decoded); handle_ec) {
        return std::unexpected(handle_ec);
      }

      clear_receive_buffer_if_needed();
      return true;
    }

    void reset_receive_buffer() {
      receive_buffer_.clear();
      receive_offset_ = 0;
    }

    void compact_receive_buffer(std::size_t remaining_bytes) {
      using difference_type = std::vector<std::byte>::difference_type;

      auto receive_offset = static_cast<difference_type>(receive_offset_);
      std::move(receive_buffer_.begin() + receive_offset, receive_buffer_.end(), receive_buffer_.begin());
      receive_buffer_.resize(remaining_bytes);
      receive_offset_ = 0;
    }

    void clear_receive_buffer_if_needed() {
      if (receive_offset_ == 0) {
        return;
      }
      if (receive_offset_ >= receive_buffer_.size()) {
        reset_receive_buffer();
        return;
      }

      constexpr std::size_t compaction_threshold_bytes{65536};

      auto remaining_bytes = receive_buffer_.size() - receive_offset_;
      auto should_compact = receive_offset_ >= compaction_threshold_bytes || receive_offset_ >= remaining_bytes;

      if (!should_compact) {
        return;
      }

      compact_receive_buffer(remaining_bytes);
    }

    [[nodiscard]] std::error_code handle_frame(const frame& frame) {
      if (frame.is_control()) {
        return handle_control_frame(frame);
      }

      if (received_close_) {
        return message_assembler_error::data_after_close;
      }

      if (frame.is_continuation()) {
        if (!data_message_opcode_) {
          return message_assembler_error::unexpected_continuation;
        }

        if (auto append_ec = append_payload(frame.application_data); append_ec) {
          return append_ec;
        }

        if (frame.fin) {
          return finalize_data_message();
        }
        return {};
      }

      if (frame.is_text() || frame.is_binary()) {
        if (data_message_opcode_) {
          return message_assembler_error::interleaved_data_frame;
        }

        data_message_opcode_ = frame.opcode;
        assembled_payload_.clear();

        if (auto append_ec = append_payload(frame.application_data); append_ec) {
          return append_ec;
        }

        if (frame.fin) {
          return finalize_data_message();
        }

        return {};
      }

      return protocol_error::opcode_invalid;
    }

    [[nodiscard]] std::error_code handle_control_frame(const frame& frame) {
      if (frame.is_close()) {
        return handle_close_frame(frame);
      }

      produced_messages_.push_back(websocket::message{
        .kind = to_message_kind(frame.opcode),
        .payload = std::vector(std::from_range, frame.payload_data),
      });
      return {};
    }

    [[nodiscard]] std::error_code handle_close_frame(const frame& frame) {
      received_close_ = true;
      data_message_opcode_.reset();
      assembled_payload_.clear();

      constexpr auto close_code_size = sizeof(close_code);

      auto frame_has_no_close_code = frame.application_data.empty();
      if (frame_has_no_close_code) {
        produced_messages_.push_back(websocket::message{
          .kind = to_message_kind(opcode::close),
        });
        return {};
      }

      auto frame_has_close_reason = frame.application_data.size() > close_code_size;
      if (frame_has_close_reason) {
        auto close_reason = frame.application_data.subspan(close_code_size);
        if (config_.validate_message_utf8 && !aero::detail::is_valid_utf8(close_reason)) {
          return protocol_error::close_reason_invalid_utf8;
        }
      }

      produced_messages_.push_back(websocket::message{
        .kind = to_message_kind(opcode::close),
        .payload = std::vector(std::from_range, frame.application_data),
      });
      return {};
    }

    [[nodiscard]] std::error_code finalize_data_message() {
      assert(data_message_opcode_);

      const auto message_opcode = *data_message_opcode_;
      const auto kind = to_message_kind(message_opcode);

      if (kind == message_kind::text) {
        if (config_.validate_message_utf8 && !aero::detail::is_valid_utf8(assembled_payload_)) {
          return protocol_error::payload_text_invalid_utf8;
        }
      }

      produced_messages_.push_back(websocket::message{
        .kind = kind,
        .payload = std::move(assembled_payload_),
      });

      assembled_payload_.clear();
      data_message_opcode_.reset();
      return {};
    }

    [[nodiscard]] std::vector<std::byte> make_close_code_buffer(websocket::close_code code) {
      const auto close_code_value = std::to_underlying(code);

      std::vector<std::byte> buffer(sizeof(websocket::close_code));
      buffer[0] = static_cast<std::byte>(static_cast<std::uint16_t>(close_code_value >> 8U) & 0xFFU);
      buffer[1] = static_cast<std::byte>(close_code_value & 0xFFU);

      return buffer;
    }

    std::vector<std::byte> receive_buffer_;
    std::size_t receive_offset_{0};

    std::optional<opcode> data_message_opcode_;
    std::vector<std::byte> assembled_payload_;

    std::deque<websocket::message> produced_messages_;
    bool received_close_{false};

    frame_decoder<role::client> decoder_;
    message_assembler_config config_;
  };

} // namespace aero::websocket::detail
