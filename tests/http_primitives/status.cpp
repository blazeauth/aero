#include "aero/http/status.hpp"
#include <ut/ut.hpp>

using namespace ut;

using aero::http::status;

int main() {
  suite http_status = [] {
    "std::format formats status code as number"_test = [] {
      std::string status_str = std::format("Status code {}", status::im_a_teapot);
      expect(status_str == "Status code 418");
    };
  };
}
