#include <gtest/gtest.h>

#include "aero/http/status_code.hpp"

namespace {
  using aero::http::status_code;
}

TEST(HttpStatusCode, CompilesAsStdFormatArgument) {
  std::ignore = std::format("Status code {}", status_code::im_a_teapot);
}
