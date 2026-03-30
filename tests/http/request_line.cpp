#include "gtest/gtest.h"

#include <format>
#include <string>
#include <string_view>

#include "aero/http/error.hpp"
#include "aero/http/method.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/version.hpp"

namespace {

  namespace http = aero::http;
  using http::request_line;
  using http::error::protocol_error;

} // namespace

TEST(HttpRequestLine, TrivialSerializeSucceeds) {
  for (http::method method : http::methods) {
    request_line line{
      .method = method,
      .target = "/products",
      .version = http::version::http1_1,
    };

    auto expected = std::format("{} /products HTTP/1.1\r\n", http::to_string(method));
    EXPECT_EQ(line.serialize(), expected);
  }
}

TEST(HttpRequestLine, SerializeReturnsEmptyForUnknownMethod) {
  request_line line{
    .method = static_cast<http::method>(0xFF),
    .target = "/products",
    .version = http::version::http1_1,
  };

  EXPECT_TRUE(line.serialize().empty());
}

TEST(HttpRequestLine, SerializeReturnsEmptyForUnknownVersion) {
  request_line line{
    .method = http::method::get,
    .target = "/products",
    .version = static_cast<http::version>(0xFF),
  };

  EXPECT_TRUE(line.serialize().empty());
}

TEST(HttpRequestLine, SerializeEmptyReturnsEmptyString) {
  request_line line{};
  EXPECT_TRUE(line.serialize().empty());
}

TEST(HttpRequestLine, ParseTrivialStringSucceeds) {
  for (http::method method : http::methods) {
    request_line expected{
      .method = method,
      .target = "/products",
      .version = http::version::http1_1,
    };

    auto text = std::format("{} /products HTTP/1.1", http::to_string(method));
    EXPECT_EQ(expected, request_line::parse(text));
  }
}

TEST(HttpRequestLine, ParseWithCrlfSuffixSucceeds) {
  request_line expected{
    .method = http::method::get,
    .target = "/products",
    .version = http::version::http1_1,
  };

  EXPECT_EQ(expected, request_line::parse("GET /products HTTP/1.1\r\n"));
}

TEST(HttpRequestLine, ParseRejectsEmptyString) {
  auto parsed = request_line::parse("");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsOnlyCrlf) {
  auto parsed = request_line::parse("\r\n");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsLfOnlyLineEnding) {
  auto parsed = request_line::parse("GET /products HTTP/1.1\n");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsEmbeddedCrlfInsideLine) {
  auto parsed = request_line::parse("GET /products\r\nHTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsLeadingSpaceBeforeMethod) {
  auto parsed = request_line::parse(" GET /products HTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsMissingSpacesBetweenTokens) {
  auto parsed = request_line::parse("GET/productsHTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsMissingSecondSpace) {
  auto parsed = request_line::parse("GET /productsHTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsDoubleSpaceBetweenMethodAndTarget) {
  auto parsed = request_line::parse("GET  /products HTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsDoubleSpaceBetweenTargetAndVersion) {
  auto parsed = request_line::parse("GET /products  HTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsTrailingSpaceAfterVersion) {
  auto parsed = request_line::parse("GET /products HTTP/1.1 ");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseRejectsExtraToken) {
  auto parsed = request_line::parse("GET /products HI HTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::request_line_invalid);
}

TEST(HttpRequestLine, ParseWithInvalidHttpVersionFails) {
  auto parsed = request_line::parse("GET /products HTP/1.8");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::version_invalid);
}

TEST(HttpRequestLine, ParseWithInvalidHttpMethodFails) {
  auto parsed = request_line::parse("POP /products HTTP/1.1");
  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::method_invalid);
}

TEST(HttpRequestLine, ParseAcceptsTargetWithoutLeadingSlash) {
  auto parsed = request_line::parse("GET products HTTP/1.1");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->method, http::method::get);
  EXPECT_EQ(parsed->target, "products");
  EXPECT_EQ(parsed->version, http::version::http1_1);
}

TEST(HttpRequestLine, ParseAcceptsAsteriskTarget) {
  auto parsed = request_line::parse("OPTIONS * HTTP/1.1");
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->method, http::method::options);
  EXPECT_EQ(parsed->target, "*");
  EXPECT_EQ(parsed->version, http::version::http1_1);
}
