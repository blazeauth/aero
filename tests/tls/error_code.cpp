#include "aero/tls/error.hpp"
#include <ut/ut.hpp>

#include "common/error_code_test_helper.hpp"

using namespace ut;

namespace tls = aero::tls;

int main() {
  suite tls_error_code = [] {
    "all backend errors have messages"_test = [] {
      test_enum_error_code_messages<tls::backend_error>(tls::backend_error_category());
    };

    "all handshake errors have messages"_test = [] {
      test_enum_error_code_messages<tls::handshake_error>(tls::handshake_error_category());
    };

    "all certificate errors have messages"_test = [] {
      test_enum_error_code_messages<tls::certificate_error>(tls::certificate_error_category());
    };

    "all context errors have messages"_test = [] {
      test_enum_error_code_messages<tls::context_error>(tls::context_error_category());
    };
  };
}
