#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <ut/ut.hpp>
#include <utility>
#include <vector>

#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

using namespace ut;

namespace http = aero::http;
using http::header_error;

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

int main() {
  suite http_headers_parser = [] {
    "parses trivial headers"_test = [] {
      const auto headers_buffer = generate_headers_buffer({
        {"Connection", "Upgrade"},
        {"Upgrade", "websocket"},
        {"Date", "Sun, 15 Feb 2026 23:19:40 GMT"},
      });

      auto parsed = http::headers::parse(headers_buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 3);
      expect(headers.first_value("Connection") == "Upgrade");
      expect(headers.first_value("Upgrade") == "websocket");
      expect(headers.first_value("Date") == "Sun, 15 Feb 2026 23:19:40 GMT");
    };

    "field names are case-insensitive"_test = [] {
      const auto headers_buffer = generate_headers_buffer({
        {"Content-Length", "123"},
        {"cOnNeCtIoN", "close"},
      });

      auto parsed = http::headers::parse(headers_buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 2);

      expect(headers.contains("content-length"));
      expect(headers.contains("CONTENT-LENGTH"));
      expect(headers.contains("connection"));
      expect(headers.contains("CONNECTION"));

      expect(headers.first_value("content-length") == "123");
      expect(headers.first_value("CONTENT-LENGTH") == "123");
      expect(headers.first_value("Connection") == "close");
    };

    "trims optional whitespace around value"_test = [] {
      auto parsed = http::headers::parse("A:    b\r\nB:\t\tc\r\nC:\t d \t\r\n\r\n");
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 3);
      expect(headers.first_value("A") == "b");
      expect(headers.first_value("B") == "c");
      expect(headers.first_value("C") == "d");
    };

    "preserves duplicate field names"_test = [] {
      const auto headers_buffer = generate_headers_buffer({
        {"Set-Cookie", "a=1"},
        {"Set-Cookie", "b=2"},
        {"X-Foo", "one"},
        {"x-foo", "two"},
      });

      auto parsed = http::headers::parse(headers_buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 4);

      auto set_cookie_values = get_all_header_values(headers, "SET-COOKIE");
      expect[set_cookie_values.size() == 2];
      expect(set_cookie_values[0] == "a=1");
      expect(set_cookie_values[1] == "b=2");

      auto x_foo_values = get_all_header_values(headers, "X-FOO");
      expect[x_foo_values.size() == 2];
      expect(x_foo_values[0] == "one");
      expect(x_foo_values[1] == "two");
    };

    "stops at headers end separator"_test = [] {
      const auto buffer = generate_headers_and_body(
        {
          {"Connection", "close"},
          {"Upgrade", "websocket"},
        },
        "Body-Line: must-not-be-parsed\r\nAnother: line\r\n");

      auto parsed = http::headers::parse(buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 2);
      expect(headers.contains("Connection"));
      expect(headers.contains("Upgrade"));
      expect(not headers.contains("Body-Line"));
      expect(not headers.contains("Another"));
    };

    "fails if obs fold continuation appears in header line"_test = [] {
      auto parsed = http::headers::parse("X-Test: first\r\n\tsecond\r\n third\r\nY: ok\r\n\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::obs_fold_not_supported);
    };

    "fails if headers end separator missing"_test = [] {
      auto parsed = http::headers::parse("A: b\r\nC: d\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::section_incomplete);
    };

    "fails if header line has no colon"_test = [] {
      auto parsed = http::headers::parse("A: b\r\nThisIsNotAHeaderField\r\n\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::field_invalid);
    };

    "fails if header name contains whitespace"_test = [] {
      auto parsed = http::headers::parse("Bad Name: value\r\n\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::name_invalid);
    };

    "fails if header name has whitespace before colon"_test = [] {
      auto parsed = http::headers::parse("BadName : value\r\n\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::name_invalid);
    };

    "fails if header section ends with double lf"_test = [] {
      auto parsed = http::headers::parse("A: b\nC: d\n\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::lf_field_endings_not_supported);
    };

    "fails if bare cr appears inside header line"_test = [] {
      auto parsed = http::headers::parse("A: b\rc\r\n\r\n");

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::field_invalid);
    };

    "fails if header value contains control characters"_test = [] {
      std::string buffer;
      buffer.append("A: b");
      buffer.push_back(static_cast<char>(0x01));
      buffer.append("c\r\n\r\n");

      auto parsed = http::headers::parse(buffer);

      expect[not parsed.has_value()];
      expect(parsed.error() == header_error::field_invalid);
    };

    "parses empty header values"_test = [] {
      auto parsed = http::headers::parse("X-Empty:\r\nY-Empty:   \t\r\n\r\n");
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 2);
      expect(headers.contains("X-Empty"));
      expect(headers.contains("Y-Empty"));
      expect(headers.first_value("X-Empty") == "");
      expect(headers.first_value("Y-Empty") == "");
    };

    "allows obs text inside header values"_test = [] {
      std::string expected_value = "before";
      expected_value.push_back(static_cast<char>(0x80));
      expected_value.push_back(static_cast<char>(0xFF));
      expected_value += "after";

      std::string buffer = "X-Text: ";
      buffer += expected_value;
      buffer += "\r\n\r\n";

      auto parsed = http::headers::parse(buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 1);
      expect(headers.first_value("X-Text") == expected_value);
    };

    "parses large duplicate set-cookie field values"_test = [] {
      const auto first_payload = std::string(16384, 'a');
      const auto second_payload = std::string(8192, 'b');

      const auto first_cookie =
        std::format("session={}; Expires=Wed, 09 Jun 2021 10:18:14 GMT; Path=/; HttpOnly; SameSite=None; Secure",
          first_payload);
      const auto second_cookie = std::format("refresh={}; Path=/auth; HttpOnly; SameSite=Lax; Secure", second_payload);

      const auto headers_buffer = generate_headers_buffer({
        {"Set-Cookie", first_cookie},
        {"Set-Cookie", second_cookie},
      });

      auto parsed = http::headers::parse(headers_buffer);
      expect[parsed.has_value()];

      auto& headers = *parsed;

      expect(headers.size() == 2);

      auto set_cookie_values = get_all_header_values(headers, "SET-COOKIE");
      expect[set_cookie_values.size() == 2];
      expect(set_cookie_values[0] == first_cookie);
      expect(set_cookie_values[1] == second_cookie);
    };
  };
}
