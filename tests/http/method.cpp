#include "aero/http/method.hpp"
#include "ut.hpp"

using aero::http::method;

ut::suite http_method = [] {
  "std::format formats method as string"_test = [] {
    std::string method_str = std::format("Method {}", method::get);
    expect(method_str == "Method GET");
  };
};

int main() {}
