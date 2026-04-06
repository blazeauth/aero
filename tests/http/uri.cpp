#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string_view>

#include "aero/http/error.hpp"
#include "aero/http/uri.hpp"

namespace http = aero::http;
using http::error::uri_error;

namespace {

  void expect_parse_error(std::string_view uri_text, uri_error expected_error) {
    auto parsed = http::uri::parse(uri_text);
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error(), expected_error);
  }

  http::uri parse_or_fail(std::string_view uri_text) {
    auto parsed = http::uri::parse(uri_text);
    EXPECT_TRUE(parsed);
    if (!parsed) {
      return http::uri{};
    }
    return *parsed;
  }

} // namespace

TEST(HttpUri, ParsesHttpSchemeCaseInsensitively) {
  http::uri parsed = parse_or_fail("HTTP://example.com");
  EXPECT_EQ(parsed.scheme(), "http");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 80);
  EXPECT_EQ(parsed.target(), "/");
  EXPECT_EQ(parsed.to_string(), "http://example.com/");
  EXPECT_FALSE(parsed.validate());
}

TEST(HttpUri, ParsesHttpsSchemeCaseInsensitivelyAndUsesDefaultPort443) {
  http::uri parsed = parse_or_fail("hTtPs://example.com/users");
  EXPECT_EQ(parsed.scheme(), "https");
  EXPECT_EQ(parsed.host(), "example.com");
  EXPECT_EQ(parsed.port(), 443);
  EXPECT_EQ(parsed.path(), "users");
  EXPECT_EQ(parsed.target(), "/users");
  EXPECT_EQ(parsed.to_string(), "https://example.com/users");
  EXPECT_FALSE(parsed.validate());
}

TEST(HttpUri, ParsesExplicitPortWhenProvided) {
  http::uri parsed = parse_or_fail("http://example.com:8080/api");
  EXPECT_EQ(parsed.port(), 8080);
  EXPECT_EQ(parsed.target(), "/api");
  EXPECT_EQ(parsed.to_string(), "http://example.com:8080/api");
}

TEST(HttpUri, RequiresSchemeDelimiter) {
  expect_parse_error("http//example.com", uri_error::missing_scheme_delimiter);
  expect_parse_error("http:example.com", uri_error::missing_scheme_delimiter);
  expect_parse_error("://example.com", uri_error::missing_scheme_delimiter);
}

TEST(HttpUri, RejectsNonHttpSchemes) {
  expect_parse_error("ws://example.com/", uri_error::invalid_scheme);
  expect_parse_error("wss://example.com/", uri_error::invalid_scheme);
  expect_parse_error("ftp://example.com/", uri_error::invalid_scheme);
}

TEST(HttpUri, RequiresNonEmptyAuthority) {
  expect_parse_error("http:///api", uri_error::empty_authority);
  expect_parse_error("https://", uri_error::empty_authority);
}

TEST(HttpUri, RejectsUserinfoInAuthority) {
  expect_parse_error("http://user@example.com/api", uri_error::userinfo_not_allowed);
  expect_parse_error("http://user:pass@example.com/api", uri_error::userinfo_not_allowed);
}

TEST(HttpUri, RejectsFragmentIdentifiersEverywhere) {
  expect_parse_error("http://example.com/#frag", uri_error::fragment_not_allowed);
  expect_parse_error("http://example.com/api#frag", uri_error::fragment_not_allowed);
  expect_parse_error("http://example.com/api?x=1#frag", uri_error::fragment_not_allowed);
}

TEST(HttpUri, AcceptsEscapedHashInQuery) {
  http::uri parsed = parse_or_fail("http://example.com/api?topic=%23general");
  EXPECT_EQ(parsed.target(), "/api?topic=%23general");
  EXPECT_EQ(parsed.to_string(), "http://example.com/api?topic=%23general");
}

TEST(HttpUri, TargetUsesSlashWhenPathIsEmptyAndPreservesQueryDelimiter) {
  http::uri parsed = parse_or_fail("http://example.com?token=abc");
  EXPECT_EQ(parsed.target(), "/?token=abc");
  EXPECT_EQ(parsed.to_string(), "http://example.com/?token=abc");
}

TEST(HttpUri, PreservesEmptyQueryDelimiter) {
  http::uri parsed = parse_or_fail("http://example.com?");
  EXPECT_TRUE(parsed.has_query());
  EXPECT_EQ(parsed.query(), "");
  EXPECT_EQ(parsed.target(), "/?");
  EXPECT_EQ(parsed.to_string(), "http://example.com/?");
}

TEST(HttpUri, ParsesIpv6LiteralHost) {
  http::uri parsed = parse_or_fail("http://[2001:db8::1]/api");
  EXPECT_EQ(parsed.scheme(), "http");
  EXPECT_EQ(parsed.host(), "[2001:db8::1]");
  EXPECT_EQ(parsed.port(), 80);
  EXPECT_EQ(parsed.target(), "/api");
  EXPECT_EQ(parsed.to_string(), "http://[2001:db8::1]/api");
}

TEST(HttpUri, RejectsIpv6LiteralWithoutClosingBracket) {
  expect_parse_error("http://[2001:db8::1/api", uri_error::invalid_ipv6_literal);
  expect_parse_error("http://[2001:db8::1", uri_error::invalid_ipv6_literal);
}

TEST(HttpUri, RejectsEmptyIpv6Literal) {
  expect_parse_error("http://[]/api", uri_error::invalid_ipv6_literal);
  expect_parse_error("http://[/api", uri_error::invalid_ipv6_literal);
}

TEST(HttpUri, RejectsPortThatIsEmptyNonNumericZeroOrOutOfRange) {
  expect_parse_error("http://example.com:/api", uri_error::empty_port);
  expect_parse_error("http://example.com:abc/api", uri_error::invalid_port);
  expect_parse_error("http://example.com:0/api", uri_error::invalid_port);
  expect_parse_error("http://example.com:70000/api", uri_error::port_out_of_range);
}

TEST(HttpUri, DefaultPortDependsOnSchemeWhenPortIsOmitted) {
  http::uri http_uri = parse_or_fail("http://example.com/api");
  http::uri https_uri = parse_or_fail("https://example.com/api");

  EXPECT_EQ(http_uri.port(), 80);
  EXPECT_EQ(https_uri.port(), 443);
}

TEST(HttpUri, ValidateRejectsInvalidComponentsInConstructedParts) {
  http::uri invalid_scheme_uri(http::uri_parts{
    .scheme = "ftp",
    .host = "example.com",
    .port = std::nullopt,
    .path = "api",
    .query = {},
    .has_query = false,
  });
  EXPECT_EQ(invalid_scheme_uri.validate(), uri_error::invalid_scheme);

  http::uri fragment_in_query_uri(http::uri_parts{
    .scheme = "http",
    .host = "example.com",
    .port = std::nullopt,
    .path = "api",
    .query = "x=1#frag",
    .has_query = true,
  });
  EXPECT_EQ(fragment_in_query_uri.validate(), uri_error::fragment_not_allowed);

  http::uri userinfo_in_host_uri(http::uri_parts{
    .scheme = "http",
    .host = "user@example.com",
    .port = std::nullopt,
    .path = "api",
    .query = {},
    .has_query = false,
  });
  EXPECT_EQ(userinfo_in_host_uri.validate(), uri_error::userinfo_not_allowed);

  http::uri invalid_port_uri(http::uri_parts{
    .scheme = "http",
    .host = "example.com",
    .port = static_cast<std::uint16_t>(0),
    .path = "api",
    .query = {},
    .has_query = false,
  });
  EXPECT_EQ(invalid_port_uri.validate(), uri_error::invalid_port);

  http::uri invalid_path_uri(http::uri_parts{
    .scheme = "http",
    .host = "example.com",
    .port = std::nullopt,
    .path = "api?bad",
    .query = {},
    .has_query = false,
  });
  EXPECT_EQ(invalid_path_uri.validate(), uri_error::invalid_path);
}
