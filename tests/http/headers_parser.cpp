#include <format>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/http/detail/headers_parser.hpp"

namespace http = aero::http;
using http::error::protocol_error;

namespace {

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

  std::vector<std::string> get_all_header_values(const http::detail::header_fields& headers, std::string_view key) {
    return headers.values_of(key) | std::ranges::to<std::vector<std::string>>();
  }

} // namespace

TEST(HttpHeadersParser, ParsesTrivialHeaders) {
  const auto headers_buffer = generate_headers_buffer({
    {"Connection", "Upgrade"},
    {"Upgrade", "websocket"},
    {"Date", "Sun, 15 Feb 2026 23:19:40 GMT"},
  });

  auto parsed = http::detail::parse_headers(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);
  EXPECT_EQ(headers.first_value_of("Connection"), "Upgrade");
  EXPECT_EQ(headers.first_value_of("Upgrade"), "websocket");
  EXPECT_EQ(headers.first_value_of("Date"), "Sun, 15 Feb 2026 23:19:40 GMT");
}

TEST(HttpHeadersParser, FieldNamesAreCaseInsensitive) {
  const auto headers_buffer = generate_headers_buffer({
    {"Content-Length", "123"},
    {"cOnNeCtIoN", "close"},
  });

  auto parsed = http::detail::parse_headers(headers_buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);

  EXPECT_TRUE(headers.contains("content-length"));
  EXPECT_TRUE(headers.contains("CONTENT-LENGTH"));
  EXPECT_TRUE(headers.contains("connection"));
  EXPECT_TRUE(headers.contains("CONNECTION"));

  EXPECT_EQ(headers.first_value_of("content-length"), "123");
  EXPECT_EQ(headers.first_value_of("CONTENT-LENGTH"), "123");
  EXPECT_EQ(headers.first_value_of("Connection"), "close");
}

TEST(HttpHeadersParser, TrimsOptionalWhitespaceAroundValue) {
  auto parsed = http::detail::parse_headers("A:    b\r\nB:\t\tc\r\nC:\t d \t\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 3);
  EXPECT_EQ(headers.first_value_of("A"), "b");
  EXPECT_EQ(headers.first_value_of("B"), "c");
  EXPECT_EQ(headers.first_value_of("C"), "d");
}

TEST(HttpHeadersParser, ParsesObsFoldContinuationLines) {
  auto parsed = http::detail::parse_headers("X-Test: first\r\n\tsecond\r\n third\r\nY: ok\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);
  EXPECT_EQ(headers.first_value_of("X-Test"), "first second third");
  EXPECT_EQ(headers.first_value_of("Y"), "ok");
}

TEST(HttpHeadersParser, PreservesDuplicateFieldNames) {
  const auto headers_buffer = generate_headers_buffer({
    {"Set-Cookie", "a=1"},
    {"Set-Cookie", "b=2"},
    {"X-Foo", "one"},
    {"x-foo", "two"},
  });

  auto parsed = http::detail::parse_headers(headers_buffer);
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

  auto parsed = http::detail::parse_headers(buffer);
  ASSERT_TRUE(parsed);

  auto& headers = *parsed;

  EXPECT_EQ(headers.size(), 2);
  EXPECT_TRUE(headers.contains("Connection"));
  EXPECT_TRUE(headers.contains("Upgrade"));
  EXPECT_FALSE(headers.contains("Body-Line"));
  EXPECT_FALSE(headers.contains("Another"));
}

TEST(HttpHeadersParser, FailsIfHeadersEndSeparatorMissing) {
  auto parsed = http::detail::parse_headers("A: b\r\nC: d\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::headers_section_incomplete);
}

TEST(HttpHeadersParser, FailsIfObsFoldWithoutPreviousHeader) {
  auto parsed = http::detail::parse_headers(" continuation-without-header\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::obs_fold_without_previous_header);
}

TEST(HttpHeadersParser, FailsIfHeaderLineHasNoColon) {
  auto parsed = http::detail::parse_headers("A: b\r\nThisIsNotAHeaderField\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameContainsWhitespace) {
  auto parsed = http::detail::parse_headers("Bad Name: value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_name_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderNameHasWhitespaceBeforeColon) {
  auto parsed = http::detail::parse_headers("BadName : value\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_name_invalid);
}

TEST(HttpHeadersParser, FailsIfLfOnlyLineEndingsAreUsed) {
  auto parsed = http::detail::parse_headers("A: b\nC: d\n\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::headers_section_incomplete);
}

TEST(HttpHeadersParser, FailsIfBareCrAppearsInsideHeaderLine) {
  auto parsed = http::detail::parse_headers("A: b\rc\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}

TEST(HttpHeadersParser, FailsIfHeaderValueContainsControlCharacters) {
  std::string buffer;
  buffer.append("A: b");
  buffer.push_back(static_cast<char>(0x01));
  buffer.append("c\r\n\r\n");

  auto parsed = http::detail::parse_headers(buffer);

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

  auto parsed = http::detail::parse_headers(buffer);

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_line_invalid);
}
