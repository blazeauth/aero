#ifndef AERO_TLS_PEER_IDENTITY_HPP
#define AERO_TLS_PEER_IDENTITY_HPP

#include <string>
#include <system_error>

#include <openssl/ssl.h>

#include "aero/tls/error.hpp"

namespace aero::tls {

  inline std::error_code set_sni(SSL* handle, const std::string& server_name) {
    if (::SSL_set_tlsext_host_name(handle, server_name.c_str()) != 1) {
      return {tls::error::handshake_error::sni_setup_failed};
    }
    return {};
  }

  inline std::error_code set_expected_peer_host(SSL* handle, const std::string& host) {
    if (::SSL_set1_host(handle, host.c_str()) != 1) {
      return {tls::error::handshake_error::hostname_setup_failed};
    }
    return {};
  }

} // namespace aero::tls

#endif
