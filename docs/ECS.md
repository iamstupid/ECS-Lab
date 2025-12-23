# ECS 库设计文档（定版 v1）

## 0. 目标与非目标

### 目标

1. **单组件遍历极快**：`each<T>(fn)` 以连续/块状 dense 存储扫描 `T` 的所有实例。
2. **OOP 风格的对象操作**：Entity 可 `has/add/remove/get` 组件，写起来像对象（buff 触发 buff、事件链式调用）。
3. **组件增删不做 archetype 迁移**：只更新对应 `Pool<T>` 与该实体的 indexer（Signature + idx）。
4. **prefab 一次性批量创建组件**：避免多次 `idx.insert` 的 O(k²)。
5. **安全句柄**：`entity_idx + gen` 校验避免 slot 复用导致悬挂句柄误操作。
6. **entity_id 为严格递增的外部标识**：便于区分 spawn 顺序、日志与调试。

### 非目标

* 不做 archetype/HLD
* 不做多组件查询/连续性优化（不需要 `view<A,B,C>`）
* 不做多线程安全（默认单线程；pmr 使用 `unsynchronized_pool_resource`）

---

## 1. 关键约束与结论

* `MaxC` 可配置，默认 **128**（不大）。
* `EntityId` 必须是 `uint64_t`，并且**严格递增**。
* `gen` 必须存在（实体不大但交互多，安全性与开发体验优先）。
* `idx` 使用 `std::pmr::vector`，分配器为 `std::pmr::unsynchronized_pool_resource`（大量小 vector 小块频繁分配）。
* 内存容器：

  * `EntityMeta` 用你提出的 **linear_arena**（安全版本，正确构造/析构）。
  * `Pool<T>` 数据用 **DenseArray<Component<T>>**（块状 4096，swap-erase）。

---

## 2. 数据模型总览

### 2.1 外部句柄：Entity

```cpp
struct Entity {
  uint64_t entity_id;   // 外部标识：严格递增，仅用于标识/排序/日志
  uint32_t entity_idx;  // EntityMeta 在 arena 中的位置（主索引）
  uint32_t gen;         // generation：句柄有效性校验
};
```

### 2.2 内部实体元数据：EntityMeta（由 linear_arena 管理）

```cpp
struct EntityMeta {
  uint64_t entity_id;               // 与 Entity.entity_id 一致
  uint32_t gen;                     // 当前 generation
  bool alive;                       // 或用 gen/状态位表示
  Signature<MaxC> sig;              // 组件存在 bitset
  std::pmr::vector<DenseIndex> idx; // 按 sig 的 set bits 升序存各组件的 denseIndex
};
```

> `entity_id` **不承载 alive/free list**，只作为外部递增 ID。
> alive/free list 由 arena 自身管理 + `alive` 标志管理。

### 2.3 组件池：Pool<T> 与 Component<T>

每个组件类型 `T` 有一个 `Pool<T>`，内部为 dense 存储：

```cpp
template<class T>
struct Component {
  static constexpr uint32_t component_id = ...; // type -> id
  uint32_t entity_idx;  // owner: 指向 EntityMeta 的索引
  uint32_t gen;         // owner generation（你已决定不省这点内存）
  T data;

  // 可选：operator-> / operator* 转发到 data，方便遍历时像 T 一样用
};
```

```cpp
template<class T>
struct Pool {
  DenseArray<Component<T>> items;
};
```

---

## 3. 核心 indexer：Signature + rank(popcnt) + idx

### 3.1 Signature

* 固定上限 bitset，默认 128（2×64-bit words）。
* 提供：`test/set/reset` 和 `rank(compId)`。

### 3.2 idx 不变量（必须保持）

* `idx.size() == popcount(sig)`
* `idx[k]` 对应 sig 中第 k 个 set bit 所代表的组件（组件 id 升序）
* 对组件 `cid`：

  * `pos = sig.rank(cid)`
  * `denseIndex = idx[pos]`

`DenseIndex` 使用 `uint32_t`（实体数量不大，足够）。

---

## 4. 生命周期与安全校验

### 4.1 创建 create()

* 从 `EntityMeta` arena 分配一个 cell（复用或扩展）。
* `meta.gen` 若是复用则已经是某个值；创建时不改变 gen（或创建时显式写入当前值）。
* `meta.alive = true`
* `meta.entity_id = ++global_entity_id_counter`（严格递增）
* 清空：`meta.sig=0; meta.idx.clear()`
* 返回 `Entity{meta.entity_id, entity_idx, meta.gen}`

### 4.2 销毁 destroy(Entity e)

每次操作必须校验句柄合法：

校验规则：

1. `meta = arena[e.entity_idx]`
2. `meta.alive == true`
3. `meta.gen == e.gen`
4. `meta.entity_id == e.entity_id`（可选但建议保留，额外防错）

销毁步骤：

1. 通过 `sig + idx` 批量删除所有组件（见 6.1）。
2. 清空 `sig/idx`，`meta.alive=false`
3. `meta.gen++`（使旧句柄失效）
4. arena `free(entity_idx)` 进入 freelist

> `gen++` 放在“删除组件之后”，避免删除过程回写 moved entity 的 idx 时触发校验失败。

---

## 5. World 结构与类型注册

### 5.1 组件 id 分配

* 使用 “每类型一个静态 id，运行时首次分配” 的 counter（不需要 hashtable）。

```cpp
using ComponentId = uint16_t;
template<class T>
ComponentId component_id();
```

要求 `component_id<T>() < MaxC`。

### 5.2 Pools 存储

World 持有一个 `pools[MaxC]` 的 type-erased 表，用于：

* destroy 时按 compId 删除
* snapshot 时 clone

可以用 `IPool` 虚接口或函数指针表。vibe coding 推荐先用 `virtual`，之后再做无虚优化。

### 5.3 pmr resource

World 持有一个：

* `std::pmr::unsynchronized_pool_resource idx_resource;`

所有 `EntityMeta.idx` 都使用该 resource 构造。

---

## 6. 核心算法（必须实现正确）

下面描述 `has/get/add/remove` 与 pool swap-erase 的回写逻辑。

### 6.1 destroy 批量删组件

destroy 时不用逐个 `remove<T>`（那会重复 rank/erase），而是：

* 遍历 sig 的 set bits（升序）
* 同步从 idx 里取 denseIndex
* 调用对应 pool 的 `erase_dense(denseIndex)`（它负责回写 moved entity）

伪码：

```cpp
i = 0
for cid in meta.sig.set_bits_ascending():
  di = meta.idx[i++]
  pools[cid]->erase_dense(di, world) // 内部通过 Component<T> 拿 owner
```

### 6.2 has<T>(Entity e)

* 校验句柄
* `return meta.sig.test(cid)`

### 6.3 get<T>(Entity e)

* 校验句柄 + assert has
* `pos = meta.sig.rank(cid)`
* `di = meta.idx[pos]`
* 返回 `pool<T>.items[di].data`

### 6.4 add<T>(Entity e, args...)

* 校验句柄
* 若 has：返回 get
* `pos = meta.sig.rank(cid)`（在 set 前）
* `meta.sig.set(cid)`
* `di = pool<T>.emplace({entity_idx=e.entity_idx, gen=e.gen}, args...)`
* `meta.idx.insert(begin+pos, di)`（O(k)，允许）
* 返回 data 引用

### 6.5 remove<T>(Entity e)

* 校验句柄
* 若无：return
* `pos = meta.sig.rank(cid)`
* `di = meta.idx[pos]`
* `pool<T>.erase_dense(di, world)`（swap-erase + 回写）
* `meta.idx.erase(begin+pos)`
* `meta.sig.reset(cid)`

---

## 7. Pool<T>：DenseArray + swap-erase + moved 回写

### 7.1 DenseArray（块状 4096）

用于连续遍历与 O(1) 随机访问：

* `emplace_back`
* `pop_back`
* `operator[]`
* `size()`

数据本体永不重分配，只增长 block 指针数组。

### 7.2 Pool<T>::emplace

创建 `Component<T>`：

* 填 `entity_idx`、`gen`、`data`
* push 到 `items`
* 返回 denseIndex

### 7.3 Pool<T>::erase_dense(di, world)

执行 swap-erase：

1. `last = size-1`
2. 若 `di != last`：

   * `moved = items[last]` move 到 `items[di]`
3. `items.pop_back()`

关键：若发生 moved，需要回写 moved owner 的 `idx`：

* `moved_owner_idx = items[di].entity_idx`
* `moved_owner_gen = items[di].gen`
* 从 arena 取 `movedMeta`
* 校验 `movedMeta.gen == moved_owner_gen && movedMeta.alive`
* `pos = movedMeta.sig.rank(cid_of_T)`
* `movedMeta.idx[pos] = di`

> 这里 `cid_of_T` 就是 `component_id<T>()`（或 `Component<T>::component_id`）。

---

## 8. 单组件遍历 each<T>

提供：

```cpp
template<class T, class Fn>
void each(Fn&& fn);
```

实现：

* 遍历 `pool<T>.items` dense
* 对每个 `Component<T>& c`：

  * 可选校验 owner gen（debug）
  * 构造一个轻量 Entity（或直接传 `entity_idx`）
  * 调用 `fn(Entity{meta.entity_id, c.entity_idx, c.gen}, c.data)` 或 `fn(c.entity_idx, c.data)`

推荐两种版本：

* `each<T>(fn(entity_idx, T&))`：最快
* `each<T>(fn(Entity, T&))`：更 OOP/更友好

---

## 9. Prefab：一次性批量创建组件

### 9.1 目标

* 一次性 set sig
* 一次性 `idx.resize(N)`
* 按 compId 升序依次 emplace，直接写 `idx[i]=denseIndex`
* 避免 N 次 `insert`（避免 O(N²)）

### 9.2 实现策略

Prefab 表示可以是：

* `Prefab<Cs...>{ tuple<Cs...> init }`
  或更通用的 runtime prefab（数组条目）。

instantiate 流程：

1. `Entity e = create()`
2. 生成条目列表：`{cid, emplaceFn, ptrToInit}`
3. 排序（N 小，插入排序即可）
4. `meta.sig` set 所有 cid
5. `meta.idx.resize(N)`
6. i 从 0..N-1：

   * `di = emplaceFn(world, e.entity_idx, e.gen, init)`
   * `meta.idx[i] = di`

---

## 10. 内存分配实现细节

### 10.1 linear_arena<EntityMeta>（安全版本）

必须保证：

* `EntityMeta` 构造时 `idx` 绑定到 `&world.idx_resource`
* free 时正确析构 `idx`（pmr vector）
* freelist 使用 **index**（uint32），不要裸指针

arena 需要提供：

* `alloc() -> uint32_t idx`
* `free(uint32_t idx)`
* `T& at(uint32_t idx)`

### 10.2 DenseArray（块状）

允许使用 `std::vector<std::unique_ptr<T[]>> blocks;` 存 block 指针表（数据不移动）。

---

## 11. Snapshot / 回滚（可选）

由于交互多、你可能做 checkpoint：

* `WorldSnapshot` 深拷贝：

  * 复制 entity arena 内容（meta）
  * 复制 pools 的 DenseArray（逐元素 copy）
  * snapshot 自己有一个 pmr resource，重建每个 meta.idx（用 assign 拷贝内容）

组件类型要求：

* `T` 至少可 move（运行时）
* 若要 snapshot：`T` 可 copy

---

## 12. “反射/转发”在 Component<T> 的实现

不做真正反射，做零成本转发：

```cpp
T* operator->() noexcept { return &data; }
T& operator*() noexcept { return data; }
```

遍历时可写 `c->foo()` / `(*c).x`。

---

## 13. 实现顺序（vibe coding 路线）

1. `Signature<128>`：test/set/reset/rank + set-bit iteration
2. `DenseArray<T>`：块状 4096 push/pop/index
3. `linear_arena<EntityMeta>`：alloc/free/at + 绑定 pmr vector
4. `Pool<T>`：emplace + erase_dense（含 moved 回写）
5. `World`：create/destroy + has/get/add/remove + each<T>
6. Prefab instantiate（排序 + 批量 set sig/idx）
7. Snapshot（如果需要）

---

## 14. 关键设计取舍总结

* **不做 archetype**：避免迁移成本，符合“对象式 buff 链式交互”。
* **gen 必须存在**：保证安全复用 slot。
* **entity_id 严格递增**：外部可读可排序，不用于索引。
* **Component<T> 存 entity_idx+gen**：让 pool erase 的 moved 回写和遍历校验非常直接。
* **idx 用 pmr vector**：大量小数组操作更稳。
