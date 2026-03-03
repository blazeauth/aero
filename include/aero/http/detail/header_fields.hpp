#ifndef AERO_HTTP_DETAIL_HEADER_FIELDS_HPP
#define AERO_HTTP_DETAIL_HEADER_FIELDS_HPP

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "aero/detail/string.hpp"
#include "aero/http/detail/common.hpp"

namespace aero::http::detail {

  class header_fields {
   public:
    using value_type = std::pair<std::string, std::string>;
    using storage_type = std::vector<value_type>;
    using iterator = storage_type::iterator;
    using const_iterator = storage_type::const_iterator;
    using size_type = storage_type::size_type;

   private:
    template <bool IsConst>
    class matching_iterator {
     public:
      using iterator_concept = std::bidirectional_iterator_tag;
      using iterator_category = std::bidirectional_iterator_tag;
      using difference_type = std::ptrdiff_t;
      using value_type = header_fields::value_type;
      using reference = std::conditional_t<IsConst, const value_type&, value_type&>;
      using pointer = std::conditional_t<IsConst, const value_type*, value_type*>;

      matching_iterator() = default;
      matching_iterator(const matching_iterator&) = default;
      matching_iterator(matching_iterator&&) = default;
      matching_iterator& operator=(const matching_iterator&) = default;
      matching_iterator& operator=(matching_iterator&&) = default;
      ~matching_iterator() = default;

      matching_iterator(std::conditional_t<IsConst, const header_fields*, header_fields*> owner, std::size_t start_index,
        std::string key)
        : owner_(owner), index_(start_index), key_(std::move(key)) {
        seek_forward();
      }

      [[nodiscard]] reference operator*() const noexcept {
        return owner_->items_[index_];
      }

      [[nodiscard]] pointer operator->() const noexcept {
        return std::addressof(owner_->items_[index_]);
      }

      matching_iterator& operator++() noexcept {
        if (index_ < owner_->items_.size()) {
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

      matching_iterator& operator--() noexcept {
        if (owner_->items_.empty()) {
          return *this;
        }

        if (index_ > owner_->items_.size()) {
          index_ = owner_->items_.size();
        }

        if (index_ == owner_->items_.size()) {
          index_ = owner_->items_.size() - 1;
          seek_backward();
          return *this;
        }

        if (index_ == 0) {
          return *this;
        }

        --index_;
        seek_backward();
        return *this;
      }

      matching_iterator operator--(int) noexcept {
        auto copy = *this;
        --(*this);
        return copy;
      }

      [[nodiscard]] std::string_view key() const noexcept {
        return std::string_view{key_};
      }

      [[nodiscard]] friend bool operator==(const matching_iterator& left, const matching_iterator& right) noexcept {
        return left.owner_ == right.owner_ && left.index_ == right.index_ && left.key_ == right.key_;
      }

      [[nodiscard]] friend bool operator!=(const matching_iterator& left, const matching_iterator& right) noexcept {
        return !(left == right);
      }

     private:
      void seek_forward() noexcept {
        while (index_ < owner_->items_.size()) {
          const auto& current = owner_->items_[index_].first;
          if (aero::detail::ascii_iequal(current, key_)) {
            return;
          }
          ++index_;
        }
      }

      void seek_backward() noexcept {
        for (;;) {
          const auto& current = owner_->items_[index_].first;
          if (aero::detail::ascii_iequal(current, key_)) {
            return;
          }
          if (index_ == 0) {
            index_ = owner_->items_.size();
            return;
          }
          --index_;
        }
      }

      std::conditional_t<IsConst, const header_fields*, header_fields*> owner_{};
      std::size_t index_{0};
      std::string key_;
    };

   public:
    using range_iterator = matching_iterator<false>;
    using const_range_iterator = matching_iterator<true>;
    using range_iterator_pair = std::pair<range_iterator, range_iterator>;
    using const_range_iterator_pair = std::pair<const_range_iterator, const_range_iterator>;

    header_fields() = default;
    header_fields(std::initializer_list<value_type> items): items_(items) {}

    [[nodiscard]] bool empty() const noexcept {
      return items_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return items_.size();
    }

    [[nodiscard]] iterator begin() noexcept {
      return items_.begin();
    }

    [[nodiscard]] iterator end() noexcept {
      return items_.end();
    }

    [[nodiscard]] const_iterator begin() const noexcept {
      return items_.begin();
    }

    [[nodiscard]] const_iterator end() const noexcept {
      return items_.end();
    }

    [[nodiscard]] const_iterator cbegin() const noexcept {
      return items_.cbegin();
    }

    [[nodiscard]] const_iterator cend() const noexcept {
      return items_.cend();
    }

    iterator emplace(std::string name, std::string value) {
      items_.emplace_back(std::move(name), std::move(value));
      return std::prev(items_.end());
    }

    void clear() noexcept {
      items_.clear();
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
      return find(name) != end(); // NOLINT(readability-container-contains)
    }

    [[nodiscard]] iterator find(std::string_view name) noexcept {
      return std::ranges::find_if(items_,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.first, name); });
    }

    [[nodiscard]] const_iterator find(std::string_view name) const noexcept {
      return std::ranges::find_if(items_,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.first, name); });
    }

    [[nodiscard]] auto fields_of(std::string_view name) {
      // \note: maybe we should care about allocations and prefer views,
      // but most of the header names will fit in SSO anyway
      auto key_copy = std::string{name};
      auto first = range_iterator{this, 0, std::string{key_copy}};
      auto last = range_iterator{this, items_.size(), std::move(key_copy)};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto fields_of(std::string_view name) const {
      auto key_copy = std::string{name};
      auto first = const_range_iterator{this, 0, std::string{key_copy}};
      auto last = const_range_iterator{this, items_.size(), std::move(key_copy)};
      return std::ranges::subrange{first, last};
    }

    [[nodiscard]] auto names() const {
      return items_ | std::views::keys;
    }

    [[nodiscard]] auto names_view() const {
      return names() | std::views::transform([](const std::string& str) -> std::string_view { return str; });
    }

    [[nodiscard]] auto values() const {
      return items_ | std::views::values;
    }

    [[nodiscard]] auto values_view() const {
      return values() | std::views::transform([](const std::string& value) -> std::string_view { return value; });
    }

    [[nodiscard]] auto values_of(std::string_view name) {
      return fields_of(name) | std::views::values;
    }

    [[nodiscard]] auto values_of(std::string_view name) const {
      return fields_of(name) | std::views::values;
    }

    [[nodiscard]] auto values_view_of(std::string_view name) {
      return values_of(name) | std::views::transform([](std::string& value) -> std::string_view { return value; });
    }

    [[nodiscard]] auto values_view_of(std::string_view name) const {
      return values_of(name) | std::views::transform([](const std::string& value) -> std::string_view { return value; });
    }

    [[nodiscard]] std::string_view first_value_of(std::string_view name) const noexcept {
      auto it = find(name);
      return it != end() ? it->second : std::string_view{};
    }

    [[nodiscard]] bool is(std::string_view name, std::string_view expected_value) const {
      auto values = this->values_view_of(name);
      if (values.empty()) {
        return false;
      }

      return std::ranges::any_of(values, [expected_value](std::string_view value) {
        using aero::detail::ascii_iequal;
        return !value.empty() && ascii_iequal(value, expected_value);
      });
    }

    [[nodiscard]] std::string to_string() const {
      using http::detail::header_name_value_separator;
      using http::detail::header_value_separator;

      if (empty()) {
        return std::string{headers_end_separator};
      }

      std::string result;
      std::set<std::string> inserted_names;

      for (std::string_view header_name : names_view()) {
        auto lowercased_name = aero::detail::to_lowercase(header_name);
        if (auto [_, inserted] = inserted_names.insert(lowercased_name); !inserted) {
          continue;
        }

        result.reserve(result.size() + header_name.size() + header_name_value_separator.size());
        result.append(header_name).append(header_name_value_separator);

        bool is_first_value = true;
        for (std::string_view value : values_view_of(header_name)) {
          if (!is_first_value) {
            result.append(header_value_separator);
          }
          is_first_value = false;
          result.append(value);
        }

        result.append(header_separator);
      }

      result.append(header_separator);
      return result;
    }

    void merge(const header_fields& other) {
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      items_.reserve(items_.size() + other.items_.size());
      std::ranges::copy(other.items_, std::back_inserter(items_));
    }

    void merge(header_fields&& other) { // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      items_.reserve(items_.size() + other.items_.size());
      std::ranges::move(other.items_, std::back_inserter(items_));
      other.clear();
    }

    void remove_values_of(std::string_view name) {
      // Do not use structured binding here, as it causes a strange
      // ICE error during compilation using GCC 15.2 compiler
      auto range = fields_of(name);
      erase(range.begin(), range.end());
    }

    void erase(iterator first, iterator last) {
      items_.erase(first, last);
    }

    void erase(range_iterator first, range_iterator last) {
      if (first == last) {
        return;
      }

      const auto key = std::string_view{first.key()};
      auto kept = std::ranges::remove_if(items_,
        [&](const value_type& field) noexcept { return aero::detail::ascii_iequal(field.first, key); });
      items_.erase(kept.begin(), kept.end());
    }

   private:
    storage_type items_;
  };

  static_assert(std::ranges::viewable_range<header_fields>);

} // namespace aero::http::detail

#endif
