#include "aero/error.hpp"
#include <ut/ut.hpp>

#include "error_code_test_helper.hpp"

using namespace ut;

int main() {
  suite error_code = [] {
    "all basic errors have messages"_test = [] {
      aero::tests::test_enum_error_code_messages<aero::basic_error>(aero::basic_error_category());
    };

    "all error conditions have messages"_test = [] {
      aero::tests::test_enum_error_condition_messages<aero::errc>(aero::error_condition_category());
    };
  };
}
