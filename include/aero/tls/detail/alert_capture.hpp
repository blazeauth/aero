#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>

#include <openssl/ssl.h>

#include "aero/tls/error.hpp"

namespace aero::tls::detail {

  struct alert {
    bool received_from_peer;
    std::uint8_t level;
    std::uint8_t description;
  };

  // NOLINTBEGIN(*-magic-numbers)

  class alert_capture final {
   public:
    void install(SSL* ssl) noexcept {
      ::SSL_set_msg_callback(ssl, alert_capture::message_callback);
      ::SSL_set_msg_callback_arg(ssl, this);
    }

    [[nodiscard]] std::optional<alert> get_last_tls_alert() const noexcept {
      auto packed_value = packed_.load(std::memory_order_relaxed);
      if (packed_value == 0) {
        return std::nullopt;
      }

      auto received_from_peer = (packed_value & received_from_peer_mask) != 0;
      auto level = static_cast<std::uint8_t>((packed_value >> 8U) & 0xFFU);
      auto description = static_cast<std::uint8_t>(packed_value & 0xFFU);

      return alert{
        .received_from_peer = received_from_peer,
        .level = level,
        .description = description,
      };
    }

   private:
    constexpr static std::uint32_t received_from_peer_mask = 1U << 16U;

    static void message_callback(int write_p, int, int content_type, const void* buffer, std::size_t length, SSL*,
      void* callback_argument) {
      if (content_type != SSL3_RT_ALERT || length < 2 || callback_argument == nullptr) {
        return;
      }

      auto* self = static_cast<alert_capture*>(callback_argument);
      auto received_from_peer = write_p == 0;

      const auto* bytes = static_cast<const unsigned char*>(buffer);
      auto level = static_cast<std::uint32_t>(bytes[0]);
      auto description = static_cast<std::uint32_t>(bytes[1]);

      auto packed_value = (received_from_peer ? received_from_peer_mask : 0U) | (level << 8U) | description;
      self->packed_.store(packed_value, std::memory_order_relaxed);
    }

    std::atomic<std::uint32_t> packed_{0};
  };

  // NOLINTEND(*-magic-numbers)

  inline std::optional<std::error_code> tls_alert_to_error_code(const tls::detail::alert& alert) {
    using tls::error::certificate_error;
    using tls::error::handshake_error;

    auto description = static_cast<int>(alert.description);

    if (description == SSL_AD_PROTOCOL_VERSION) {
      return handshake_error::tls_version_unsupported;
    }

#ifdef SSL_AD_INSUFFICIENT_SECURITY
    if (description == SSL_AD_INSUFFICIENT_SECURITY) {
      return handshake_error::tls_version_unsupported;
    }
#endif

    if (description == SSL_AD_CERTIFICATE_EXPIRED) {
      return certificate_error::cert_expired;
    }

#ifdef SSL_AD_CERTIFICATE_REVOKED
    if (description == SSL_AD_CERTIFICATE_REVOKED) {
      return certificate_error::cert_revoked;
    }
#endif

    if (description == SSL_AD_UNKNOWN_CA) {
      return certificate_error::cert_authority_invalid;
    }

#ifdef SSL_AD_BAD_CERTIFICATE
    if (description == SSL_AD_BAD_CERTIFICATE) {
      return certificate_error::cert_invalid;
    }
#endif

#ifdef SSL_AD_UNSUPPORTED_CERTIFICATE
    if (description == SSL_AD_UNSUPPORTED_CERTIFICATE) {
      return certificate_error::cert_invalid;
    }
#endif

    return std::nullopt;
  }

} // namespace aero::tls::detail
