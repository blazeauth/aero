#include <gtest/gtest.h>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

namespace http = aero::http;
using http::detail::crlf;
using http::detail::double_crlf;

namespace {

  std::vector<std::string> values_of(const http::headers& fields, std::string_view name) {
    std::vector<std::string> values{};
    for (std::string_view value : fields.values(name)) {
      values.emplace_back(value);
    }
    return values;
  }

  std::vector<std::string> collect_values_to_strings(auto&& values_range) {
    std::vector<std::string> result{};
    for (auto&& value : values_range) {
      result.emplace_back(value);
    }
    return result;
  }

  std::vector<std::string_view> collect_values_to_string_views(auto&& values_range) {
    std::vector<std::string_view> result{};
    for (std::string_view value_view : values_range) {
      result.emplace_back(value_view);
    }
    return result;
  }

  bool contains_case_insensitive_value(const http::headers& fields, std::string_view name, std::string_view expected_value) {
    return std::ranges::any_of(fields.values(name),
      [expected_value](std::string_view value) { return aero::detail::ascii_iequal(value, expected_value); });
  }

} // namespace

TEST(HttpHeaders, DefaultConstructedIsEmpty) {
  http::headers fields{};

  EXPECT_TRUE(fields.empty());
  EXPECT_EQ(fields.size(), 0U);
  EXPECT_EQ(fields.begin(), fields.end());
}

TEST(HttpHeaders, ConstructedFromInitializerListPreservesInsertionOrderAndDuplicates) {
  http::headers fields{{"A", "1"}, {"B", "2"}, {"C", "3"}, {"D", "4"}, {"A", "11"}};
  auto repeated_record_values = collect_values_to_strings(fields.values("A"));

  EXPECT_EQ(fields.size(), 5U);
  ASSERT_EQ(repeated_record_values.size(), 2U);

  EXPECT_EQ(repeated_record_values[0], "1");
  EXPECT_EQ(repeated_record_values[1], "11");

  ASSERT_TRUE(fields.first_value("B"));
  ASSERT_TRUE(fields.first_value("C"));
  ASSERT_TRUE(fields.first_value("D"));

  EXPECT_EQ(*fields.first_value("B"), "2");
  EXPECT_EQ(*fields.first_value("C"), "3");
  EXPECT_EQ(*fields.first_value("D"), "4");
}

TEST(HttpHeaders, CaseInsensitiveValueMatchReturnsTrue) {
  http::headers fields{
    {"Connection", "UpGrAde"},
    {"CoNteNt-TYpe", "application/JSON"},
    {"Sec-Websocket-Accept", "aaabbb"},
  };

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Connection", "upgrade"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "content-type", "application/json"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Sec-Websocket-Accept", "aaabbb"));
}

TEST(HttpHeaders, AddPreservesInsertionOrderForIteration) {
  http::headers fields{};
  fields.add("A", "1");
  fields.add("B", "2");
  fields.add("C", "3");

  ASSERT_EQ(fields.size(), 3U);

  auto it = fields.begin();
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->name, "A");
  EXPECT_EQ(it->value, "1");

  ++it;
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->name, "B");
  EXPECT_EQ(it->value, "2");

  ++it;
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->name, "C");
  EXPECT_EQ(it->value, "3");
}

TEST(HttpHeaders, ContainsAndFindAreCaseInsensitive) {
  http::headers fields{};

  fields.add("Content-Length", "123");

  EXPECT_TRUE(fields.contains("content-length"));
  EXPECT_TRUE(fields.contains("CONTENT-LENGTH"));
  EXPECT_FALSE(fields.contains("Connection"));

  const auto it = fields.find("CONTENT-length");
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->value, "123");
}

TEST(HttpHeaders, FieldsIteratesAllOccurrencesInOriginalOrder) {
  http::headers fields{};

  fields.add("Set-Cookie", "a=1");
  fields.add("X", "x");
  fields.add("set-cookie", "b=2");
  fields.add("Y", "y");
  fields.add("SET-COOKIE", "c=3");

  const auto cookies = values_of(fields, "set-cookie");

  ASSERT_EQ(cookies.size(), 3U);
  EXPECT_EQ(cookies[0], "a=1");
  EXPECT_EQ(cookies[1], "b=2");
  EXPECT_EQ(cookies[2], "c=3");
}

TEST(HttpHeaders, EraseRemovesAllOccurrencesOfThatFieldName) {
  http::headers fields{};

  fields.add("Set-Cookie", "a=1");
  fields.add("Set-Cookie", "b=2");
  fields.add("X", "x");

  ASSERT_TRUE(fields.contains("set-cookie"));
  ASSERT_EQ(values_of(fields, "SET-COOKIE").size(), 2U);

  fields.erase("set-cookie");

  EXPECT_FALSE(fields.contains("Set-Cookie"));
  EXPECT_TRUE(fields.contains("X"));
  EXPECT_EQ(fields.size(), 1U);
}

TEST(HttpHeaders, FieldsViewIteratesMatchingPairsInInsertionOrder) {
  http::headers fields{};
  fields.add("Set-Cookie", "a=1");
  fields.add("X", "x");
  fields.add("set-cookie", "b=2");
  fields.add("Y", "y");
  fields.add("SET-COOKIE", "c=3");

  auto range = fields.fields("set-cookie");

  auto collected_names = std::vector<std::string>{};
  auto collected_values = std::vector<std::string>{};

  for (auto&& [name, value] : range) {
    collected_names.emplace_back(name);
    collected_values.emplace_back(value);
  }

  ASSERT_EQ(collected_values.size(), 3U);
  EXPECT_EQ(collected_values[0], "a=1");
  EXPECT_EQ(collected_values[1], "b=2");
  EXPECT_EQ(collected_values[2], "c=3");

  ASSERT_EQ(collected_names.size(), 3U);
  EXPECT_EQ(collected_names[0], "Set-Cookie");
  EXPECT_EQ(collected_names[1], "set-cookie");
  EXPECT_EQ(collected_names[2], "SET-COOKIE");
}

TEST(HttpHeaders, FieldsViewOnConstWorksAndIsCaseInsensitive) {
  http::headers mutable_fields{};
  mutable_fields.add("Set-Cookie", "a=1");
  mutable_fields.add("set-cookie", "b=2");
  mutable_fields.add("X", "x");

  const auto& fields = mutable_fields;
  auto range = fields.fields("SET-COOKIE");

  std::vector<std::string> collected_values{};
  for (auto&& field : range) {
    collected_values.emplace_back(field.value);
  }

  ASSERT_EQ(collected_values.size(), 2U);
  EXPECT_EQ(collected_values[0], "a=1");
  EXPECT_EQ(collected_values[1], "b=2");
}

TEST(HttpHeaders, FieldsViewIsEmptyWhenKeyDoesNotExist) {
  http::headers fields{};
  fields.add("A", "1");
  fields.add("B", "2");

  auto range = fields.fields("Missing");

  EXPECT_TRUE(std::ranges::empty(range));
  EXPECT_EQ(std::ranges::distance(range), 0);
}

TEST(HttpHeaders, ValuesViewReturnsOnlyValuesInInsertionOrder) {
  http::headers fields{};
  fields.add("Set-Cookie", "a=1");
  fields.add("X", "x");
  fields.add("set-cookie", "b=2");
  fields.add("SET-COOKIE", "c=3");

  auto values = collect_values_to_strings(fields.values("set-cookie"));

  ASSERT_EQ(values.size(), 3U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
  EXPECT_EQ(values[2], "c=3");
}

TEST(HttpHeaders, ValuesViewOnConstReturnsOnlyValues) {
  http::headers mutable_fields{};
  mutable_fields.add("Set-Cookie", "a=1");
  mutable_fields.add("set-cookie", "b=2");
  mutable_fields.add("X", "x");

  const auto& fields = mutable_fields;
  auto values = collect_values_to_strings(fields.values("SET-COOKIE"));

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
}

TEST(HttpHeaders, ValueViewsReturnStringViewsForValues) {
  http::headers fields{};
  fields.add("Set-Cookie", "a=1");
  fields.add("X", "x");
  fields.add("set-cookie", "b=2");

  auto values = collect_values_to_string_views(fields.values("SET-COOKIE"));

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
}

TEST(HttpHeaders, ValueViewsOnConstAreCompatibleWithRangesAlgorithms) {
  http::headers mutable_fields{};
  mutable_fields.add("Set-Cookie", "a=1");
  mutable_fields.add("set-cookie", "b=22");
  mutable_fields.add("SET-COOKIE", "ccc");
  mutable_fields.add("X", "x");

  const auto& fields = mutable_fields;
  auto value_views = fields.values("set-cookie");

  EXPECT_FALSE(std::ranges::empty(value_views));
  EXPECT_EQ(std::ranges::distance(value_views), 3);
}

TEST(HttpHeaders, SerializeHeadersSeparatedWithCrlf) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"C", "c"},
  };

  http::headers growing_fields{};

  for (const auto& [header_name, header_value] : fields) {
    growing_fields.add(header_name, header_value);

    auto fields_str = growing_fields.serialize();

    std::string_view clean_str = fields_str;
    clean_str.remove_suffix(double_crlf.size());

    auto split_headers = std::views::split(clean_str, crlf);
    auto str_headers_count = std::ranges::distance(split_headers);

    EXPECT_EQ(growing_fields.size(), static_cast<http::headers::size_type>(str_headers_count));
  }
}

TEST(HttpHeaders, SerializeNonEmptyHeadersEndsWithDoubleCrlf) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"C", "c"},
  };

  http::headers growing_fields{};

  for (const auto& [header_name, header_value] : fields) {
    growing_fields.add(header_name, header_value);
    EXPECT_TRUE(growing_fields.serialize().ends_with(double_crlf));
  }
}

TEST(HttpHeaders, SerializeEmptyHeadersEndsWithSingleCrlf) {
  http::headers fields{};
  EXPECT_EQ(fields.serialize(), crlf);
}

TEST(HttpHeaders, SerializeFormatsMultipleFields) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"c", "c"},
  };

  EXPECT_EQ(fields.serialize(), "Set-Cookie: a=1\r\nA: a\r\nB: b\r\nc: c\r\n\r\n");
}

TEST(HttpHeaders, SerializePreservesRepeatedFieldsAsSeparateLines) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
    {"set-cookie", "b=1"},
  };

  EXPECT_EQ(fields.serialize(), "Set-Cookie: a=1\r\nset-cookie: b=1\r\n\r\n");
}

TEST(HttpHeaders, SerializePreservesMultipleRepeatedFieldsInInsertionOrder) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
    {"Set-Cookie", "b=22"},
    {"Set-Cookie", "ccc"},
  };

  EXPECT_EQ(fields.serialize(), "Set-Cookie: a=1\r\nSet-Cookie: b=22\r\nSet-Cookie: ccc\r\n\r\n");
}

TEST(HttpHeaders, ParseWrapperSucceeds) {
  auto parsed = http::headers::parse("Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
  ASSERT_TRUE(parsed);

  auto& parsed_headers = *parsed;

  EXPECT_EQ(parsed_headers.size(), 2U);
  EXPECT_TRUE(parsed_headers.contains("upgrade"));
  EXPECT_TRUE(parsed_headers.contains("CONNECTION"));

  ASSERT_TRUE(parsed_headers.first_value("Upgrade"));
  ASSERT_TRUE(parsed_headers.first_value("connection"));

  EXPECT_EQ(*parsed_headers.first_value("Upgrade"), "websocket");
  EXPECT_EQ(*parsed_headers.first_value("connection"), "Upgrade");
}

TEST(HttpHeaders, ReplaceReplacesAllOccurrences) {
  http::headers fields{};
  fields.add("Set-Cookie", "a=1");
  EXPECT_EQ(fields.occurrences("Set-Cookie"), 1U);

  fields.replace("Set-Cookie", "c=3");
  EXPECT_EQ(fields.occurrences("Set-Cookie"), 1U);

  ASSERT_TRUE(fields.first_value("Set-Cookie"));
  EXPECT_EQ(*fields.first_value("Set-Cookie"), "c=3");
}

TEST(HttpHeaders, AddPreservesDuplicatesBeforeReplace) {
  http::headers fields{
    {"Set-Cookie", "a"},
    {"Set-Cookie", "b=2"},
  };

  EXPECT_EQ(fields.occurrences("Set-Cookie"), 2U);

  fields.replace("Set-Cookie", "c=3");
  EXPECT_EQ(fields.occurrences("Set-Cookie"), 1U);

  ASSERT_TRUE(fields.first_value("Set-Cookie"));
  EXPECT_EQ(*fields.first_value("Set-Cookie"), "c=3");
}

TEST(HttpHeaders, ContainsIsCaseInsensitiveAndFieldsEnumeratesAllOccurrences) {
  http::headers fields{};

  fields.add("Upgrade", "websocket");
  fields.add("upgrade", "h2c");

  EXPECT_TRUE(fields.contains("UPGRADE"));
  EXPECT_EQ(fields.occurrences("UpGrAdE"), 2U);

  std::vector<std::string> values{};
  for (const auto& value : fields.values("UPGRADE")) {
    values.emplace_back(value);
  }

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0], "websocket");
  EXPECT_EQ(values[1], "h2c");
}

TEST(HttpHeaders, FirstValueReturnsPresentContentLengthHeaderValue) {
  http::headers fields{
    {"Content-Length", "123123"},
  };

  auto content_length = fields.first_value("Content-Length");
  ASSERT_TRUE(content_length);
  EXPECT_EQ(*content_length, "123123");
}

TEST(HttpHeaders, FirstValueMissingContentLengthReturnsNullopt) {
  http::headers fields{};

  auto content_length = fields.first_value("Content-Length");
  EXPECT_FALSE(content_length);
}

TEST(HttpHeaders, FirstValueReturnsPresentContentTypeHeaderValue) {
  http::headers fields{
    {"Content-Type", "application/json"},
  };

  auto content_type = fields.first_value("Content-Type");
  ASSERT_TRUE(content_type);
  EXPECT_EQ(*content_type, "application/json");
}

TEST(HttpHeaders, FirstValueMissingContentTypeReturnsNullopt) {
  http::headers fields{};

  auto content_type = fields.first_value("Content-Type");
  EXPECT_FALSE(content_type);
}

TEST(HttpHeaders, AppendCopiesAndReturnsSizeOfBothObjects) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{
    {"Hello-World", "aero"},
  };

  fields.append(other_fields);

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Hello-World", "aero"));
  EXPECT_EQ(fields.size(), 2U);

  EXPECT_TRUE(contains_case_insensitive_value(other_fields, "Hello-World", "aero"));
}

TEST(HttpHeaders, AppendMovesFieldsAndClearsSource) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{
    {"Hello-World", "aero"},
  };

  fields.append(std::move(other_fields));

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Hello-World", "aero"));
  EXPECT_EQ(fields.size(), 2U);

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
  EXPECT_EQ(other_fields.begin(), other_fields.end());
}

TEST(HttpHeaders, AppendCopiesNothingFromEmptyObject) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{};

  auto fields_size_before_append = fields.size();
  fields.append(other_fields);

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_EQ(fields_size_before_append, fields.size());
}

TEST(HttpHeaders, AppendCopiesInitializedObjectToEmpty) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{};

  auto fields_size_before_append = fields.size();
  other_fields.append(fields);

  EXPECT_TRUE(contains_case_insensitive_value(other_fields, "Host", "example.com"));
  EXPECT_EQ(other_fields.size(), fields_size_before_append);
}

TEST(HttpHeaders, AppendMovesInitializedObjectToEmpty) {
  http::headers fields{};

  http::headers other_fields{
    {"Host", "example.com"},
    {"Hello-World", "aero"},
  };

  auto other_fields_size_before_append = other_fields.size();
  fields.append(std::move(other_fields));

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Hello-World", "aero"));
  EXPECT_EQ(fields.size(), other_fields_size_before_append);

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
}

TEST(HttpHeaders, AppendCopiesNothingWhenBothObjectsEmpty) {
  http::headers fields{};
  http::headers other_fields{};

  fields.append(other_fields);

  EXPECT_TRUE(fields.empty());
  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(fields.size(), 0U);
}

TEST(HttpHeaders, AppendPreservesDuplicateHeaderValuesOnCopy) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
  };

  http::headers other_fields{
    {"Set-Cookie", "b=2"},
  };

  fields.append(other_fields);

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Set-Cookie", "a=1"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Set-Cookie", "b=2"));
  EXPECT_EQ(fields.size(), 2U);

  auto cookie_values = values_of(fields, "Set-Cookie");

  ASSERT_EQ(cookie_values.size(), 2U);
  EXPECT_EQ(cookie_values[0], "a=1");
  EXPECT_EQ(cookie_values[1], "b=2");

  EXPECT_TRUE(contains_case_insensitive_value(other_fields, "Set-Cookie", "b=2"));
  EXPECT_EQ(other_fields.size(), 1U);
}

TEST(HttpHeaders, AppendPreservesDuplicateHeaderValuesOnMove) {
  http::headers fields{
    {"Set-Cookie", "a=1"},
  };

  http::headers other_fields{
    {"Set-Cookie", "b=2"},
  };

  fields.append(std::move(other_fields));

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Set-Cookie", "a=1"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Set-Cookie", "b=2"));
  EXPECT_EQ(fields.size(), 2U);

  auto cookie_values = values_of(fields, "Set-Cookie");

  ASSERT_EQ(cookie_values.size(), 2U);
  EXPECT_EQ(cookie_values[0], "a=1");
  EXPECT_EQ(cookie_values[1], "b=2");

  EXPECT_TRUE(other_fields.empty());
}

TEST(HttpHeaders, AppendFindsMergedValuesCaseInsensitively) {
  http::headers fields{
    {"host", "example.com"},
  };

  http::headers other_fields{
    {"Host", "example.org"},
  };

  fields.append(other_fields);

  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "HOST", "example.org"));

  auto host_values = values_of(fields, "HOST");

  ASSERT_EQ(host_values.size(), 2U);
  EXPECT_EQ(host_values[0], "example.com");
  EXPECT_EQ(host_values[1], "example.org");
}

TEST(HttpHeaders, AppendKeepsFirstValueFromExistingFieldsWhenAppending) {
  http::headers fields{
    {"X-Test", "first"},
  };

  http::headers other_fields{
    {"X-Test", "second"},
  };

  fields.append(other_fields);

  ASSERT_TRUE(fields.first_value("X-Test"));
  EXPECT_EQ(*fields.first_value("X-Test"), "first");
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Test", "first"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Test", "second"));
}

TEST(HttpHeaders, AppendPreservesInsertionOrderWhenAppending) {
  http::headers fields{
    {"A", "1"},
    {"B", "2"},
  };

  http::headers other_fields{
    {"C", "3"},
    {"D", "4"},
  };

  fields.append(other_fields);

  std::vector<http::headers::value_type> all_fields(fields.begin(), fields.end());

  ASSERT_EQ(all_fields.size(), 4U);
  EXPECT_EQ(all_fields[0].name, "A");
  EXPECT_EQ(all_fields[0].value, "1");
  EXPECT_EQ(all_fields[1].name, "B");
  EXPECT_EQ(all_fields[1].value, "2");
  EXPECT_EQ(all_fields[2].name, "C");
  EXPECT_EQ(all_fields[2].value, "3");
  EXPECT_EQ(all_fields[3].name, "D");
  EXPECT_EQ(all_fields[3].value, "4");
}

TEST(HttpHeaders, AppendCopiesLargeNumberOfFields) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{};
  for (std::size_t i{}; i < 1000; ++i) {
    other_fields.add("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
  }

  fields.append(other_fields);

  EXPECT_EQ(fields.size(), 1001U);
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Header-0", "Value-0"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Header-999", "Value-999"));

  EXPECT_FALSE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 1000U);
  EXPECT_TRUE(contains_case_insensitive_value(other_fields, "X-Header-0", "Value-0"));
  EXPECT_TRUE(contains_case_insensitive_value(other_fields, "X-Header-999", "Value-999"));
}

TEST(HttpHeaders, AppendMovesLargeNumberOfFieldsAndClearsSource) {
  http::headers fields{
    {"Host", "example.com"},
  };

  http::headers other_fields{};
  for (std::size_t i{}; i < 1000; ++i) {
    other_fields.add("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
  }

  fields.append(std::move(other_fields));

  EXPECT_EQ(fields.size(), 1001U);
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Header-0", "Value-0"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "X-Header-999", "Value-999"));

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
}

TEST(HttpHeaders, AppendSelfAppendDoesNothing) {
  http::headers fields{
    {"Host", "example.com"},
    {"Hello-World", "aero"},
  };

  auto fields_size_before_append = fields.size();
  fields.append(fields);

  EXPECT_EQ(fields.size(), fields_size_before_append);
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Host", "example.com"));
  EXPECT_TRUE(contains_case_insensitive_value(fields, "Hello-World", "aero"));
}
