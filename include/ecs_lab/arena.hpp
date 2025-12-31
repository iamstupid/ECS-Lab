#pragma once

#include "ecs_lab/ecs_types.hpp"
#include "ecs_lab/signature.hpp"

#include <memory>
#include <memory_resource>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs_lab {

class EntityProxy;

struct EntityMeta {
  explicit EntityMeta(std::pmr::memory_resource* resource)
      : idx(resource) {}
  EntityMeta(std::pmr::memory_resource* resource, const EntityMeta& other)
      : entity_id(other.entity_id),
        entity_idx(other.entity_idx),
        gen(other.gen),
        sig(other.sig),
        idx(resource) {
    idx = other.idx;
  }

  std::uint64_t entity_id = 0;
  std::uint32_t entity_idx = 0;
  std::uint32_t gen = 1;
  Signature<kMaxComponents> sig{};
  std::pmr::vector<DenseIndex> idx;
  // Cached pointer to a World-owned proxy object. Not serialized in snapshots.
  EntityProxy* proxy = nullptr;
};

class LinearArena {
public:
  explicit LinearArena(std::pmr::memory_resource* resource)
      : resource_(resource) {}

  LinearArena(LinearArena&& other) noexcept
      : blocks_(std::move(other.blocks_)),
        bump_(other.bump_),
        free_head_(other.free_head_),
        resource_(other.resource_) {
    other.bump_ = 0;
    other.free_head_ = kInvalidIndex;
    other.resource_ = nullptr;
  }

  LinearArena(const LinearArena& other)
      : resource_(other.resource_) {
    copy_from(other);
  }
  LinearArena& operator=(const LinearArena& other) {
    if (this == &other) {
      return *this;
    }
    clear_storage();
    resource_ = other.resource_;
    copy_from(other);
    return *this;
  }

  LinearArena& operator=(LinearArena&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    destroy_all();
    blocks_.clear();
    blocks_ = std::move(other.blocks_);
    bump_ = other.bump_;
    free_head_ = other.free_head_;
    resource_ = other.resource_;
    other.bump_ = 0;
    other.free_head_ = kInvalidIndex;
    other.resource_ = nullptr;
    return *this;
  }

  LinearArena clone_with_resource(std::pmr::memory_resource* resource) const {
    LinearArena out(resource);
    out.copy_from(*this);
    return out;
  }

  ~LinearArena() {
    destroy_all();
  }

  std::uint32_t alloc() {
    if (free_head_ != kInvalidIndex) {
      const std::uint32_t idx = free_head_;
      auto& meta = *ptr(idx);
      free_head_ = static_cast<std::uint32_t>(meta.entity_id);
      return idx;
    }

    const std::uint32_t idx = bump_;
    ensure_block_for(idx);
    std::construct_at(ptr(idx), resource_);
    ++bump_;
    return idx;
  }

  void free(std::uint32_t idx) {
    auto& meta = *ptr(idx);
    meta.entity_id = static_cast<std::uint64_t>(free_head_);
    free_head_ = idx;
  }

  EntityMeta& at(std::uint32_t idx) {
    return *ptr(idx);
  }

  const EntityMeta& at(std::uint32_t idx) const {
    return *ptr(idx);
  }

  std::size_t size() const { return bump_; }

private:
  void clear_storage() {
    destroy_all();
    free_head_ = kInvalidIndex;
  }

  void copy_from(const LinearArena& other) {
    free_head_ = other.free_head_;
    bump_ = 0;
    if (other.bump_ > 0) {
      ensure_block_for(other.bump_ - 1);
    }
    for (std::uint32_t i = 0; i < other.bump_; ++i) {
      std::construct_at(ptr(i), resource_, *other.ptr(i));
      ++bump_;
    }
  }

  void destroy_all() {
    for (std::uint32_t i = 0; i < bump_; ++i) {
      std::destroy_at(ptr(i));
    }
  }

  void ensure_block_for(std::uint32_t idx) {
    const std::size_t block_idx = idx / kBlockSize;
    while (block_idx >= blocks_.size()) {
      blocks_.push_back(std::make_unique<Storage[]>(kBlockSize));
    }
  }

  EntityMeta* ptr(std::uint32_t idx) {
    const std::size_t block_idx = idx / kBlockSize;
    const std::size_t offset = idx % kBlockSize;
    Storage* block = blocks_[block_idx].get();
    return std::launder(reinterpret_cast<EntityMeta*>(&block[offset]));
  }

  const EntityMeta* ptr(std::uint32_t idx) const {
    const std::size_t block_idx = idx / kBlockSize;
    const std::size_t offset = idx % kBlockSize;
    const Storage* block = blocks_[block_idx].get();
    return std::launder(reinterpret_cast<const EntityMeta*>(&block[offset]));
  }

  using Storage = std::aligned_storage_t<sizeof(EntityMeta), alignof(EntityMeta)>;
  static constexpr std::size_t kBlockSize = 4096;
  std::vector<std::unique_ptr<Storage[]>> blocks_;
  std::uint32_t bump_ = 0;
  std::uint32_t free_head_ = kInvalidIndex;
  std::pmr::memory_resource* resource_ = nullptr;
};

} // namespace ecs_lab
