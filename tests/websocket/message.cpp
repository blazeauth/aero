#include <gtest/gtest.h>

#include "aero/websocket/message.hpp"

namespace {
  using aero::websocket::message_kind;
} // namespace

TEST(WebsocketMessageKind, StdFormatFormatsMessageKindAsString) {
  EXPECT_EQ(std::format("Message kind {}", message_kind::text), "Message kind text");
  EXPECT_EQ(std::format("Message kind {}", message_kind::binary), "Message kind binary");
  EXPECT_EQ(std::format("Message kind {}", message_kind::close), "Message kind close");
  EXPECT_EQ(std::format("Message kind {}", message_kind::ping), "Message kind ping");
  EXPECT_EQ(std::format("Message kind {}", message_kind::pong), "Message kind pong");
}
