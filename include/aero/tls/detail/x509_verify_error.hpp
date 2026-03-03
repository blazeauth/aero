#ifndef AERO_TLS_DETAIL_X509_VERIFY_ERROR_HPP
#define AERO_TLS_DETAIL_X509_VERIFY_ERROR_HPP

#include <openssl/ssl.h>

#include "aero/tls/error.hpp"
#include <optional>

namespace aero::tls::detail {

  enum class x509_verify_error : int { // NOLINT(*-enum-size)
    ok = X509_V_OK,

    certificate_expired = X509_V_ERR_CERT_HAS_EXPIRED,
    certificate_not_started = X509_V_ERR_CERT_NOT_YET_VALID,
    certificate_revoked = X509_V_ERR_CERT_REVOKED,
    certificate_signature_invalid = X509_V_ERR_CERT_SIGNATURE_FAILURE,
    certificate_invalid_purpose = X509_V_ERR_INVALID_PURPOSE,

    issuer_not_found = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY,
    issuer_certificate_missing = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT,

    certificate_not_trusted = X509_V_ERR_CERT_UNTRUSTED,
    self_signed_certificate = X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT,
    self_signed_in_chain = X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN,
    unable_to_verify_leaf_signature = X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE,

    crl_unavailable = X509_V_ERR_UNABLE_TO_GET_CRL,

    application_verification_failed = X509_V_ERR_APPLICATION_VERIFICATION,

#if defined(X509_V_ERR_HOSTNAME_MISMATCH)
    hostname_mismatch = X509_V_ERR_HOSTNAME_MISMATCH,
#else
    hostname_mismatch = X509_V_ERR_APPLICATION_VERIFICATION,
#endif
  };

  inline void set_verify_error(SSL* ssl, x509_verify_error error) {
    ::SSL_set_verify_result(ssl, static_cast<int>(error));
  }

  inline void reset_verify_error(SSL* ssl) {
    ::SSL_set_verify_result(ssl, X509_V_OK);
  }

  inline std::optional<tls::error::certificate_error> verify_error_to_cert_error(tls::detail::x509_verify_error error) {
    using tls::detail::x509_verify_error;
    using tls::error::certificate_error;
    switch (error) {
    case x509_verify_error::ok:
      return std::nullopt;
    case x509_verify_error::certificate_expired:
      return certificate_error::cert_expired;
    case x509_verify_error::certificate_not_started:
      return certificate_error::cert_not_started;
    case x509_verify_error::certificate_revoked:
      return certificate_error::cert_revoked;
    case x509_verify_error::certificate_signature_invalid:
      return certificate_error::cert_signature_invalid;
    case x509_verify_error::certificate_invalid_purpose:
      return certificate_error::cert_eku_invalid;
    case x509_verify_error::hostname_mismatch:
      return certificate_error::cert_hostname_mismatch;
    case x509_verify_error::crl_unavailable:
      return certificate_error::cert_revocation_unknown;
    case x509_verify_error::issuer_not_found:
    case x509_verify_error::issuer_certificate_missing:
      return certificate_error::cert_chain_incomplete;
    case x509_verify_error::certificate_not_trusted:
      return certificate_error::cert_untrusted;
    case x509_verify_error::self_signed_certificate:
    case x509_verify_error::self_signed_in_chain:
    case x509_verify_error::unable_to_verify_leaf_signature:
      return certificate_error::cert_authority_invalid;
    case x509_verify_error::application_verification_failed:
    default:
      return certificate_error::verification_failed;
    }
    return std::nullopt;
  }

} // namespace aero::tls::detail

#endif
