#include "aero/http/status_line.hpp"
#include <gtest/gtest.h>
#include <utility>

#include "aero/http/detail/status_line_parser.hpp"
#include "aero/http/status_code.hpp"

namespace http = aero::http;
using aero::http::status_code;
using aero::http::status_line;

namespace {

  std::string generate_status_line_buffer(status_line status_line) {
    std::string status_line_str = std::format("{} {}", status_line.protocol, std::to_underlying(status_line.status_code));
    if (status_line.has_reason_phrase()) {
      status_line_str.append(" " + status_line.reason_phrase);
    }
    return status_line_str + "\r\n";
  }

} // namespace

TEST(HttpStatusLine, ParsesWithReasonPhrase) {
  status_line status_line{
    .protocol = "HTTP/1.1",
    .status_code = status_code::ok,
    .reason_phrase = "OK",
  };

  auto status_line_buf = generate_status_line_buffer(status_line);
  auto parsed_status_line = http::detail::parse_status_line(status_line_buf);

  ASSERT_TRUE(parsed_status_line.has_value());
  EXPECT_EQ(parsed_status_line, status_line);
}

TEST(HttpStatusLine, ParsesWithoutReasonPhrase) {
  status_line status_line{
    .protocol = "HTTP/1.1",
    .status_code = status_code::ok,
  };

  auto status_line_buf = generate_status_line_buffer(status_line);
  auto parsed_status_line = http::detail::parse_status_line(status_line_buf);

  ASSERT_TRUE(parsed_status_line.has_value());
  EXPECT_TRUE(parsed_status_line->reason_phrase.empty());
  EXPECT_EQ(parsed_status_line, status_line);
}

TEST(HttpStatusLine, RejectsInvalidProtocol) {
  status_line status_line{
    .protocol = "TP/1.1",
    .status_code = status_code::ok,
  };

  auto status_line_buf = generate_status_line_buffer(status_line);
  auto parsed_status_line = http::detail::parse_status_line(status_line_buf);

  ASSERT_FALSE(parsed_status_line.has_value());
}

// HTTP 1.1 RFC says that header section can have "zero or more header field lines"
TEST(HttpStatusLine, ParsesStatusLineWhenNoHeadersFollow) {
  status_line status_line{
    .protocol = "HTTP/1.0",
    .status_code = status_code::ok,
    .reason_phrase = "OK",
  };
  std::string_view status_line_buf{"HTTP/1.0 200 OK\r\n\r\n"};
  auto parsed_status_line = http::detail::parse_status_line(status_line_buf);

  ASSERT_TRUE(parsed_status_line.has_value());
  EXPECT_TRUE(!parsed_status_line->reason_phrase.empty());
  EXPECT_EQ(parsed_status_line, status_line);
}

TEST(HttpStatusLine, ToStringReturnsValidString) {
  status_line status_line{
    .protocol = "HTTP/1.0",
    .status_code = status_code::ok,
    .reason_phrase = "OK",
  };

  EXPECT_EQ(status_line.to_string(), "HTTP/1.0 200 OK");
}
