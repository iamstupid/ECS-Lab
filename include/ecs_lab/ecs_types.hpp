#pragma once

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ecs_lab {

using ComponentId = std::uint16_t;
using DenseIndex = std::uint32_t;

constexpr ComponentId kMaxComponents = 128;
constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;
constexpr std::uint32_t kGenAliveBit = 0x80000000u;
constexpr std::uint32_t kGenMask = 0x7FFFFFFFu;

struct Entity {
  std::uint64_t entity_id = 0;
  std::uint32_t entity_idx = 0;
  std::uint32_t gen = 0;
};

template <typename... Ts>
struct Prefab {
  std::tuple<Ts...> data;
};

template <typename... Ts>
Prefab<std::decay_t<Ts>...> make_prefab(Ts&&... args) {
  return Prefab<std::decay_t<Ts>...>{
      std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(args)...)};
}

template <typename... Ts>
struct are_unique;

template <>
struct are_unique<> : std::true_type {};

template <typename T, typename... Rest>
struct are_unique<T, Rest...>
    : std::bool_constant<(!std::is_same_v<T, Rest> && ...) && are_unique<Rest...>::value> {};

} // namespace ecs_lab
