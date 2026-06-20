#include "aero/websocket/detail/accept_challenge.hpp"
#include "ut.hpp"
#include <string_view>

namespace websocket_detail = aero::websocket::detail;

ut::suite websocket_accept_challenge = [] {
  "generates random key"_test = [] {
    for (int i{}; i < 10; i++) {
      expect(websocket_detail::generate_sec_websocket_key() != websocket_detail::generate_sec_websocket_key());
    }
  };

  "matches the rfc 6455 challenge example"_test = [] {
    constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
    expect(websocket_detail::compute_sec_websocket_accept(sec_websocket_key) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  };
};

int main() {}
