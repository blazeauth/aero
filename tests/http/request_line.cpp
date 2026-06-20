#include <format>
#include <string>
#include <string_view>
#include <ut/ut.hpp>

#include "aero/http/error.hpp"
#include "aero/http/method.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/version.hpp"

using namespace ut;

namespace http = aero::http;
using http::protocol_error;
using http::request_line;

int main() {
  suite http_request_line = [] {
    "serializes a trivial request line"_test = [] {
      for (http::method method : http::methods) {
        request_line line{
          .method = method,
          .target = "/products",
          .version = http::version::http1_1,
        };

        auto expected = std::format("{} /products HTTP/1.1\r\n", http::to_string(method));
        expect(line.serialize() == expected);
      }
    };

    "returns an empty string for an unknown method"_test = [] {
      request_line line{
        .method = static_cast<http::method>(0xFF),
        .target = "/products",
        .version = http::version::http1_1,
      };

      expect(line.serialize().empty());
    };

    "returns an empty string for an unknown version"_test = [] {
      request_line line{
        .method = http::method::get,
        .target = "/products",
        .version = static_cast<http::version>(0xFF),
      };

      expect(line.serialize().empty());
    };

    "serializes an empty request line as an empty string"_test = [] {
      request_line line{};
      expect(line.serialize().empty());
    };

    "parses a trivial request line"_test = [] {
      for (http::method method : http::methods) {
        request_line expected{
          .method = method,
          .target = "/products",
          .version = http::version::http1_1,
        };

        auto text = std::format("{} /products HTTP/1.1", http::to_string(method));
        expect(expected == request_line::parse(text));
      }
    };

    "parses a request line with a crlf suffix"_test = [] {
      request_line expected{
        .method = http::method::get,
        .target = "/products",
        .version = http::version::http1_1,
      };

      expect(expected == request_line::parse("GET /products HTTP/1.1\r\n"));
    };

    "rejects empty string"_test = [] {
      auto parsed = request_line::parse("");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects only crlf"_test = [] {
      auto parsed = request_line::parse("\r\n");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects lf-only line ending"_test = [] {
      auto parsed = request_line::parse("GET /products HTTP/1.1\n");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects embedded crlf inside line"_test = [] {
      auto parsed = request_line::parse("GET /products\r\nHTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects leading space before method"_test = [] {
      auto parsed = request_line::parse(" GET /products HTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects missing spaces between tokens"_test = [] {
      auto parsed = request_line::parse("GET/productsHTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects missing second space"_test = [] {
      auto parsed = request_line::parse("GET /productsHTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects double space between method and target"_test = [] {
      auto parsed = request_line::parse("GET  /products HTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects double space between target and version"_test = [] {
      auto parsed = request_line::parse("GET /products  HTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects trailing space after version"_test = [] {
      auto parsed = request_line::parse("GET /products HTTP/1.1 ");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects extra token"_test = [] {
      auto parsed = request_line::parse("GET /products HI HTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::request_line_invalid);
    };

    "rejects an invalid http version"_test = [] {
      auto parsed = request_line::parse("GET /products HTP/1.8");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::version_invalid);
    };

    "rejects an invalid http method"_test = [] {
      auto parsed = request_line::parse("POP /products HTTP/1.1");
      expect[not parsed.has_value()];
      expect(parsed.error() == protocol_error::method_invalid);
    };

    "accepts target without leading slash"_test = [] {
      auto parsed = request_line::parse("GET products HTTP/1.1");
      expect[parsed.has_value()];
      expect(parsed->method == http::method::get);
      expect(parsed->target == "products");
      expect(parsed->version == http::version::http1_1);
    };

    "accepts asterisk target"_test = [] {
      auto parsed = request_line::parse("OPTIONS * HTTP/1.1");
      expect[parsed.has_value()];
      expect(parsed->method == http::method::options);
      expect(parsed->target == "*");
      expect(parsed->version == http::version::http1_1);
    };
  };
}
