#include "aero/http/error.hpp"
#include "ut.hpp"

#include "error_code_test_helper.hpp"

namespace http = aero::http;

ut::suite http_error_code = [] {
  "all header errors have messages"_test = [] {
    aero::tests::test_enum_error_code_messages<http::header_error>(http::header_error_category());
  };

  "all protocol errors have messages"_test = [] {
    aero::tests::test_enum_error_code_messages<http::protocol_error>(http::protocol_error_category());
  };

  "all uri errors have messages"_test = [] {
    aero::tests::test_enum_error_code_messages<http::uri_error>(http::uri_error_category());
  };

  "all connection errors have messages"_test = [] {
    aero::tests::test_enum_error_code_messages<http::connection_error>(http::connection_error_category());
  };
};

int main() {}
