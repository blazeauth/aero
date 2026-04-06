#pragma once

#include <chrono>
#include <cstddef>

#ifdef AERO_USE_TLS
#include <asio/ssl/context.hpp>
#endif

#include "aero/http/detail/common.hpp"
#include "aero/http/detail/connection_pool.hpp"
#include "aero/net/detail/basic_transport.hpp"

namespace aero::http {

  struct client_options {
    std::size_t max_idle_connections_per_endpoint{http::detail::default_max_idle_connections_per_endpoint};
    std::size_t transport_buffer_size{net::detail::default_buffer_size};
    std::size_t max_response_body_size{http::detail::max_response_body_size};
    bool reuse_connections{true};
    std::chrono::steady_clock::duration expect_continue_timeout{std::chrono::seconds{1}};

#ifdef AERO_USE_TLS
    asio::ssl::context* tls_context{nullptr};
#endif
  };

} // namespace aero::http
