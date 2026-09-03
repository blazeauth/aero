#pragma once

#include <optional>

#include <wolfssl/error-ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include "aero/tls/error.hpp"

namespace aero::tls::detail {

  // The primary error reporting channel for wolfSSL regarding certificate
  // issues is SSL_get_error(), while SSL_get_verify_result() in wolfSSL is
  // set only when OpenSSL compatibility is enabled. Even then, in wolfSSL,
  // the SSL_get_verify_result() does not report errors related to hostname
  // mismatch or key usage violations, so aero maps SSL_get_error itself.
  //
  // https://github.com/wolfSSL/wolfssl/pull/11108 partially solves this.
  // However, this error mapper should not be removed even after a new
  // version of wolfSSL is released with the necessary patch. The behavior is
  // determined by an external dependency, and it's not certain that the
  // consumer will be using the latest version. It costs us nothing, and at
  // the same time, we maintain consistent behavior for users of both new and
  // older versions of wolfSSL (<= 5.9.2)

  inline std::optional<tls::certificate_error> wolfssl_error_to_cert_error(int error) {
    switch (error) {
    case ASN_AFTER_DATE_E:
      return certificate_error::cert_expired;
    case ASN_BEFORE_DATE_E:
      return certificate_error::cert_not_started;
    case CRL_CERT_REVOKED:
      return certificate_error::cert_revoked;
    case CRL_MISSING:
    case CRL_CERT_DATE_ERR:
    case ASN_CRL_NO_SIGNER_E:
      return certificate_error::cert_revocation_unknown;
    case ASN_SIG_CONFIRM_E:
      return certificate_error::cert_signature_invalid;
    case ASN_NO_SIGNER_E:
    case ASN_SELF_SIGNED_E:
    case ASN_PATHLEN_INV_E:
    case ASN_PATHLEN_SIZE_E:
    case MAX_CHAIN_ERROR:
      return certificate_error::cert_authority_invalid;
    case DOMAIN_NAME_MISMATCH:
    case IPADDR_MISMATCH:
      return certificate_error::cert_hostname_mismatch;
    case EXTKEYUSE_AUTH_E:
    case KEYUSE_SIGNATURE_E:
    case KEYUSE_ENCIPHER_E:
      return certificate_error::cert_eku_invalid;
    case VERIFY_CERT_ERROR:
      return certificate_error::verification_failed;
    default:
      return std::nullopt;
    }
  }

} // namespace aero::tls::detail
