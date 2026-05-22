#include <gtest/gtest.h>

#include "aero/http/error.hpp"

#include "../error_code_test_helper.hpp"

namespace http = aero::http;

TEST(HttpErrorCodes, AllHeaderErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<http::header_error>(http::header_error_category());
}

TEST(HttpErrorCodes, AllProtocolErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<http::protocol_error>(http::protocol_error_category());
}

TEST(HttpErrorCodes, AllUriErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<http::uri_error>(http::uri_error_category());
}

TEST(HttpErrorCodes, AllConnectionErrorsHaveMessage) {
  aero::tests::test_enum_error_code_messages<http::connection_error>(http::connection_error_category());
}
