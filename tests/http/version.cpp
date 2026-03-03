#include "gtest/gtest.h"

#include "aero/http/error.hpp"
#include "aero/http/version.hpp"

namespace {

  namespace http = aero::http;
  using http::error::protocol_error;

} // namespace

TEST(HttpVersion, ToStringReturnsCanonicalTokens) {
  EXPECT_EQ(http::to_string(http::version::http1_0), "HTTP/1.0");
  EXPECT_EQ(http::to_string(http::version::http1_1), "HTTP/1.1");
}

TEST(HttpVersion, ParseCanonicalTokensSucceeds) {
  EXPECT_EQ(http::parse_version("HTTP/1.0"), http::version::http1_0);
  EXPECT_EQ(http::parse_version("HTTP/1.1"), http::version::http1_1);
}

TEST(HttpVersion, ParseRejectsWrongPrefix) {
  auto parsed = http::parse_version("HTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::version_invalid);
}

TEST(HttpVersion, ParseRejectsWrongSeparator) {
  auto parsed = http::parse_version("HTTP-1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::version_invalid);
}

TEST(HttpVersion, ParseRejectsUnsupportedMinorOrMajor) {
  for (std::string_view text : {"HTTP/0.9", "HTTP/1.2", "HTTP/2.0", "HTTP/1.01"}) {
    auto parsed = http::parse_version(text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), protocol_error::version_invalid);
  }
}

TEST(HttpVersion, ParseRejectsCaseDifferencesAndWhitespace) {
  for (std::string_view text : {"http/1.1", "HTTP/1.1 ", " HTTP/1.1", "HTTP/ 1.1", "HTTP/1.1\r\n"}) {
    auto parsed = http::parse_version(text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), protocol_error::version_invalid);
  }
}

TEST(HttpVersion, ToStringReturnsEmptyForUnknownEnumValue) {
  auto unknown_version = static_cast<http::version>(0);
  EXPECT_TRUE(http::to_string(unknown_version).empty());
}
