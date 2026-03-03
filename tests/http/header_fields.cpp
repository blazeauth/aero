#include <gtest/gtest.h>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "aero/http/detail/common.hpp"
#include "aero/http/detail/header_fields.hpp"

namespace http = aero::http;
using http::detail::header_fields;
using http::detail::header_separator;
using http::detail::headers_end_separator;

namespace {

  std::vector<std::string> values_of(const header_fields& fields, std::string_view name) {
    std::vector<std::string> values{};
    const auto [range_begin, range_end] = fields.fields_of(name);
    for (auto it = range_begin; it != range_end; ++it) {
      values.emplace_back(it->second);
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

} // namespace

TEST(HttpHeaderFields, DefaultConstructedIsEmpty) {
  header_fields fields{};

  EXPECT_TRUE(fields.empty());
  EXPECT_EQ(fields.size(), 0U);
  EXPECT_EQ(fields.begin(), fields.end());
}

TEST(HttpHeaderFields, ConstructedFromInitializerListPreservesInsertionOrderAndDuplicates) {
  header_fields fields{{"A", "1"}, {"B", "2"}, {"C", "3"}, {"D", "4"}, {"A", "11"}};
  auto repeated_record_values = collect_values_to_strings(fields.values_of("A"));

  EXPECT_EQ(fields.size(), 5);
  ASSERT_EQ(repeated_record_values.size(), 2);

  EXPECT_EQ(repeated_record_values[0], "1");
  EXPECT_EQ(repeated_record_values[1], "11");
  EXPECT_EQ(fields.first_value_of("B"), "2");
  EXPECT_EQ(fields.first_value_of("C"), "3");
  EXPECT_EQ(fields.first_value_of("D"), "4");
}

TEST(HttpHeaderFields, CaseInsensitiveValueMatchReturnsTrue) {
  header_fields fields{
    {"Connection", "UpGrAde"},
    {"CoNteNt-TYpe", "application/JSON"},
    {"Sec-Websocket-Accept", "aaabbb"},
  };

  EXPECT_TRUE(fields.is("Connection", "upgrade"));
  EXPECT_TRUE(fields.is("content-type", "application/json"));
  EXPECT_TRUE(fields.is("Sec-Websocket-Accept", "aaabbb"));
}

TEST(HttpHeaderFields, EmplacePreservesInsertionOrderForIteration) {
  header_fields fields{};
  fields.emplace("A", "1");
  fields.emplace("B", "2");
  fields.emplace("C", "3");

  ASSERT_EQ(fields.size(), 3U);

  auto it = fields.begin();
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->first, "A");
  EXPECT_EQ(it->second, "1");

  ++it;
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->first, "B");
  EXPECT_EQ(it->second, "2");

  ++it;
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->first, "C");
  EXPECT_EQ(it->second, "3");
}

TEST(HttpHeaderFields, ContainsAndFindAreCaseInsensitive) {
  header_fields fields{};

  fields.emplace("Content-Length", "123");

  EXPECT_TRUE(fields.contains("content-length"));
  EXPECT_TRUE(fields.contains("CONTENT-LENGTH"));
  EXPECT_FALSE(fields.contains("Connection"));

  const auto it = fields.find("CONTENT-length");
  ASSERT_NE(it, fields.end());
  EXPECT_EQ(it->second, "123");
}

TEST(HttpHeaderFields, EqualRangeIteratesAllOccurrencesInOriginalOrder) {
  header_fields fields{};

  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("X", "x");
  fields.emplace("set-cookie", "b=2");
  fields.emplace("Y", "y");
  fields.emplace("SET-COOKIE", "c=3");

  const auto cookies = values_of(fields, "set-cookie");

  ASSERT_EQ(cookies.size(), 3U);
  EXPECT_EQ(cookies[0], "a=1");
  EXPECT_EQ(cookies[1], "b=2");
  EXPECT_EQ(cookies[2], "c=3");
}

TEST(HttpHeaderFields, EraseRangeRemovesAllOccurrencesOfThatFieldName) {
  header_fields fields{};

  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("Set-Cookie", "b=2");
  fields.emplace("X", "x");

  ASSERT_TRUE(fields.contains("set-cookie"));
  ASSERT_EQ(values_of(fields, "SET-COOKIE").size(), 2U);

  auto cookie_fields = fields.fields_of("set-cookie");
  fields.erase(cookie_fields.begin(), cookie_fields.end());

  EXPECT_FALSE(fields.contains("Set-Cookie"));
  EXPECT_TRUE(fields.contains("X"));
  EXPECT_EQ(fields.size(), 1U);
}

TEST(HttpHeaderFields, EqualRangeViewIteratesMatchingPairsInInsertionOrder) {
  header_fields fields{};
  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("X", "x");
  fields.emplace("set-cookie", "b=2");
  fields.emplace("Y", "y");
  fields.emplace("SET-COOKIE", "c=3");

  auto range = fields.fields_of("set-cookie");

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

TEST(HttpHeaderFields, EqualRangeViewOnConstWorksAndIsCaseInsensitive) {
  header_fields fields{};
  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("set-cookie", "b=2");
  fields.emplace("X", "x");

  auto range = fields.fields_of("SET-COOKIE");

  std::vector<std::string> collected_values{};
  for (auto&& pair : range) {
    collected_values.emplace_back(pair.second);
  }

  ASSERT_EQ(collected_values.size(), 2U);
  EXPECT_EQ(collected_values[0], "a=1");
  EXPECT_EQ(collected_values[1], "b=2");
}

TEST(HttpHeaderFields, EqualRangeViewIsEmptyWhenKeyDoesNotExist) {
  header_fields fields{};
  fields.emplace("A", "1");
  fields.emplace("B", "2");

  auto range = fields.fields_of("Missing");

  EXPECT_TRUE(std::ranges::empty(range));
  EXPECT_EQ(std::ranges::distance(range), 0);
}

TEST(HttpHeaderFields, EqualRangeValuesViewReturnsOnlyValuesInInsertionOrder) {
  header_fields fields{};
  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("X", "x");
  fields.emplace("set-cookie", "b=2");
  fields.emplace("SET-COOKIE", "c=3");

  auto values = collect_values_to_strings(fields.values_view_of("set-cookie"));

  ASSERT_EQ(values.size(), 3U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
  EXPECT_EQ(values[2], "c=3");
}

TEST(HttpHeaderFields, EqualRangeValuesViewOnConstReturnsOnlyValues) {
  header_fields mutable_fields{};
  mutable_fields.emplace("Set-Cookie", "a=1");
  mutable_fields.emplace("set-cookie", "b=2");
  mutable_fields.emplace("X", "x");

  const auto& fields = mutable_fields;

  auto values = collect_values_to_strings(fields.values_view_of("SET-COOKIE"));

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
}

TEST(HttpHeaderFields, EqualRangeValueViewsReturnStringViewsForValues) {
  header_fields fields{};
  fields.emplace("Set-Cookie", "a=1");
  fields.emplace("X", "x");
  fields.emplace("set-cookie", "b=2");

  auto values = collect_values_to_string_views(fields.values_view_of("SET-COOKIE"));

  ASSERT_EQ(values.size(), 2U);
  EXPECT_EQ(values[0], "a=1");
  EXPECT_EQ(values[1], "b=2");
}

TEST(HttpHeaderFields, EqualRangeValueViewsOnConstAreCompatibleWithRangesAlgorithms) {
  header_fields mutable_fields{};
  mutable_fields.emplace("Set-Cookie", "a=1");
  mutable_fields.emplace("set-cookie", "b=22");
  mutable_fields.emplace("SET-COOKIE", "ccc");
  mutable_fields.emplace("X", "x");

  const auto& fields = mutable_fields;

  auto value_views = fields.values_view_of("set-cookie");

  EXPECT_FALSE(std::ranges::empty(value_views));
  EXPECT_EQ(std::ranges::distance(value_views), 3);
}

TEST(HttpHeaderFields, ToStringHeadersSeparatedWithCrlf) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"C", "c"},
  };

  header_fields growing_fields{};

  for (const auto& [header_name, header_value] : fields) {
    growing_fields.emplace(header_name, header_value);

    auto fields_str = growing_fields.to_string();

    std::string_view clean_str = fields_str;
    clean_str.remove_suffix(headers_end_separator.size());

    auto split_headers = std::views::split(clean_str, header_separator);
    auto str_headers_count = std::ranges::distance(split_headers);

    EXPECT_EQ(growing_fields.size(), str_headers_count);
  }
}

TEST(HttpHeaderFields, ToStringHeadersEndsWithDoubleCrlf) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"C", "c"},
  };

  header_fields growing_fields{};

  for (const auto& [header_name, header_value] : fields) {
    growing_fields.emplace(header_name, header_value);
    EXPECT_TRUE(growing_fields.to_string().ends_with(headers_end_separator));
  }
}

TEST(HttpHeaderFields, ToStringEmptyHeadersEndsWithDoubleCrlf) {
  header_fields fields{};
  EXPECT_EQ(fields.to_string(), headers_end_separator);
}

TEST(HttpHeaderFields, ToStringFormatsMultipleFields) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
    {"A", "a"},
    {"B", "b"},
    {"c", "c"},
  };

  EXPECT_EQ(fields.to_string(), "Set-Cookie: a=1\r\nA: a\r\nB: b\r\nc: c\r\n\r\n");
}

TEST(HttpHeaderFields, ToStringFormatsFieldsCaseInsensitive) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
    {"set-cookie", "b=1"},
  };

  EXPECT_EQ(fields.to_string(), "Set-Cookie: a=1, b=1\r\n\r\n");
}

TEST(HttpHeaderFields, ToStringParsesFieldWithMultipleValues) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
    {"Set-Cookie", "b=22"},
    {"Set-Cookie", "ccc"},
  };

  EXPECT_EQ(fields.to_string(), "Set-Cookie: a=1, b=22, ccc\r\n\r\n");
}

TEST(HttpHeaderFields, MergeCopiesAndReturnsSizeOfBothObjects) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{
    {"Hello-World", "aero"},
  };

  fields.merge(other_fields);

  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("Hello-World", "aero"));
  EXPECT_EQ(fields.size(), 2);

  EXPECT_TRUE(other_fields.is("Hello-World", "aero"));
}

TEST(HttpHeaderFields, MergeMovesFieldsAndClearsSource) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{
    {"Hello-World", "aero"},
  };

  fields.merge(std::move(other_fields));

  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("Hello-World", "aero"));
  EXPECT_EQ(fields.size(), 2U);

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
  EXPECT_EQ(other_fields.begin(), other_fields.end());
}

TEST(HttpHeaderFields, MergeCopiesNothingFromEmptyObject) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{};

  auto fields_size_before_merge = fields.size();
  fields.merge(other_fields);

  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_EQ(fields_size_before_merge, fields.size());
}

TEST(HttpHeaderFields, MergeCopiesInitializedObjectToEmpty) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{};

  auto fields_size_before_merge = fields.size();
  other_fields.merge(fields);

  EXPECT_TRUE(other_fields.is("Host", "example.com"));
  EXPECT_EQ(other_fields.size(), fields_size_before_merge);
}

TEST(HttpHeaderFields, MergeMovesInitializedObjectToEmpty) {
  header_fields fields{};

  header_fields other_fields{
    {"Host", "example.com"},
    {"Hello-World", "aero"},
  };

  auto other_fields_size_before_merge = other_fields.size();
  fields.merge(std::move(other_fields));

  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("Hello-World", "aero"));
  EXPECT_EQ(fields.size(), other_fields_size_before_merge);

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
}

TEST(HttpHeaderFields, MergeCopiesNothingWhenBothObjectsEmpty) {
  header_fields fields{};
  header_fields other_fields{};

  fields.merge(other_fields);

  EXPECT_TRUE(fields.empty());
  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(fields.size(), 0U);
}

TEST(HttpHeaderFields, MergePreservesDuplicateHeaderValuesOnCopy) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
  };

  header_fields other_fields{
    {"Set-Cookie", "b=2"},
  };

  fields.merge(other_fields);

  EXPECT_TRUE(fields.is("Set-Cookie", "a=1"));
  EXPECT_TRUE(fields.is("Set-Cookie", "b=2"));
  EXPECT_EQ(fields.size(), 2U);

  auto cookie_values = values_of(fields, "Set-Cookie");

  ASSERT_EQ(cookie_values.size(), 2U);
  EXPECT_EQ(cookie_values[0], "a=1");
  EXPECT_EQ(cookie_values[1], "b=2");

  EXPECT_TRUE(other_fields.is("Set-Cookie", "b=2"));
  EXPECT_EQ(other_fields.size(), 1U);
}

TEST(HttpHeaderFields, MergePreservesDuplicateHeaderValuesOnMove) {
  header_fields fields{
    {"Set-Cookie", "a=1"},
  };

  header_fields other_fields{
    {"Set-Cookie", "b=2"},
  };

  fields.merge(std::move(other_fields));

  EXPECT_TRUE(fields.is("Set-Cookie", "a=1"));
  EXPECT_TRUE(fields.is("Set-Cookie", "b=2"));
  EXPECT_EQ(fields.size(), 2U);

  auto cookie_values = values_of(fields, "Set-Cookie");

  ASSERT_EQ(cookie_values.size(), 2U);
  EXPECT_EQ(cookie_values[0], "a=1");
  EXPECT_EQ(cookie_values[1], "b=2");

  EXPECT_TRUE(other_fields.empty());
}

TEST(HttpHeaderFields, MergeFindsMergedValuesCaseInsensitively) {
  header_fields fields{
    {"host", "example.com"},
  };

  header_fields other_fields{
    {"Host", "example.org"},
  };

  fields.merge(other_fields);

  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("HOST", "example.org"));

  auto host_values = values_of(fields, "HOST");

  ASSERT_EQ(host_values.size(), 2U);
  EXPECT_EQ(host_values[0], "example.com");
  EXPECT_EQ(host_values[1], "example.org");
}

TEST(HttpHeaderFields, MergeKeepsFirstValueFromExistingFieldsWhenAppending) {
  header_fields fields{
    {"X-Test", "first"},
  };

  header_fields other_fields{
    {"X-Test", "second"},
  };

  fields.merge(other_fields);

  EXPECT_EQ(fields.first_value_of("X-Test"), "first");
  EXPECT_TRUE(fields.is("X-Test", "first"));
  EXPECT_TRUE(fields.is("X-Test", "second"));
}

TEST(HttpHeaderFields, MergePreservesInsertionOrderWhenAppending) {
  header_fields fields{
    {"A", "1"},
    {"B", "2"},
  };

  header_fields other_fields{
    {"C", "3"},
    {"D", "4"},
  };

  fields.merge(other_fields);

  std::vector<header_fields::value_type> all_fields(fields.begin(), fields.end());

  ASSERT_EQ(all_fields.size(), 4U);
  EXPECT_EQ(all_fields[0].first, "A");
  EXPECT_EQ(all_fields[0].second, "1");
  EXPECT_EQ(all_fields[1].first, "B");
  EXPECT_EQ(all_fields[1].second, "2");
  EXPECT_EQ(all_fields[2].first, "C");
  EXPECT_EQ(all_fields[2].second, "3");
  EXPECT_EQ(all_fields[3].first, "D");
  EXPECT_EQ(all_fields[3].second, "4");
}

TEST(HttpHeaderFields, MergeCopiesLargeNumberOfFields) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{};
  for (std::size_t i{}; i < 1000; ++i) {
    other_fields.emplace("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
  }

  fields.merge(other_fields);

  EXPECT_EQ(fields.size(), 1001U);
  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("X-Header-0", "Value-0"));
  EXPECT_TRUE(fields.is("X-Header-999", "Value-999"));

  EXPECT_FALSE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 1000U);
  EXPECT_TRUE(other_fields.is("X-Header-0", "Value-0"));
  EXPECT_TRUE(other_fields.is("X-Header-999", "Value-999"));
}

TEST(HttpHeaderFields, MergeMovesLargeNumberOfFieldsAndClearsSource) {
  header_fields fields{
    {"Host", "example.com"},
  };

  header_fields other_fields{};
  for (std::size_t i{}; i < 1000; ++i) {
    other_fields.emplace("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
  }

  fields.merge(std::move(other_fields));

  EXPECT_EQ(fields.size(), 1001U);
  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("X-Header-0", "Value-0"));
  EXPECT_TRUE(fields.is("X-Header-999", "Value-999"));

  EXPECT_TRUE(other_fields.empty());
  EXPECT_EQ(other_fields.size(), 0U);
}

TEST(HttpHeaderFields, MergeSelfMergeDoesNothing) {
  header_fields fields{
    {"Host", "example.com"},
    {"Hello-World", "aero"},
  };

  auto fields_size_before_merge = fields.size();
  fields.merge(fields);

  EXPECT_EQ(fields.size(), fields_size_before_merge);
  EXPECT_TRUE(fields.is("Host", "example.com"));
  EXPECT_TRUE(fields.is("Hello-World", "aero"));
}
