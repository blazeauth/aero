#include <cstdint>
#include <expected>
#include <string_view>
#include <ut/ut.hpp>

#include "aero/websocket/error.hpp"
#include "aero/websocket/uri.hpp"

using namespace ut;

namespace websocket = aero::websocket;

using aero::websocket::uri_error;

void expect_parse_error(std::string_view uri_text, uri_error expected_error) {
  auto parsed = websocket::uri::parse(uri_text);
  expect[not parsed.has_value()];
  expect(parsed.error() == expected_error);
}

websocket::uri parse_or_fail(std::string_view uri_text) {
  auto parsed = websocket::uri::parse(uri_text);
  expect(parsed.has_value());
  if (!parsed) {
    return websocket::uri{};
  }
  return *parsed;
}

int main() {
  suite websocket_uri = [] {
    "parses ws scheme case-insensitively"_test = [] {
      websocket::uri parsed = parse_or_fail("WS://example.com");
      expect(parsed.scheme() == "ws");
      expect(parsed.host() == "example.com");
      expect(parsed.port() == 80);
      expect(parsed.to_string() == "ws://example.com/");
      expect(not parsed.validate());
    };

    "parses wss scheme case-insensitively and uses default port 443"_test = [] {
      websocket::uri parsed = parse_or_fail("wSs://example.com/chat");
      expect(parsed.scheme() == "wss");
      expect(parsed.host() == "example.com");
      expect(parsed.port() == 443);
      expect(parsed.to_string() == "wss://example.com/chat");
      expect(not parsed.validate());
    };

    "parses explicit port when provided"_test = [] {
      websocket::uri parsed = parse_or_fail("ws://example.com:8080/chat");
      expect(parsed.port() == 8080);
      expect(parsed.to_string() == "ws://example.com:8080/chat");
    };

    "requires scheme delimiter"_test = [] {
      expect_parse_error("ws//example.com", uri_error::scheme_delimiter_missing);
      expect_parse_error("ws:example.com", uri_error::scheme_delimiter_missing);
      expect_parse_error("://example.com", uri_error::scheme_delimiter_missing);
    };

    "rejects non-websocket schemes"_test = [] {
      expect_parse_error("http://example.com/", uri_error::scheme_invalid);
      expect_parse_error("https://example.com/", uri_error::scheme_invalid);
      expect_parse_error("ftp://example.com/", uri_error::scheme_invalid);
    };

    "requires non-empty authority"_test = [] {
      expect_parse_error("ws:///chat", uri_error::authority_empty);
      expect_parse_error("wss://", uri_error::authority_empty);
    };

    "rejects userinfo in authority"_test = [] {
      expect_parse_error("ws://user@example.com/chat", uri_error::userinfo_not_allowed);
      expect_parse_error("ws://user:pass@example.com/chat", uri_error::userinfo_not_allowed);
    };

    "rejects fragment identifiers everywhere"_test = [] {
      expect_parse_error("ws://example.com/#frag", uri_error::fragment_not_allowed);
      expect_parse_error("ws://example.com/chat#frag", uri_error::fragment_not_allowed);
      expect_parse_error("ws://example.com/chat?x=1#frag", uri_error::fragment_not_allowed);
    };

    "accepts escaped hash in query"_test = [] {
      websocket::uri parsed = parse_or_fail("ws://example.com/chat?topic=%23general");
      expect(parsed.to_string() == "ws://example.com/chat?topic=%23general");
    };

    "resource name uses slash when path is empty and preserves query delimiter"_test = [] {
      websocket::uri parsed = parse_or_fail("ws://example.com?token=abc");
      expect(parsed.to_string() == "ws://example.com/?token=abc");
    };

    "parses ipv6 literal host"_test = [] {
      websocket::uri parsed = parse_or_fail("ws://[2001:db8::1]/chat");
      expect(parsed.scheme() == "ws");
      expect(parsed.host() == "[2001:db8::1]");
      expect(parsed.port() == 80);
      expect(parsed.to_string() == "ws://[2001:db8::1]/chat");
    };

    "rejects ipv6 literal without closing bracket"_test = [] {
      expect_parse_error("ws://[2001:db8::1/chat", uri_error::ipv6_literal_invalid);
      expect_parse_error("ws://[2001:db8::1", uri_error::ipv6_literal_invalid);
    };

    "rejects empty ipv6 literal"_test = [] {
      expect_parse_error("ws://[]/chat", uri_error::ipv6_literal_invalid);
      expect_parse_error("ws://[/chat", uri_error::ipv6_literal_invalid);
    };

    "rejects empty non-numeric zero or out of range ports"_test = [] {
      expect_parse_error("ws://example.com:/chat", uri_error::port_empty);
      expect_parse_error("ws://example.com:abc/chat", uri_error::port_invalid);
      expect_parse_error("ws://example.com:0/chat", uri_error::port_invalid);
      expect_parse_error("ws://example.com:70000/chat", uri_error::port_out_of_range);
    };

    "default port depends on scheme when port is omitted"_test = [] {
      websocket::uri ws_uri = parse_or_fail("ws://example.com/chat");
      websocket::uri wss_uri = parse_or_fail("wss://example.com/chat");

      expect(ws_uri.port() == 80);
      expect(wss_uri.port() == 443);
    };

    "validate rejects invalid constructed components"_test = [] {
      websocket::uri invalid_scheme_uri(websocket::uri_parts{
        .scheme = "http",
        .host = "example.com",
        .port = std::nullopt,
        .path = "chat",
        .query = {},
      });
      expect(invalid_scheme_uri.validate() == uri_error::scheme_invalid);

      websocket::uri fragment_in_path_uri(websocket::uri_parts{
        .scheme = "ws",
        .host = "example.com",
        .port = std::nullopt,
        .path = "chat#frag",
        .query = {},
      });
      expect(fragment_in_path_uri.validate() == uri_error::fragment_not_allowed);

      websocket::uri userinfo_in_host_uri(websocket::uri_parts{
        .scheme = "ws",
        .host = "user@example.com",
        .port = std::nullopt,
        .path = "chat",
        .query = {},
      });
      expect(userinfo_in_host_uri.validate() == uri_error::userinfo_not_allowed);

      websocket::uri invalid_port_uri(websocket::uri_parts{
        .scheme = "ws",
        .host = "example.com",
        .port = static_cast<std::uint16_t>(0),
        .path = "chat",
        .query = {},
      });
      expect(invalid_port_uri.validate() == uri_error::port_invalid);
    };
  };
}
