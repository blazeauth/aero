#include <cstdint>
#include <optional>
#include <string_view>
#include <ut/ut.hpp>

#include "aero/http/error.hpp"
#include "aero/http/uri.hpp"

using namespace ut;

namespace http = aero::http;
using http::uri_error;

void expect_parse_error(std::string_view uri_text, uri_error expected_error) {
  auto parsed = http::uri::parse(uri_text);
  expect[not parsed.has_value()];
  expect(parsed.error() == expected_error);
}

http::uri parse_or_fail(std::string_view uri_text) {
  auto parsed = http::uri::parse(uri_text);
  expect(parsed.has_value());
  if (!parsed) {
    return http::uri{};
  }
  return *parsed;
}

int main() {
  suite http_uri = [] {
    "parses http scheme case-insensitively"_test = [] {
      http::uri parsed = parse_or_fail("HTTP://example.com");
      expect(parsed.scheme() == "http");
      expect(parsed.host() == "example.com");
      expect(parsed.port() == 80);
      expect(parsed.target() == "/");
      expect(parsed.to_string() == "http://example.com/");
      expect(not parsed.validate());
    };

    "parses https scheme case-insensitively and uses default port 443"_test = [] {
      http::uri parsed = parse_or_fail("hTtPs://example.com/users");
      expect(parsed.scheme() == "https");
      expect(parsed.host() == "example.com");
      expect(parsed.port() == 443);
      expect(parsed.path() == "users");
      expect(parsed.target() == "/users");
      expect(parsed.to_string() == "https://example.com/users");
      expect(not parsed.validate());
    };

    "parses explicit port when provided"_test = [] {
      http::uri parsed = parse_or_fail("http://example.com:8080/api");
      expect(parsed.port() == 8080);
      expect(parsed.target() == "/api");
      expect(parsed.to_string() == "http://example.com:8080/api");
    };

    "requires scheme delimiter"_test = [] {
      expect_parse_error("http//example.com", uri_error::scheme_delimiter_missing);
      expect_parse_error("http:example.com", uri_error::scheme_delimiter_missing);
      expect_parse_error("://example.com", uri_error::scheme_delimiter_missing);
    };

    "rejects non-http schemes"_test = [] {
      expect_parse_error("ws://example.com/", uri_error::scheme_invalid);
      expect_parse_error("wss://example.com/", uri_error::scheme_invalid);
      expect_parse_error("ftp://example.com/", uri_error::scheme_invalid);
    };

    "requires non-empty authority"_test = [] {
      expect_parse_error("http:///api", uri_error::authority_empty);
      expect_parse_error("https://", uri_error::authority_empty);
    };

    "rejects userinfo in authority"_test = [] {
      expect_parse_error("http://user@example.com/api", uri_error::userinfo_not_allowed);
      expect_parse_error("http://user:pass@example.com/api", uri_error::userinfo_not_allowed);
    };

    "rejects fragment identifiers everywhere"_test = [] {
      expect_parse_error("http://example.com/#frag", uri_error::fragment_not_allowed);
      expect_parse_error("http://example.com/api#frag", uri_error::fragment_not_allowed);
      expect_parse_error("http://example.com/api?x=1#frag", uri_error::fragment_not_allowed);
    };

    "accepts escaped hash in query"_test = [] {
      http::uri parsed = parse_or_fail("http://example.com/api?topic=%23general");
      expect(parsed.target() == "/api?topic=%23general");
      expect(parsed.to_string() == "http://example.com/api?topic=%23general");
    };

    "target uses slash when path is empty and preserves query delimiter"_test = [] {
      http::uri parsed = parse_or_fail("http://example.com?token=abc");
      expect(parsed.target() == "/?token=abc");
      expect(parsed.to_string() == "http://example.com/?token=abc");
    };

    "preserves empty query delimiter"_test = [] {
      http::uri parsed = parse_or_fail("http://example.com?");
      expect(parsed.has_query());
      expect(parsed.query().empty());
      expect(parsed.target() == "/?");
      expect(parsed.to_string() == "http://example.com/?");
    };

    "parses ipv6 literal host"_test = [] {
      http::uri parsed = parse_or_fail("http://[2001:db8::1]/api");
      expect(parsed.scheme() == "http");
      expect(parsed.host() == "[2001:db8::1]");
      expect(parsed.port() == 80);
      expect(parsed.target() == "/api");
      expect(parsed.to_string() == "http://[2001:db8::1]/api");
    };

    "rejects ipv6 literal without closing bracket"_test = [] {
      expect_parse_error("http://[2001:db8::1/api", uri_error::ipv6_literal_invalid);
      expect_parse_error("http://[2001:db8::1", uri_error::ipv6_literal_invalid);
    };

    "rejects empty ipv6 literal"_test = [] {
      expect_parse_error("http://[]/api", uri_error::ipv6_literal_invalid);
      expect_parse_error("http://[/api", uri_error::ipv6_literal_invalid);
    };

    "rejects empty non-numeric zero or out of range ports"_test = [] {
      expect_parse_error("http://example.com:/api", uri_error::port_empty);
      expect_parse_error("http://example.com:abc/api", uri_error::port_invalid);
      expect_parse_error("http://example.com:0/api", uri_error::port_invalid);
      expect_parse_error("http://example.com:70000/api", uri_error::port_out_of_range);
    };

    "default port depends on scheme when port is omitted"_test = [] {
      http::uri http_uri = parse_or_fail("http://example.com/api");
      http::uri https_uri = parse_or_fail("https://example.com/api");

      expect(http_uri.port() == 80);
      expect(https_uri.port() == 443);
    };

    "validate rejects invalid constructed components"_test = [] {
      http::uri invalid_scheme_uri(http::uri_parts{
        .scheme = "ftp",
        .host = "example.com",
        .port = std::nullopt,
        .path = "api",
        .query = {},
        .has_query = false,
      });
      expect(invalid_scheme_uri.validate() == uri_error::scheme_invalid);

      http::uri fragment_in_query_uri(http::uri_parts{
        .scheme = "http",
        .host = "example.com",
        .port = std::nullopt,
        .path = "api",
        .query = "x=1#frag",
        .has_query = true,
      });
      expect(fragment_in_query_uri.validate() == uri_error::fragment_not_allowed);

      http::uri userinfo_in_host_uri(http::uri_parts{
        .scheme = "http",
        .host = "user@example.com",
        .port = std::nullopt,
        .path = "api",
        .query = {},
        .has_query = false,
      });
      expect(userinfo_in_host_uri.validate() == uri_error::userinfo_not_allowed);

      http::uri invalid_port_uri(http::uri_parts{
        .scheme = "http",
        .host = "example.com",
        .port = static_cast<std::uint16_t>(0),
        .path = "api",
        .query = {},
        .has_query = false,
      });
      expect(invalid_port_uri.validate() == uri_error::port_invalid);

      http::uri invalid_path_uri(http::uri_parts{
        .scheme = "http",
        .host = "example.com",
        .port = std::nullopt,
        .path = "api?bad",
        .query = {},
        .has_query = false,
      });
      expect(invalid_path_uri.validate() == uri_error::path_invalid);
    };
  };
}
