#include <ut/ut.hpp>
#include <utility>

#include "aero/http/status.hpp"
#include "aero/http/status_line.hpp"

using namespace ut;

namespace http = aero::http;
using aero::http::status;
using aero::http::status_line;

std::string generate_status_line_buffer(status_line status_line) {
  std::string status_line_str = std::format("{} {}", status_line.protocol, std::to_underlying(status_line.status_code));
  if (status_line.has_reason_phrase()) {
    status_line_str.append(" " + status_line.reason_phrase);
  }
  return status_line_str + "\r\n";
}

int main() {
  suite http_status_line = [] {
    "parses with reason phrase"_test = [] {
      status_line status_line{
        .protocol = "HTTP/1.1",
        .status_code = status::ok,
        .reason_phrase = "OK",
      };

      auto status_line_buf = generate_status_line_buffer(status_line);
      auto parsed_status_line = http::status_line::parse(status_line_buf);

      expect[parsed_status_line.has_value()];
      expect(parsed_status_line == status_line);
    };

    "parses without reason phrase"_test = [] {
      status_line status_line{
        .protocol = "HTTP/1.1",
        .status_code = status::ok,
      };

      auto status_line_buf = generate_status_line_buffer(status_line);
      auto parsed_status_line = http::status_line::parse(status_line_buf);

      expect[parsed_status_line.has_value()];
      expect(parsed_status_line->reason_phrase.empty());
      expect(parsed_status_line == status_line);
    };

    "rejects invalid protocol"_test = [] {
      status_line status_line{
        .protocol = "TP/1.1",
        .status_code = status::ok,
      };

      auto status_line_buf = generate_status_line_buffer(status_line);
      auto parsed_status_line = http::status_line::parse(status_line_buf);

      expect[not parsed_status_line.has_value()];
    };

    // HTTP 1.1 RFC says that header section can have "zero or more header field lines"
    "parses status line when no headers follow"_test = [] {
      status_line status_line{
        .protocol = "HTTP/1.0",
        .status_code = status::ok,
        .reason_phrase = "OK",
      };
      std::string_view status_line_buf{"HTTP/1.0 200 OK\r\n\r\n"};
      auto parsed_status_line = http::status_line::parse(status_line_buf);

      expect[parsed_status_line.has_value()];
      expect(not parsed_status_line->reason_phrase.empty());
      expect(parsed_status_line == status_line);
    };

    "serializes a valid status line"_test = [] {
      status_line status_line{
        .protocol = "HTTP/1.0",
        .status_code = status::ok,
        .reason_phrase = "OK",
      };

      expect(status_line.serialize() == "HTTP/1.0 200 OK");
    };

    "serializes an empty status line as an empty string"_test = [] {
      status_line status_line{};
      expect(status_line.serialize().empty());
    };
  };
}
