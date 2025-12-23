#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs_lab {

template <typename T, std::size_t BlockSize = 4096>
class DenseArray {
public:
  DenseArray() = default;

  DenseArray(const DenseArray& other) {
    reserve_blocks(other.blocks_.size());
    for (std::size_t i = 0; i < other.size_; ++i) {
      emplace_back(other[i]);
    }
  }

  DenseArray& operator=(const DenseArray& other) {
    if (this == &other) {
      return *this;
    }
    clear();
    reserve_blocks(other.blocks_.size());
    for (std::size_t i = 0; i < other.size_; ++i) {
      emplace_back(other[i]);
    }
    return *this;
  }

  ~DenseArray() {
    clear();
  }

  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T& operator[](std::size_t idx) {
    return *ptr(idx);
  }

  const T& operator[](std::size_t idx) const {
    return *ptr(idx);
  }

  template <typename... Args>
  std::size_t emplace_back(Args&&... args) {
    const std::size_t idx = size_;
    ensure_capacity(idx);
    new (ptr(idx)) T(std::forward<Args>(args)...);
    ++size_;
    return idx;
  }

  void pop_back() {
    if (size_ == 0) {
      return;
    }
    --size_;
    ptr(size_)->~T();
  }

  void clear() {
    while (size_ > 0) {
      pop_back();
    }
  }

private:
  using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  void reserve_blocks(std::size_t count) {
    blocks_.reserve(count);
  }

  void ensure_capacity(std::size_t idx) {
    const std::size_t block_idx = idx / BlockSize;
    if (block_idx >= blocks_.size()) {
      blocks_.push_back(std::make_unique<Storage[]>(BlockSize));
    }
  }

  T* ptr(std::size_t idx) {
    const std::size_t block_idx = idx / BlockSize;
    const std::size_t offset = idx % BlockSize;
    Storage* block = blocks_[block_idx].get();
    return std::launder(reinterpret_cast<T*>(&block[offset]));
  }

  const T* ptr(std::size_t idx) const {
    const std::size_t block_idx = idx / BlockSize;
    const std::size_t offset = idx % BlockSize;
    const Storage* block = blocks_[block_idx].get();
    return std::launder(reinterpret_cast<const T*>(&block[offset]));
  }

  std::vector<std::unique_ptr<Storage[]>> blocks_;
  std::size_t size_ = 0;
};

} // namespace ecs_lab
