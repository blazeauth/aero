#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/websocket/close_code.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_decoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/detail/utf8_validator.hpp"
#include "aero/websocket/error.hpp"
#include "aero/websocket/message.hpp"

namespace aero::websocket::detail {

  struct message_reader_config {
    std::optional<std::size_t> max_message_size;
    bool validate_message_utf8{true};
    bool validate_utf8_early{true};
  };

  [[nodiscard]] inline message_kind message_kind_from_opcode(opcode value) noexcept {
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
    case opcode::continuation:
    default:
      assert(false && "opcode does not map to a complete websocket message kind");
      std::unreachable();
    }
  }

  class message_reader_buffer {
   public:
    void append(std::span<const std::byte> bytes) {
      bytes_.append_range(bytes);
    }

    [[nodiscard]] std::span<const std::byte> available_bytes() const noexcept {
      if (offset_ >= bytes_.size()) {
        return {};
      }
      return {bytes_.data() + offset_, bytes_.size() - offset_};
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return available_bytes().size();
    }

    [[nodiscard]] std::size_t max_size() const noexcept {
      return bytes_.max_size();
    }

    [[nodiscard]] bool empty() const noexcept {
      return size() == 0;
    }

    void consume(std::size_t byte_count) {
      assert(byte_count <= size());
      offset_ += byte_count;
      discard_consumed_if_needed();
    }

    void discard_consumed_if_needed() {
      if (offset_ == 0) {
        return;
      }

      if (offset_ >= bytes_.size()) {
        clear();
        return;
      }

      constexpr std::size_t compaction_threshold_bytes{65536};

      const auto remaining_bytes = bytes_.size() - offset_;
      const auto should_compact = offset_ >= compaction_threshold_bytes || offset_ >= remaining_bytes;
      if (!should_compact) {
        return;
      }

      compact(remaining_bytes);
    }

    void clear() noexcept {
      bytes_.clear();
      offset_ = 0;
    }

   private:
    void compact(std::size_t remaining_bytes) {
      using difference_type = std::vector<std::byte>::difference_type;

      const auto offset = static_cast<difference_type>(offset_);
      std::move(bytes_.begin() + offset, bytes_.end(), bytes_.begin());
      bytes_.resize(remaining_bytes);
      offset_ = 0;
    }

    std::vector<std::byte> bytes_;
    std::size_t offset_{0};
  };

  class data_message_state {
    using message_reader_error = websocket::message_reader_error;
    using protocol_error = websocket::protocol_error;

    struct message_in_progress {
      detail::opcode opcode{};
      std::vector<std::byte> payload;
      utf8_validator utf8;
    };

   public:
    explicit data_message_state(message_reader_config config = {}): config_(config) {}

    void reset() noexcept {
      message_.reset();
    }

    [[nodiscard]] std::error_code validate_next_frame(const frame& frame) const {
      // Control frames are single-frame messages, so they cannot expect
      // the continuation, meaning that no interleaving is possible
      if (frame.is_control()) {
        return {};
      }

      if (frame.is_continuation()) {
        // Received continuation frame without previously receiving
        // initiation frame (with FIN=0): unexpected continuation
        if (!message_) {
          return message_reader_error::unexpected_continuation;
        }
        return {};
      }

      if (!frame.is_text() && !frame.is_binary()) {
        return protocol_error::opcode_invalid;
      }

      // Received non-control & non-continuation frame while
      // expecting continuation frame for current message
      if (message_) {
        return message_reader_error::interleaved_data_frame;
      }

      return {};
    }

    void begin_frame(const frame& frame) {
      assert(!frame.is_control());
      assert(!frame.is_continuation());
      assert(!validate_next_frame(frame));

      message_.emplace(message_in_progress{
        .opcode = frame.opcode,
      });
    }

    [[nodiscard]] std::error_code append_payload(const frame& frame, std::span<const std::byte> chunk) {
      assert(!frame.is_control());
      assert(message_);

      if (auto validation_ec = validate_utf8_chunk(frame, chunk); validation_ec) {
        return validation_ec;
      }

      if (config_.max_message_size.has_value() && message_->payload.size() + chunk.size() > *config_.max_message_size) {
        return message_reader_error::message_too_big;
      }

      message_->payload.append_range(chunk);
      return {};
    }

    [[nodiscard]] std::expected<std::optional<websocket::message>, std::error_code> finish_frame(const frame& frame) {
      assert(!frame.is_control());

      if (!frame.fin) {
        return {};
      }

      assert(message_);

      if (message_->opcode == opcode::text && config_.validate_message_utf8 && !message_->utf8.finish()) {
        return std::unexpected(protocol_error::payload_text_invalid_utf8);
      }

      return take_message();
    }

   private:
    [[nodiscard]] bool should_track_utf8() const noexcept {
      if (!message_ || message_->opcode != opcode::text) {
        return false;
      }

      return config_.validate_message_utf8 || config_.validate_utf8_early;
    }

    [[nodiscard]] std::error_code validate_utf8_chunk(const frame& frame, std::span<const std::byte> chunk) {
      if (!should_track_utf8() || chunk.empty()) {
        return {};
      }

      if (message_->utf8.write(chunk)) {
        return {};
      }

      if (!frame.fin) {
        if (config_.validate_utf8_early) {
          return protocol_error::payload_fragment_invalid_utf8;
        }
        return {};
      }

      if (config_.validate_message_utf8 || config_.validate_utf8_early) {
        return protocol_error::payload_text_invalid_utf8;
      }

      return {};
    }

    [[nodiscard]] websocket::message take_message() {
      assert(message_);

      websocket::message message{
        .kind = message_kind_from_opcode(message_->opcode),
        .payload = std::move(message_->payload),
      };

      message_.reset();
      return message;
    }

    message_reader_config config_;
    std::optional<message_in_progress> message_;
  };

  class message_reader {
    using message_reader_error = websocket::message_reader_error;
    using protocol_error = websocket::protocol_error;

    enum class reader_state : std::uint8_t {
      waiting_for_frame_header,
      reading_frame_payload,
      closed,
    };

    enum class read_progress : std::uint8_t {
      need_more_bytes,
      made_progress,
    };

    using read_result = std::expected<read_progress, std::error_code>;

   public:
    explicit message_reader(message_reader_config config = {}): config_(config), data_message_(config) {}

    [[nodiscard]] std::error_code consume(std::span<const std::byte> bytes) {
      if (state_ == reader_state::closed) {
        return message_reader_error::data_after_close;
      }

      receive_buffer_.append(bytes);

      for (;;) {
        auto read_result = try_read_one();
        if (!read_result) {
          return read_result.error();
        }

        // Not enough data to form a WebSocket message, this is not
        // an error, we just need more data from the socket
        if (read_result == read_progress::need_more_bytes) {
          return {};
        }

        if (state_ == reader_state::closed && !receive_buffer_.empty()) {
          return message_reader_error::data_after_close;
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
      return receive_buffer_.size();
    }

    [[nodiscard]] bool closed() const noexcept {
      return state_ == reader_state::closed;
    }

    void reset() noexcept {
      receive_buffer_.clear();
      produced_messages_.clear();
      active_frame_.reset();
      data_message_.reset();
      state_ = reader_state::waiting_for_frame_header;
    }

   private:
    struct frame_in_progress {
      frame header;
      std::size_t payload_bytes_processed{0};

      [[nodiscard]] std::size_t frame_size() const noexcept {
        return header.header_size() + static_cast<std::size_t>(header.payload_length);
      }
    };

    [[nodiscard]] read_result try_read_one() {
      switch (state_) {
      case reader_state::waiting_for_frame_header:
        return try_read_frame_header();
      case reader_state::reading_frame_payload:
        return try_read_frame_payload();
      case reader_state::closed:
        return read_progress::need_more_bytes;
      }

      std::unreachable();
    }

    [[nodiscard]] read_result try_read_frame_header() {
      assert(state_ == reader_state::waiting_for_frame_header);

      // Ask transport to read more bytes from the socket
      // before trying to parse frame header
      if (receive_buffer_.size() < frame::min_header_size) {
        return read_progress::need_more_bytes;
      }

      auto decoded_header = decoder_.decode_header(receive_buffer_.available_bytes());
      if (!decoded_header) {
        if (decoded_header.error() == protocol_error::buffer_truncated) {
          return read_progress::need_more_bytes;
        }
        return std::unexpected(decoded_header.error());
      }

      // This check is actually needed. By RFC6455, message can carry up to
      // 0x7FFF'FFFF'FFFF'FFFF bytes, but this does not mean that the vector
      // can store this much bytes on any platform. On the 32-bit platforms,
      // size_type will have a maximum capacity of 0xFFFFFFFF, meaning that
      // the vector::max_size will be outgrown by any message bigger than than.
      //
      // This is kind of an edge-case, but if there will be any issues with
      // this, we can add up multiple std::vector's when one of them is full
      if (decoded_header->payload_length > receive_buffer_.max_size()) [[unlikely]] {
        return std::unexpected(protocol_error::payload_length_too_big);
      }

      if (auto validation_ec = data_message_.validate_next_frame(*decoded_header); validation_ec) {
        return std::unexpected(validation_ec);
      }

      active_frame_ = frame_in_progress{.header = *decoded_header};
      if (!decoded_header->is_control() && !decoded_header->is_continuation()) {
        data_message_.begin_frame(*decoded_header);
      }
      state_ = reader_state::reading_frame_payload;

      return read_progress::made_progress;
    }

    [[nodiscard]] read_result try_read_frame_payload() {
      assert(state_ == reader_state::reading_frame_payload);
      assert(active_frame_.has_value());

      if (active_frame_->header.is_control()) {
        return try_read_control_frame_payload();
      }

      return try_read_data_frame_payload();
    }

    [[nodiscard]] read_result try_read_control_frame_payload() {
      assert(active_frame_.has_value());
      assert(active_frame_->header.is_control());

      const auto bytes = receive_buffer_.available_bytes();
      const auto buffered_payload_len = buffered_payload_length(*active_frame_, bytes);
      if (buffered_payload_len < active_frame_->header.payload_length) {
        receive_buffer_.discard_consumed_if_needed();
        return read_progress::need_more_bytes;
      }

      if (auto finish_ec = finish_control_frame(bytes); finish_ec) {
        return std::unexpected(finish_ec);
      }

      return read_progress::made_progress;
    }

    [[nodiscard]] read_result try_read_data_frame_payload() {
      assert(active_frame_.has_value());
      assert(!active_frame_->header.is_control());

      auto& active_frame = *active_frame_;
      const auto bytes = receive_buffer_.available_bytes();
      const auto buffered_payload_len = buffered_payload_length(active_frame, bytes);

      if (buffered_payload_len > active_frame.payload_bytes_processed) {
        auto payload_chunk = next_payload_chunk(active_frame, bytes, buffered_payload_len);
        if (auto append_ec = data_message_.append_payload(active_frame.header, payload_chunk); append_ec) {
          return std::unexpected(append_ec);
        }

        active_frame.payload_bytes_processed = buffered_payload_len;
      }

      if (buffered_payload_len < active_frame.header.payload_length) {
        receive_buffer_.discard_consumed_if_needed();
        return read_progress::need_more_bytes;
      }

      if (auto finish_ec = finish_data_frame(); finish_ec) {
        return std::unexpected(finish_ec);
      }

      return read_progress::made_progress;
    }

    [[nodiscard]] static std::size_t buffered_payload_length(const frame_in_progress& active_frame,
      std::span<const std::byte> bytes) noexcept {
      const auto header_size = active_frame.header.header_size();
      if (bytes.size() <= header_size) {
        return 0;
      }

      const auto visible_payload_bytes = bytes.size() - header_size;
      return (std::min<std::size_t>)(visible_payload_bytes, active_frame.header.payload_length);
    }

    [[nodiscard]] static std::span<const std::byte> next_payload_chunk(const frame_in_progress& active_frame,
      std::span<const std::byte> bytes, std::size_t payload_length_in_buffer) noexcept {
      const auto new_payload_bytes = payload_length_in_buffer - active_frame.payload_bytes_processed;
      const auto payload_offset = active_frame.header.header_size() + active_frame.payload_bytes_processed;
      return bytes.subspan(payload_offset, new_payload_bytes);
    }

    [[nodiscard]] std::error_code finish_data_frame() {
      assert(active_frame_.has_value());
      assert(!active_frame_->header.is_control());

      auto message_result = data_message_.finish_frame(active_frame_->header);
      if (!message_result) {
        return message_result.error();
      }

      if (*message_result) {
        produced_messages_.push_back(std::move(**message_result));
      }

      finish_active_frame(reader_state::waiting_for_frame_header);
      return {};
    }

    [[nodiscard]] std::error_code finish_control_frame(std::span<const std::byte> bytes) {
      assert(active_frame_.has_value());
      assert(active_frame_->header.is_control());

      auto completed_frame = active_frame_->header;
      const auto frame_size = active_frame_->frame_size();
      auto payload = std::vector(std::from_range, bytes.subspan(completed_frame.header_size(), completed_frame.payload_length));
      completed_frame.payload_data = std::span<const std::byte>{payload};
      completed_frame.application_data = completed_frame.payload_data;

      if (auto validate_ec = completed_frame.validate(); validate_ec) {
        return validate_ec;
      }

      if (auto dispatch_ec = dispatch_control_frame(completed_frame); dispatch_ec) {
        return dispatch_ec;
      }

      const auto next_state = completed_frame.is_close() ? reader_state::closed : reader_state::waiting_for_frame_header;
      if (completed_frame.is_close()) {
        data_message_.reset();
      }

      finish_active_frame(next_state, frame_size);
      return {};
    }

    void finish_active_frame(reader_state next_state) {
      assert(active_frame_.has_value());
      finish_active_frame(next_state, active_frame_->frame_size());
    }

    void finish_active_frame(reader_state next_state, std::size_t frame_size) {
      receive_buffer_.consume(frame_size);
      active_frame_.reset();
      state_ = next_state;
    }

    [[nodiscard]] std::error_code dispatch_control_frame(const frame& frame) {
      assert(frame.is_control());

      if (frame.is_close()) {
        return handle_close_frame(frame);
      }

      produced_messages_.push_back(websocket::message{
        .kind = message_kind_from_opcode(frame.opcode),
        .payload = std::vector(std::from_range, frame.payload_data),
      });
      return {};
    }

    [[nodiscard]] std::error_code handle_close_frame(const frame& frame) {
      assert(frame.is_close());

      if (frame.application_data.empty()) {
        produced_messages_.push_back(websocket::message{
          .kind = message_kind_from_opcode(opcode::close),
        });
        return {};
      }

      const auto frame_has_close_reason = frame.application_data.size() > sizeof(close_code);
      if (frame_has_close_reason) {
        auto close_reason = frame.application_data.subspan(sizeof(close_code));
        if (config_.validate_message_utf8 && !is_valid_utf8(close_reason)) {
          return protocol_error::close_reason_invalid_utf8;
        }
      }

      produced_messages_.push_back(websocket::message{
        .kind = message_kind_from_opcode(opcode::close),
        .payload = std::vector(std::from_range, frame.application_data),
      });

      return {};
    }

    message_reader_config config_;
    message_reader_buffer receive_buffer_;
    std::optional<frame_in_progress> active_frame_;
    data_message_state data_message_;
    std::deque<websocket::message> produced_messages_;
    reader_state state_{reader_state::waiting_for_frame_header};
    frame_decoder<role::client> decoder_;
  };

} // namespace aero::websocket::detail
