#ifndef AERO_DETAIL_CONCEPTS_HPP
#define AERO_DETAIL_CONCEPTS_HPP

#include <concepts>
#include <ranges>

namespace aero::detail::concepts {

  template <typename T, typename... U> concept either = (std::same_as<T, U> || ...);

  template <typename Range>
  concept string_view_constructible_range =
    std::ranges::forward_range<Range> && requires(std::ranges::range_reference_t<Range> element) { std::string_view{element}; };

} // namespace aero::detail::concepts

#endif
