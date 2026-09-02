#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ssl.hpp>
#include <asio/strand.hpp>

#include <ut/ut.hpp>

#include "aero/net/transport.hpp"
#include "aero/tls/error.hpp"

#include "common/certificates/certificates.hpp"
#include "common/tls_context.hpp"

using namespace ut;
using namespace std::chrono_literals;

namespace certificates = aero::tests::certificates;
using aero::tls::certificate_error;
using aero::tls::handshake_error;
using tcp = asio::ip::tcp;

namespace {

  constexpr auto connect_timeout = 5s;
  constexpr auto accept_timeout = 10s;

  class handshake_server {
   public:
    explicit handshake_server(asio::ssl::context& ctx)
      : acceptor_(io_context_, resolve_localhost_endpoint()), port_(acceptor_.local_endpoint().port()), thread_([this, &ctx] {
          asio::ssl::stream<tcp::socket> stream{io_context_, ctx};

          std::error_code accept_ec = asio::error::would_block;
          acceptor_.async_accept(stream.next_layer(), [&](std::error_code ec) { accept_ec = ec; });
          io_context_.run_for(accept_timeout);

          if (accept_ec) {
            return;
          }

          std::error_code ignored;
          static_cast<void>(stream.handshake(asio::ssl::stream_base::server, ignored));
          static_cast<void>(stream.next_layer().close(ignored));
        }) {}

    handshake_server(const handshake_server&) = delete;
    handshake_server& operator=(const handshake_server&) = delete;
    handshake_server(handshake_server&&) = delete;
    handshake_server& operator=(handshake_server&&) = delete;

    ~handshake_server() {
      io_context_.stop();
      thread_.join();
    }

    [[nodiscard]] asio::ip::port_type port() const {
      return port_;
    }

   private:
    [[nodiscard]] tcp::endpoint resolve_localhost_endpoint() {
      tcp::resolver resolver{io_context_};
      auto endpoints = resolver.resolve("localhost", "0");
      return *endpoints.begin();
    }

    asio::io_context io_context_;
    tcp::acceptor acceptor_;
    asio::ip::port_type port_;
    std::thread thread_;
  };

  std::error_code handshake_with(asio::ssl::context& server_ctx, asio::ssl::context& client_ctx) {
    handshake_server server{server_ctx};

    asio::io_context io_context;
    aero::net::transport transport{asio::make_strand(io_context.get_executor()), client_ctx};

    std::error_code result;
    bool finished = false;

    transport.async_connect("localhost", server.port(), [&](std::error_code ec) {
      result = ec;
      finished = true;
    });
    io_context.run_for(connect_timeout);

    expect(finished) << "async_connect did not complete within " << connect_timeout.count() << "s";
    return result;
  }

  void require_client_certificate(asio::ssl::context& server_ctx) {
    server_ctx.add_certificate_authority(asio::buffer(certificates::root));
    server_ctx.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
  }

  struct handshake_and_read_result {
    std::error_code handshake_ec;
    std::error_code read_ec;
  };

  handshake_and_read_result handshake_then_read(asio::ssl::context& server_ctx, asio::ssl::context& client_ctx) {
    handshake_server server{server_ctx};

    asio::io_context io_context;
    aero::net::transport transport{asio::make_strand(io_context.get_executor()), client_ctx};

    handshake_and_read_result result;
    bool finished = false;
    std::array<std::byte, 1> byte{};
    transport.async_connect("localhost", server.port(), [&](std::error_code handshake_ec) {
      result.handshake_ec = handshake_ec;
      if (handshake_ec) {
        finished = true;
        return;
      }
      transport.async_read_some(asio::buffer(byte), [&](std::error_code read_ec, std::size_t) {
        result.read_ec = read_ec;
        finished = true;
      });
    });
    io_context.run_for(connect_timeout);

    expect(finished) << "connect and first read did not complete within " << connect_timeout.count() << "s";
    return result;
  }

  std::error_code handshake_with(std::string_view server_certificate) {
    auto server_ctx = aero::tests::make_tls_server_context(server_certificate);
    auto client_ctx = aero::tests::make_tls_client_context();
    return handshake_with(server_ctx, client_ctx);
  }

  auto describe(std::error_code ec) {
    return std::string{ec.category().name()} + ": " + ec.message();
  }

} // namespace

int main() {
  suite tls_certificate_errors = [] {
    "trusted certificate matching the host succeeds"_test = [] {
      auto ec = handshake_with(certificates::leaf_valid);
      expect(not ec) << describe(ec);
    };

    "expired certificate reports cert_expired"_test = [] {
      auto ec = handshake_with(certificates::leaf_expired);
      expect(ec == certificate_error::cert_expired) << describe(ec);
    };

    "certificate not yet valid reports cert_not_started"_test = [] {
      auto ec = handshake_with(certificates::leaf_not_yet_valid);
      expect(ec == certificate_error::cert_not_started) << describe(ec);
    };

    "self-signed certificate outside the trust store reports cert_authority_invalid"_test = [] {
      auto ec = handshake_with(certificates::leaf_self_signed);
      expect(ec == certificate_error::cert_authority_invalid) << describe(ec);
    };

    "certificate issued for another host reports cert_hostname_mismatch"_test = [] {
      auto ec = handshake_with(certificates::leaf_wrong_host);
      expect(ec == certificate_error::cert_hostname_mismatch) << describe(ec);
    };

    "leaf without its intermediate reports cert_authority_invalid"_test = [] {
      auto ec = handshake_with(certificates::leaf_behind_intermediate);
      expect(ec == certificate_error::cert_authority_invalid) << describe(ec);
    };

    "certificate with clientAuth-only EKU reports cert_eku_invalid"_test = [] {
      auto ec = handshake_with(certificates::leaf_client_auth_only);
      expect(ec == certificate_error::cert_eku_invalid) << describe(ec);
    };

    "server capped at TLS 1.2 against a TLS 1.3-only client reports tls_version_unsupported"_test = [] {
      auto server_ctx = aero::tests::make_tls_server_context(certificates::leaf_valid);
      server_ctx.set_options(asio::ssl::context::no_tlsv1_3);

      auto client_ctx = aero::tests::make_tls_client_context();
      client_ctx.set_options(asio::ssl::context::no_tlsv1 | asio::ssl::context::no_tlsv1_1 | asio::ssl::context::no_tlsv1_2);

      auto ec = handshake_with(server_ctx, client_ctx);
      expect(ec == handshake_error::tls_version_unsupported) << describe(ec);
    };

    "TLS 1.2 server demanding a client certificate fails the handshake without a certificate error"_test = [] {
      auto server_ctx = aero::tests::make_tls_server_context(certificates::leaf_valid);
      server_ctx.set_options(asio::ssl::context::no_tlsv1_3);
      require_client_certificate(server_ctx);

      auto client_ctx = aero::tests::make_tls_client_context();

      auto ec = handshake_with(server_ctx, client_ctx);
      expect(static_cast<bool>(ec)) << "handshake succeeded without the required client certificate";
      expect(ec.category() != aero::tls::certificate_error_category()) << describe(ec);
    };

    "TLS 1.3 server demanding a client certificate completes the handshake and fails the first read"_test = [] {
      auto server_ctx = aero::tests::make_tls_server_context(certificates::leaf_valid);
      require_client_certificate(server_ctx);

      auto client_ctx = aero::tests::make_tls_client_context();

      auto [handshake_ec, read_ec] = handshake_then_read(server_ctx, client_ctx);
      expect(not handshake_ec) << describe(handshake_ec);
      expect(static_cast<bool>(read_ec)) << "first read succeeded without the required client certificate";
    };
  };
}
