#pragma once

#include "ecs_lab/arena.hpp"
#include "ecs_lab/pool.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <tuple>
#include <utility>
#include <vector>

namespace ecs_lab {

class EntityProxy;

template <typename T>
struct QueryAccess {
  ComponentId cid = 0;
  Pool<T>* pool = nullptr;
};

template <typename T>
T& query_get(EntityMeta& meta, const QueryAccess<T>& access) {
  assert(access.pool != nullptr);
  const std::size_t pos = meta.sig.rank(access.cid);
  assert(pos < meta.idx.size());
  const DenseIndex di = meta.idx[pos];
  return access.pool->items[di].data;
}

class World {
public:
  struct Snapshot {
    Snapshot()
        : idx_resource(std::make_unique<std::pmr::unsynchronized_pool_resource>()),
          arena(idx_resource.get()) {
      pools.resize(kMaxComponents);
    }

    Snapshot(Snapshot&&) noexcept = default;
    Snapshot& operator=(Snapshot&&) noexcept = default;
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    std::unique_ptr<std::pmr::unsynchronized_pool_resource> idx_resource;
    LinearArena arena;
    std::vector<std::unique_ptr<IPool>> pools;
    std::uint64_t next_entity_id = 0;
  };

  World()
      : arena_(&idx_resource_) {
    pools_.resize(kMaxComponents);
  }

  std::shared_ptr<EntityProxy> get_proxy(Entity e);

  Entity create() {
    const std::uint32_t idx = arena_.alloc();
    auto& meta = arena_.at(idx);
    meta.entity_id = ++next_entity_id_;
    meta.entity_idx = idx;
    meta.gen = (meta.gen & kGenMask) | kGenAliveBit;
    meta.sig.clear();
    meta.idx.clear();
    return Entity{meta.entity_id, idx, meta.gen};
  }

  void destroy(Entity e) {
    auto* meta = validate(e);
    if (!meta) {
      return;
    }

    invalidate_proxy_all(*meta);

    std::size_t i = 0;
    meta->sig.for_each_set_bit([&](ComponentId cid) {
      const DenseIndex di = meta->idx[i++];
      if (cid < pools_.size() && pools_[cid]) {
        pools_[cid]->erase_dense(di, *this);
      }
    });

    meta->sig.clear();
    meta->idx.clear();
    meta->gen = (meta->gen + 1u) & kGenMask;
    arena_.free(e.entity_idx);
  }

  bool is_alive(Entity e) const {
    return validate_const(e) != nullptr;
  }

  // Reconstruct a full Entity handle (including entity_id) from (entity_idx, gen).
  // Returns Entity{0,0,0} if the handle is not alive / mismatched.
  Entity resolve_idx_gen(std::uint32_t entity_idx, std::uint32_t gen) const {
    if (entity_idx >= arena_.size()) {
      return Entity{};
    }
    const auto& meta = arena_.at(entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != gen) {
      return Entity{};
    }
    return Entity{meta.entity_id, entity_idx, gen};
  }

  template <typename T>
  bool has(Entity e) const {
    const auto* meta = validate_const(e);
    if (!meta) {
      return false;
    }
    return meta->sig.test(component_id<T>());
  }

  template <typename T>
  T* try_get(Entity e) {
    auto* meta = validate(e);
    if (!meta) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta->sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta->sig.rank(cid);
    const DenseIndex di = meta->idx[pos];
    auto& pool = get_pool<T>();
    return &pool.items[di].data;
  }

  // Fast access when you only have (entity_idx, gen).
  // Useful for compact references inside components (idx+gen), without storing entity_id.
  template <typename T>
  T* try_get_idx_gen(std::uint32_t entity_idx, std::uint32_t gen) {
    if (entity_idx >= arena_.size()) {
      return nullptr;
    }
    auto& meta = arena_.at(entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != gen) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta.sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta.sig.rank(cid);
    const DenseIndex di = meta.idx[pos];
    auto* pool = get_pool_if_exists<T>();
    if (!pool) {
      return nullptr;
    }
    return &pool->items[di].data;
  }

  template <typename T>
  Component<T>* try_get_component(Entity e) {
    auto* meta = validate(e);
    if (!meta) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta->sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta->sig.rank(cid);
    const DenseIndex di = meta->idx[pos];
    auto& pool = get_pool<T>();
    return &pool.items[di];
  }

  template <typename T>
  const T* try_get_idx_gen(std::uint32_t entity_idx, std::uint32_t gen) const {
    if (entity_idx >= arena_.size()) {
      return nullptr;
    }
    const auto& meta = arena_.at(entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != gen) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta.sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta.sig.rank(cid);
    const DenseIndex di = meta.idx[pos];
    const auto* pool = get_pool_const<T>();
    if (!pool) {
      return nullptr;
    }
    return &pool->items[di].data;
  }

  template <typename T>
  const T* try_get(Entity e) const {
    const auto* meta = validate_const(e);
    if (!meta) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta->sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta->sig.rank(cid);
    const DenseIndex di = meta->idx[pos];
    const auto* pool = get_pool_const<T>();
    if (!pool) {
      return nullptr;
    }
    return &pool->items[di].data;
  }

  template <typename T>
  const Component<T>* try_get_component(Entity e) const {
    const auto* meta = validate_const(e);
    if (!meta) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    if (!meta->sig.test(cid)) {
      return nullptr;
    }
    const std::size_t pos = meta->sig.rank(cid);
    const DenseIndex di = meta->idx[pos];
    const auto* pool = get_pool_const<T>();
    if (!pool) {
      return nullptr;
    }
    return &pool->items[di];
  }

  template <typename T>
  T& get(Entity e) {
    auto* ptr = try_get<T>(e);
    assert(ptr != nullptr);
    return *ptr;
  }

  template <typename T, typename... Args>
  T& add(Entity e, Args&&... args) {
    auto* meta = validate(e);
    assert(meta != nullptr);
    const ComponentId cid = component_id<T>();
    if (meta->sig.test(cid)) {
      return get<T>(e);
    }

    const std::size_t pos = meta->sig.rank(cid);
    meta->sig.set(cid);
    auto& pool = get_pool<T>();
    const DenseIndex di = pool.emplace(e.entity_idx, e.gen, std::forward<Args>(args)...);
    meta->idx.insert(meta->idx.begin() + static_cast<std::ptrdiff_t>(pos), di);
    notify_proxy_component_ptr(*meta, cid, &pool.items[di]);
    return pool.items[di].data;
  }

  template <typename T>
  void remove(Entity e) {
    auto* meta = validate(e);
    if (!meta) {
      return;
    }
    const ComponentId cid = component_id<T>();
    if (!meta->sig.test(cid)) {
      return;
    }

    const std::size_t pos = meta->sig.rank(cid);
    const DenseIndex di = meta->idx[pos];
    if (cid < pools_.size() && pools_[cid]) {
      pools_[cid]->erase_dense(di, *this);
    }
    meta->idx.erase(meta->idx.begin() + static_cast<std::ptrdiff_t>(pos));
    meta->sig.reset(cid);
    notify_proxy_missing(*meta, cid);
  }

  void add_missing_components(Entity dst, Entity src) {
    auto* dst_meta = validate(dst);
    const auto* src_meta = validate_const(src);
    if (!dst_meta || !src_meta) {
      return;
    }

    std::size_t i = 0;
    src_meta->sig.for_each_set_bit([&](ComponentId cid) {
      const DenseIndex src_di = src_meta->idx[i++];
      if (dst_meta->sig.test(cid)) {
        return;
      }
      if (cid >= pools_.size() || !pools_[cid]) {
        return;
      }
      const std::size_t pos = dst_meta->sig.rank(cid);
      dst_meta->sig.set(cid);
      const DenseIndex di = pools_[cid]->clone_dense(dst_meta->entity_idx, dst_meta->gen, src_di);
      dst_meta->idx.insert(dst_meta->idx.begin() + static_cast<std::ptrdiff_t>(pos), di);
      notify_proxy_component_ptr(*dst_meta, cid, pools_[cid]->component_ptr(di));
    });
  }

  template <typename T, typename Fn>
  void each(Fn&& fn) {
    auto& pool = get_pool<T>();
    const std::size_t count = pool.items.size();
    for (std::size_t i = 0; i < count; ++i) {
      auto& comp = pool.items[i];
      auto& meta = arena_.at(comp.entity_idx);
      if ((meta.gen & kGenAliveBit) == 0 || meta.gen != comp.gen) {
        continue;
      }
      Entity e{meta.entity_id, comp.entity_idx, comp.gen};
      fn(e, comp.data);
    }
  }

  template <typename T0, typename... Ts, typename Fn>
  void query(Fn&& fn) {
    static_assert(are_unique<T0, Ts...>::value, "Query component types must be unique.");

    auto* pool0 = get_pool_if_exists<T0>();
    if (!pool0) {
      return;
    }

    auto access = std::make_tuple(QueryAccess<Ts>{component_id<Ts>(), get_pool_if_exists<Ts>()}...);
    if constexpr (sizeof...(Ts) > 0) {
      bool ok = true;
      std::apply([&](auto&... a) { ok = ((a.pool != nullptr) && ...); }, access);
      if (!ok) {
        return;
      }
    }

    Signature<kMaxComponents> required{};
    required.clear();
    required.set(component_id<T0>());
    (required.set(component_id<Ts>()), ...);

    const std::size_t count = pool0->items.size();
    for (std::size_t i = 0; i < count; ++i) {
      auto& comp0 = pool0->items[i];
      auto& meta = arena_.at(comp0.entity_idx);
      if ((meta.gen & kGenAliveBit) == 0 || meta.gen != comp0.gen) {
        continue;
      }
      if constexpr (sizeof...(Ts) > 0) {
        if (!meta.sig.contains_all(required)) {
          continue;
        }
      }
      Entity e{meta.entity_id, comp0.entity_idx, comp0.gen};

      if constexpr (sizeof...(Ts) == 0) {
        fn(e, comp0.data);
      } else {
        std::apply([&](auto&... a) { fn(e, comp0.data, query_get(meta, a)...); }, access);
      }
    }
  }

  template <typename... Ts>
  Entity instantiate(const Prefab<Ts...>& prefab) {
    static_assert(are_unique<Ts...>::value, "Prefab component types must be unique.");
    Entity e = create();
    constexpr std::size_t count = sizeof...(Ts);
    if constexpr (count == 0) {
      return e;
    }

    auto& meta = arena_.at(e.entity_idx);
    auto entries = make_prefab_entries(prefab);
    std::sort(entries.begin(), entries.end(),
              [](const PrefabEntry& a, const PrefabEntry& b) { return a.cid < b.cid; });

    meta.sig.clear();
    meta.idx.clear();
    meta.idx.resize(count);

    for (std::size_t i = 0; i < count; ++i) {
      const auto& entry = entries[i];
      if (i > 0) {
        assert(entries[i - 1].cid != entry.cid);
      }
      meta.sig.set(entry.cid);
      DenseIndex di = kInvalidIndex;
      entry.emplace(*this, meta, entry.data, di);
      meta.idx[i] = di;
    }
    return e;
  }

  Snapshot snapshot() const {
    Snapshot snap;
    snap.next_entity_id = next_entity_id_;
    snap.arena = arena_.clone_with_resource(snap.idx_resource.get());
    snap.pools.resize(pools_.size());
    for (std::size_t i = 0; i < pools_.size(); ++i) {
      if (pools_[i]) {
        snap.pools[i] = pools_[i]->clone();
      }
    }
    return snap;
  }

  void restore(const Snapshot& snap) {
    arena_ = snap.arena.clone_with_resource(&idx_resource_);
    pools_.clear();
    pools_.resize(kMaxComponents);
    for (std::size_t i = 0; i < snap.pools.size(); ++i) {
      if (snap.pools[i]) {
        pools_[i] = snap.pools[i]->clone();
      }
    }
    next_entity_id_ = snap.next_entity_id;
  }

private:
  struct PrefabEntry {
    ComponentId cid = 0;
    const void* data = nullptr;
    void (*emplace)(World& world, EntityMeta& meta, const void* data, DenseIndex& out) = nullptr;
  };

  template <typename T>
  static void prefab_emplace(World& world, EntityMeta& meta, const void* data, DenseIndex& out) {
    auto& pool = world.get_pool<T>();
    const auto* value = static_cast<const T*>(data);
    out = pool.emplace(meta.entity_idx, meta.gen, *value);
  }

  template <typename... Ts, std::size_t... I>
  static std::array<PrefabEntry, sizeof...(Ts)> make_prefab_entries(const Prefab<Ts...>& prefab,
                                                                    std::index_sequence<I...>) {
    return {PrefabEntry{component_id<Ts>(), &std::get<I>(prefab.data), &prefab_emplace<Ts>}...};
  }

  template <typename... Ts>
  static std::array<PrefabEntry, sizeof...(Ts)> make_prefab_entries(const Prefab<Ts...>& prefab) {
    return make_prefab_entries(prefab, std::index_sequence_for<Ts...>{});
  }

  template <typename T>
  Pool<T>& get_pool() {
    const ComponentId cid = component_id<T>();
    if (!pools_[cid]) {
      pools_[cid] = std::make_unique<Pool<T>>();
    }
    return *static_cast<Pool<T>*>(pools_[cid].get());
  }

  template <typename T>
  const Pool<T>* get_pool_const() const {
    const ComponentId cid = component_id<T>();
    if (cid >= pools_.size()) {
      return nullptr;
    }
    return static_cast<const Pool<T>*>(pools_[cid].get());
  }

  template <typename T>
  Pool<T>* get_pool_if_exists() {
    const ComponentId cid = component_id<T>();
    if (cid >= pools_.size()) {
      return nullptr;
    }
    return static_cast<Pool<T>*>(pools_[cid].get());
  }

  EntityMeta* validate(Entity e) {
    if (e.entity_idx >= arena_.size()) {
      return nullptr;
    }
    auto& meta = arena_.at(e.entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != e.gen || meta.entity_id != e.entity_id) {
      return nullptr;
    }
    return &meta;
  }

  const EntityMeta* validate_const(Entity e) const {
    if (e.entity_idx >= arena_.size()) {
      return nullptr;
    }
    const auto& meta = arena_.at(e.entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != e.gen || meta.entity_id != e.entity_id) {
      return nullptr;
    }
    return &meta;
  }

  void update_moved(DenseIndex di, std::uint32_t entity_idx, std::uint32_t gen, ComponentId cid) {
    if (entity_idx >= arena_.size()) {
      return;
    }
    auto& meta = arena_.at(entity_idx);
    if ((meta.gen & kGenAliveBit) == 0 || meta.gen != gen) {
      return;
    }
    const std::size_t pos = meta.sig.rank(cid);
    if (pos < meta.idx.size()) {
      meta.idx[pos] = di;
    }
    if (cid < pools_.size() && pools_[cid]) {
      notify_proxy_component_ptr(meta, cid, pools_[cid]->component_ptr(di));
    } else {
      invalidate_proxy_component(meta, cid);
    }
  }

  std::pmr::unsynchronized_pool_resource idx_resource_{};
  std::pmr::unsynchronized_pool_resource proxy_resource_{};
  LinearArena arena_;
  std::vector<std::unique_ptr<IPool>> pools_;
  std::uint64_t next_entity_id_ = 0;

  template <typename T>
  friend class Pool;

  void invalidate_proxy_component(EntityMeta& meta, ComponentId cid);
  void notify_proxy_missing(EntityMeta& meta, ComponentId cid);
  void notify_proxy_component_ptr(EntityMeta& meta, ComponentId cid, void* comp_ptr);
  void invalidate_proxy_all(EntityMeta& meta);
};

class EntityProxy {
public:
  EntityProxy() = default;
  EntityProxy(World& world, Entity entity)
      : world_(&world), entity_(entity) {
    cache_.fill(nullptr);
  }

  void reset(World& world, Entity entity) {
    world_ = &world;
    entity_ = entity;
    cache_.fill(nullptr);
  }

  void clear_cache() {
    cache_.fill(nullptr);
  }

  Entity entity() const { return entity_; }

  bool is_alive() const {
    return alive_ && world_ && world_->is_alive(entity_);
  }

  template <typename T>
  T* try_get() {
    if (!world_ || !alive_) {
      return nullptr;
    }
    const ComponentId cid = component_id<T>();
    void* slot = cache_[cid];
    if (slot == missing_tag()) {
      return nullptr;
    }
    if (slot) {
      auto* comp = static_cast<Component<T>*>(slot);
      if (comp->entity_idx == entity_.entity_idx && comp->gen == entity_.gen) {
        return &comp->data;
      }
      cache_[cid] = nullptr;
    }
    auto* comp = world_->try_get_component<T>(entity_);
    if (!comp) {
      cache_[cid] = missing_tag();
      return nullptr;
    }
    cache_[cid] = comp;
    return &comp->data;
  }

  template <typename T>
  const T* try_get() const {
    return const_cast<EntityProxy*>(this)->try_get<T>();
  }

  template <typename T>
  T& get() {
    auto* ptr = try_get<T>();
    assert(ptr != nullptr);
    return *ptr;
  }

  template <typename T>
  const T& get() const {
    const auto* ptr = try_get<T>();
    assert(ptr != nullptr);
    return *ptr;
  }

  template <typename T>
  bool has() {
    return try_get<T>() != nullptr;
  }

private:
  void invalidate_component(ComponentId cid) {
    if (cid < cache_.size()) {
      cache_[cid] = nullptr;
    }
  }

  void mark_missing(ComponentId cid) {
    if (cid < cache_.size()) {
      cache_[cid] = missing_tag();
    }
  }

  void cache_component(ComponentId cid, void* comp_ptr) {
    if (cid < cache_.size()) {
      cache_[cid] = comp_ptr;
    }
  }

  void invalidate_all() {
    cache_.fill(nullptr);
  }

  void mark_dead() {
    alive_ = false;
    world_ = nullptr;
    entity_ = {};
  }

  World* world_ = nullptr;
  Entity entity_{};
  bool alive_ = true;
  std::array<void*, kMaxComponents> cache_{};

  static void* missing_tag() {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(1));
  }

  friend class World;
};

inline std::shared_ptr<EntityProxy> World::get_proxy(Entity e) {
  auto* meta = validate(e);
  if (!meta) {
    return {};
  }
  if (auto existing = meta->proxy.lock()) {
    return existing;
  }
  auto alloc = std::pmr::polymorphic_allocator<EntityProxy>(&proxy_resource_);
  auto proxy = std::allocate_shared<EntityProxy>(alloc, *this, e);
  meta->proxy = proxy;
  return proxy;
}

inline void World::invalidate_proxy_component(EntityMeta& meta, ComponentId cid) {
  if (auto proxy = meta.proxy.lock()) {
    proxy->invalidate_component(cid);
  }
}

inline void World::notify_proxy_missing(EntityMeta& meta, ComponentId cid) {
  if (auto proxy = meta.proxy.lock()) {
    proxy->mark_missing(cid);
  }
}

inline void World::notify_proxy_component_ptr(EntityMeta& meta, ComponentId cid, void* comp_ptr) {
  if (auto proxy = meta.proxy.lock()) {
    proxy->cache_component(cid, comp_ptr);
  }
}

inline void World::invalidate_proxy_all(EntityMeta& meta) {
  if (auto proxy = meta.proxy.lock()) {
    proxy->invalidate_all();
    proxy->mark_dead();
  }
  meta.proxy.reset();
}

template <typename T>
void Pool<T>::erase_dense(DenseIndex di, World& world) {
  const std::size_t last = items.size() - 1;
  if (di != last) {
    items[di] = std::move(items[last]);
    world.update_moved(di, items[di].entity_idx, items[di].gen, component_id<T>());
  }
  items.pop_back();
}

} // namespace ecs_lab
