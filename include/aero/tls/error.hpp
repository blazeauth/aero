#ifndef AERO_TLS_ERRORS_HPP
#define AERO_TLS_ERRORS_HPP

#include <cstdint>
#include <system_error>
#include <type_traits>

namespace aero::tls::error {

  enum class handshake_error : std::uint8_t {
    sni_setup_failed = 1,
    hostname_setup_failed,
    tls_version_unsupported,
  };

  enum class certificate_error : std::uint8_t {
    verification_failed = 1,
    cert_expired,
    cert_not_started,
    cert_revoked,
    cert_authority_invalid,
    cert_invalid,
    cert_hostname_mismatch,
    cert_untrusted,
    cert_signature_invalid,
    cert_eku_invalid,
    cert_chain_incomplete,
    cert_revocation_unknown,
  };

  enum class context_error : std::uint8_t {
    cannot_disable_active_tls_version = 1,
  };

  namespace detail {

    class handshake_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.tls.handshake_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::tls::error::handshake_error;
        switch (static_cast<handshake_error>(value)) {
        case handshake_error::sni_setup_failed:
          return "failed to set SNI host name";
        case handshake_error::hostname_setup_failed:
          return "failed to configure hostname verification";
        case handshake_error::tls_version_unsupported:
          return "peer offered only unsupported TLS versions";
        default:
          return "unknown handshake error";
        }
      }
    };

    class certificate_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.tls.certificate_error";
      }

      [[nodiscard]] std::string message(int ev) const override {
        using aero::tls::error::certificate_error;
        switch (static_cast<certificate_error>(ev)) {
        case certificate_error::verification_failed:
          return "peer certificate verification failed";
        case certificate_error::cert_expired:
          return "peer provided an expired certificate";
        case certificate_error::cert_not_started:
          return "peer provided a certificate that has not yet started";
        case certificate_error::cert_revoked:
          return "peer provided a revoked certificate";
        case certificate_error::cert_authority_invalid:
          return "peer provided certificate with invalid authority";
        case certificate_error::cert_invalid:
          return "peer provided an invalid certificate";
        case certificate_error::cert_hostname_mismatch:
          return "peer certificate does not match requested host name";
        case certificate_error::cert_untrusted:
          return "peer provided an untrusted certificate";
        case certificate_error::cert_signature_invalid:
          return "peer certificate signature is invalid";
        case certificate_error::cert_eku_invalid:
          return "peer certificate is not valid for authentication (EKU)";
        case certificate_error::cert_chain_incomplete:
          return "peer certificate chain is incomplete";
        case certificate_error::cert_revocation_unknown:
          return "peer certificate revocation status could not be determined";
        default:
          return "unknown certificate error";
        }
      }
    };

    class context_error_category : public std::error_category {
     public:
      [[nodiscard]] const char* name() const noexcept override {
        return "aero.tls.context_error";
      }

      [[nodiscard]] std::string message(int value) const override {
        using aero::tls::error::context_error;
        switch (static_cast<context_error>(value)) {
        case context_error::cannot_disable_active_tls_version:
          return "cannot disable active TLS version";
        default:
          return "unknown context error";
        }
      }
    };

  } // namespace detail

  const inline std::error_category& handshake_error_category() noexcept {
    static const detail::handshake_error_category category;
    return category;
  }

  const inline std::error_category& certificate_error_category() noexcept {
    static const detail::certificate_error_category category;
    return category;
  }

  const inline std::error_category& context_error_category() noexcept {
    static const detail::context_error_category category;
    return category;
  }

  inline std::error_code make_error_code(handshake_error value) {
    return {static_cast<int>(value), handshake_error_category()};
  }

  inline std::error_code make_error_code(certificate_error value) {
    return {static_cast<int>(value), certificate_error_category()};
  }

  inline std::error_code make_error_code(context_error value) {
    return {static_cast<int>(value), context_error_category()};
  }

} // namespace aero::tls::error

template <>
struct std::is_error_code_enum<aero::tls::error::handshake_error> : std::true_type {};
template <>
struct std::is_error_code_enum<aero::tls::error::certificate_error> : std::true_type {};
template <>
struct std::is_error_code_enum<aero::tls::error::context_error> : std::true_type {};

#endif
