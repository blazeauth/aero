#include <gtest/gtest.h>

#include "aero/websocket/close_code.hpp"

namespace {
  using aero::websocket::close_code;
}

TEST(WebsocketCloseCode, StdFormatFormatsCloseCodeAsNumber) {
  std::string close_code_str = std::format("Close code {}", close_code::normal);
  EXPECT_EQ(close_code_str, "Close code 1000");
}
