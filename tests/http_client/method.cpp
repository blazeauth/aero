#include "aero/http/method.hpp"
#include <ut/ut.hpp>

using namespace ut;

using aero::http::method;

int main() {
  suite http_method = [] {
    "std::format formats method as string"_test = [] {
      std::string method_str = std::format("Method {}", method::GET);
      expect(method_str == "Method GET");
    };
  };
}
