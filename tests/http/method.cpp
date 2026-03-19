#include <gtest/gtest.h>

#include "aero/http/method.hpp"

namespace {
  using aero::http::method;
}

TEST(HttpMethod, CompilesAsStdFormatArgument) {
  std::ignore = std::format("Method {}", method::get);
}
