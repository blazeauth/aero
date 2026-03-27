#ifndef AERO_HTTP_HEADERS_HPP
#define AERO_HTTP_HEADERS_HPP

#pragma once

#include <algorithm>
#include <expected>
#include <initializer_list>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"

namespace aero::http {

  class headers {
   public:
    struct field_type {
      std::string name;
      std::string value;
    };

    using value_type = field_type;
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

      matching_iterator(owner_type owner, size_t start_index, std::string key)
        : owner(owner), index(start_index), key(std::move(key)) {
        seek_forward();
      }

      [[nodiscard]] reference operator*() const noexcept {
        return owner->items[index];
      }
      [[nodiscard]] pointer operator->() const noexcept {
        return std::addressof(owner->items[index]);
      }

      matching_iterator& operator++() noexcept {
        if (index < owner->items.size()) {
          ++index;
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
        return left.owner == right.owner && left.index == right.index && aero::detail::ascii_iequal(left.key, right.key);
      }

     private:
      void seek_forward() noexcept {
        while (index < owner->items.size()) {
          const auto& current = owner->items[index].name;
          if (aero::detail::ascii_iequal(current, key)) {
            return;
          }
          ++index;
        }
      }

      owner_type owner{};
      size_t index{0};
      std::string key;
    };

   public:
    using range_iterator = matching_iterator<false>;
    using const_range_iterator = matching_iterator<true>;

    headers() = default;
    headers(std::initializer_list<value_type> fields): items(fields) {}

    static std::expected<headers, std::error_code> parse(std::string_view buffer);
    static std::expected<headers, std::error_code> parse(std::span<const std::byte> buffer);

    [[nodiscard]] bool empty() const& noexcept {
      return items.empty();
    }
    [[nodiscard]] size_type size() const& noexcept {
      return items.size();
    }
    [[nodiscard]] iterator begin() & noexcept {
      return items.begin();
    }
    [[nodiscard]] iterator end() & noexcept {
      return items.end();
    }
    [[nodiscard]] const_iterator begin() const& noexcept {
      return items.begin();
    }
    [[nodiscard]] const_iterator end() const& noexcept {
      return items.end();
    }
    [[nodiscard]] const_iterator cbegin() const& noexcept {
      return items.cbegin();
    }
    [[nodiscard]] const_iterator cend() const& noexcept {
      return items.cend();
    }
    [[nodiscard]] value_type& back() & noexcept {
      return items.back();
    }
    [[nodiscard]] const value_type& back() const& noexcept {
      return items.back();
    }

    void clear() noexcept {
      items.clear();
    }

    [[nodiscard]] iterator find(std::string_view name) & noexcept {
      return std::ranges::find_if(items,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.name, name); });
    }

    [[nodiscard]] const_iterator find(std::string_view name) const& noexcept {
      return std::ranges::find_if(items,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.name, name); });
    }

    [[nodiscard]] auto fields(std::string_view name) & {
      auto key_copy = std::string{name};
      auto first = range_iterator{this, 0, key_copy};
      auto last = range_iterator{this, items.size(), std::move(key_copy)};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto fields(std::string_view name) const& {
      auto key_copy = std::string{name};
      auto first = const_range_iterator{this, 0, key_copy};
      auto last = const_range_iterator{this, items.size(), std::move(key_copy)};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto names() const& {
      return items | std::views::transform([](const field_type& field) noexcept -> std::string_view { return field.name; });
    }

    [[nodiscard]] auto values() const& {
      return items | std::views::transform([](const field_type& field) noexcept -> std::string_view { return field.value; });
    }

    [[nodiscard]] auto values(std::string_view name) const& {
      return fields(name) |
             std::views::transform([](const field_type& field) noexcept -> std::string_view { return field.value; });
    }

    [[nodiscard]] size_t occurrences(std::string_view name) const& noexcept {
      return std::ranges::count_if(items,
        [name](const field_type& field) noexcept { return aero::detail::ascii_iequal(field.name, name); });
    }

    [[nodiscard]] std::optional<std::string_view> first_value(std::string_view name) const& noexcept {
      auto iterator = find(name);
      if (iterator == end()) {
        return std::nullopt;
      }
      return std::string_view{iterator->value};
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
      return find(name) != end(); // NOLINT(*-container-contains)
    }

    [[nodiscard]] std::string serialize() const {
      using http::detail::crlf;
      using http::detail::header_name_value_separator;

      if (empty()) {
        return std::string{crlf};
      }

      constexpr size_t value_separator_length = header_name_value_separator.length();
      constexpr size_t crlf_length = crlf.length();

      std::size_t expected_length{};
      for (const auto& [name, value] : items) {
        expected_length += name.length() + value.length() + value_separator_length + crlf_length;
      }

      std::string result;
      result.reserve(expected_length + crlf_length);

      for (const auto& [name, value] : items) {
        // Skip empty field names. Empty field values are still valid under RFC9112
        if (name.empty()) {
          continue;
        }
        result.append(name).append(": ").append(value).append(crlf);
      }

      result.append(crlf);

      return result;
    }

    void replace(std::string name, std::string value) {
      erase(name);
      add(std::move(name), std::move(value));
    }

    iterator add(std::string name, std::string value) & {
      items.emplace_back(std::move(name), std::move(value));
      return std::prev(items.end());
    }

    void append(const headers& other) {
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      items.reserve(items.size() + other.items.size());
      std::ranges::copy(other.items, std::back_inserter(items));
    }

    void append(headers&& other) { // NOLINT(*-rvalue-reference-param-not-moved)
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      items.reserve(items.size() + other.items.size());
      std::ranges::move(other.items, std::back_inserter(items));
      other.clear();
    }

    void erase(std::string_view name) {
      auto to_remove = std::ranges::remove_if(items,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.name, name); });
      items.erase(to_remove.begin(), to_remove.end());
    }

    // Disallow use on temporary objects
    iterator begin() && = delete;
    iterator end() && = delete;
    [[nodiscard]] const_iterator begin() const&& = delete;
    [[nodiscard]] const_iterator end() const&& = delete;
    [[nodiscard]] const_iterator cbegin() const&& = delete;
    [[nodiscard]] const_iterator cend() const&& = delete;
    value_type& back() && = delete;
    [[nodiscard]] const value_type& back() const&& = delete;
    iterator find(std::string_view name) && = delete;
    [[nodiscard]] const_iterator find(std::string_view name) const&& = delete;
    auto fields(std::string_view name) && = delete;
    [[nodiscard]] auto fields(std::string_view name) const&& = delete;
    [[nodiscard]] auto names() const&& = delete;
    [[nodiscard]] auto values() const&& = delete;
    [[nodiscard]] auto values(std::string_view name) const&& = delete;
    [[nodiscard]] std::optional<std::string_view> first_value(std::string_view name) const&& = delete;
    iterator add(std::string name, std::string value) && = delete;

   private:
    std::vector<value_type> items;
  };

} // namespace aero::http

#include "aero/http/impl/header_parser.ipp"

#endif
