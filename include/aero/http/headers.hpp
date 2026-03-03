#ifndef AERO_HTTP_HEADERS_HPP
#define AERO_HTTP_HEADERS_HPP

#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "aero/detail/string.hpp"
#include "aero/http/detail/headers_parser.hpp"
#include "aero/http/error.hpp"

namespace aero::http {

  class headers {
   public:
    using fields_type = http::detail::header_fields;
    using const_iterator = fields_type::const_iterator;

    headers() = default;
    headers(std::initializer_list<fields_type::value_type> headers): fields_(headers) {}
    explicit headers(fields_type headers): fields_(std::move(headers)) {}

    static std::expected<headers, std::error_code> parse(std::string_view buffer) {
      auto headers = http::detail::parse_headers(buffer);
      if (!headers) {
        return std::unexpected(headers.error());
      }
      return http::headers{*headers};
    }

    static std::expected<headers, std::error_code> parse(std::span<const std::byte> buffer) {
      auto buffer_str = std::string_view{reinterpret_cast<const char*>(buffer.data()), buffer.size()};
      auto headers = http::detail::parse_headers(buffer_str);
      if (!headers) {
        return std::unexpected(headers.error());
      }
      return http::headers{*headers};
    }

    void set(std::string name, std::string value) {
      fields_.remove_values_of(name);
      fields_.emplace(std::move(name), std::move(value));
    }

    void add(std::string name, std::string value) {
      fields_.emplace(std::move(name), std::move(value));
    }

    void clear() noexcept {
      fields_.clear();
    }

    [[nodiscard]] std::string operator[](std::string_view name) const {
      auto it = fields_.find(name);
      return it != fields_.end() ? it->second : std::string{};
    }

    [[nodiscard]] std::optional<std::string> try_get(std::string_view name) const {
      auto it = fields_.find(name);
      if (it == fields_.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    [[nodiscard]] std::string_view first_value_of(std::string_view name) const noexcept {
      return fields_.first_value_of(name);
    }

    [[nodiscard]] auto names() const {
      return fields_.names();
    }

    [[nodiscard]] auto names_view() const {
      return fields_.names_view();
    }

    [[nodiscard]] auto values_of(std::string_view name) {
      return fields_.values_of(name);
    }

    [[nodiscard]] auto values_of(std::string_view name) const {
      return fields_.values_of(name);
    }

    [[nodiscard]] auto values_view_of(std::string_view name) {
      return fields_.values_view_of(name);
    }

    [[nodiscard]] auto values_view_of(std::string_view name) const {
      return fields_.values_view_of(name);
    }

    // Does case insensitive header value comparison
    [[nodiscard]] bool is(std::string_view name, std::string_view expected_value) const {
      return fields_.is(name, expected_value);
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
      return fields_.contains(name);
    }

    [[nodiscard]] std::size_t occurrences(std::string_view name) const {
      auto [range_begin, range_end] = fields_.fields_of(name);
      return static_cast<std::size_t>(std::ranges::distance(range_begin, range_end));
    }

    void merge(const headers& other) {
      fields_.merge(other.fields_);
    }

    void merge(headers&& other) { // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
      fields_.merge(std::move(other.fields_));
    }

    void remove(std::string_view name) {
      fields_.remove_values_of(name);
    }

    void erase(fields_type::iterator first, fields_type::iterator last) {
      fields_.erase(first, last);
    }

    void erase(fields_type::range_iterator first, fields_type::range_iterator last) {
      fields_.erase(first, last);
    }

    [[nodiscard]] std::size_t count() const noexcept {
      return fields_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
      return fields_.empty();
    }

    [[nodiscard]] std::string to_string() const {
      return fields_.to_string();
    }

    [[nodiscard]] const_iterator begin() const noexcept {
      return fields_.begin();
    }

    [[nodiscard]] const_iterator end() const noexcept {
      return fields_.end();
    }

    template <std::integral T = std::uint32_t>
    [[nodiscard]] std::expected<T, std::error_code> content_length() const {
      auto content_len_str = first_value_of("content-length");
      if (content_len_str.empty()) {
        return std::unexpected(http::error::protocol_error::content_length_missing);
      }

      return aero::detail::to_decimal<T>(content_len_str);
    }

    [[nodiscard]] std::expected<std::string_view, std::error_code> content_type() const {
      auto content_len_str = first_value_of("content-type");
      if (content_len_str.empty()) {
        return std::unexpected(http::error::protocol_error::content_type_missing);
      }

      return content_len_str;
    }

    explicit operator fields_type() const {
      return fields_;
    }

   private:
    fields_type fields_;
  };

} // namespace aero::http

#endif
