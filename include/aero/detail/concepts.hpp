#pragma once

#include <concepts>

namespace aero::detail::concepts {

  template <typename T, typename... U> concept either = (std::same_as<T, U> || ...);

} // namespace aero::detail::concepts
