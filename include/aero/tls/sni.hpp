#ifndef AERO_TLS_SNI_HPP
#define AERO_TLS_SNI_HPP

#include <system_error>

#include "openssl/ssl.h"

#include "aero/tls/error.hpp"

namespace aero::tls {

  inline std::error_code set_host_name(SSL* handle, const std::string& host) {
    if (::SSL_set_tlsext_host_name(handle, host.c_str()) != 1) {
      return {tls::error::handshake_error::hostname_setup_failed};
    }
    return {};
  }

  inline std::error_code set_sni(SSL* handle, const std::string& sni) {
    if (::SSL_set1_host(handle, sni.c_str()) != 1) {
      return {tls::error::handshake_error::sni_setup_failed};
    }
    return {};
  }

} // namespace aero::tls

#endif
