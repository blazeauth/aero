#pragma once

#include <asio/recycling_allocator.hpp>
#include <cstddef>

namespace aero::detail {

  constexpr inline std::size_t default_allocator_alignment = 16;

  // See https://github.com/chriskohlhoff/asio/pull/1724
  template <typename T = std::byte, std::size_t Alignment = default_allocator_alignment>
  struct alignas(Alignment < alignof(T) ? alignof(T) : Alignment) aligned_allocator {
    using value_type = T;
    using is_always_equal = std::true_type;

    aligned_allocator() noexcept = default;

    template <typename OtherT>
    explicit aligned_allocator(const aligned_allocator<OtherT, Alignment>&) noexcept {}

    template <typename OtherT>
    struct rebind {
      using other = aligned_allocator<OtherT, Alignment>;
    };

    [[nodiscard]] T* allocate(std::size_t element_count) {
      return asio::recycling_allocator<T>{}.allocate(element_count);
    }

    void deallocate(T* pointer, std::size_t element_count) noexcept {
      asio::recycling_allocator<T>{}.deallocate(pointer, element_count);
    }

    template <typename OtherT>
    bool operator==(const aligned_allocator<OtherT, Alignment>&) const noexcept {
      return true;
    }

    template <typename OtherT>
    bool operator!=(const aligned_allocator<OtherT, Alignment>&) const noexcept {
      return false;
    }
  };

} // namespace aero::detail
