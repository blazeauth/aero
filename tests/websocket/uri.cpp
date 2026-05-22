#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string_view>

#include "aero/websocket/error.hpp"
#include "aero/websocket/uri.hpp"

namespace {

  namespace websocket = aero::websocket;

  using aero::websocket::uri_error;

  void expect_parse_error(std::string_view uri_text, uri_error expected_error) {
    auto parsed = websocket::uri::parse(uri_text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), expected_error);
  }

  websocket::uri parse_or_fail(std::string_view uri_text) {
    auto parsed = websocket::uri::parse(uri_text);
    EXPECT_TRUE(parsed);
    if (!parsed) {
      return websocket::uri{};
    }
    return *parsed;
  }

} // namespace

TEST(WebSocketUri, ParsesWsSchemeCaseInsensitively) {
  websocket::uri parsed = parse_or_fail("WS://example.com");
  EXPECT_EQ(parsed.scheme(), "ws");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 80);
  EXPECT_EQ(parsed.to_string(), "ws://example.com/");
  EXPECT_FALSE(parsed.validate());
}

TEST(WebSocketUri, ParsesWssSchemeCaseInsensitivelyAndUsesDefaultPort443) {
  websocket::uri parsed = parse_or_fail("wSs://example.com/chat");
  EXPECT_EQ(parsed.scheme(), "wss");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 443);
  EXPECT_EQ(parsed.to_string(), "wss://example.com/chat");
  EXPECT_FALSE(parsed.validate());
}

TEST(WebSocketUri, ParsesExplicitPortWhenProvided) {
  websocket::uri parsed = parse_or_fail("ws://example.com:8080/chat");
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
  websocket::uri parsed = parse_or_fail("ws://example.com/chat?topic=%23general");
  EXPECT_EQ(parsed.to_string(), "ws://example.com/chat?topic=%23general");
}

TEST(WebSocketUri, ResourceNameUsesSlashWhenPathIsEmptyAndPreservesQueryDelimiter) {
  websocket::uri parsed = parse_or_fail("ws://example.com?token=abc");
  EXPECT_EQ(parsed.to_string(), "ws://example.com/?token=abc");
}

TEST(WebSocketUri, ParsesIpv6LiteralHost) {
  websocket::uri parsed = parse_or_fail("ws://[2001:db8::1]/chat");
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
  websocket::uri ws_uri = parse_or_fail("ws://example.com/chat");
  websocket::uri wss_uri = parse_or_fail("wss://example.com/chat");

  EXPECT_EQ(ws_uri.port(), 80);
  EXPECT_EQ(wss_uri.port(), 443);
}

TEST(WebSocketUri, ValidateRejectsInvalidComponentsInConstructedParts) {
  websocket::uri invalid_scheme_uri(websocket::uri_parts{
    .scheme = "http",
    .host = "example.com",
    .port = std::nullopt,
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(invalid_scheme_uri.validate(), uri_error::invalid_scheme);

  websocket::uri fragment_in_path_uri(websocket::uri_parts{
    .scheme = "ws",
    .host = "example.com",
    .port = std::nullopt,
    .path = "chat#frag",
    .query = {},
  });
  EXPECT_EQ(fragment_in_path_uri.validate(), uri_error::fragment_not_allowed);

  websocket::uri userinfo_in_host_uri(websocket::uri_parts{
    .scheme = "ws",
    .host = "user@example.com",
    .port = std::nullopt,
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(userinfo_in_host_uri.validate(), uri_error::userinfo_not_allowed);

  websocket::uri invalid_port_uri(websocket::uri_parts{
    .scheme = "ws",
    .host = "example.com",
    .port = static_cast<std::uint16_t>(0),
    .path = "chat",
    .query = {},
  });
  EXPECT_EQ(invalid_port_uri.validate(), uri_error::invalid_port);
}
