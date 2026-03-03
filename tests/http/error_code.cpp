#include <gtest/gtest.h>

#include "aero/http/error.hpp"

#include "../error_code_test_helper.hpp"

namespace {

  namespace error = aero::http::error;

  using error::protocol_error;
  using error::protocol_error_category;

} // namespace

TEST(HttpErrorCodes, AllProtocolErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<protocol_error>(protocol_error_category());
}
