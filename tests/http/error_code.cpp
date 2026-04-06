#include <gtest/gtest.h>

#include "aero/http/error.hpp"

#include "../error_code_test_helper.hpp"

namespace {

  namespace error = aero::http::error;

  using error::connection_error;
  using error::connection_error_category;
  using error::header_error;
  using error::header_error_category;
  using error::protocol_error;
  using error::protocol_error_category;
  using error::uri_error;
  using error::uri_error_category;

} // namespace

TEST(HttpErrorCodes, AllHeaderErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<header_error>(header_error_category());
}

TEST(HttpErrorCodes, AllProtocolErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<protocol_error>(protocol_error_category());
}

TEST(HttpErrorCodes, AllUriErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<uri_error>(uri_error_category());
}

TEST(HttpErrorCodes, AllConnectionErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<connection_error>(connection_error_category());
}
