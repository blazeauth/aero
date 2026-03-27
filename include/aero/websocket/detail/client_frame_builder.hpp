#pragma once

#include <expected>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef AERO_USE_TLS
#include <openssl/rand.h>
#else
#include <algorithm>
#include <random>
#endif

#include "aero/detail/utf8.hpp"
#include "aero/websocket/close_code.hpp"
#include "aero/websocket/concepts/masking_key_source.hpp"
#include "aero/websocket/detail/frame.hpp"
#include "aero/websocket/detail/frame_encoder.hpp"
#include "aero/websocket/detail/opcode.hpp"
#include "aero/websocket/error.hpp"

namespace aero::websocket::detail {

  struct client_frame_builder_options {
    bool validate_utf8{true};
  };

  class csprng_masking_key_source {
   public:
    [[nodiscard]] std::expected<masking_key, std::error_code> next() {
      masking_key result{};
#ifdef AERO_USE_TLS
      // RFC6455 says the masking key MUST come from a strong source of entropy.
      // When TLS is enabled, we just ask the TLS library CSPRNG for 4 bytes
      // which is specifically meant for cryptographic use
      if (RAND_bytes(reinterpret_cast<uint8_t*>(result.data()), result.size()) != 1) {
        return std::unexpected(websocket::error::protocol_error::masking_key_generation_failed);
      }
#else
      // In a "no TLS" build, std::random_device is the only standard way to
      // provide non-deterministic entropy. The standard doesn't *guarantee*
      // it's crypto-secure, but on Windows, Linux, and macOS mainstream
      // implementations use the OS CSPRNG, which is good enough for FC6455
      // masking keys. Maybe in the future we should use a platform
      // APIs to generate correct random with strong source of entropy
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<unsigned int> dist(0, 255);
      std::ranges::generate(result, [&]() { return static_cast<std::byte>(dist(gen)); });
#endif
      return result;
    }
  };

  template <concepts::masking_key_source MaskingKeySource = csprng_masking_key_source>
  class client_frame_builder {
    using protocol_error = websocket::error::protocol_error;
    [[maybe_unused]] constexpr static auto max_control_frame_size = 125;

   public:
    using result_type = std::expected<std::vector<std::byte>, std::error_code>;
    using masking_key_source = MaskingKeySource;

    explicit client_frame_builder(client_frame_builder_options opts = {}): validate_utf8_(opts.validate_utf8) {}
    explicit client_frame_builder(masking_key_source masking_key_source, client_frame_builder_options opts = {})
      : masking_key_source_(std::move(masking_key_source)), validate_utf8_(opts.validate_utf8) {}

    [[nodiscard]] result_type build_ping_frame(std::span<const std::byte> payload) {
      if (payload.size() > max_control_frame_size) {
        return std::unexpected(protocol_error::control_frame_payload_too_big);
      }
      return build_masked_frame(payload, opcode::ping);
    }

    [[nodiscard]] result_type build_pong_frame(std::span<const std::byte> payload) {
      if (payload.size() > max_control_frame_size) {
        return std::unexpected(protocol_error::control_frame_payload_too_big);
      }
      return build_masked_frame(payload, opcode::pong);
    }

    [[nodiscard]] result_type build_close_frame(websocket::close_code close_code,
      std::optional<std::string_view> close_reason) {
      if (close_reason && (close_reason->size() + sizeof(websocket::close_code)) > max_control_frame_size) {
        return std::unexpected(protocol_error::control_frame_payload_too_big);
      }

      if (close_reason && !validate_utf8(*close_reason)) {
        return std::unexpected(protocol_error::close_reason_invalid_utf8);
      }

      const auto close_code_value = std::to_underlying(close_code);
      std::size_t payload_length{sizeof(websocket::close_code)};
      if (close_reason) {
        payload_length += close_reason->size();
      }

      std::vector<std::byte> payload;
      payload.reserve(payload_length);
      std::array close_code_bytes{
        static_cast<std::byte>(close_code_value >> 8U),
        static_cast<std::byte>(close_code_value),
      };

      payload.push_back(close_code_bytes[0]);
      payload.push_back(close_code_bytes[1]);

      if (close_reason) {
        std::span close_reason_bytes(reinterpret_cast<const std::byte*>(close_reason->data()), close_reason->size());
        payload.insert_range(payload.end(), close_reason_bytes);
      }

      return build_masked_frame(payload, opcode::close);
    }

    [[nodiscard]] result_type build_text_frame(std::string_view payload) {
      if (!validate_utf8(payload)) {
        return std::unexpected(protocol_error::payload_text_invalid_utf8);
      }

      return build_masked_frame(std::span{reinterpret_cast<const std::byte*>(payload.data()), payload.size()}, opcode::text);
    }

    [[nodiscard]] result_type build_binary_frame(std::span<const std::byte> payload) {
      return build_masked_frame(payload, opcode::binary);
    }

   private:
    [[nodiscard]] result_type build_masked_frame(std::span<const std::byte> payload, detail::opcode opcode) {
      const auto masking_key = masking_key_source_.next();
      if (!masking_key.has_value()) {
        return std::unexpected(masking_key.error());
      }

      const frame frame{
        .fin = true,
        .rsv1 = false,
        .rsv2 = false,
        .rsv3 = false,
        .opcode = opcode,
        .masked = true,
        .payload_length = payload.size(),
        .masking_key = masking_key.value(),
      };
      if (auto validation_ec = frame.validate(); validation_ec) {
        return std::unexpected(validation_ec);
      }
      const auto header_size = frame.header_size();

      std::vector<std::byte> message(header_size + frame.payload_length);
      std::span message_bytes{message};

      const auto encode_result = frame_encoder_.encode(message_bytes, frame);
      if (!encode_result) {
        return std::unexpected(encode_result.error());
      }

      if (!payload.empty()) {
        auto mask_ec = frame_encoder_.mask(message_bytes.subspan(header_size), payload, masking_key.value());
        if (mask_ec) {
          return std::unexpected(mask_ec);
        }
      }

      return message;
    }

    [[nodiscard]] bool validate_utf8(std::string_view content) const {
      if (!validate_utf8_) {
        return true;
      }
      return aero::detail::is_valid_utf8(content);
    }

    frame_encoder frame_encoder_{};
    masking_key_source masking_key_source_{};
    bool validate_utf8_;
  };

} // namespace aero::websocket::detail
