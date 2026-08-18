#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aero/detail/attributes.hpp"
#include "aero/util/string.hpp"

namespace aero::urls {

  struct query_param {
    std::string name;
    std::optional<std::string> value;

    [[nodiscard]] bool has_value() const noexcept {
      return value.has_value();
    }

    [[nodiscard]] std::string_view value_or(std::string_view fallback AERO_LIFETIMEBOUND) const noexcept {
      return value.has_value() ? std::string_view{*value} : fallback;
    }
  };

  class query_params {
   public:
    using value_type = urls::query_param;
    using allocator_type = std::vector<value_type>::allocator_type;
    using pointer = std::vector<value_type>::pointer;
    using const_pointer = std::vector<value_type>::const_pointer;
    using reference = std::vector<value_type>::reference;
    using const_reference = std::vector<value_type>::const_reference;
    using size_type = std::vector<value_type>::size_type;
    using difference_type = std::vector<value_type>::difference_type;

    using iterator = std::vector<value_type>::iterator;
    using const_iterator = std::vector<value_type>::const_iterator;
    using reverse_iterator = std::vector<value_type>::reverse_iterator;
    using const_reverse_iterator = std::vector<value_type>::const_reverse_iterator;

    query_params() = default;
    query_params(std::initializer_list<value_type> params): params_(params) {}

    static std::expected<query_params, std::error_code> parse(std::string_view str);
    static std::expected<query_params, std::error_code> parse(std::span<const std::byte> bytes);

    [[nodiscard]] bool empty() const noexcept {
      return params_.empty();
    }
    [[nodiscard]] size_type size() const noexcept {
      return params_.size();
    }
    [[nodiscard]] size_type capacity() const noexcept {
      return params_.capacity();
    }

    [[nodiscard]] iterator begin() & noexcept {
      return params_.begin();
    }
    [[nodiscard]] iterator end() & noexcept {
      return params_.end();
    }
    [[nodiscard]] const_iterator begin() const& noexcept {
      return params_.begin();
    }
    [[nodiscard]] const_iterator end() const& noexcept {
      return params_.end();
    }

    [[nodiscard]] reverse_iterator rbegin() & noexcept {
      return params_.rbegin();
    }
    [[nodiscard]] reverse_iterator rend() & noexcept {
      return params_.rend();
    }
    [[nodiscard]] const_reverse_iterator rbegin() const& noexcept {
      return params_.rbegin();
    }
    [[nodiscard]] const_reverse_iterator rend() const& noexcept {
      return params_.rend();
    }

    [[nodiscard]] const_iterator cbegin() const& noexcept {
      return params_.cbegin();
    }
    [[nodiscard]] const_iterator cend() const& noexcept {
      return params_.cend();
    }

    [[nodiscard]] query_param& front() & noexcept {
      return params_.front();
    }
    [[nodiscard]] const query_param& front() const& noexcept {
      return params_.front();
    }
    [[nodiscard]] query_param& back() & noexcept {
      return params_.back();
    }
    [[nodiscard]] const query_param& back() const& noexcept {
      return params_.back();
    }

    void reserve(size_type count) {
      params_.reserve(count);
    }

    void clear() noexcept {
      params_.clear();
    }

    [[nodiscard]] iterator find(std::string_view name) & noexcept {
      return std::ranges::find_if(params_, [&](const query_param& param) noexcept { return param.name == name; });
    }

    [[nodiscard]] const_iterator find(std::string_view name) const& noexcept {
      return std::ranges::find_if(params_, [&](const query_param& param) noexcept { return param.name == name; });
    }

    [[nodiscard]] auto entries(std::string_view name AERO_LIFETIMEBOUND) & {
      return params_ | std::views::filter([name](const query_param& param) noexcept { return param.name == name; });
    }

    [[nodiscard]] auto entries(std::string_view name AERO_LIFETIMEBOUND) const& {
      return params_ | std::views::filter([name](const query_param& param) noexcept { return param.name == name; });
    }

    [[nodiscard]] auto names() const& {
      return params_ | std::views::transform([](const query_param& param) noexcept -> std::string_view { return param.name; });
    }

    [[nodiscard]] auto values() const& {
      return params_ | std::views::transform(
                         [](const query_param& param) noexcept -> std::optional<std::string_view> { return param.value; });
    }

    [[nodiscard]] auto values(std::string_view name AERO_LIFETIMEBOUND) const& {
      return entries(name) | std::views::transform([](const query_param& param) noexcept -> std::optional<std::string_view> {
        return param.value;
      });
    }

    [[nodiscard]] std::size_t count(std::string_view name) const& noexcept {
      return std::ranges::count(params_, name, &query_param::name);
    }

    [[nodiscard]] std::optional<std::string_view> first_value(std::string_view name) const& noexcept {
      auto iterator = find(name);
      if (iterator == end()) {
        return std::nullopt;
      }
      return iterator->value;
    }

    [[nodiscard]] bool contains(std::string_view name) const noexcept {
      return find(name) != end(); // NOLINT(*-contains)
    }

    [[nodiscard]] std::string to_string() const {
      std::string result;
      std::size_t total_size{};

      for (const query_param& param : params_) {
        total_size += param.name.size() + 1; // 1 for '&'
        if (param.value.has_value()) {
          total_size += 1 + param.value->size(); // 1 for '='
        }
      }

      result.reserve(total_size);

      bool is_first = true;
      for (const query_param& param : params_) {
        if (!std::exchange(is_first, false)) {
          result += '&';
        }

        result += encode_query_param(param.name);
        if (param.value.has_value()) {
          result += '=';
          result += encode_query_param(*param.value);
        }
      }

      return result;
    }

    iterator set(std::string name) & {
      auto target = find(name);
      if (target == end()) {
        return add(std::move(name));
      }

      target->name = std::move(name);
      target->value = std::nullopt;

      auto duplicates = std::ranges::remove_if(std::next(target), params_.end(), [&](const query_param& field) noexcept {
        return field.name == target->name;
      });
      params_.erase(duplicates.begin(), duplicates.end());

      return target;
    }

    iterator set(std::string name, std::string value) & {
      auto target = find(name);
      if (target == end()) {
        return add(std::move(name), std::move(value));
      }

      target->name = std::move(name);
      target->value = std::move(value);

      auto duplicates = std::ranges::remove_if(std::next(target), params_.end(), [&](const query_param& field) noexcept {
        return field.name == target->name;
      });
      params_.erase(duplicates.begin(), duplicates.end());

      return target;
    }

    iterator add(std::string name) & {
      params_.emplace_back(std::move(name), std::nullopt);
      return std::prev(params_.end());
    }

    iterator add(std::string name, std::string value) & {
      params_.emplace_back(std::move(name), std::move(value));
      return std::prev(params_.end());
    }

    void append(const query_params& other) {
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      params_.reserve(params_.size() + other.params_.size());
      std::ranges::copy(other.params_, std::back_inserter(params_));
    }

    void append(query_params&& other) { // NOLINT(*-rvalue-reference-param-not-moved)
      if (std::addressof(other) == this || other.empty()) {
        return;
      }
      params_.reserve(params_.size() + other.params_.size());
      std::ranges::move(other.params_, std::back_inserter(params_));
      other.clear();
    }

    void erase(std::string_view name) {
      auto to_remove = std::ranges::remove(params_, name, &value_type::name);
      params_.erase(to_remove.begin(), to_remove.end());
    }

    // NOLINTBEGIN(*-use-nodiscard)
    // Disallow use on temporary objects
    iterator begin() && = delete;
    iterator end() && = delete;
    const_iterator begin() const&& = delete;
    const_iterator end() const&& = delete;
    reverse_iterator rbegin() && = delete;
    reverse_iterator rend() && = delete;
    const_reverse_iterator rbegin() const&& = delete;
    const_reverse_iterator rend() const&& = delete;
    const_iterator cbegin() const&& = delete;
    const_iterator cend() const&& = delete;
    query_param& front() && = delete;
    const query_param& front() const&& = delete;
    query_param& back() && = delete;
    const query_param& back() const&& = delete;
    iterator find(std::string_view name) && = delete;
    const_iterator find(std::string_view name) const&& = delete;
    auto entries(std::string_view name) && = delete;
    auto entries(std::string_view name) const&& = delete;
    auto names() const&& = delete;
    auto values() const&& = delete;
    auto values(std::string_view name) const&& = delete;
    std::optional<std::string_view> first_value(std::string_view name) const&& = delete;
    iterator add(std::string name) && = delete;
    iterator add(std::string name, std::string value) && = delete;
    iterator set(std::string name) && = delete;
    iterator set(std::string name, std::string value) && = delete;
    // NOLINTEND(*-use-nodiscard)

   private:
    static std::string encode_query_param(std::string_view input) {
      constexpr std::string_view hex_digits = "0123456789ABCDEF";

      std::string output;
      output.reserve(input.size());

      for (char c : input) {
        // WHATWG URL SPEC, Section 1.3:
        // If spaceAsPlus is true and byte is 0x20 (SP), then append U+002B (+)
        // to output and continue.
        if (c == ' ') {
          output.push_back('+');
          continue;
        }

        // WHATWG URL SPEC, Section 1.3:
        // The application/x-www-form-urlencoded percent-encode set contains all
        // code points, except the ASCII alphanumeric, U+002A (*), U+002D (-),
        // U+002E (.), and U+005F (_).
        if (aero::is_alnum(c) || c == '*' || c == '-' || c == '.' || c == '_') {
          output.push_back(c);
          continue;
        }

        output.push_back('%');
        output.push_back(hex_digits[static_cast<unsigned char>(c) >> 4U]);
        output.push_back(hex_digits[static_cast<unsigned char>(c) & 0x0FU]);
      }

      return output;
    }

    std::vector<value_type> params_;
  };

} // namespace aero::urls

#include "aero/urls/impl/query_params_parser.ipp"
