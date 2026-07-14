#include <format>
#include <iomanip>
#include <string>
#include <string_view>
#include <ut/ut.hpp>

#include "aero/http/error.hpp"
#include "aero/http/method.hpp"
#include "aero/http/request_line.hpp"
#include "aero/http/version.hpp"

using namespace ut;

namespace http = aero::http;
using http::method, http::version, http::request_line, http::protocol_error;

bool parses_to(std::string_view line, http::method method, std::string_view target, http::version version) {
  auto result = http::request_line::parse(line);
  expect(result.has_value()) << "expected success for: " << line;
  if (not result.has_value()) {
    return false;
  }
  return result->method == method && result->target == target && result->version == version;
}

void expect_rejected(std::string_view line, std::error_code ec) {
  auto result = http::request_line::parse(line);
  expect(not result.has_value()) << "expected rejection for: " << line;
  if (result.has_value()) {
    return;
  }
  expect(result.error() == ec) << "wrong error code" << result.error() << ", expected" << ec << "for:" << std::quoted(line);
}

int main() {
  suite http_request_line_parser = [] {
    "parses a trivial request line for every implemented method"_test = [] {
      for (http::method method : http::methods) {
        std::string line;
        line += http::to_string(method);
        line += " /products HTTP/1.1";
        expect(parses_to(line, method, "/products", version::http1_1));
      }
    };

    "rejects additional well-formed HTTP versions with version_unsupported error"_test = [] {
      expect_rejected("GET /products HTTP/2.0", protocol_error::version_unsupported);
      expect_rejected("GET /products HTTP/0.9", protocol_error::version_unsupported);
      expect_rejected("GET /products HTTP/9.9", protocol_error::version_unsupported);
      expect_rejected("GET /products HTTP/0.0", protocol_error::version_unsupported);
    };

    "parses higher HTTP/1.x minor versions as is_http11 per RFC 9110 Section 6.2"_test = [] {
      for (char minor = '2'; minor <= '9'; ++minor) {
        std::string line = "GET /products HTTP/1.";
        line.push_back(minor);

        expect(parses_to(line, method::GET, "/products", version::http1_1)) << "failed to parse as http11: " << line;
      }
    };

    "parses asterisk-form for OPTIONS"_test = [] {
      expect(parses_to("OPTIONS * HTTP/1.1", method::OPTIONS, "*", version::http1_1));
    };

    "parses origin-form for OPTIONS"_test = [] {
      expect(parses_to("OPTIONS /products HTTP/1.1", method::OPTIONS, "/products", version::http1_1));
    };

    "parses absolute-form for OPTIONS"_test = [] {
      expect(parses_to("OPTIONS http://www.example.org:8001 HTTP/1.1",
        method::OPTIONS,
        "http://www.example.org:8001",
        version::http1_1));
    };

    "rejects empty and terminated line inputs"_test = [] {
      expect_rejected("", protocol_error::request_line_invalid);
      expect_rejected("\r\n", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1\r\n", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1\n", protocol_error::request_line_invalid);
      expect_rejected("GET /products\r\nHTTP/1.1", protocol_error::request_line_invalid);
    };

    "rejects crlf, cr and lf suffixes"_test = [] {
      expect_rejected("GET /products HTTP/1.1\r\n", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1\r", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1\n", protocol_error::request_line_invalid);
    };

    "rejects incomplete request-line shapes"_test = [] {
      expect_rejected("GET/productsHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET/products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("POP HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products", protocol_error::request_line_invalid);
      expect_rejected("GET /products ", protocol_error::request_line_invalid);
      expect_rejected("GET  HTTP/1.1", protocol_error::request_line_invalid);
    };

    "rejects request-line spacing that is not exactly method SP target SP version"_test = [] {
      expect_rejected(" GET /products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET  /products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /productsHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products  HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1 ", protocol_error::request_line_invalid);
      expect_rejected("GET /products HI HTTP/1.1", protocol_error::request_line_invalid);
    };

    "rejects non-SP whitespace around request-line components"_test = [] {
      expect_rejected("\tGET /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GET\t/products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products\tHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET\v/products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products\vHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET\f/products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products\fHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET\r/products HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products\rHTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /products HTTP/1.1\t", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1.1\r", protocol_error::request_line_invalid);
    };

    "rejects malformed HTTP-version"_test = [] {
      expect_rejected("GET /products HTP/1.8", protocol_error::version_invalid);
      expect_rejected("GET /products http/1.1", protocol_error::version_invalid);
      expect_rejected("GET /products Http/1.1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/11.1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1.11", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1.", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/.1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1-1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/x.y", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/a.1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1.a", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1.1.0", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP//1.1", protocol_error::version_invalid);
      expect_rejected("GET /products HTTP/1/1", protocol_error::version_invalid);
    };

    "rejects control and non-ascii bytes in HTTP-version"_test = [] {
      for (char byte : {'\0', '\x01', '\x1f', '\x7f'}) {
        std::string line = "GET /products HTTP/1.";
        line.push_back(byte);
        expect_rejected(line, protocol_error::version_invalid);
      }

      std::string line = "GET /products HTTP/1.";
      line.push_back(static_cast<char>(0x80));
      expect_rejected(line, protocol_error::version_invalid);
    };

    "rejects valid but unsupported method tokens with 501"_test = [] {
      expect_rejected("POP /products HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("get /products HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("M-SEARCH /products HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("123 /products HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("!#$%&'*+-.^_`|~ /products HTTP/1.1", protocol_error::method_unsupported);
    };

    "rejects CONNECT method as unsupported"_test = [] {
      // aero does not implement the CONNECT method, so its authority-form
      // request-target is rejected at the method parser stage
      expect_rejected("CONNECT example.com:443 HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("CONNECT [2001:db8::1]:443 HTTP/1.1", protocol_error::method_unsupported);
    };

    "rejects unsupported method tokens before request-target validation"_test = [] {
      expect_rejected("POP * HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("POP products HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("M-SEARCH * HTTP/1.1", protocol_error::method_unsupported);
      expect_rejected("PRI * HTTP/2.0", protocol_error::method_unsupported);
    };

    "rejects delimiter characters inside method token"_test = [] {
      expect_rejected("GE/T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE:T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE;T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE=T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE?T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE@T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE[T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE]T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE{T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE}T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE\\T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE\"T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE<T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE>T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE(T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE)T /products HTTP/1.1", protocol_error::method_invalid);
      expect_rejected("GE,T /products HTTP/1.1", protocol_error::method_invalid);
    };

    "rejects control and non-ascii bytes in method"_test = [] {
      for (char byte : {'\0', '\x01', '\x1f', '\x7f'}) {
        std::string line = "GE";
        line.push_back(byte);
        line += "T /products HTTP/1.1";
        expect_rejected(line, protocol_error::method_invalid);
      }

      std::string line = "GE";
      line.push_back(static_cast<char>(0x80));
      line += "T /products HTTP/1.1";
      expect_rejected(line, protocol_error::method_invalid);
    };

    "rejects request-targets that match no RFC 9112 form for implemented methods"_test = [] {
      expect_rejected("GET products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET ?x=1 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET example.com:443 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("OPTIONS example.com:443 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET ** HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("OPTIONS ** HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("OPTIONS *?x=1 HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects asterisk-form for implemented non-OPTIONS methods"_test = [] {
      expect_rejected("GET * HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("POST * HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("HEAD * HTTP/1.1", protocol_error::request_target_invalid);
    };

    "parses valid origin-form request-target edge cases"_test = [] {
      expect(parses_to("GET / HTTP/1.1", method::GET, "/", version::http1_1));
      expect(parses_to("GET // HTTP/1.1", method::GET, "//", version::http1_1));
      expect(parses_to("GET /// HTTP/1.1", method::GET, "///", version::http1_1));
      expect(parses_to("GET /products/ HTTP/1.1", method::GET, "/products/", version::http1_1));
      expect(parses_to("GET /products//42 HTTP/1.1", method::GET, "/products//42", version::http1_1));
      expect(parses_to("GET /. HTTP/1.1", method::GET, "/.", version::http1_1));
      expect(parses_to("GET /.. HTTP/1.1", method::GET, "/..", version::http1_1));
      expect(parses_to("GET /a/../b HTTP/1.1", method::GET, "/a/../b", version::http1_1));
      expect(parses_to("GET /products;id=123 HTTP/1.1", method::GET, "/products;id=123", version::http1_1));
      expect(parses_to("GET /a:b@c HTTP/1.1", method::GET, "/a:b@c", version::http1_1));
      expect(parses_to("GET /a!$&'()*+,;=:@-._~ HTTP/1.1", method::GET, "/a!$&'()*+,;=:@-._~", version::http1_1));
      expect(parses_to("GET /a%2Fb HTTP/1.1", method::GET, "/a%2Fb", version::http1_1));
      expect(parses_to("GET /a%2fb HTTP/1.1", method::GET, "/a%2fb", version::http1_1));
      expect(parses_to("GET /a%00b HTTP/1.1", method::GET, "/a%00b", version::http1_1));
      expect(parses_to("GET /a%23b HTTP/1.1", method::GET, "/a%23b", version::http1_1));
      expect(parses_to("GET /%5B%5D HTTP/1.1", method::GET, "/%5B%5D", version::http1_1));
      expect(parses_to("GET /%D0%BF%D1%83%D1%82%D1%8C HTTP/1.1", method::GET, "/%D0%BF%D1%83%D1%82%D1%8C", version::http1_1));
      expect(parses_to("GET /search? HTTP/1.1", method::GET, "/search?", version::http1_1));
      expect(parses_to("GET /search?q HTTP/1.1", method::GET, "/search?q", version::http1_1));
      expect(parses_to("GET /search?q= HTTP/1.1", method::GET, "/search?q=", version::http1_1));
      expect(parses_to("GET /search?=value HTTP/1.1", method::GET, "/search?=value", version::http1_1));
      expect(parses_to("GET /search?q=a/b?c=d&x=1 HTTP/1.1", method::GET, "/search?q=a/b?c=d&x=1", version::http1_1));
      expect(parses_to("GET /search?x=!$&'()*+,;=:@-._~/? HTTP/1.1",
        method::GET,
        "/search?x=!$&'()*+,;=:@-._~/?",
        version::http1_1));
      expect(parses_to("GET /search?x=100%25 HTTP/1.1", method::GET, "/search?x=100%25", version::http1_1));
      expect(
        parses_to("GET /search?x=%23&y=%3F&z=%5B%5D HTTP/1.1", method::GET, "/search?x=%23&y=%3F&z=%5B%5D", version::http1_1));
      expect(parses_to("GET /* HTTP/1.1", method::GET, "/*", version::http1_1));
    };

    "rejects raw whitespace inside request-target"_test = [] {
      // The space inside the target gives the line a third SP, which no
      // well-formed request-line can have, so this is reported as a format
      // violation rather than a bad target
      expect_rejected("GET /a b HTTP/1.1", protocol_error::request_line_invalid);
      expect_rejected("GET /a\tb HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a\vb HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a\fb HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects control characters in request-target"_test = [] {
      for (char byte : {'\r', '\n', '\0', '\x01', '\x1f', '\x7f'}) {
        std::string line = "GET /a";
        line.push_back(byte);
        line += "b HTTP/1.1";
        expect_rejected(line, protocol_error::request_target_invalid);
      }
    };

    "rejects raw non-ascii bytes in request-target"_test = [] {
      std::string single_byte_line = "GET /a";
      single_byte_line.push_back(static_cast<char>(0x80));
      single_byte_line += "b HTTP/1.1";
      expect_rejected(single_byte_line, protocol_error::request_target_invalid);

      std::string utf8_line = "GET /caf";
      utf8_line.push_back(static_cast<char>(0xc3));
      utf8_line.push_back(static_cast<char>(0xa9));
      utf8_line += " HTTP/1.1";
      expect_rejected(utf8_line, protocol_error::request_target_invalid);
    };

    "rejects raw characters not allowed in origin-form path"_test = [] {
      expect_rejected("GET /a\"b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a<b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a>b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a[b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a]b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a\\b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a^b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a`b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a{b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a|b HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a}b HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects raw characters not allowed in origin-form query"_test = [] {
      expect_rejected("GET /a?x=\" HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=< HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=> HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=[ HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=] HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=\\ HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=^ HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=` HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x={ HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=| HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /a?x=} HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects fragment marker in origin-form"_test = [] {
      expect_rejected("GET /products#section HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products?x=1#section HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /#section HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects malformed percent-encoding in origin-form path"_test = [] {
      expect_rejected("GET /% HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /%0 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /%GG HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /%2G HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /%u1234 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products% HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products%0 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products%GG HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products%2G HTTP/1.1", protocol_error::request_target_invalid);
    };

    "rejects malformed percent-encoding in origin-form query"_test = [] {
      expect_rejected("GET /products?x=% HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products?x=%0 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products?x=%GG HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products?x=%2G HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET /products?x=%u1234 HTTP/1.1", protocol_error::request_target_invalid);
    };

    "parses valid absolute-form request-targets"_test = [] {
      expect(parses_to("GET http://example.com HTTP/1.1", method::GET, "http://example.com", version::http1_1));
      expect(parses_to("GET http://example.com/ HTTP/1.1", method::GET, "http://example.com/", version::http1_1));
      expect(parses_to("GET http://example.com?x=1 HTTP/1.1", method::GET, "http://example.com?x=1", version::http1_1));
      expect(parses_to("GET http://example.com/products?x=1 HTTP/1.1",
        method::GET,
        "http://example.com/products?x=1",
        version::http1_1));
      expect(parses_to("GET https://example.com HTTP/1.1", method::GET, "https://example.com", version::http1_1));
      expect(parses_to("GET https://example.com:443/products HTTP/1.1",
        method::GET,
        "https://example.com:443/products",
        version::http1_1));
      expect(parses_to("GET http://127.0.0.1:8080/products HTTP/1.1",
        method::GET,
        "http://127.0.0.1:8080/products",
        version::http1_1));
      expect(parses_to("GET http://[2001:db8::1]:8080/products HTTP/1.1",
        method::GET,
        "http://[2001:db8::1]:8080/products",
        version::http1_1));
      expect(
        parses_to("GET HTTP://EXAMPLE.COM/products HTTP/1.1", method::GET, "HTTP://EXAMPLE.COM/products", version::http1_1));
    };

    "rejects invalid absolute-form request-targets"_test = [] {
      expect_rejected("GET http:///products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET https:///products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://user@example.com/products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://example.com/products#section HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://example.com/products%GG HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://example.com/%u1234 HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://example.com:abc/products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://[2001:db8::1/products HTTP/1.1", protocol_error::request_target_invalid);
      expect_rejected("GET http://example.com/a[b HTTP/1.1", protocol_error::request_target_invalid);
    };
  };

  suite http_request_line_serialization = [] {
    "serializes a trivial request line"_test = [] {
      for (http::method method : http::methods) {
        request_line line{.method = method, .target = "/products", .version = version::http1_1};
        std::string expected{http::to_string(method)};
        expected += " /products HTTP/1.1\r\n";

        expect(line.serialize() == expected);
      }
    };

    "returns an empty string for an unknown method"_test = [] {
      request_line line{.method = static_cast<http::method>(0xFF), .target = "/products", .version = version::http1_1};
      expect(line.serialize().empty());
    };

    "returns an empty string for an unknown version"_test = [] {
      request_line line{.method = method::GET, .target = "/products", .version = static_cast<http::version>(0xFF)};
      expect(line.serialize().empty());
    };

    "serializes an empty request line as an empty string"_test = [] {
      request_line line{};
      expect(line.serialize().empty());
    };
  };
}
