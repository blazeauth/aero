#pragma once

#include <atomic>
#include <cstdint>

#if AERO_USE_WOLFSSL
#include <wolfssl/ssl.h>
#endif

#if AERO_USE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace aero::tls {

  namespace detail {
    inline std::atomic<std::uint32_t> init_ref_count{0}; // NOLINT(*-avoid-non-const-global-variables)
  }

  inline bool initialize_library() {
    // \todo: Current implementation is potentially unsafe for concurrent calls (TOCTOU)

    auto previous_ref_count = detail::init_ref_count.fetch_add(1, std::memory_order_acq_rel);
    if (previous_ref_count > 0) {
      return true;
    }

    bool init_success = false;

#if AERO_USE_WOLFSSL
    init_success = (::wolfSSL_Init() == WOLFSSL_SUCCESS);
#elif AERO_USE_OPENSSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    if (::SSL_library_init() == 1) {
      ::SSL_load_error_strings();
      ::OpenSSL_add_all_algorithms();
      ::ERR_load_crypto_strings();
      init_success = true;
    }
#else
    init_success = (::OPENSSL_init_ssl(0, nullptr) == 1);
#endif
#else
    init_success = false;
#endif

    if (!init_success) {
      detail::init_ref_count.fetch_sub(1, std::memory_order_acq_rel);
    }

    return init_success;
  }

  inline bool cleanup_library() {
    auto previous_ref_count = detail::init_ref_count.fetch_sub(1, std::memory_order_acq_rel);
    if (previous_ref_count == 0) {
      detail::init_ref_count.fetch_add(1, std::memory_order_acq_rel);
      return false;
    }

    if (previous_ref_count != 1) {
      return true;
    }

#if AERO_USE_WOLFSSL
    return (::wolfSSL_Cleanup() == WOLFSSL_SUCCESS);
#elif AERO_USE_OPENSSL
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    ::EVP_cleanup();
    ::CRYPTO_cleanup_all_ex_data();
    ::ERR_free_strings();

    // Not using OPENSSL_cleanup in cleanup process.
    // https://manpages.debian.org/experimental/libssl-doc/OPENSSL_cleanup.3ssl.en.html
    // "Once OPENSSL_cleanup() has been called the library cannot be reinitialised."
    return true;
#else
    return true;
#endif
#else
    return false;
#endif
  }

} // namespace aero::tls
