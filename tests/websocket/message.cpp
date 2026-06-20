#include "aero/websocket/message.hpp"
#include <ut/ut.hpp>

using namespace ut;

using aero::websocket::message_kind;

int main() {
  suite websocket_message = [] {
    "std::format formats message kind as string"_test = [] {
      expect(std::format("Message kind {}", message_kind::text) == "Message kind text");
      expect(std::format("Message kind {}", message_kind::binary) == "Message kind binary");
      expect(std::format("Message kind {}", message_kind::close) == "Message kind close");
      expect(std::format("Message kind {}", message_kind::ping) == "Message kind ping");
      expect(std::format("Message kind {}", message_kind::pong) == "Message kind pong");
    };
  };
}
