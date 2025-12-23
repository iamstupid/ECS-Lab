# ECS Lab API + Evaluation

This document describes the current ECS implementation in `src/ecs_lab` and evaluates its behavior, performance traits, and limitations.

## Evaluation (Current State)

**What works well**
- **Fast single-component iteration**: `World::each<T>` scans `DenseArray` contiguously.
- **OOP-like ergonomics**: `Entity` + `World::has/add/remove/get` makes usage straightforward.
- **Deterministic IDs**: `entity_id` is strictly increasing for debugging and ordering.
- **Snapshot support**: `World::snapshot/restore` performs a deep copy for checkpointing.
- **Prefab instantiation**: `Prefab<Ts...>` lets you batch-initialize components with one pass.

**Key limitations / risks**
- **EntityProxy cache invalidation**: `EntityProxy` caches `Component<T>*` (tri-state: unknown/missing/present) and is **actively updated** on add/remove/move/destroy for the affected entity. Swap-erase moves notify the moved entity's proxy, so caches no longer go stale just because *some other* entity's `T` was removed. Proxies are still best treated as **frame-local** (or call `clear_cache()` after large structural changes), and should not survive snapshot/restore.
- **Component count limit**: `kMaxComponents` is 128. `component_id<T>()` asserts if exceeded.
- **Generational wrap**: `gen` uses 31 bits (MSB is alive). After 2^31 destroy cycles of the same slot, stale handles can alias.
- **Copy requirement for dynamic prefab/snapshot**: `Pool<T>::clone_dense` and `Snapshot` require `T` to be **copyable**.
- **Single-threaded**: no synchronization; pmr uses `unsynchronized_pool_resource`.
- **Runtime component id order**: `component_id<T>()` is allocated on first use; order depends on instantiation order.

**Performance notes**
- `Signature::rank` is optimized for `kWordCount == 2` (default 128 components).
- `DenseArray` is block-based; swap-erase can move components and invalidate raw pointers.

---

## Public API (ecs_lab)

Include umbrella header:
```cpp
#include "ecs_lab/ecs.hpp"
```

### Core Types

**Entity**
```cpp
struct Entity {
  uint64_t entity_id;  // Strictly increasing ID for debugging/ordering
  uint32_t entity_idx; // Index into arena
  uint32_t gen;        // Generation (MSB is alive bit)
};
```

**ComponentId / DenseIndex**
- `ComponentId` (`uint16_t`): type ID in [0, kMaxComponents)
- `DenseIndex` (`uint32_t`): index inside a component pool

Constants:
```cpp
constexpr ComponentId kMaxComponents = 128;
constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
constexpr uint32_t kGenAliveBit = 0x80000000u;
constexpr uint32_t kGenMask = 0x7FFFFFFFu;
```

### World

```cpp
class World {
public:
  std::shared_ptr<EntityProxy> get_proxy(Entity e);
  Entity create();
  void destroy(Entity e);
  bool is_alive(Entity e) const;

  template <typename T> bool has(Entity e) const;
  template <typename T> T* try_get(Entity e);
  template <typename T> const T* try_get(Entity e) const;
  template <typename T> T& get(Entity e);

  template <typename T, typename... Args> T& add(Entity e, Args&&... args);
  template <typename T> void remove(Entity e);

  template <typename T, typename Fn> void each(Fn&& fn);

  template <typename... Ts> Entity instantiate(const Prefab<Ts...>& prefab);

  void add_missing_components(Entity dst, Entity src);

  Snapshot snapshot() const;
  void restore(const Snapshot& snap);

  // For EntityProxy caching (internal use)
  template <typename T> Component<T>* try_get_component(Entity e);
  template <typename T> const Component<T>* try_get_component(Entity e) const;
};
```

**Behavior details**
- `create()` sets alive bit, clears signature/idx.
- `destroy()` removes all components via pools, clears signature, increments `gen` (alive bit cleared), and returns slot to free list.
- `add()` inserts component into its pool (swap-erase pool), updates signature and `idx` mapping.
- `remove()` swap-erases the component in its pool and updates moved entity index.
- `each<T>(fn)` iterates over all live `T` components.
- `instantiate(prefab)` bulk-creates components in sorted component-id order.
- `add_missing_components(dst, src)` copies **only components not already present** in `dst`.

**Complexity**
- `has/get/try_get/remove` are `O(k)` where `k` is number of set bits in signature (rank).
- `each<T>` is `O(n)` over component pool size.

### Prefab

```cpp
template <typename... Ts>
struct Prefab { std::tuple<Ts...> data; };

template <typename... Ts>
Prefab<std::decay_t<Ts>...> make_prefab(Ts&&... args);
```

**Example**
```cpp
auto prefab = ecs_lab::make_prefab(Position{1,2}, Health{100});
auto e = world.instantiate(prefab);
```

### EntityProxy (cached access)

```cpp
class EntityProxy {
public:
  EntityProxy();
  EntityProxy(World& world, Entity entity);
  void reset(World& world, Entity entity);
  void clear_cache();
  Entity entity() const;
  bool is_alive() const;

  template <typename T> T* try_get();
  template <typename T> const T* try_get() const;
  template <typename T> T& get();
  template <typename T> const T& get() const;
  template <typename T> bool has();
};
```

**Important**
EntityProxy caches `Component<T>*` with a tri-state cache:
- **unknown** (not queried yet)
- **missing** (queried but component absent)
- **present** (cached component pointer)

The world updates proxies on **add/remove/move/destroy** for the affected entity:
- add/move -> cache pointer
- remove -> mark missing
- destroy -> invalidate all and mark dead

It is still recommended to treat proxies as **frame-local** and call `clear_cache()` after large structural changes.

### Snapshot

```cpp
struct World::Snapshot {
  std::unique_ptr<std::pmr::unsynchronized_pool_resource> idx_resource;
  LinearArena arena;
  std::vector<std::unique_ptr<IPool>> pools;
  std::uint64_t next_entity_id;
};
```

`snapshot()` deep-copies arena + pools (requires components to be copyable).  
`restore()` replaces the world state with the snapshot.

---

## Internal Types (Contributor Reference)

These are implementation details but useful for contributors:

- **Signature** (`signature.hpp`)
  - Fixed-size bitset; `rank(cid)` counts set bits before `cid`.
  - `kWordCount == 2` is optimized (128 components).
- **DenseArray** (`dense_array.hpp`)
  - Blocked array (default block 4096).
  - Stable addresses within a block, but **swap-erase moves elements**.
- **LinearArena** (`arena.hpp`)
  - Blocked storage for `EntityMeta`.
  - Free list stored in `EntityMeta.entity_id`.
- **Pool<T>** (`pool.hpp`)
  - Stores `Component<T>` in `DenseArray`.
  - `erase_dense` uses swap-erase and calls `World::update_moved`.
  - `clone_dense` copies component data for dynamic prefab.

---

## Usage Notes

- **Component type requirements**
  - `add_missing_components` and `snapshot/restore` require components to be copyable.
- **Entity handle safety**
  - `gen` uses 31 bits (MSB is alive). Old handles are invalidated on destroy.
  - Stale handles are rejected by `validate`.
- **Threading**
  - Single-threaded design; no internal locking.
