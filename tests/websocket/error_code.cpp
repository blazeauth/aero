#include "aero/websocket/error.hpp"
#include <ut/ut.hpp>

#include "common/error_code_test_helper.hpp"

using namespace ut;

namespace websocket = aero::websocket;

int main() {
  suite websocket_error_code = [] {
    "all protocol errors have messages"_test = [] {
      test_enum_error_code_messages<websocket::protocol_error>(websocket::protocol_error_category());
    };

    "all handshake errors have messages"_test = [] {
      test_enum_error_code_messages<websocket::handshake_error>(websocket::handshake_error_category());
    };
  };
}
