#include <ranges>
#include <string>
#include <string_view>
#include <ut/ut.hpp>
#include <vector>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"
#include "aero/http/error.hpp"
#include "aero/http/headers.hpp"

using namespace ut;

namespace http = aero::http;
using http::header_error;
using http::detail::crlf;
using http::detail::double_crlf;

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

int main() {
  suite http_headers = [] {
    "default constructed headers are empty"_test = [] {
      http::headers fields{};

      expect(fields.empty());
      expect(fields.size() == 0U);
      expect(fields.begin() == fields.end());
    };

    "initializer list preserves insertion order and duplicates"_test = [] {
      http::headers fields{{"A", "1"}, {"B", "2"}, {"C", "3"}, {"D", "4"}, {"A", "11"}};
      auto repeated_record_values = collect_values_to_strings(fields.values("A"));

      expect(fields.size() == 5U);
      expect[repeated_record_values.size() == 2U];

      expect(repeated_record_values[0] == "1");
      expect(repeated_record_values[1] == "11");

      expect[fields.first_value("B").has_value()];
      expect[fields.first_value("C").has_value()];
      expect[fields.first_value("D").has_value()];

      expect(*fields.first_value("B") == "2");
      expect(*fields.first_value("C") == "3");
      expect(*fields.first_value("D") == "4");
    };

    "case-insensitive value match returns true"_test = [] {
      http::headers fields{
        {"Connection", "UpGrAde"},
        {"CoNteNt-TYpe", "application/JSON"},
        {"Sec-Websocket-Accept", "aaabbb"},
      };

      expect(contains_case_insensitive_value(fields, "Connection", "upgrade"));
      expect(contains_case_insensitive_value(fields, "content-type", "application/json"));
      expect(contains_case_insensitive_value(fields, "Sec-Websocket-Accept", "aaabbb"));
    };

    "adding a field preserves insertion order for iteration"_test = [] {
      http::headers fields{};
      fields.add("A", "1");
      fields.add("B", "2");
      fields.add("C", "3");

      expect[fields.size() == 3U];

      auto it = fields.begin();
      expect[it != fields.end()];
      expect(it->name == "A");
      expect(it->value == "1");

      ++it;
      expect[it != fields.end()];
      expect(it->name == "B");
      expect(it->value == "2");

      ++it;
      expect[it != fields.end()];
      expect(it->name == "C");
      expect(it->value == "3");
    };

    "contains and find are case-insensitive"_test = [] {
      http::headers fields{};

      fields.add("Content-Length", "123");

      expect(fields.contains("content-length"));
      expect(fields.contains("CONTENT-LENGTH"));
      expect(not fields.contains("Connection"));

      const auto it = fields.find("CONTENT-length");
      expect[it != fields.end()];
      expect(it->value == "123");
    };

    "fields iterates all occurrences in original order"_test = [] {
      http::headers fields{};

      fields.add("Set-Cookie", "a=1");
      fields.add("X", "x");
      fields.add("set-cookie", "b=2");
      fields.add("Y", "y");
      fields.add("SET-COOKIE", "c=3");

      const auto cookies = values_of(fields, "set-cookie");

      expect[cookies.size() == 3U];
      expect(cookies[0] == "a=1");
      expect(cookies[1] == "b=2");
      expect(cookies[2] == "c=3");
    };

    "erasing a field removes all matching occurrences"_test = [] {
      http::headers fields{};

      fields.add("Set-Cookie", "a=1");
      fields.add("Set-Cookie", "b=2");
      fields.add("X", "x");

      expect[fields.contains("set-cookie")];
      expect[values_of(fields, "SET-COOKIE").size() == 2U];

      fields.erase("set-cookie");

      expect(not fields.contains("Set-Cookie"));
      expect(fields.contains("X"));
      expect(fields.size() == 1U);
    };

    "fields view iterates matching pairs in insertion order"_test = [] {
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

      expect[collected_values.size() == 3U];
      expect(collected_values[0] == "a=1");
      expect(collected_values[1] == "b=2");
      expect(collected_values[2] == "c=3");

      expect[collected_names.size() == 3U];
      expect(collected_names[0] == "Set-Cookie");
      expect(collected_names[1] == "set-cookie");
      expect(collected_names[2] == "SET-COOKIE");
    };

    "fields view on const works and is case-insensitive"_test = [] {
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

      expect[collected_values.size() == 2U];
      expect(collected_values[0] == "a=1");
      expect(collected_values[1] == "b=2");
    };

    "fields view is empty when key does not exist"_test = [] {
      http::headers fields{};
      fields.add("A", "1");
      fields.add("B", "2");

      auto range = fields.fields("Missing");

      expect(std::ranges::empty(range));
      expect(std::ranges::distance(range) == 0);
    };

    "values view returns only values in insertion order"_test = [] {
      http::headers fields{};
      fields.add("Set-Cookie", "a=1");
      fields.add("X", "x");
      fields.add("set-cookie", "b=2");
      fields.add("SET-COOKIE", "c=3");

      auto values = collect_values_to_strings(fields.values("set-cookie"));

      expect[values.size() == 3U];
      expect(values[0] == "a=1");
      expect(values[1] == "b=2");
      expect(values[2] == "c=3");
    };

    "values view on const returns only values"_test = [] {
      http::headers mutable_fields{};
      mutable_fields.add("Set-Cookie", "a=1");
      mutable_fields.add("set-cookie", "b=2");
      mutable_fields.add("X", "x");

      const auto& fields = mutable_fields;
      auto values = collect_values_to_strings(fields.values("SET-COOKIE"));

      expect[values.size() == 2U];
      expect(values[0] == "a=1");
      expect(values[1] == "b=2");
    };

    "value views return string views for values"_test = [] {
      http::headers fields{};
      fields.add("Set-Cookie", "a=1");
      fields.add("X", "x");
      fields.add("set-cookie", "b=2");

      auto values = collect_values_to_string_views(fields.values("SET-COOKIE"));

      expect[values.size() == 2U];
      expect(values[0] == "a=1");
      expect(values[1] == "b=2");
    };

    "value views on const are compatible with ranges algorithms"_test = [] {
      http::headers mutable_fields{};
      mutable_fields.add("Set-Cookie", "a=1");
      mutable_fields.add("set-cookie", "b=22");
      mutable_fields.add("SET-COOKIE", "ccc");
      mutable_fields.add("X", "x");

      const auto& fields = mutable_fields;
      auto value_views = fields.values("set-cookie");

      expect(not std::ranges::empty(value_views));
      expect(std::ranges::distance(value_views) == 3);
    };

    "serializes headers separated with crlf"_test = [] {
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

        expect(growing_fields.size() == static_cast<http::headers::size_type>(str_headers_count));
      }
    };

    "serializes non-empty headers with a double crlf suffix"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
        {"A", "a"},
        {"B", "b"},
        {"C", "c"},
      };

      http::headers growing_fields{};

      for (const auto& [header_name, header_value] : fields) {
        growing_fields.add(header_name, header_value);
        expect(growing_fields.serialize().ends_with(double_crlf));
      }
    };

    "serializes empty headers as an empty string"_test = [] {
      http::headers fields{};
      expect(fields.serialize().empty());
    };

    "serializes multiple fields"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
        {"A", "a"},
        {"B", "b"},
        {"c", "c"},
      };

      expect(fields.serialize() == "Set-Cookie: a=1\r\nA: a\r\nB: b\r\nc: c\r\n\r\n");
    };

    "serializes repeated fields as separate lines"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
        {"set-cookie", "b=1"},
      };

      expect(fields.serialize() == "Set-Cookie: a=1\r\nset-cookie: b=1\r\n\r\n");
    };

    "serializes repeated fields in insertion order"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
        {"Set-Cookie", "b=22"},
        {"Set-Cookie", "ccc"},
      };

      expect(fields.serialize() == "Set-Cookie: a=1\r\nSet-Cookie: b=22\r\nSet-Cookie: ccc\r\n\r\n");
    };

    "parse returns headers for a valid header block"_test = [] {
      auto parsed = http::headers::parse("Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n");
      expect[parsed.has_value()];

      auto& parsed_headers = *parsed;

      expect(parsed_headers.size() == 2U);
      expect(parsed_headers.contains("upgrade"));
      expect(parsed_headers.contains("CONNECTION"));

      expect[parsed_headers.first_value("Upgrade").has_value()];
      expect[parsed_headers.first_value("connection").has_value()];

      expect(*parsed_headers.first_value("Upgrade") == "websocket");
      expect(*parsed_headers.first_value("connection") == "Upgrade");
    };

    "replace updates all occurrences"_test = [] {
      http::headers fields{};
      fields.add("Set-Cookie", "a=1");
      expect(fields.occurrences("Set-Cookie") == 1U);

      fields.replace("Set-Cookie", "c=3");
      expect(fields.occurrences("Set-Cookie") == 1U);

      expect[fields.first_value("Set-Cookie").has_value()];
      expect(*fields.first_value("Set-Cookie") == "c=3");
    };

    "add keeps duplicates until replace is called"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a"},
        {"Set-Cookie", "b=2"},
      };

      expect(fields.occurrences("Set-Cookie") == 2U);

      fields.replace("Set-Cookie", "c=3");
      expect(fields.occurrences("Set-Cookie") == 1U);

      expect[fields.first_value("Set-Cookie").has_value()];
      expect(*fields.first_value("Set-Cookie") == "c=3");
    };

    "contains is case-insensitive and fields enumerates all occurrences"_test = [] {
      http::headers fields{};

      fields.add("Upgrade", "websocket");
      fields.add("upgrade", "h2c");

      expect(fields.contains("UPGRADE"));
      expect(fields.occurrences("UpGrAdE") == 2U);

      std::vector<std::string> values{};
      for (const auto& value : fields.values("UPGRADE")) {
        values.emplace_back(value);
      }

      expect[values.size() == 2U];
      expect(values[0] == "websocket");
      expect(values[1] == "h2c");
    };

    "first_value returns the content-length header value"_test = [] {
      http::headers fields{
        {"Content-Length", "123123"},
      };

      auto content_length = fields.first_value("Content-Length");
      expect[content_length.has_value()];
      expect(*content_length == "123123");
    };

    "first_value returns nullopt when content-length is missing"_test = [] {
      http::headers fields{};

      auto content_length = fields.first_value("Content-Length");
      expect(not content_length.has_value());
    };

    "first_value returns the content-type header value"_test = [] {
      http::headers fields{
        {"Content-Type", "application/json"},
      };

      auto content_type = fields.first_value("Content-Type");
      expect[content_type.has_value()];
      expect(*content_type == "application/json");
    };

    "first_value returns nullopt when content-type is missing"_test = [] {
      http::headers fields{};

      auto content_type = fields.first_value("Content-Type");
      expect(not content_type.has_value());
    };

    "append copies and returns size of both objects"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{
        {"Hello-World", "aero"},
      };

      fields.append(other_fields);

      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "Hello-World", "aero"));
      expect(fields.size() == 2U);

      expect(contains_case_insensitive_value(other_fields, "Hello-World", "aero"));
    };

    "append moves fields and clears source"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{
        {"Hello-World", "aero"},
      };

      fields.append(std::move(other_fields));

      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "Hello-World", "aero"));
      expect(fields.size() == 2U);

      expect(other_fields.empty());
      expect(other_fields.size() == 0U);
      expect(other_fields.begin() == other_fields.end());
    };

    "append copies nothing from empty object"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{};

      auto fields_size_before_append = fields.size();
      fields.append(other_fields);

      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(fields_size_before_append == fields.size());
    };

    "append copies initialized object to empty"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{};

      auto fields_size_before_append = fields.size();
      other_fields.append(fields);

      expect(contains_case_insensitive_value(other_fields, "Host", "example.com"));
      expect(other_fields.size() == fields_size_before_append);
    };

    "append moves initialized object to empty"_test = [] {
      http::headers fields{};

      http::headers other_fields{
        {"Host", "example.com"},
        {"Hello-World", "aero"},
      };

      auto other_fields_size_before_append = other_fields.size();
      fields.append(std::move(other_fields));

      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "Hello-World", "aero"));
      expect(fields.size() == other_fields_size_before_append);

      expect(other_fields.empty());
      expect(other_fields.size() == 0U);
    };

    "append copies nothing when both objects empty"_test = [] {
      http::headers fields{};
      http::headers other_fields{};

      fields.append(other_fields);

      expect(fields.empty());
      expect(other_fields.empty());
      expect(fields.size() == 0U);
    };

    "append preserves duplicate header values on copy"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
      };

      http::headers other_fields{
        {"Set-Cookie", "b=2"},
      };

      fields.append(other_fields);

      expect(contains_case_insensitive_value(fields, "Set-Cookie", "a=1"));
      expect(contains_case_insensitive_value(fields, "Set-Cookie", "b=2"));
      expect(fields.size() == 2U);

      auto cookie_values = values_of(fields, "Set-Cookie");

      expect[cookie_values.size() == 2U];
      expect(cookie_values[0] == "a=1");
      expect(cookie_values[1] == "b=2");

      expect(contains_case_insensitive_value(other_fields, "Set-Cookie", "b=2"));
      expect(other_fields.size() == 1U);
    };

    "append preserves duplicate header values on move"_test = [] {
      http::headers fields{
        {"Set-Cookie", "a=1"},
      };

      http::headers other_fields{
        {"Set-Cookie", "b=2"},
      };

      fields.append(std::move(other_fields));

      expect(contains_case_insensitive_value(fields, "Set-Cookie", "a=1"));
      expect(contains_case_insensitive_value(fields, "Set-Cookie", "b=2"));
      expect(fields.size() == 2U);

      auto cookie_values = values_of(fields, "Set-Cookie");

      expect[cookie_values.size() == 2U];
      expect(cookie_values[0] == "a=1");
      expect(cookie_values[1] == "b=2");

      expect(other_fields.empty());
    };

    "append finds merged values case-insensitively"_test = [] {
      http::headers fields{
        {"host", "example.com"},
      };

      http::headers other_fields{
        {"Host", "example.org"},
      };

      fields.append(other_fields);

      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "HOST", "example.org"));

      auto host_values = values_of(fields, "HOST");

      expect[host_values.size() == 2U];
      expect(host_values[0] == "example.com");
      expect(host_values[1] == "example.org");
    };

    "append keeps first value from existing fields when appending"_test = [] {
      http::headers fields{
        {"X-Test", "first"},
      };

      http::headers other_fields{
        {"X-Test", "second"},
      };

      fields.append(other_fields);

      expect[fields.first_value("X-Test").has_value()];
      expect(*fields.first_value("X-Test") == "first");
      expect(contains_case_insensitive_value(fields, "X-Test", "first"));
      expect(contains_case_insensitive_value(fields, "X-Test", "second"));
    };

    "append preserves insertion order when appending"_test = [] {
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

      expect[all_fields.size() == 4U];
      expect(all_fields[0].name == "A");
      expect(all_fields[0].value == "1");
      expect(all_fields[1].name == "B");
      expect(all_fields[1].value == "2");
      expect(all_fields[2].name == "C");
      expect(all_fields[2].value == "3");
      expect(all_fields[3].name == "D");
      expect(all_fields[3].value == "4");
    };

    "append copies large number of fields"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{};
      for (std::size_t i{}; i < 1000; ++i) {
        other_fields.add("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
      }

      fields.append(other_fields);

      expect(fields.size() == 1001U);
      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "X-Header-0", "Value-0"));
      expect(contains_case_insensitive_value(fields, "X-Header-999", "Value-999"));

      expect(not other_fields.empty());
      expect(other_fields.size() == 1000U);
      expect(contains_case_insensitive_value(other_fields, "X-Header-0", "Value-0"));
      expect(contains_case_insensitive_value(other_fields, "X-Header-999", "Value-999"));
    };

    "append moves large number of fields and clears source"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
      };

      http::headers other_fields{};
      for (std::size_t i{}; i < 1000; ++i) {
        other_fields.add("X-Header-" + std::to_string(i), "Value-" + std::to_string(i));
      }

      fields.append(std::move(other_fields));

      expect(fields.size() == 1001U);
      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "X-Header-0", "Value-0"));
      expect(contains_case_insensitive_value(fields, "X-Header-999", "Value-999"));

      expect(other_fields.empty());
      expect(other_fields.size() == 0U);
    };

    "self append does nothing"_test = [] {
      http::headers fields{
        {"Host", "example.com"},
        {"Hello-World", "aero"},
      };

      auto fields_size_before_append = fields.size();
      fields.append(fields);

      expect(fields.size() == fields_size_before_append);
      expect(contains_case_insensitive_value(fields, "Host", "example.com"));
      expect(contains_case_insensitive_value(fields, "Hello-World", "aero"));
    };

    "content-length returns parsed integer"_test = [] {
      http::headers fields{
        {"Content-Length", "500"},
      };

      expect(fields.content_length() == 500);
    };

    "content-length parses header names case-insensitively"_test = [] {
      http::headers fields{
        {"CoNtEnT-LeNgtH", "500"},
      };

      expect(fields.content_length() == 500);
    };

    "content-length parses uint64"_test = [] {
      http::headers fields{
        {"Content-Length", "18446744073709551615"},
      };

      expect(fields.content_length<std::uint64_t>() == 18446744073709551615ULL);
    };

    "content-length returns an unexpected error on missing header"_test = [] {
      http::headers fields{};

      auto content_length = fields.content_length();

      expect[not content_length.has_value()];
      expect(content_length.error() == header_error::content_length_missing);
    };

    "content-type returns parsed mime type"_test = [] {
      http::headers fields{
        {"Content-Type", "application/json"},
      };

      expect(fields.content_type() == "application/json");
    };

    "content-type parses header names case-insensitively"_test = [] {
      http::headers fields{
        {"CoNtEnT-TyPe", "application/json"},
      };

      expect(fields.content_type() == "application/json");
    };

    "content-type returns an unexpected error on missing header"_test = [] {
      http::headers fields{};

      auto content_type = fields.content_type();

      expect[not content_type.has_value()];
      expect(content_type.error() == header_error::content_type_missing);
    };
  };
}
