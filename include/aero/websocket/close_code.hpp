#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <system_error>

#include "aero/websocket/error.hpp"

namespace aero::websocket {

  enum class close_code : std::uint16_t {
    normal = 1000,
    going_away = 1001,
    protocol_error = 1002,
    unsupported_data = 1003,
    no_status_received = 1005, // Read-only, should never be sent by client frame
    abnormal_closure = 1006,   // Read-only, should never be sent by client frame
    invalid_payload = 1007,
    policy_violation = 1008,
    message_too_big = 1009,
    mandatory_extension = 1010,
    internal_error = 1011,
    service_restart = 1012,
    try_again_later = 1013,
    bad_gateway = 1014,
    tls_handshake_fail = 1015, // Read-only, should never be sent by client frame
    // IANA registered application codes (3000-3999)
    unauthorized = 3000,
    forbidden = 3003,
    timeout = 3008,
  };

  [[nodiscard]] constexpr bool is_close_code_reserved(std::uint16_t value) noexcept {
    auto is_in_range = [value](std::uint16_t min, std::uint16_t max) {
      return value >= min && value <= max;
    };
    // NOLINTNEXTLINE(*-magic-numbers)
    return is_in_range(1004, 1006) || value == 1015;
  }

  [[nodiscard]] inline std::expected<close_code, std::error_code> to_close_code(std::uint16_t value) {
    constexpr std::array close_codes{
      close_code::normal,
      close_code::going_away,
      close_code::protocol_error,
      close_code::unsupported_data,
      close_code::no_status_received,
      close_code::abnormal_closure,
      close_code::invalid_payload,
      close_code::policy_violation,
      close_code::message_too_big,
      close_code::mandatory_extension,
      close_code::internal_error,
      close_code::service_restart,
      close_code::try_again_later,
      close_code::bad_gateway,
      close_code::tls_handshake_fail,
      close_code::unauthorized,
      close_code::forbidden,
      close_code::timeout,
    };

    if (std::ranges::contains(close_codes, static_cast<close_code>(value))) {
      return static_cast<close_code>(value);
    }

    // NOLINTNEXTLINE(*-magic-numbers)
    if (value >= 3000 && value <= 4999) {
      return static_cast<close_code>(value);
    }

    return std::unexpected(error::protocol_error::close_code_invalid);
  }

  [[nodiscard]] inline std::expected<close_code, std::error_code> parse_close_code(std::uint16_t value) {
    if (is_close_code_reserved(value)) {
      return std::unexpected(error::protocol_error::close_code_reserved);
    }

    return to_close_code(value);
  }

  [[nodiscard]] constexpr bool is_close_code_server_only(const websocket::close_code code) {
    return code == close_code::no_status_received || code == close_code::abnormal_closure ||
           code == close_code::tls_handshake_fail;
  }

} // namespace aero::websocket

template <>
struct std::formatter<aero::websocket::close_code> : std::formatter<std::underlying_type_t<aero::websocket::close_code>> {
  auto format(const aero::websocket::close_code& value, std::format_context& ctx) const {
    return std::formatter<std::underlying_type_t<aero::websocket::close_code>>{}.format(std::to_underlying(value), ctx);
  }
};
