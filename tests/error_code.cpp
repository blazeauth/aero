#include <gtest/gtest.h>

#include "aero/error.hpp"

#include "error_code_test_helper.hpp"

namespace {

  namespace error = aero::error;

  using error::basic_error;
  using error::errc;

  using error::basic_error_category;
  using error::error_condition_category;

} // namespace

TEST(ErrorCodes, AllBasicErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<basic_error>(basic_error_category());
}

TEST(ErrorCodes, AllErrorConditionsHaveMessage) {
  aero::tests::test_enum_error_condition_messages<errc>(error_condition_category());
}
