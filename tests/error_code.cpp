#include <gtest/gtest.h>

#include "aero/error.hpp"

#include "error_code_test_helper.hpp"

TEST(ErrorCodes, AllBasicErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<aero::basic_error>(aero::basic_error_category());
}

TEST(ErrorCodes, AllErrorConditionsHaveMessage) {
  aero::tests::test_enum_error_condition_messages<aero::errc>(aero::error_condition_category());
}
