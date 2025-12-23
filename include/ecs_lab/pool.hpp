#pragma once

#include "ecs_lab/component.hpp"
#include "ecs_lab/dense_array.hpp"

#include <memory>

namespace ecs_lab {

class World;

struct IPool {
  virtual ~IPool() = default;
  virtual void erase_dense(DenseIndex di, World& world) = 0;
  virtual DenseIndex clone_dense(std::uint32_t dst_entity_idx, std::uint32_t dst_gen, DenseIndex src_di) = 0;
  virtual void* component_ptr(DenseIndex di) = 0;
  virtual std::unique_ptr<IPool> clone() const = 0;
};

template <typename T>
class Pool final : public IPool {
public:
  DenseArray<Component<T>> items;

  template <typename... Args>
  DenseIndex emplace(std::uint32_t entity_idx, std::uint32_t gen, Args&&... args) {
    return static_cast<DenseIndex>(items.emplace_back(entity_idx, gen, std::forward<Args>(args)...));
  }

  void erase_dense(DenseIndex di, World& world) override;
  DenseIndex clone_dense(std::uint32_t dst_entity_idx, std::uint32_t dst_gen, DenseIndex src_di) override {
    const auto& src = items[src_di];
    return static_cast<DenseIndex>(items.emplace_back(dst_entity_idx, dst_gen, src.data));
  }
  void* component_ptr(DenseIndex di) override {
    return &items[di];
  }
  std::unique_ptr<IPool> clone() const override {
    auto out = std::make_unique<Pool<T>>();
    out->items = items;
    return out;
  }
};

} // namespace ecs_lab
