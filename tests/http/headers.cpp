#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

namespace http = aero::http;
using http::error::protocol_error;

TEST(HttpHeaders, ParseWrapperSucceeds) {
  auto parsed = http::headers::parse("Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& parsed_headers = *parsed;

  EXPECT_EQ(parsed_headers.count(), 2);
  EXPECT_TRUE(parsed_headers.contains("upgrade"));
  EXPECT_TRUE(parsed_headers.contains("CONNECTION"));
  EXPECT_EQ(parsed_headers["Upgrade"], "websocket");
  EXPECT_EQ(parsed_headers["connection"], "Upgrade");
}

TEST(HttpHeaders, ParseWrapperPropagatesError) {
  auto parsed = http::headers::parse("A (bad): b\r\n\r\n");

  ASSERT_FALSE(parsed);
  EXPECT_EQ(parsed.error(), protocol_error::header_name_invalid);
}

TEST(HttpHeaders, SetReplacesAllOccurrences) {
  http::headers headers;
  headers.add("Set-Cookie", "a=1");
  EXPECT_EQ(headers.occurrences("Set-Cookie"), 1);

  headers.set("Set-Cookie", "c=3");
  EXPECT_EQ(headers.occurrences("Set-Cookie"), 1);
  EXPECT_EQ(headers["Set-Cookie"], "c=3");
}

TEST(HttpHeaders, AddPreservesDuplicates) {
  http::headers headers{
    {"Set-Cookie", "a"},
    {"Set-Cookie", "b=2"},
  };

  EXPECT_EQ(headers.occurrences("Set-Cookie"), 2);

  headers.set("Set-Cookie", "c=3");
  EXPECT_EQ(headers.occurrences("Set-Cookie"), 1);
  EXPECT_EQ(headers["Set-Cookie"], "c=3");
}

TEST(HttpHeaders, ContainsIsCaseInsensitiveAndEqualRangeEnumeratesAllOccurrences) {
  http::headers headers;

  headers.add("Upgrade", "websocket");
  headers.add("upgrade", "h2c");

  EXPECT_TRUE(headers.contains("UPGRADE"));
  EXPECT_EQ(headers.occurrences("UpGrAdE"), 2);

  std::vector<std::string> values{};
  for (const auto& value : headers.values_of("UPGRADE")) {
    values.emplace_back(value);
  }

  ASSERT_EQ(values.size(), 2);
  EXPECT_EQ(values[0], "websocket");
  EXPECT_EQ(values[1], "h2c");
}

TEST(HttpHeaders, ContentLengthMethodSucceeds) {
  http::headers fields{
    {"Content-Length", "123123"},
  };

  auto content_length = fields.content_length();
  ASSERT_TRUE(content_length);
  EXPECT_EQ(*content_length, 123123);
}

TEST(HttpHeaders, ContentLengthMethodParsesUint64) {
  http::headers fields{
    {"Content-Length", "18446744073709551615"},
  };

  auto content_length = fields.content_length<std::uint64_t>();
  ASSERT_TRUE(content_length);
  EXPECT_EQ(*content_length, 18446744073709551615ULL);
}

TEST(HttpHeaders, ContentLengthMissingHeaderPropagatesContentLengthMissingError) {
  http::headers fields{};

  auto content_length = fields.content_length();
  ASSERT_FALSE(content_length);
  EXPECT_EQ(content_length.error(), http::error::protocol_error::content_length_missing);
}

TEST(HttpHeaders, ContentTypeMethodSucceeds) {
  http::headers fields{
    {"Content-Type", "application/json"},
  };

  auto content_type = fields.content_type();
  ASSERT_TRUE(content_type);
  EXPECT_EQ(*content_type, "application/json");
}

TEST(HttpHeaders, ContentTypeMissingHeaderPropagatesContentTypeMissingError) {
  http::headers fields{};

  auto content_type = fields.content_type();
  ASSERT_FALSE(content_type);
  EXPECT_EQ(content_type.error(), http::error::protocol_error::content_type_missing);
}
