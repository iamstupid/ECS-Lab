#include "ecs_lab/signature.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::uint32_t xorshift32(std::uint32_t& state) {
  std::uint32_t x = state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state = x;
  return x;
}

} // namespace

int main(int argc, char** argv) {
  std::size_t iterations = 50'000'000;
  bool run_mem = true;
  bool run_pure = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--pure") {
      run_mem = false;
      continue;
    }
    if (arg == "--mem") {
      run_pure = false;
      continue;
    }
    if (!arg.empty() && std::isdigit(static_cast<unsigned char>(arg[0]))) {
      iterations = static_cast<std::size_t>(std::stoull(arg));
    }
  }

  ecs_lab::Signature<> sig;
  for (ecs_lab::ComponentId i = 0; i < ecs_lab::kMaxComponents; i += 2) {
    sig.set(i);
  }

  std::uint32_t rng = 0x12345678u;
  if (run_mem) {
    auto cids = std::make_unique<ecs_lab::ComponentId[]>(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
      cids[i] = static_cast<ecs_lab::ComponentId>(xorshift32(rng) % ecs_lab::kMaxComponents);
    }
    const ecs_lab::ComponentId* cids_ptr = cids.get();
    volatile std::size_t sink = 0;

    const auto start = std::chrono::high_resolution_clock::now();
    const ecs_lab::ComponentId* it = cids_ptr;
    const ecs_lab::ComponentId* end_ptr = cids_ptr + iterations;
    for (; it != end_ptr; ++it) {
      sink += sig.rank(*it);
    }
    const auto end_time = std::chrono::high_resolution_clock::now();

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start).count();
    const double per_call = static_cast<double>(ns) / static_cast<double>(iterations);

    std::cout << "Signature::rank benchmark (memory)\n";
    std::cout << "iterations: " << iterations << "\n";
    std::cout << "total: " << ns / 1e6 << " ms\n";
    std::cout << "ns/call: " << per_call << "\n";
    std::cout << "sink: " << sink << "\n";
  }

  if (run_pure) {
    constexpr std::size_t kPureCount = 1024;
    constexpr std::size_t kPureMask = kPureCount - 1;
    ecs_lab::ComponentId pure_cids[kPureCount];
    for (std::size_t i = 0; i < kPureCount; ++i) {
      pure_cids[i] = static_cast<ecs_lab::ComponentId>(xorshift32(rng) % ecs_lab::kMaxComponents);
    }

    const auto* data = pure_cids;
    std::size_t acc0 = 0;
    std::size_t acc1 = 0;
    std::size_t acc2 = 0;
    std::size_t acc3 = 0;

    const auto start = std::chrono::high_resolution_clock::now();
    std::size_t i = 0;
    const std::size_t limit = iterations & ~static_cast<std::size_t>(3);
    for (; i < limit; i += 4) {
      acc0 += sig.rank(data[i & kPureMask]);
      acc1 += sig.rank(data[(i + 1) & kPureMask]);
      acc2 += sig.rank(data[(i + 2) & kPureMask]);
      acc3 += sig.rank(data[(i + 3) & kPureMask]);
    }
    for (; i < iterations; ++i) {
      acc0 += sig.rank(data[i & kPureMask]);
    }
    const auto end_time = std::chrono::high_resolution_clock::now();

    const std::size_t acc = acc0 + acc1 + acc2 + acc3;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    volatile std::size_t sink = acc;

    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start).count();
    const double per_call = static_cast<double>(ns) / static_cast<double>(iterations);

    std::cout << "Signature::rank benchmark (pure)\n";
    std::cout << "iterations: " << iterations << "\n";
    std::cout << "total: " << ns / 1e6 << " ms\n";
    std::cout << "ns/call: " << per_call << "\n";
    std::cout << "sink: " << sink << "\n";
  }
  return 0;
}
