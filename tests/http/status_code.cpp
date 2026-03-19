#include <gtest/gtest.h>

#include "aero/http/status_code.hpp"

namespace {
  using aero::http::status_code;
}

TEST(HttpStatusCode, StdFormatFormatsStatusCodeAsNumber) {
  std::string status_str = std::format("Status code {}", status_code::im_a_teapot);
  EXPECT_EQ(status_str, "Status code 418");
}
