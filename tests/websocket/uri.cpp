#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string_view>

#include "aero/websocket/error.hpp"
#include "aero/websocket/uri.hpp"

namespace ws = aero::websocket;
using ws::error::uri_error;

namespace {

  void expect_parse_error(std::string_view uri_text, uri_error expected_error) {
    auto parsed = ws::uri::parse(uri_text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), expected_error);
  }

  ws::uri parse_or_fail(std::string_view uri_text) {
    auto parsed = ws::uri::parse(uri_text);
    EXPECT_TRUE(parsed);
    if (!parsed) {
      return ws::uri{};
    }
    return *parsed;
  }

} // namespace

TEST(WebSocketUri, ParsesWsSchemeCaseInsensitively) {
  ws::uri parsed = parse_or_fail("WS://example.com");
  EXPECT_EQ(parsed.scheme(), "ws");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 80);
  EXPECT_EQ(parsed.to_string(), "ws://example.com/");
  EXPECT_FALSE(parsed.validate());
}

TEST(WebSocketUri, ParsesWssSchemeCaseInsensitivelyAndUsesDefaultPort443) {
  ws::uri parsed = parse_or_fail("wSs://example.com/chat");
  EXPECT_EQ(parsed.scheme(), "wss");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 443);
  EXPECT_EQ(parsed.to_string(), "wss://example.com/chat");
  EXPECT_FALSE(parsed.validate());
}

TEST(WebSocketUri, ParsesExplicitPortWhenProvided) {
  ws::uri parsed = parse_or_fail("ws://example.com:8080/chat");
  EXPECT_EQ(parsed.port(), 8080);
  EXPECT_EQ(parsed.to_string(), "ws://example.com:8080/chat");
}

TEST(WebSocketUri, RequiresSchemeDelimiter) {
  expect_parse_error("ws//example.com", uri_error::missing_scheme_delimiter);
  expect_parse_error("ws:example.com", uri_error::missing_scheme_delimiter);
  expect_parse_error("://example.com", uri_error::missing_scheme_delimiter);
}

TEST(WebSocketUri, RejectsNonWebSocketSchemes) {
  expect_parse_error("http://example.com/", uri_error::invalid_scheme);
  expect_parse_error("https://example.com/", uri_error::invalid_scheme);
  expect_parse_error("ftp://example.com/", uri_error::invalid_scheme);
}

TEST(WebSocketUri, RequiresNonEmptyAuthority) {
  expect_parse_error("ws:///chat", uri_error::empty_authority);
  expect_parse_error("wss://", uri_error::empty_authority);
}

TEST(WebSocketUri, RejectsUserinfoInAuthority) {
  expect_parse_error("ws://user@example.com/chat", uri_error::userinfo_not_allowed);
  expect_parse_error("ws://user:pass@example.com/chat", uri_error::userinfo_not_allowed);
}

TEST(WebSocketUri, RejectsFragmentIdentifiersEverywhere) {
  expect_parse_error("ws://example.com/#frag", uri_error::fragment_not_allowed);
  expect_parse_error("ws://example.com/chat#frag", uri_error::fragment_not_allowed);
  expect_parse_error("ws://example.com/chat?x=1#frag", uri_error::fragment_not_allowed);
}

TEST(WebSocketUri, AcceptsEscapedHashInQuery) {
  ws::uri parsed = parse_or_fail("ws://example.com/chat?topic=%23general");
  EXPECT_EQ(parsed.to_string(), "ws://example.com/chat?topic=%23general");
}

TEST(WebSocketUri, ResourceNameUsesSlashWhenPathIsEmptyAndPreservesQueryDelimiter) {
  ws::uri parsed = parse_or_fail("ws://example.com?token=abc");
  EXPECT_EQ(parsed.to_string(), "ws://example.com/?token=abc");
}

TEST(WebSocketUri, ParsesIpv6LiteralHost) {
  ws::uri parsed = parse_or_fail("ws://[2001:db8::1]/chat");
  EXPECT_EQ(parsed.scheme(), "ws");
  EXPECT_EQ(parsed.host(), "[2001:db8::1]");
  EXPECT_EQ(parsed.port(), 80);
  EXPECT_EQ(parsed.to_string(), "ws://[2001:db8::1]/chat");
}

TEST(WebSocketUri, RejectsIpv6LiteralWithoutClosingBracket) {
  expect_parse_error("ws://[2001:db8::1/chat", uri_error::invalid_ipv6_literal);
  expect_parse_error("ws://[2001:db8::1", uri_error::invalid_ipv6_literal);
}

TEST(WebSocketUri, RejectsEmptyIpv6Literal) {
  expect_parse_error("ws://[]/chat", uri_error::invalid_ipv6_literal);
  expect_parse_error("ws://[/chat", uri_error::invalid_ipv6_literal);
}

TEST(WebSocketUri, RejectsPortThatIsEmptyNonNumericZeroOrOutOfRange) {
  expect_parse_error("ws://example.com:/chat", uri_error::empty_port);
  expect_parse_error("ws://example.com:abc/chat", uri_error::invalid_port);
  expect_parse_error("ws://example.com:0/chat", uri_error::invalid_port);
  expect_parse_error("ws://example.com:70000/chat", uri_error::port_out_of_range);
}

TEST(WebSocketUri, DefaultPortDependsOnSchemeWhenPortIsOmitted) {
  ws::uri ws_uri = parse_or_fail("ws://example.com/chat");
  ws::uri wss_uri = parse_or_fail("wss://example.com/chat");

  EXPECT_EQ(ws_uri.port(), 80);
  EXPECT_EQ(wss_uri.port(), 443);
}

TEST(WebSocketUri, ValidateRejectsInvalidComponentsInConstructedParts) {
  ws::uri invalid_scheme_uri(ws::uri_parts{
    .scheme = "http",
    .host = "example.com",
    .port = std::nullopt,
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(invalid_scheme_uri.validate(), uri_error::invalid_scheme);

  ws::uri fragment_in_path_uri(ws::uri_parts{
    .scheme = "ws",
    .host = "example.com",
    .port = std::nullopt,
    .path = "chat#frag",
    .query = {},
  });
  EXPECT_EQ(fragment_in_path_uri.validate(), uri_error::fragment_not_allowed);

  ws::uri userinfo_in_host_uri(ws::uri_parts{
    .scheme = "ws",
    .host = "user@example.com",
    .port = std::nullopt,
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(userinfo_in_host_uri.validate(), uri_error::userinfo_not_allowed);

  ws::uri invalid_port_uri(ws::uri_parts{
    .scheme = "ws",
    .host = "example.com",
    .port = static_cast<std::uint16_t>(0),
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(invalid_port_uri.validate(), uri_error::invalid_port);
}
