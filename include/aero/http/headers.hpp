#pragma once

#include <algorithm>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "aero/detail/attributes.hpp"
#include "aero/http/detail/line_endings.hpp"
#include "aero/http/error.hpp"
#include "aero/util/string.hpp"

namespace aero::http {

  namespace detail {

    [[nodiscard]] inline std::string_view trim_optional_whitespace(std::string_view text) {
      std::size_t first_non_whitespace = text.find_first_not_of(" \t");
      if (first_non_whitespace == std::string_view::npos) {
        return {};
      }
      std::size_t last_non_whitespace = text.find_last_not_of(" \t");
      return text.substr(first_non_whitespace, last_non_whitespace - first_non_whitespace + 1);
    }

  } // namespace detail

  struct header {
    std::string name;
    std::string value;

    [[nodiscard]] bool contains_token(std::string_view token) const {
      if (token.empty()) {
        return false;
      }

      for (auto&& split_value : value | std::views::split(',')) {
        std::string_view candidate{split_value};

        if (aero::striequal(detail::trim_optional_whitespace(candidate), token)) {
          return true;
        }
      }

      return false;
    }
  };

  class headers {
   public:
    using value_type = http::header;
    using iterator = std::vector<value_type>::iterator;
    using const_iterator = std::vector<value_type>::const_iterator;
    using size_type = std::vector<value_type>::size_type;

   private:
    template <bool IsConst>
    class matching_iterator {
      using owner_type = std::conditional_t<IsConst, const headers*, headers*>;

     public:
      using iterator_concept = std::forward_iterator_tag;
      using iterator_category = std::forward_iterator_tag;
      using difference_type = std::ptrdiff_t;
      using value_type = headers::value_type;
      using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
      using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;

      matching_iterator() = default;
      matching_iterator(const matching_iterator&) = default;
      matching_iterator(matching_iterator&&) = default;
      matching_iterator& operator=(const matching_iterator&) = default;
      matching_iterator& operator=(matching_iterator&&) = default;
      ~matching_iterator() = default;

      matching_iterator(owner_type owner, std::size_t start_index, std::string_view key)
        : owner_(owner), index_(start_index), key_(key) {
        seek_forward();
      }

      [[nodiscard]] reference operator*() const noexcept {
        return owner_->headers_[index_];
      }
      [[nodiscard]] pointer operator->() const noexcept {
        return std::addressof(owner_->headers_[index_]);
      }

      matching_iterator& operator++() noexcept {
        if (index_ < owner_->headers_.size()) {
          ++index_;
          seek_forward();
        }
        return *this;
      }

      matching_iterator operator++(int) noexcept {
        auto copy = *this;
        ++(*this);
        return copy;
      }

      [[nodiscard]] friend bool operator==(const matching_iterator& left, const matching_iterator& right) noexcept {
        return left.owner_ == right.owner_ && left.index_ == right.index_ && aero::striequal(left.key_, right.key_);
      }

     private:
      void seek_forward() noexcept {
        while (index_ < owner_->headers_.size()) {
          const auto& current = owner_->headers_[index_].name;
          if (aero::striequal(current, key_)) {
            return;
          }
          ++index_;
        }
      }

      owner_type owner_{};
      std::size_t index_{0};
      std::string_view key_;
    };

   public:
    using range_iterator = matching_iterator<false>;
    using const_range_iterator = matching_iterator<true>;

    headers() = default;
    headers(std::initializer_list<http::header> fields): headers_(fields) {}

    static std::expected<headers, std::error_code> parse(std::string_view buffer);
    static std::expected<headers, std::error_code> parse(std::span<const std::byte> buffer);

    [[nodiscard]] bool empty() const noexcept {
      return headers_.empty();
    }
    [[nodiscard]] size_type size() const noexcept {
      return headers_.size();
    }
    [[nodiscard]] size_type capacity() const noexcept {
      return headers_.capacity();
    }
    [[nodiscard]] iterator begin() & noexcept {
      return headers_.begin();
    }
    [[nodiscard]] iterator end() & noexcept {
      return headers_.end();
    }
    [[nodiscard]] const_iterator begin() const& noexcept {
      return headers_.begin();
    }
    [[nodiscard]] const_iterator end() const& noexcept {
      return headers_.end();
    }
    [[nodiscard]] const_iterator cbegin() const& noexcept {
      return headers_.cbegin();
    }
    [[nodiscard]] const_iterator cend() const& noexcept {
      return headers_.cend();
    }
    [[nodiscard]] http::header& front() & noexcept {
      return headers_.front();
    }
    [[nodiscard]] const http::header& front() const& noexcept {
      return headers_.front();
    }
    [[nodiscard]] http::header& back() & noexcept {
      return headers_.back();
    }
    [[nodiscard]] const http::header& back() const& noexcept {
      return headers_.back();
    }

    void reserve(size_type count) {
      headers_.reserve(count);
    }
    void clear() noexcept {
      headers_.clear();
    }

    [[nodiscard]] iterator find(std::string_view name) & noexcept {
      return std::ranges::find_if(headers_,
        [&](const http::header& field) noexcept { return aero::striequal(field.name, name); });
    }

    [[nodiscard]] const_iterator find(std::string_view name) const& noexcept {
      return std::ranges::find_if(headers_,
        [&](const http::header& field) noexcept { return aero::striequal(field.name, name); });
    }

    [[nodiscard]] auto fields(std::string_view name AERO_LIFETIMEBOUND) & {
      auto first = range_iterator{this, 0, name};
      auto last = range_iterator{this, headers_.size(), name};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto fields(std::string_view name AERO_LIFETIMEBOUND) const& {
      auto first = const_range_iterator{this, 0, name};
      auto last = const_range_iterator{this, headers_.size(), name};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto names() const& {
      return headers_ |
             std::views::transform([](const http::header& field) noexcept -> std::string_view { return field.name; });
    }

    [[nodiscard]] auto values() const& {
      return headers_ |
             std::views::transform([](const http::header& field) noexcept -> std::string_view { return field.value; });
    }

    [[nodiscard]] auto values(std::string_view name AERO_LIFETIMEBOUND) const& {
      return fields(name) |
             std::views::transform([](const http::header& field) noexcept -> std::string_view { return field.value; });
    }

    [[nodiscard]] std::size_t count(std::string_view name) const& noexcept {
      return std::ranges::count_if(headers_,
        [name](const http::header& field) noexcept { return aero::striequal(field.name, name); });
    }

    [[nodiscard]] std::optional<std::string_view> first_value(std::string_view name) const& noexcept {
      auto iterator = find(name);
      if (iterator == end()) {
        return std::nullopt;
      }
      return std::string_view{iterator->value};
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
      return find(name) != end(); // NOLINT(*-contains)
    }

    [[nodiscard]] bool contains_token(std::string_view name, std::string_view token) const {
      return std::ranges::any_of(fields(name), [token](const http::header& field) { return field.contains_token(token); });
    }

    [[nodiscard]] std::string serialize() const {
      using http::detail::crlf;
      constexpr static std::string_view value_separator = ": ";

      if (headers_.empty()) {
        return std::string{crlf};
      }

      // A single range loop on the headers to reserve space, and a second
      // one for appending, will be much faster than potentially frequent
      // reallocations within a single append-loop
      std::size_t expected_length{};
      for (const auto& [name, value] : headers_) {
        expected_length += name.length() + value.length() + value_separator.length() + crlf.length();
      }

      std::string result;
      result.reserve(expected_length + crlf.length());

      for (const auto& [name, value] : headers_) {
        result.append(name).append(value_separator).append(value).append(crlf);
      }

      return result.append(crlf);
    }

    iterator set(std::string name, std::string value) & {
      auto target = find(name);
      if (target == end()) {
        return add(std::move(name), std::move(value));
      }

      target->name = std::move(name);
      target->value = std::move(value);

      auto duplicates = std::ranges::remove_if(std::next(target), headers_.end(), [&](const http::header& field) noexcept {
        return aero::striequal(field.name, target->name);
      });
      headers_.erase(duplicates.begin(), duplicates.end());

      return target;
    }

    iterator add(std::string name, std::string value) & {
      headers_.emplace_back(std::move(name), std::move(value));
      return std::prev(headers_.end());
    }

    void append(const headers& other) {
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      headers_.reserve(headers_.size() + other.headers_.size());
      std::ranges::copy(other.headers_, std::back_inserter(headers_));
    }

    void append(headers&& other) { // NOLINT(*-rvalue-reference-param-not-moved)
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      headers_.reserve(headers_.size() + other.headers_.size());
      std::ranges::move(other.headers_, std::back_inserter(headers_));
      other.clear();
    }

    void erase(std::string_view name) {
      auto to_remove =
        std::ranges::remove_if(headers_, [&](const http::header& field) noexcept { return aero::striequal(field.name, name); });
      headers_.erase(to_remove.begin(), to_remove.end());
    }

    // NOLINTBEGIN(*-use-nodiscard)
    // Disallow use on temporary objects
    iterator begin() && = delete;
    iterator end() && = delete;
    const_iterator begin() const&& = delete;
    const_iterator end() const&& = delete;
    const_iterator cbegin() const&& = delete;
    const_iterator cend() const&& = delete;
    header& front() && = delete;
    const header& front() const&& = delete;
    header& back() && = delete;
    const header& back() const&& = delete;
    iterator find(std::string_view name) && = delete;
    const_iterator find(std::string_view name) const&& = delete;
    auto fields(std::string_view name) && = delete;
    auto fields(std::string_view name) const&& = delete;
    auto names() const&& = delete;
    auto values() const&& = delete;
    auto values(std::string_view name) const&& = delete;
    std::optional<std::string_view> first_value(std::string_view name) const&& = delete;
    iterator add(std::string name, std::string value) && = delete;
    iterator set(std::string name, std::string value) && = delete;
    // NOLINTEND(*-use-nodiscard)

   private:
    std::vector<value_type> headers_;
  };

  static_assert(std::ranges::forward_range<decltype(std::declval<headers&>().fields(""))>);
  static_assert(std::ranges::forward_range<decltype(std::declval<headers&>().names())>);
  static_assert(std::ranges::forward_range<decltype(std::declval<headers&>().values(""))>);

  template <std::integral T = int>
  [[nodiscard]] inline std::expected<T, std::error_code> content_length(const http::headers& headers) noexcept {
    auto content_len_str = headers.first_value("content-length");
    if (!content_len_str.has_value()) {
      return std::unexpected(header_error::content_length_missing);
    }

    return aero::to_decimal<T>(*content_len_str);
  }

  [[nodiscard]] inline std::expected<std::string_view, std::error_code> content_type(const http::headers& headers) noexcept {
    auto content_type_str = headers.first_value("content-type");
    if (!content_type_str.has_value()) {
      return std::unexpected(header_error::content_type_missing);
    }

    return *content_type_str;
  }

} // namespace aero::http

#include "aero/http/impl/headers_parser.ipp"
