#pragma once

#include <asio/ssl/verify_mode.hpp>

namespace aero::tls {

  enum class verify_mode : asio::ssl::verify_mode { // NOLINT(*-enum-size)
    verify_none = asio::ssl::verify_none,
    verify_peer = asio::ssl::verify_peer,
    verify_fail_if_no_peer_cert = asio::ssl::verify_fail_if_no_peer_cert,
    verify_client_once = asio::ssl::verify_client_once,
  };

} // namespace aero::tls
