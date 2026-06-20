#include "aero/error.hpp"
#include "ut.hpp"

#include "error_code_test_helper.hpp"

ut::suite error_code = [] {
  "all basic errors have messages"_test = [] {
    aero::tests::test_enum_error_code_messages<aero::basic_error>(aero::basic_error_category());
  };

  "all error conditions have messages"_test = [] {
    aero::tests::test_enum_error_condition_messages<aero::errc>(aero::error_condition_category());
  };
};

int main() {}
