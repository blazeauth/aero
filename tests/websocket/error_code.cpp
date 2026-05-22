#include <gtest/gtest.h>

#include "aero/websocket/error.hpp"

#include "../error_code_test_helper.hpp"

namespace websocket = aero::websocket;

TEST(WebsocketErrorCodes, AllProtocolErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<websocket::protocol_error>(websocket::protocol_error_category());
}

TEST(WebsocketErrorCodes, AllHandshakeErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<websocket::handshake_error>(websocket::handshake_error_category());
}

TEST(WebsocketErrorCodes, AllUriErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<websocket::uri_error>(websocket::uri_error_category());
}

TEST(WebsocketErrorCodes, AllMessageReaderErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<websocket::message_reader_error>(websocket::message_reader_category());
}
