#pragma once

#include "ecs_lab/ecs_types.hpp"

#include <cassert>
#include <utility>

namespace ecs_lab {

inline ComponentId next_component_id() {
  static ComponentId next = 0;
  return next++;
}

template <typename T>
ComponentId component_id() {
  static ComponentId id = next_component_id();
  assert(id < kMaxComponents);
  return id;
}

template <typename T>
struct Component {
  std::uint32_t entity_idx = 0;
  std::uint32_t gen = 0;
  T data;

  Component() = default;

  template <typename... Args>
  Component(std::uint32_t idx, std::uint32_t g, Args&&... args)
      : entity_idx(idx), gen(g), data(std::forward<Args>(args)...) {}

  T* operator->() noexcept { return &data; }
  const T* operator->() const noexcept { return &data; }
  T& operator*() noexcept { return data; }
  const T& operator*() const noexcept { return data; }

  static ComponentId component_id();
};

template <typename T>
ComponentId Component<T>::component_id() {
  return ::ecs_lab::component_id<T>();
}

} // namespace ecs_lab
