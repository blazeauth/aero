#include "aero/http/status_code.hpp"
#include "ut.hpp"

using aero::http::status_code;

ut::suite http_status_code = [] {
  "std::format formats status code as number"_test = [] {
    std::string status_str = std::format("Status code {}", status_code::im_a_teapot);
    expect(status_str == "Status code 418");
  };
};

int main() {}
