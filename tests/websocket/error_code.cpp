#include <gtest/gtest.h>

#include "aero/websocket/error.hpp"

#include "../error_code_test_helper.hpp"

namespace {

  namespace error = aero::websocket::error;

  using error::handshake_error;
  using error::message_reader_error;
  using error::protocol_error;
  using error::uri_error;

  using error::handshake_error_category;
  using error::message_reader_category;
  using error::protocol_error_category;
  using error::uri_error_category;

} // namespace

TEST(WebsocketErrorCodes, AllProtocolErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<protocol_error>(protocol_error_category());
}

TEST(WebsocketErrorCodes, AllHandshakeErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<handshake_error>(handshake_error_category());
}

TEST(WebsocketErrorCodes, AllUriErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<uri_error>(uri_error_category());
}

TEST(WebsocketErrorCodes, AllMessageReaderErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<message_reader_error>(message_reader_category());
}
