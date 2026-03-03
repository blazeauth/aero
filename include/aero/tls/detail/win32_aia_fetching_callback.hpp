#ifndef AERO_TLS_DETAIL_WIN32_AIA_FETCHING_CALLBACK_HPP
#define AERO_TLS_DETAIL_WIN32_AIA_FETCHING_CALLBACK_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <asio/ssl/verify_context.hpp>

#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include "aero/tls/detail/x509_verify_error.hpp"

#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

namespace aero::tls::detail {

  inline std::wstring utf8_to_utf16(const char* utf8_text) {
    if (utf8_text == nullptr || *utf8_text == '\0') {
      return {};
    }

    const int required_size_including_null = ::MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, nullptr, 0);
    if (required_size_including_null <= 0) {
      return {};
    }

    std::wstring wide_text(static_cast<std::size_t>(required_size_including_null), L'\0');
    const int converted = ::MultiByteToWideChar(CP_UTF8, 0, utf8_text, -1, wide_text.data(), required_size_including_null);
    if (converted <= 0) {
      return {};
    }

    if (!wide_text.empty() && wide_text.back() == L'\0') {
      wide_text.pop_back();
    }

    return wide_text;
  }

  struct cert_context_guard final {
    PCCERT_CONTEXT ptr{nullptr};
    cert_context_guard() = default;
    explicit cert_context_guard(PCCERT_CONTEXT ctx): ptr(ctx) {}
    ~cert_context_guard() {
      if (ptr != nullptr) {
        ::CertFreeCertificateContext(ptr);
      }
    }
    cert_context_guard(const cert_context_guard&) = delete;
    cert_context_guard& operator=(const cert_context_guard&) = delete;
    cert_context_guard(cert_context_guard&&) = delete;
    cert_context_guard& operator=(cert_context_guard&&) = delete;

    explicit operator bool() const noexcept {
      return ptr != nullptr;
    }
  };

  struct chain_guard final {
    PCCERT_CHAIN_CONTEXT ptr{nullptr};
    chain_guard() = default;
    explicit chain_guard(PCCERT_CHAIN_CONTEXT ctx): ptr(ctx) {}
    ~chain_guard() {
      if (ptr != nullptr) {
        ::CertFreeCertificateChain(ptr);
      }
    }
    chain_guard(const chain_guard&) = delete;
    chain_guard& operator=(const chain_guard&) = delete;
    chain_guard(chain_guard&&) = delete;
    chain_guard& operator=(chain_guard&&) = delete;

    explicit operator bool() const noexcept {
      return ptr != nullptr;
    }
  };

  struct store_guard {
    HCERTSTORE handle{nullptr};
    explicit store_guard(HCERTSTORE store): handle(store) {}
    ~store_guard() {
      if (handle != nullptr) {
        ::CertCloseStore(handle, 0);
      }
    }
    store_guard(const store_guard&) = delete;
    store_guard& operator=(const store_guard&) = delete;
    store_guard(store_guard&&) = delete;
    store_guard& operator=(store_guard&&) = delete;

    explicit operator bool() const noexcept {
      return handle != nullptr;
    }
  };

  [[nodiscard]] inline PCCERT_CONTEXT make_cert_context_from_x509(X509* x509) {
    if (x509 == nullptr) {
      return nullptr;
    }
    const auto der_len = ::i2d_X509(x509, nullptr);
    if (der_len <= 0) {
      return nullptr;
    }
    std::vector<std::uint8_t> der(static_cast<std::size_t>(der_len));
    auto* der_data = der.data();
    if (::i2d_X509(x509, &der_data) <= 0) {
      return nullptr;
    }
    return ::CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
  }

  inline STACK_OF(X509) * get_peer_cert_chain([[maybe_unused]] SSL* ssl, [[maybe_unused]] X509_STORE_CTX* store_ctx) {
#if AERO_USE_OPENSSL || defined(SESSION_CERTS)
    return ::SSL_get_peer_cert_chain(ssl);
#else
    return ::X509_STORE_CTX_get_chain(store_ctx);
#endif
  }

  inline bool add_peer_chain_certs_to_store(SSL* ssl, X509_STORE_CTX* store_ctx, HCERTSTORE store) {
    auto* peer_cert_chain = get_peer_cert_chain(ssl, store_ctx);
    if (peer_cert_chain == nullptr) {
      // Peer did not provide intermediates, so we count
      // on Windows CertGetCertificateChain AIA fetching
      return true;
    }

    const auto x509_count = sk_X509_num(peer_cert_chain);
    for (int i = 0; i < x509_count; ++i) {
      X509* cert = sk_X509_value(peer_cert_chain, i);
      cert_context_guard cert_ctx{make_cert_context_from_x509(cert)};
      if (!cert_ctx) {
        continue;
      }
      if (::CertAddCertificateContextToStore(store, cert_ctx.ptr, CERT_STORE_ADD_NEW, nullptr) == 0) {
        if (::GetLastError() == CRYPT_E_EXISTS) {
          continue;
        }
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] inline aero::tls::detail::x509_verify_error trust_flags_to_x509_error(std::uint32_t flags) {
    using aero::tls::detail::x509_verify_error;

    if ((flags & CERT_TRUST_IS_REVOKED) != 0U) {
      return x509_verify_error::certificate_revoked;
    }
    if ((flags & CERT_TRUST_IS_NOT_TIME_VALID) != 0U) {
      return x509_verify_error::certificate_expired;
    }
    if ((flags & CERT_TRUST_IS_NOT_SIGNATURE_VALID) != 0U) {
      return x509_verify_error::certificate_signature_invalid;
    }
    if ((flags & CERT_TRUST_IS_NOT_VALID_FOR_USAGE) != 0U) {
      return x509_verify_error::certificate_invalid_purpose;
    }
    if ((flags & CERT_TRUST_IS_UNTRUSTED_ROOT) != 0U) {
      return x509_verify_error::certificate_not_trusted;
    }
    if ((flags & CERT_TRUST_IS_PARTIAL_CHAIN) != 0U) {
      return x509_verify_error::issuer_not_found;
    }
    if ((flags & CERT_TRUST_REVOCATION_STATUS_UNKNOWN) != 0U) {
      return x509_verify_error::crl_unavailable;
    }

    return x509_verify_error::application_verification_failed;
  }

  [[nodiscard]] inline aero::tls::detail::x509_verify_error policy_error_to_x509_error(DWORD policy_error) {
    using aero::tls::detail::x509_verify_error;

    if (policy_error == CERT_E_CN_NO_MATCH) {
      return x509_verify_error::hostname_mismatch;
    }

    return x509_verify_error::application_verification_failed;
  }

  inline x509_verify_error win32_aia_fetching_callback(bool, asio::ssl::verify_context& verify_ctx) noexcept {
    auto* store_ctx = verify_ctx.native_handle();
    if (store_ctx == nullptr) {
      return x509_verify_error::application_verification_failed;
    }

    // Perform verification only once, at the leaf certificate
    const bool is_leaf_certificate = (::X509_STORE_CTX_get_error_depth(store_ctx) == 0);
    if (!is_leaf_certificate) {
      return x509_verify_error::ok;
    }

    // Get SSL* from verify context to access SNI and peer chain
    auto* ssl = static_cast<SSL*>(::X509_STORE_CTX_get_ex_data(store_ctx, ::SSL_get_ex_data_X509_STORE_CTX_idx()));
    if (ssl == nullptr) {
      return x509_verify_error::application_verification_failed;
    }

    auto* leaf_cert = ::X509_STORE_CTX_get0_cert(store_ctx);
    cert_context_guard leaf_cert_ctx{make_cert_context_from_x509(leaf_cert)};
    if (!leaf_cert_ctx) {
      return x509_verify_error::application_verification_failed;
    }

    store_guard peer_chain_store{::CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr)};
    if (!peer_chain_store) {
      return x509_verify_error::application_verification_failed;
    }

    // Add peer provided intermediates to in-memory store so
    // CertGetCertificateChain can build a full chain
    if (!add_peer_chain_certs_to_store(ssl, store_ctx, peer_chain_store.handle)) {
      return x509_verify_error::application_verification_failed;
    }

    // Require serverAuth EKU for the leaf certificate
    std::string server_auth_oid{szOID_PKIX_KP_SERVER_AUTH};
    LPSTR server_auth_oid_ptr{server_auth_oid.data()};

    CERT_ENHKEY_USAGE eku{};
    eku.cUsageIdentifier = 1;
    eku.rgpszUsageIdentifier = &server_auth_oid_ptr;

    CERT_USAGE_MATCH match{};
    match.dwType = USAGE_MATCH_TYPE_AND;
    match.Usage = eku;

    CERT_CHAIN_PARA chain_params{};
    chain_params.cbSize = sizeof(chain_params);
    chain_params.RequestedUsage = match;

    constexpr DWORD chain_flags = CERT_CHAIN_CACHE_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    PCCERT_CHAIN_CONTEXT raw_chain_ctx = nullptr;

    if (::CertGetCertificateChain(nullptr,
          leaf_cert_ctx.ptr,
          nullptr,
          peer_chain_store.handle,
          &chain_params,
          chain_flags,
          nullptr,
          &raw_chain_ctx) == 0) {
      return x509_verify_error::application_verification_failed;
    }
    chain_guard chain_ctx{raw_chain_ctx};

    auto cert_time_validity = ::CertVerifyTimeValidity(nullptr, leaf_cert_ctx.ptr->pCertInfo);
    if (cert_time_validity == 1) {
      return x509_verify_error::certificate_expired;
    }
    if (cert_time_validity == -1) {
      return x509_verify_error::certificate_not_started;
    }

    const auto* sni = ::SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (sni == nullptr) {
      return x509_verify_error::application_verification_failed;
    }

    auto server_name_wstr = utf8_to_utf16(sni);

    SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl_policy{};
    ssl_policy.cbSize = sizeof(ssl_policy);
    ssl_policy.dwAuthType = AUTHTYPE_SERVER;
    ssl_policy.fdwChecks = 0;
    ssl_policy.pwszServerName = server_name_wstr.data();

    CERT_CHAIN_POLICY_PARA policy_params{};
    policy_params.cbSize = sizeof(policy_params);
    policy_params.pvExtraPolicyPara = &ssl_policy;

    CERT_CHAIN_POLICY_STATUS policy_status{};
    policy_status.cbSize = sizeof(policy_status);

    if (::CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain_ctx.ptr, &policy_params, &policy_status) == 0) {
      return trust_flags_to_x509_error(chain_ctx.ptr->TrustStatus.dwErrorStatus);
    }

    if (policy_status.dwError != 0) {
      auto mapped_by_flags = trust_flags_to_x509_error(chain_ctx.ptr->TrustStatus.dwErrorStatus);
      if (mapped_by_flags != x509_verify_error::application_verification_failed) {
        return mapped_by_flags;
      }
      return policy_error_to_x509_error(policy_status.dwError);
    }

    return x509_verify_error::ok;
  }

} // namespace aero::tls::detail

#endif
