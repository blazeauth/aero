#pragma once

#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "aero/detail/bytes.hpp"
#include "aero/websocket/close_code.hpp"

namespace aero::websocket {

  enum class message_kind : std::uint8_t {
    text,
    binary,
    close,
    ping,
    pong
  };

  [[nodiscard]] constexpr std::string_view to_string(message_kind kind) noexcept {
    switch (kind) {
    case message_kind::text:
      return "text";
    case message_kind::binary:
      return "binary";
    case message_kind::close:
      return "close";
    case message_kind::ping:
      return "ping";
    case message_kind::pong:
      return "pong";
    default:
      return "unknown";
    }
  }

  struct message {
    websocket::message_kind kind{};
    std::vector<std::byte> payload;

    [[nodiscard]] bool is_text() const noexcept {
      return kind == message_kind::text;
    }

    [[nodiscard]] bool is_binary() const noexcept {
      return kind == message_kind::binary;
    }

    [[nodiscard]] bool is_close() const noexcept {
      return kind == message_kind::close;
    }

    [[nodiscard]] bool is_ping() const noexcept {
      return kind == message_kind::ping;
    }

    [[nodiscard]] bool is_pong() const noexcept {
      return kind == message_kind::pong;
    }

    [[nodiscard]] bool is_control() const noexcept {
      return is_ping() || is_pong() || is_close();
    }

    [[nodiscard]] bool has_payload() const noexcept {
      return !payload.empty();
    }

    [[nodiscard]] std::string_view text() const noexcept {
      return {reinterpret_cast<const char*>(payload.data()), payload.size()};
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
      return {payload};
    }

    [[nodiscard]] bool has_close_code() const noexcept {
      return is_close() && payload.size() >= sizeof(websocket::close_code);
    }

    [[nodiscard]] std::optional<websocket::close_code> close_code() const noexcept {
      if (!has_close_code()) {
        return std::nullopt;
      }
      return aero::detail::read_big_endian<websocket::close_code>(bytes().first<2>());
    }

    [[nodiscard]] bool has_close_reason() const noexcept {
      return is_close() && payload.size() > sizeof(websocket::close_code);
    }

    [[nodiscard]] std::optional<std::string_view> close_reason() const noexcept {
      if (!has_close_reason()) {
        return std::nullopt;
      }
      return text().substr(sizeof(websocket::close_code));
    }

    [[nodiscard]] std::string_view kind_string() const noexcept {
      return websocket::to_string(kind);
    }
  };

} // namespace aero::websocket

template <>
struct std::formatter<aero::websocket::message_kind> : std::formatter<std::string_view> {
  auto format(const aero::websocket::message_kind& value, std::format_context& ctx) const {
    return std::formatter<std::string_view>{}.format(to_string(value), ctx);
  }
};
