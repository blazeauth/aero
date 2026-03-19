#include <gtest/gtest.h>

#include "aero/http/method.hpp"

namespace {
  using aero::http::method;
}

TEST(HttpMethod, StdFormatFormatsMethodAsString) {
  std::string method_str = std::format("Method {}", method::get);
  EXPECT_EQ(method_str, "Method GET");
}
