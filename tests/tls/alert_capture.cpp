#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <tuple>
#include <ut/ut.hpp>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "aero/tls/detail/alert_capture.hpp"

using namespace ut;

using aero::tls::detail::alert_capture;

namespace {

  struct client_ssl {
    SSL_CTX* ctx{nullptr};
    SSL* ssl{nullptr};
    BIO* read_bio{nullptr};
    BIO* write_bio{nullptr};

    client_ssl()
      : ctx(SSL_CTX_new(TLS_client_method())),
        ssl(SSL_new(ctx)),
        read_bio(BIO_new(BIO_s_mem())),
        write_bio(BIO_new(BIO_s_mem())) {
      SSL_set_bio(ssl, read_bio, write_bio);
      SSL_set_connect_state(ssl);
    }

    client_ssl(const client_ssl&) = delete;
    client_ssl& operator=(const client_ssl&) = delete;
    client_ssl(client_ssl&&) = delete;
    client_ssl& operator=(client_ssl&&) = delete;

    ~client_ssl() {
      SSL_free(ssl);
      SSL_CTX_free(ctx);
    }
  };

  // TLS record: content type alert (21), version 1.2, length 2, level fatal (2), description handshake_failure (40)
  constexpr std::array<unsigned char, 7> fatal_handshake_failure_alert{0x15, 0x03, 0x03, 0x00, 0x02, 0x02, 0x28};

  void deliver_alert(client_ssl& client) {
    BIO_write(client.read_bio, fatal_handshake_failure_alert.data(), static_cast<int>(fatal_handshake_failure_alert.size()));
    std::ignore = SSL_do_handshake(client.ssl);
  }

} // namespace

int main() {
  suite tls_alert_capture = [] {
    "captures fatal alert received from peer"_test = [] {
      client_ssl client;

      alert_capture capture;
      capture.install(client.ssl);

      deliver_alert(client);

      auto alert = capture.get_last_tls_alert();
      require(alert.has_value()) << "alert record never reached the message callback";
      expect(alert->received_from_peer);
      expect(alert->level == 2);
      expect(alert->description == 40);
    };

    "destructor uninstalls the SSL message callback"_test = [] {
      client_ssl client;

      alignas(alert_capture) std::array<std::byte, sizeof(alert_capture)> storage{};
      auto* capture = new (storage.data()) alert_capture; // NOLINT(cppcoreguidelines-owning-memory)
      capture->install(client.ssl);
      capture->~alert_capture();
      storage.fill(std::byte{0xAB});

      deliver_alert(client);

      bool storage_untouched = std::ranges::all_of(storage, [](std::byte value) { return value == std::byte{0xAB}; });
      expect(storage_untouched) << "message callback is still installed and wrote into destroyed alert_capture storage";
    };
  };
}
