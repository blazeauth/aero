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
  using http::error::protocol_error;

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

TEST(HttpHeadersParser, ParsesObsFoldContinuationLines) {
  auto parsed = http::headers::parse("X-Test: first\r\n\tsecond\r\n third\r\nY: ok\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);
  EXPECT_EQ(headers.first_value("X-Test"), "first second third");
  EXPECT_EQ(headers.first_value("Y"), "ok");
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

TEST(HttpHeadersParser, FailsIfHeadersEndSeparatorMissing) {
  auto parsed = http::headers::parse("A: b\r\nC: d\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::headers_section_incomplete);
}

TEST(HttpHeadersParser, FailsIfObsFoldWithoutPreviousHeader) {
  auto parsed = http::headers::parse(" continuation-without-header\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::obs_fold_without_previous_header);
}

TEST(HttpHeadersParser, FailsIfHeaderLineHasNoColon) {
  auto parsed = http::headers::parse("A: b\r\nThisIsNotAHeaderField\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameContainsWhitespace) {
  auto parsed = http::headers::parse("Bad Name: value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_name_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameHasWhitespaceBeforeColon) {
  auto parsed = http::headers::parse("BadName : value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_name_invalid);
}

TEST(HttpHeadersParser, FailsIfLfOnlyLineEndingsAreUsed) {
  auto parsed = http::headers::parse("A: b\nC: d\n\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::headers_section_incomplete);
}

TEST(HttpHeadersParser, FailsIfBareCrAppearsInsideHeaderLine) {
  auto parsed = http::headers::parse("A: b\rc\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderValueContainsControlCharacters) {
  std::string buffer;
  buffer.append("A: b");
  buffer.push_back(static_cast<char>(0x01));
  buffer.append("c\r\n\r\n");

  auto parsed = http::headers::parse(buffer);

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, FailsIfObsFoldContinuationContainsControlCharacters) {
  constexpr char ascii_delete = 0x7F;
  std::string buffer;
  buffer.append("X-Test: first\r\n");
  buffer.append("\tsecond");
  buffer.push_back(ascii_delete);
  buffer.append("third\r\n");
  buffer.append("\r\n");

  auto parsed = http::headers::parse(buffer);

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, SplitsKnownCommaSeparatedHeaderIntoMultipleRecords) {
  const auto headers_buffer = generate_headers_buffer({
    {"Accept", "text/html, application/json, image/png"},
    {"Date", "Sun, 15 Feb 2026 23:19:40 GMT"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 4);

  auto accept_values = get_all_header_values(headers, "ACCEPT");
  ASSERT_EQ(accept_values.size(), 3);
  EXPECT_EQ(accept_values[0], "text/html");
  EXPECT_EQ(accept_values[1], "application/json");
  EXPECT_EQ(accept_values[2], "image/png");

  EXPECT_EQ(headers.first_value("Accept"), "text/html");
  EXPECT_EQ(headers.first_value("Date"), "Sun, 15 Feb 2026 23:19:40 GMT");
}

TEST(HttpHeadersParser, SplitsKnownCommaSeparatedHeaderCaseInsensitively) {
  const auto headers_buffer = generate_headers_buffer({
    {"cOnNeCtIoN", "keep-alive, Upgrade"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto connection_values = get_all_header_values(headers, "CONNECTION");
  ASSERT_EQ(connection_values.size(), 2);
  EXPECT_EQ(connection_values[0], "keep-alive");
  EXPECT_EQ(connection_values[1], "Upgrade");
}

TEST(HttpHeadersParser, TrimsWhitespaceAroundCommaSeparatedSegments) {
  const auto headers_buffer = generate_headers_buffer({
    {"Accept", "  text/html  ,\t application/json \t,   image/png   "},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);

  auto accept_values = get_all_header_values(headers, "Accept");
  ASSERT_EQ(accept_values.size(), 3);
  EXPECT_EQ(accept_values[0], "text/html");
  EXPECT_EQ(accept_values[1], "application/json");
  EXPECT_EQ(accept_values[2], "image/png");
}

TEST(HttpHeadersParser, DoesNotSplitCommasInsideQuotedStrings) {
  const auto headers_buffer = generate_headers_buffer({
    {"Accept", "text/html; q=\"0,8\", application/json"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto accept_values = get_all_header_values(headers, "Accept");
  ASSERT_EQ(accept_values.size(), 2);
  EXPECT_EQ(accept_values[0], "text/html; q=\"0,8\"");
  EXPECT_EQ(accept_values[1], "application/json");
}

TEST(HttpHeadersParser, DoesNotSplitCommasInsideComments) {
  const auto headers_buffer = generate_headers_buffer({
    {"Accept", "text/html (level,one), application/json"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto accept_values = get_all_header_values(headers, "Accept");
  ASSERT_EQ(accept_values.size(), 2);
  EXPECT_EQ(accept_values[0], "text/html (level,one)");
  EXPECT_EQ(accept_values[1], "application/json");
}

TEST(HttpHeadersParser, IgnoresEmptyCommaSeparatedSegments) {
  const auto headers_buffer = generate_headers_buffer({
    {"Accept", "text/html, , \t, application/json,   ,image/png"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);

  auto accept_values = get_all_header_values(headers, "Accept");
  ASSERT_EQ(accept_values.size(), 3);
  EXPECT_EQ(accept_values[0], "text/html");
  EXPECT_EQ(accept_values[1], "application/json");
  EXPECT_EQ(accept_values[2], "image/png");
}

TEST(HttpHeadersParser, DoesNotSplitHeadersOutsideCommaSeparatedAllowList) {
  const auto headers_buffer = generate_headers_buffer({
    {"Set-Cookie", "a=1, b=2"},
    {"Date", "Sun, 15 Feb 2026 23:19:40 GMT"},
  });

  auto parsed = http::headers::parse(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto set_cookie_values = get_all_header_values(headers, "Set-Cookie");
  ASSERT_EQ(set_cookie_values.size(), 1);
  EXPECT_EQ(set_cookie_values[0], "a=1, b=2");

  EXPECT_EQ(headers.first_value("Date"), "Sun, 15 Feb 2026 23:19:40 GMT");
}

TEST(HttpHeadersParser, SplitsCommaSeparatedHeaderAfterObsFoldNormalization) {
  auto parsed = http::headers::parse("Connection: keep-alive,\r\n Upgrade\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  auto connection_values = get_all_header_values(headers, "Connection");
  ASSERT_EQ(connection_values.size(), 2);
  EXPECT_EQ(connection_values[0], "keep-alive");
  EXPECT_EQ(connection_values[1], "Upgrade");
}
