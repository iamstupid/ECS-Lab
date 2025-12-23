#pragma once

#include "ecs_lab/ecs_types.hpp"

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>

#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__BMI2__) && !defined(_MSC_VER)
#include <immintrin.h>
#endif

namespace ecs_lab {

template <std::size_t MaxC = kMaxComponents>
class Signature {
public:
  static constexpr std::size_t kWordBits = 64;
  static constexpr std::size_t kWordCount = (MaxC + kWordBits - 1) / kWordBits;

  void clear() {
    words_.fill(0);
  }

  bool test(ComponentId cid) const {
    assert(cid < MaxC);
    const std::size_t word = cid / kWordBits;
    const std::size_t bit = cid % kWordBits;
    return (words_[word] >> bit) & 1ULL;
  }

  void set(ComponentId cid) {
    assert(cid < MaxC);
    const std::size_t word = cid / kWordBits;
    const std::size_t bit = cid % kWordBits;
    const std::uint64_t mask = (1ULL << bit);
    if ((words_[word] & mask) == 0) {
      words_[word] |= mask;
    }
  }

  void reset(ComponentId cid) {
    assert(cid < MaxC);
    const std::size_t word = cid / kWordBits;
    const std::size_t bit = cid % kWordBits;
    const std::uint64_t mask = (1ULL << bit);
    if ((words_[word] & mask) != 0) {
      words_[word] &= ~mask;
    }
  }

  std::size_t popcount() const {
    std::size_t count = 0;
    for (std::size_t i = 0; i < kWordCount; ++i) {
      count += static_cast<std::size_t>(popcnt64(words_[i]));
    }
    return count;
  }

  inline std::size_t rank(ComponentId cid) const {
    assert(cid < MaxC);
    const std::size_t word = static_cast<std::size_t>(cid) >> 6;
    const std::size_t bit = static_cast<std::size_t>(cid) & (kWordBits - 1);
    if constexpr (kWordCount == 1) {
      return static_cast<std::size_t>(popcnt64(lowbits(words_[word], static_cast<unsigned>(bit))));
    } else if constexpr (kWordCount == 2) {
      const std::uint64_t low = lowbits(words_[word], static_cast<unsigned>(bit));
      const std::size_t add_mask =
          static_cast<std::size_t>(-static_cast<std::ptrdiff_t>(word != 0));
      const std::size_t count0 = static_cast<std::size_t>(popcnt64(words_[0])) & add_mask;
      return count0 + static_cast<std::size_t>(popcnt64(low));
    } else {
      std::size_t count = 0;
      for (std::size_t i = 0; i < word; ++i) {
        count += static_cast<std::size_t>(popcnt64(words_[i]));
      }
      count += static_cast<std::size_t>(popcnt64(lowbits(words_[word], static_cast<unsigned>(bit))));
      return count;
    }
  }

  template <typename Fn>
  void for_each_set_bit(Fn&& fn) const {
    for (std::size_t word = 0; word < kWordCount; ++word) {
      std::uint64_t v = words_[word];
      while (v != 0) {
        const unsigned long bit = static_cast<unsigned long>(std::countr_zero(v));
        const ComponentId cid = static_cast<ComponentId>(word * kWordBits + bit);
        if (cid < MaxC) {
          fn(cid);
        }
        v &= (v - 1ULL);
      }
    }
  }

private:
  static inline std::uint32_t popcnt64(std::uint64_t value) {
#if defined(_MSC_VER)
    return static_cast<std::uint32_t>(__popcnt64(value));
#elif defined(__GNUC__) || defined(__clang__)
    return static_cast<std::uint32_t>(__builtin_popcountll(value));
#else
    return static_cast<std::uint32_t>(std::popcount(value));
#endif
  }

  static inline std::uint64_t lowbits(std::uint64_t value, unsigned bit) {
#if defined(__BMI2__) && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
    return _bzhi_u64(value, bit);
#else
    return value & ((1ULL << bit) - 1ULL);
#endif
  }

  std::array<std::uint64_t, kWordCount> words_{};
};

} // namespace ecs_lab
