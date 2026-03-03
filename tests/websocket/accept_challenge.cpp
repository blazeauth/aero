#include <gtest/gtest.h>

#include "aero/websocket/detail/accept_challenge.hpp"

namespace detail = aero::websocket::detail;

TEST(WebsocketAcceptChallenge, GeneratesRandomKey) {
  for (int i{}; i < 10; i++) {
    EXPECT_NE(detail::generate_sec_websocket_key(), detail::generate_sec_websocket_key());
  }
}

TEST(WebsocketAcceptChallenge, Rfc6455Conformant) {
  constexpr std::string_view sec_websocket_key{"dGhlIHNhbXBsZSBub25jZQ=="};
  EXPECT_EQ(detail::compute_sec_websocket_accept(sec_websocket_key), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}
