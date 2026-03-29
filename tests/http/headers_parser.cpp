#include <cstddef>
#include <format>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

namespace {

  namespace http = aero::http;
  using http::error::header_error;

  std::string generate_headers_buffer(std::vector<std::pair<std::string, std::string>> headers) {
    std::string buffer;
    for (const auto& [header_key, header_value] : headers) {
      buffer += std::format("{}: {}\r\n", header_key, header_value);
    }
    buffer += "\r\n";
    return buffer;
  }

  std::string generate_headers_and_body(std::vector<std::pair<std::string, std::string>> headers, std::string_view body) {
    auto buffer = generate_headers_buffer(std::move(headers));
    buffer.append(body.data(), body.size());
    return buffer;
  }

  std::vector<std::string> get_all_header_values(const http::headers& headers, std::string_view key) {
    return headers.values(key) | std::ranges::to<std::vector<std::string>>();
  }

} // namespace

TEST(HttpHeadersParser, ParsesTrivialHeaders) {
  const auto headers_buffer = generate_headers_buffer({
    {"Connection", "Upgrade"},
    {"Upgrade", "websocket"},
    {"Date", "Sun, 15 Feb 2026 23:19:40 GMT"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);
  EXPECT_EQ(headers.first_value("Connection"), "Upgrade");
  EXPECT_EQ(headers.first_value("Upgrade"), "websocket");
  EXPECT_EQ(headers.first_value("Date"), "Sun, 15 Feb 2026 23:19:40 GMT");
}

TEST(HttpHeadersParser, FieldNamesAreCaseInsensitive) {
  const auto headers_buffer = generate_headers_buffer({
    {"Content-Length", "123"},
    {"cOnNeCtIoN", "close"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  EXPECT_TRUE(headers.contains("content-length"));
  EXPECT_TRUE(headers.contains("CONTENT-LENGTH"));
  EXPECT_TRUE(headers.contains("connection"));
  EXPECT_TRUE(headers.contains("CONNECTION"));

  EXPECT_EQ(headers.first_value("content-length"), "123");
  EXPECT_EQ(headers.first_value("CONTENT-LENGTH"), "123");
  EXPECT_EQ(headers.first_value("Connection"), "close");
}

TEST(HttpHeadersParser, TrimsOptionalWhitespaceAroundValue) {
  auto parsed = http::headers::parse("A:    b\r\nB:\t\tc\r\nC:\t d \t\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);
  EXPECT_EQ(headers.first_value("A"), "b");
  EXPECT_EQ(headers.first_value("B"), "c");
  EXPECT_EQ(headers.first_value("C"), "d");
}

TEST(HttpHeadersParser, PreservesDuplicateFieldNames) {
  const auto headers_buffer = generate_headers_buffer({
    {"Set-Cookie", "a=1"},
    {"Set-Cookie", "b=2"},
    {"X-Foo", "one"},
    {"x-foo", "two"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 4);

  auto set_cookie_values = get_all_header_values(headers, "SET-COOKIE");
  ASSERT_EQ(set_cookie_values.size(), 2);
  EXPECT_EQ(set_cookie_values[0], "a=1");
  EXPECT_EQ(set_cookie_values[1], "b=2");

  auto x_foo_values = get_all_header_values(headers, "X-FOO");
  ASSERT_EQ(x_foo_values.size(), 2);
  EXPECT_EQ(x_foo_values[0], "one");
  EXPECT_EQ(x_foo_values[1], "two");
}

TEST(HttpHeadersParser, StopsAtHeadersEndSeparator) {
  const auto buffer = generate_headers_and_body(
    {
      {"Connection", "close"},
      {"Upgrade", "websocket"},
    },
    "Body-Line: must-not-be-parsed\r\nAnother: line\r\n");

  auto parsed = http::headers::parse(buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);
  EXPECT_TRUE(headers.contains("Connection"));
  EXPECT_TRUE(headers.contains("Upgrade"));
  EXPECT_FALSE(headers.contains("Body-Line"));
  EXPECT_FALSE(headers.contains("Another"));
}

TEST(HttpHeadersParser, FailsIfObsFoldContinuationAppearsInHeaderLine) {
  auto parsed = http::headers::parse("X-Test: first\r\n\tsecond\r\n third\r\nY: ok\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::obs_fold_not_supported);
}

TEST(HttpHeadersParser, FailsIfHeadersEndSeparatorMissing) {
  auto parsed = http::headers::parse("A: b\r\nC: d\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::section_incomplete);
}

TEST(HttpHeadersParser, FailsIfHeaderLineHasNoColon) {
  auto parsed = http::headers::parse("A: b\r\nThisIsNotAHeaderField\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::field_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameContainsWhitespace) {
  auto parsed = http::headers::parse("Bad Name: value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::name_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameHasWhitespaceBeforeColon) {
  auto parsed = http::headers::parse("BadName : value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::name_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderSectionEndsWithDoubleLf) {
  auto parsed = http::headers::parse("A: b\nC: d\n\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::lf_field_endings_not_supported);
}

TEST(HttpHeadersParser, FailsIfBareCrAppearsInsideHeaderLine) {
  auto parsed = http::headers::parse("A: b\rc\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::field_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderValueContainsControlCharacters) {
  std::string buffer;
  buffer.append("A: b");
  buffer.push_back(static_cast<char>(0x01));
  buffer.append("c\r\n\r\n");

  auto parsed = http::headers::parse(buffer);

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), header_error::field_invalid);
}

TEST(HttpHeadersParser, ParsesEmptyHeaderValues) {
  auto parsed = http::headers::parse("X-Empty:\r\nY-Empty:   \t\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);
  EXPECT_TRUE(headers.contains("X-Empty"));
  EXPECT_TRUE(headers.contains("Y-Empty"));
  EXPECT_EQ(headers.first_value("X-Empty"), "");
  EXPECT_EQ(headers.first_value("Y-Empty"), "");
}

TEST(HttpHeadersParser, AllowsObsTextInsideHeaderValues) {
  std::string expected_value = "before";
  expected_value.push_back(static_cast<char>(0x80));
  expected_value.push_back(static_cast<char>(0xFF));
  expected_value += "after";

  std::string buffer = "X-Text: ";
  buffer += expected_value;
  buffer += "\r\n\r\n";

  auto parsed = http::headers::parse(buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 1);
  EXPECT_EQ(headers.first_value("X-Text"), expected_value);
}

TEST(HttpHeadersParser, ParsesLargeDuplicateSetCookieFieldValues) {
  const auto first_payload = std::string(16384, 'a');
  const auto second_payload = std::string(8192, 'b');

  const auto first_cookie =
    std::format("session={}; Expires=Wed, 09 Jun 2021 10:18:14 GMT; Path=/; HttpOnly; SameSite=None; Secure", first_payload);
  const auto second_cookie = std::format("refresh={}; Path=/auth; HttpOnly; SameSite=Lax; Secure", second_payload);

  const auto headers_buffer = generate_headers_buffer({
    {"Set-Cookie", first_cookie},
    {"Set-Cookie", second_cookie},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto set_cookie_values = get_all_header_values(headers, "SET-COOKIE");
  ASSERT_EQ(set_cookie_values.size(), 2);
  EXPECT_EQ(set_cookie_values[0], first_cookie);
  EXPECT_EQ(set_cookie_values[1], second_cookie);
}
