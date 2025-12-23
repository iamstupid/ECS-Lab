# ECS-Lab API Guide

This document describes the ECS-Lab API, design goals, and recommended usage patterns. It is intended for engine-side developers who need a predictable, checkpoint-friendly ECS with simple ergonomics.

Contents
- Overview and design goals
- Core concepts
- API reference (World, Entity, Prefab, Snapshot, EntityProxy)
- Component storage and performance
- Best practices
- Common pitfalls
- Examples

---

## Overview and Design Goals

ECS-Lab is a minimal, single-threaded ECS focused on:
- Deterministic entity IDs and stable generational handles
- Fast iteration over a single component type
- Simple OOP-like access via `World::has/add/remove/get`
- Snapshot/restore for deterministic checkpointing (TAS-friendly)
- Minimal dependencies and easy integration

Key choices:
- Component storage is dense and swap-erased for speed
- Entity metadata is stored in a linear arena with a free list
- `Signature::rank` maps component IDs to dense indices efficiently
- `EntityProxy` provides cached component access with explicit invalidation rules

---

## Core Concepts

### Entity
An `Entity` is a lightweight handle with three fields:
- `entity_id`: a strictly increasing ID for debugging and ordering
- `entity_idx`: index into the entity arena (dense slot)
- `gen`: generation with MSB as alive bit

Entities are value types. Stale handles are rejected by `World::validate`.

### Component
Components are plain structs. Each component type gets a runtime `ComponentId` the first time it is used.

Component requirements (current):
- Must be copyable for `snapshot/restore` and `add_missing_components`
- Must be trivially movable or at least movable; swap-erase may move values

### Pools and Dense Arrays
Each component type has a `Pool<T>` storing `Component<T>` values in a dense array. Removing a component uses swap-erase, which can move the last element into the removed slot. This is fast but invalidates pointers to moved components.

### Signature
`Signature` is a fixed-size bitset (default 128 components). Each entity has a signature to record which components are present. `Signature::rank(cid)` returns the number of set bits before `cid`, which gives the index into the entity's dense index array.

---

## API Reference

### Include
```cpp
#include "ecs_lab/ecs.hpp"
```

### Namespace
All public types are in `ecs_lab`.

---

## World

### Construction
```cpp
ecs_lab::World world;
```

Internally, the world owns:
- A linear arena for entity metadata
- A pool list for each component type
- A PMR resource for per-entity index vectors
- A PMR resource for `EntityProxy` allocations

### create / destroy
```cpp
Entity e = world.create();
world.destroy(e);
```
Behavior:
- `create()` allocates a slot, increments `entity_id`, sets alive bit, clears signature and indices
- `destroy()` removes all components, clears signature, increments generation, frees slot

### is_alive
```cpp
bool alive = world.is_alive(e);
```
Returns `false` for stale or destroyed handles.

---

### has / try_get / get
```cpp
bool has_pos = world.has<Position>(e);
Position* pos = world.try_get<Position>(e);
Position& pos_ref = world.get<Position>(e);
```
- `has<T>` is safe for stale handles
- `try_get<T>` returns `nullptr` if missing or stale
- `get<T>` asserts if missing

Complexity:
- `has`/`get`/`try_get` are O(rank) due to signature rank computation

---

### add
```cpp
Position& p = world.add<Position>(e, 3, 4);
```
- If component already exists, returns the existing component (no reinit)
- If not, emplaces in the pool, updates signature and index list
- Swap-erase can move other components in the pool (see below)

---

### remove
```cpp
world.remove<Position>(e);
```
- No-op if missing or stale
- Uses swap-erase on the component pool
- Updates moved entity's index and proxy cache

---

### each
```cpp
world.each<Health>([](ecs_lab::Entity e, Health& h) {
  // iterate all Health components
});
```
- Iterates contiguous pool storage
- Skips stale entities whose generations do not match

---

### query (multi-component)
```cpp
world.query<Position, Health>([](ecs_lab::Entity e, Position& p, Health& h) {
  // iterates Position pool; filters entities that also have Health
});
```
- Iterates over the **first** component pool (`Position` above)
- Uses the entity `Signature` bitmap to filter entities that have **all** requested components
- Returns immediately if any required component type has no pool (i.e., never used in this world)

Notes:
- Like `each`, avoid structural changes (add/remove) of involved component types during iteration

---

### instantiate (Prefab)
```cpp
auto prefab = ecs_lab::make_prefab(Position{1,2}, Health{10});
Entity e = world.instantiate(prefab);
```
- Prefab types must be unique (static_assert)
- Components are inserted in sorted `ComponentId` order

---

### add_missing_components (dynamic prefab)
```cpp
world.add_missing_components(dst, src);
```
- Copies all components that `dst` does not already have
- Requires component types to be copyable
- Uses pool clone to preserve layout and metadata

---

### snapshot / restore
```cpp
auto snap = world.snapshot();
// mutate world
world.restore(snap);
```
- `snapshot()` deep-copies arena and pools using PMR resources
- `restore()` replaces world state with snapshot
- Component types must be copyable

Notes:
- Any `EntityProxy` created before `restore()` is invalid after restore

---

## EntityProxy (cached access)

`EntityProxy` caches component pointers for a single entity to speed up repeated cross-component access.

### Creating a proxy
```cpp
auto proxy = world.get_proxy(e); // shared_ptr<EntityProxy>
```
If a proxy already exists for the entity, the same `shared_ptr` is returned.

### Accessing components
```cpp
Position* p = proxy->try_get<Position>();
Health& h = proxy->get<Health>();
```

### Cache behavior
Each component slot is tri-state:
- **unknown**: not queried yet
- **missing**: queried, component not present
- **present**: cached `Component<T>*`

The world keeps proxy caches synchronized for the entity:
- add -> cache pointer
- remove -> mark missing
- move (swap-erase) -> update pointer
- destroy -> invalidate all + mark dead

Important:
- Proxies are best treated as **frame-local**
- After major structural changes or snapshot/restore, call `clear_cache()` or discard proxies

---

## Prefab

```cpp
template <typename... Ts>
struct Prefab { std::tuple<Ts...> data; };

auto prefab = ecs_lab::make_prefab(Position{1,2}, Health{9});
```

Prefabs are plain data containers. `instantiate` constructs each component by copy.

---

## Snapshot

```cpp
struct World::Snapshot {
  std::unique_ptr<std::pmr::unsynchronized_pool_resource> idx_resource;
  LinearArena arena;
  std::vector<std::unique_ptr<IPool>> pools;
  std::uint64_t next_entity_id;
};
```

Snapshot uses deep copy of arena + pools. It is designed for deterministic checkpoints, not incremental diffs.

---

## Best Practices

1. Prefer `try_get` in gameplay logic
   - Avoid assertions in release builds
   - Makes missing components explicit

2. Use `EntityProxy` for hot paths only
   - Cache improves repeated access to the same entity
   - Avoid storing proxies across large structural changes or snapshots

3. Treat components as POD-like data
   - Swap-erase moves data; avoid external pointers to components

4. Batch structural changes
   - Frequent add/remove can invalidate pointers and reduce cache effectiveness

5. Use `add_missing_components` for dynamic prefab composition
   - Good for runtime template inheritance

6. Prefer `each<T>` for systems
    - Single-component iteration is cache friendly
    - Keep system logic simple; do cross-component lookups inside the loop if needed

   Prefer `query<T0, Ts...>` when you know you need multiple components
    - Pick `T0` (the first type) to be the most selective / smallest pool for best performance

7. Snapshot usage
    - Use snapshots at known-safe points (end of frame, after logic)
    - Avoid holding `EntityProxy` or raw pointers across restore

---

## Common Pitfalls

- **Stale handles**: entity IDs can be reused by index; always check `is_alive` or rely on `try_get`.
- **Pointer invalidation**: swap-erase moves components. Do not store raw pointers unless you control structural changes.
- **Proxy lifetime**: proxies are invalid after `restore` and can become inconsistent if held too long.
- **Component ID order**: `component_id<T>()` is allocated on first use. The ordering is runtime-dependent.
- **Copy requirement**: snapshots and dynamic prefabs require copyable component types.

---

## Example: Basic System

```cpp
struct Position { int x, y; };
struct Velocity { int vx, vy; };

void step(ecs_lab::World& world) {
  world.each<Position>([&](ecs_lab::Entity e, Position& p) {
    if (auto* v = world.try_get<Velocity>(e)) {
      p.x += v->vx;
      p.y += v->vy;
    }
  });
}
```

---

## Example: Using EntityProxy

```cpp
void process(ecs_lab::World& world, ecs_lab::Entity e) {
  auto proxy = world.get_proxy(e);
  if (!proxy) return;

  if (auto* pos = proxy->try_get<Position>()) {
    if (auto* vel = proxy->try_get<Velocity>()) {
      pos->x += vel->vx;
      pos->y += vel->vy;
    }
  }
}
```

---

## Example: Dynamic Prefab

```cpp
// Assume source has Position + Health
world.add_missing_components(dst, src);
```

---

## Example: Snapshot and Restore

```cpp
auto snap = world.snapshot();
// Run simulation for N steps...
world.restore(snap); // revert
```

---

## Design Rationale (Summary)

- **LinearArena + free list**: fast allocation, predictable memory layout
- **DenseArray**: contiguous storage for iteration and cache efficiency
- **Signature + rank**: O(rank) lookup while keeping per-entity state compact
- **Swap-erase**: O(1) removal at the cost of pointer stability
- **EntityProxy**: localized cache to reduce repeated `rank + pool` work
- **Snapshot**: deep-copy for deterministic checkpoints (TAS)

---

## Roadmap Ideas

Potential enhancements if needed:
- Packed archetypes for multi-component iteration
- Incremental snapshot (diff-based)
- Component serialization hooks
- Optional handle-stable pools (no swap-erase)
- Multi-threaded job execution with read-only views

---

End of document.
