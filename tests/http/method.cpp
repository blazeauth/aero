#include <gtest/gtest.h>

#include "aero/http/method.hpp"

namespace {
  using aero::http::method;
}

TEST(HttpMethod, CompilesAsStdFormatArgument) {
  std::ignore = std::format("Method {}", method::get);
}

TEST(HttpMethod, MethodNameFormatsCorrectly) {
  std::string method_str = std::format("Method {}", method::get);
  EXPECT_EQ(method_str, "Method GET");
}
