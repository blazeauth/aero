#include "aero/websocket/close_code.hpp"
#include <ut/ut.hpp>

using namespace ut;

using aero::websocket::close_code;

int main() {
  suite websocket_close_code = [] {
    "std::format formats close code as number"_test = [] {
      std::string close_code_str = std::format("Close code {}", close_code::normal);
      expect(close_code_str == "Close code 1000");
    };
  };
}
