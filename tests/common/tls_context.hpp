#pragma once

#include <string_view>

#include <asio/buffer.hpp>
#include <asio/ssl.hpp>

#include "common/certificates/certificates.hpp"

namespace aero::tests {

  inline asio::ssl::context make_tls_server_context(std::string_view certificate = certificates::leaf_valid) {
    asio::ssl::context ctx{asio::ssl::context::tls_server};
    ctx.use_certificate_chain(asio::buffer(certificate));
    ctx.use_private_key(asio::buffer(certificates::leaf_key), asio::ssl::context::pem);
    return ctx;
  }

  inline asio::ssl::context make_tls_client_context() {
    asio::ssl::context ctx{asio::ssl::context::tls_client};
    ctx.add_certificate_authority(asio::buffer(certificates::root));
    ctx.set_verify_mode(asio::ssl::verify_peer);
    return ctx;
  }

} // namespace aero::tests
