#include "aero/http/error.hpp"
#include <ut/ut.hpp>

#include "error_code_test_helper.hpp"

using namespace ut;

namespace http = aero::http;

int main() {
  suite http_error_code = [] {
    "all header errors have messages"_test = [] {
      test_enum_error_code_messages<http::header_error>(http::header_error_category());
    };

    "all protocol errors have messages"_test = [] {
      test_enum_error_code_messages<http::protocol_error>(http::protocol_error_category());
    };
  };
}
