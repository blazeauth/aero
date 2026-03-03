#include <gtest/gtest.h>

#include "aero/base64/base64.hpp"

using aero::base64_encode;

TEST(Base64, EncodesToBase64) {
  constexpr std::string_view string{"one, two, three, four, five"};
  constexpr std::string_view string_b64{"b25lLCB0d28sIHRocmVlLCBmb3VyLCBmaXZl"};

  EXPECT_EQ(base64_encode(string), string_b64);
  EXPECT_EQ(base64_encode(std::span{reinterpret_cast<const std::byte*>(string.data()), string.size()}), string_b64);
}
