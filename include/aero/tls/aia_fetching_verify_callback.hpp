#pragma once

#include <asio/ssl/verify_context.hpp>

#include "aero/tls/detail/x509_verify_error.hpp"

#if _WIN32
#include "aero/tls/detail/win32_aia_fetching_callback.hpp"
#include <openssl/x509_vfy.h>
#endif

#define AERO_AIA_FETCHING_CALLBACK_SUPPORTED _WIN32

namespace aero::tls {

  inline bool aia_fetching_verify_callback(bool preverified, [[maybe_unused]] asio::ssl::verify_context& verify_ctx) {
#if AERO_AIA_FETCHING_CALLBACK_SUPPORTED
    auto* store_ctx = verify_ctx.native_handle();
    if (store_ctx == nullptr) {
      return false;
    }

    auto* ssl = static_cast<SSL*>(::X509_STORE_CTX_get_ex_data(store_ctx, ::SSL_get_ex_data_X509_STORE_CTX_idx()));
    if (ssl == nullptr) {
      return false;
    }

    ::X509_STORE_CTX_set_error(store_ctx, X509_V_OK);

    auto verify_error = detail::win32_aia_fetching_callback(preverified, verify_ctx);

    ::X509_STORE_CTX_set_error(store_ctx, static_cast<int>(verify_error));
    ::SSL_set_verify_result(ssl, static_cast<int>(verify_error));

    return verify_error == detail::x509_verify_error::ok;
#else
    return preverified;
#endif
  }

} // namespace aero::tls
