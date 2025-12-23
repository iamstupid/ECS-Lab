#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ecs_lab/ecs.hpp"

namespace {

struct Position {
  int x = 0;
  int y = 0;
};

struct Health {
  int hp = 0;
};

} // namespace

TEST_CASE("ECS create/destroy lifecycle") {
  ecs_lab::World world;
  auto e = world.create();
  CHECK(world.is_alive(e));

  world.destroy(e);
  CHECK(!world.is_alive(e));

  auto e2 = world.create();
  CHECK(world.is_alive(e2));
  CHECK(e2.entity_id > e.entity_id);
  CHECK(e2.entity_idx == e.entity_idx);
  CHECK(e2.gen == e.gen + 1);
}

TEST_CASE("ECS add/get/remove") {
  ecs_lab::World world;
  auto e = world.create();

  CHECK(!world.has<Position>(e));
  auto& pos = world.add<Position>(e, 3, 4);
  CHECK(world.has<Position>(e));
  CHECK(pos.x == 3);
  CHECK(pos.y == 4);

  auto& pos2 = world.get<Position>(e);
  CHECK(pos2.x == 3);
  CHECK(pos2.y == 4);

  world.remove<Position>(e);
  CHECK(!world.has<Position>(e));
}

TEST_CASE("ECS swap-erase updates moved entity index") {
  ecs_lab::World world;
  auto a = world.create();
  auto b = world.create();

  world.add<Health>(a, 10);
  world.add<Health>(b, 20);

  world.remove<Health>(a);
  CHECK(!world.has<Health>(a));
  CHECK(world.has<Health>(b));
  CHECK(world.get<Health>(b).hp == 20);
}

TEST_CASE("ECS each iterates components") {
  ecs_lab::World world;
  auto a = world.create();
  auto b = world.create();
  world.add<Health>(a, 5);
  world.add<Health>(b, 7);

  int sum = 0;
  int count = 0;
  world.each<Health>([&](ecs_lab::Entity, Health& h) {
    sum += h.hp;
    ++count;
  });

  CHECK(count == 2);
  CHECK(sum == 12);
}

TEST_CASE("ECS prefab instantiation") {
  ecs_lab::World world;
  auto prefab = ecs_lab::make_prefab(Position{1, 2}, Health{9});
  auto e = world.instantiate(prefab);

  CHECK(world.has<Position>(e));
  CHECK(world.has<Health>(e));
  CHECK(world.get<Position>(e).x == 1);
  CHECK(world.get<Position>(e).y == 2);
  CHECK(world.get<Health>(e).hp == 9);
}

TEST_CASE("ECS snapshot restore") {
  ecs_lab::World world;
  auto a = world.create();
  world.add<Position>(a, 3, 4);

  auto snap = world.snapshot();

  auto b = world.create();
  world.add<Health>(b, 11);
  world.remove<Position>(a);

  CHECK(world.has<Health>(b));
  CHECK(!world.has<Position>(a));

  world.restore(snap);

  CHECK(world.is_alive(a));
  CHECK(!world.is_alive(b));
  CHECK(world.has<Position>(a));
  CHECK(world.get<Position>(a).x == 3);
  CHECK(world.get<Position>(a).y == 4);
}

TEST_CASE("ECS add_missing_components copies from source") {
  ecs_lab::World world;
  auto src = world.create();
  auto dst = world.create();

  world.add<Position>(src, 10, 20);
  world.add<Health>(src, 42);
  world.add<Position>(dst, 1, 2);

  world.add_missing_components(dst, src);

  CHECK(world.has<Position>(dst));
  CHECK(world.has<Health>(dst));
  CHECK(world.get<Position>(dst).x == 1);
  CHECK(world.get<Position>(dst).y == 2);
  CHECK(world.get<Health>(dst).hp == 42);
}

TEST_CASE("ECS EntityProxy caches component access") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 5, 6);

  auto proxy = world.get_proxy(e);
  REQUIRE(proxy != nullptr);
  auto* pos = proxy->try_get<Position>();
  REQUIRE(pos != nullptr);
  CHECK(pos->x == 5);
  CHECK(pos->y == 6);

  pos->x = 9;
  CHECK(world.get<Position>(e).x == 9);

  world.remove<Position>(e);
  CHECK(proxy->try_get<Position>() == nullptr);
}

TEST_CASE("ECS EntityProxy recovers after remove/add") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 1, 2);

  auto proxy = world.get_proxy(e);
  REQUIRE(proxy != nullptr);
  REQUIRE(proxy->try_get<Position>() != nullptr);

  world.remove<Position>(e);
  CHECK(proxy->try_get<Position>() == nullptr);

  world.add<Position>(e, 7, 8);
  auto* pos = proxy->try_get<Position>();
  REQUIRE(pos != nullptr);
  CHECK(pos->x == 7);
  CHECK(pos->y == 8);
}

// ============================================================================
// Additional test cases for thorough coverage
// ============================================================================

struct Velocity {
  float vx = 0.0f;
  float vy = 0.0f;
};

struct Tag {};

struct Counter {
  int value = 0;
};

TEST_CASE("Signature rank calculates correctly with multiple components") {
  ecs_lab::World world;
  auto e = world.create();

  world.add<Position>(e, 1, 2);
  world.add<Health>(e, 100);
  world.add<Velocity>(e, 3.0f, 4.0f);

  CHECK(world.has<Position>(e));
  CHECK(world.has<Health>(e));
  CHECK(world.has<Velocity>(e));

  CHECK(world.get<Position>(e).x == 1);
  CHECK(world.get<Health>(e).hp == 100);
  CHECK(world.get<Velocity>(e).vx == 3.0f);

  world.remove<Health>(e);
  CHECK(!world.has<Health>(e));
  CHECK(world.has<Position>(e));
  CHECK(world.has<Velocity>(e));
  CHECK(world.get<Position>(e).x == 1);
  CHECK(world.get<Velocity>(e).vx == 3.0f);
}

TEST_CASE("Component add/remove order independence") {
  ecs_lab::World world;

  SUBCASE("Add in order A, B, C; remove B") {
    auto e = world.create();
    world.add<Position>(e, 1, 1);
    world.add<Health>(e, 50);
    world.add<Velocity>(e, 2.0f, 2.0f);

    world.remove<Health>(e);
    CHECK(world.get<Position>(e).x == 1);
    CHECK(world.get<Velocity>(e).vx == 2.0f);
  }

  SUBCASE("Add in order C, A, B; remove A") {
    auto e = world.create();
    world.add<Velocity>(e, 5.0f, 5.0f);
    world.add<Position>(e, 3, 3);
    world.add<Health>(e, 75);

    world.remove<Position>(e);
    CHECK(world.get<Velocity>(e).vx == 5.0f);
    CHECK(world.get<Health>(e).hp == 75);
  }
}

TEST_CASE("Swap-erase edge cases") {
  ecs_lab::World world;

  SUBCASE("Remove only component in pool") {
    auto e = world.create();
    world.add<Health>(e, 10);
    world.remove<Health>(e);
    CHECK(!world.has<Health>(e));

    int count = 0;
    world.each<Health>([&](ecs_lab::Entity, Health&) { ++count; });
    CHECK(count == 0);
  }

  SUBCASE("Remove first entity's component (swap with last)") {
    auto a = world.create();
    auto b = world.create();
    auto c = world.create();

    world.add<Health>(a, 10);
    world.add<Health>(b, 20);
    world.add<Health>(c, 30);

    world.remove<Health>(a);

    CHECK(!world.has<Health>(a));
    CHECK(world.has<Health>(b));
    CHECK(world.has<Health>(c));
    CHECK(world.get<Health>(b).hp == 20);
    CHECK(world.get<Health>(c).hp == 30);
  }

  SUBCASE("Remove last entity's component (no swap needed)") {
    auto a = world.create();
    auto b = world.create();

    world.add<Health>(a, 10);
    world.add<Health>(b, 20);

    world.remove<Health>(b);

    CHECK(world.has<Health>(a));
    CHECK(!world.has<Health>(b));
    CHECK(world.get<Health>(a).hp == 10);
  }
}

TEST_CASE("Entity slot reuse with generation increment") {
  ecs_lab::World world;

  auto e1 = world.create();
  const auto old_idx = e1.entity_idx;
  const auto old_gen = e1.gen;

  world.destroy(e1);
  CHECK(!world.is_alive(e1));

  auto e2 = world.create();
  CHECK(e2.entity_idx == old_idx);
  CHECK(e2.gen == ((old_gen + 1) & ecs_lab::kGenMask) | ecs_lab::kGenAliveBit);

  CHECK(!world.is_alive(e1));
  CHECK(world.is_alive(e2));
}

TEST_CASE("Stale entity handle rejected after destruction") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 5, 5);

  auto stale_handle = e;
  world.destroy(e);

  CHECK(!world.is_alive(stale_handle));
  CHECK(!world.has<Position>(stale_handle));
  CHECK(world.try_get<Position>(stale_handle) == nullptr);

  auto e2 = world.create();
  world.add<Position>(e2, 10, 10);

  CHECK(!world.has<Position>(stale_handle));
  CHECK(world.get<Position>(e2).x == 10);
}

TEST_CASE("Each iteration skips destroyed entities") {
  ecs_lab::World world;
  auto a = world.create();
  auto b = world.create();
  auto c = world.create();

  world.add<Counter>(a, 1);
  world.add<Counter>(b, 2);
  world.add<Counter>(c, 3);

  world.destroy(b);

  int sum = 0;
  int count = 0;
  world.each<Counter>([&](ecs_lab::Entity, Counter& ct) {
    sum += ct.value;
    ++count;
  });

  CHECK(count == 2);
  CHECK(sum == 4);
}

TEST_CASE("Each iteration modification safety") {
  ecs_lab::World world;

  for (int i = 0; i < 10; ++i) {
    auto e = world.create();
    world.add<Counter>(e, i);
  }

  world.each<Counter>([&](ecs_lab::Entity, Counter& ct) {
    ct.value *= 2;
  });

  int sum = 0;
  world.each<Counter>([&](ecs_lab::Entity, Counter& ct) {
    sum += ct.value;
  });

  CHECK(sum == 90);
}

TEST_CASE("Empty prefab creates entity without components") {
  ecs_lab::World world;
  auto prefab = ecs_lab::Prefab<>{};
  auto e = world.instantiate(prefab);

  CHECK(world.is_alive(e));
  CHECK(!world.has<Position>(e));
  CHECK(!world.has<Health>(e));
}

TEST_CASE("Single component prefab") {
  ecs_lab::World world;
  auto prefab = ecs_lab::make_prefab(Health{42});
  auto e = world.instantiate(prefab);

  CHECK(world.has<Health>(e));
  CHECK(!world.has<Position>(e));
  CHECK(world.get<Health>(e).hp == 42);
}

TEST_CASE("Prefab with many components") {
  ecs_lab::World world;
  auto prefab = ecs_lab::make_prefab(
      Position{1, 2},
      Health{100},
      Velocity{3.0f, 4.0f}
  );
  auto e = world.instantiate(prefab);

  CHECK(world.has<Position>(e));
  CHECK(world.has<Health>(e));
  CHECK(world.has<Velocity>(e));

  CHECK(world.get<Position>(e).x == 1);
  CHECK(world.get<Position>(e).y == 2);
  CHECK(world.get<Health>(e).hp == 100);
  CHECK(world.get<Velocity>(e).vx == 3.0f);
}

TEST_CASE("EntityProxy invalidated on entity destroy") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 1, 2);

  auto proxy = world.get_proxy(e);
  REQUIRE(proxy != nullptr);
  CHECK(proxy->is_alive());

  world.destroy(e);

  CHECK(!proxy->is_alive());
  CHECK(proxy->try_get<Position>() == nullptr);
}

TEST_CASE("EntityProxy shared across multiple get_proxy calls") {
  ecs_lab::World world;
  auto e = world.create();

  auto proxy1 = world.get_proxy(e);
  auto proxy2 = world.get_proxy(e);

  CHECK(proxy1.get() == proxy2.get());
}

TEST_CASE("EntityProxy cache updated on swap-erase move") {
  ecs_lab::World world;
  auto a = world.create();
  auto b = world.create();

  world.add<Health>(a, 10);
  world.add<Health>(b, 20);

  auto proxy_b = world.get_proxy(b);
  auto* hp_before = proxy_b->try_get<Health>();
  REQUIRE(hp_before != nullptr);
  CHECK(hp_before->hp == 20);

  world.remove<Health>(a);

  auto* hp_after = proxy_b->try_get<Health>();
  REQUIRE(hp_after != nullptr);
  CHECK(hp_after->hp == 20);
}

TEST_CASE("Snapshot and restore preserves entity state correctly") {
  ecs_lab::World world;

  auto e1 = world.create();
  auto e2 = world.create();
  world.add<Position>(e1, 10, 20);
  world.add<Health>(e1, 50);
  world.add<Position>(e2, 30, 40);

  auto snap = world.snapshot();

  world.destroy(e1);
  auto e3 = world.create();
  world.add<Velocity>(e3, 1.0f, 2.0f);
  world.get<Position>(e2).x = 999;

  world.restore(snap);

  CHECK(world.is_alive(e1));
  CHECK(world.is_alive(e2));
  CHECK(!world.is_alive(e3));

  CHECK(world.get<Position>(e1).x == 10);
  CHECK(world.get<Health>(e1).hp == 50);
  CHECK(world.get<Position>(e2).x == 30);
}

TEST_CASE("Snapshot restore then modify works") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Health>(e, 100);

  auto snap = world.snapshot();

  world.get<Health>(e).hp = 50;
  world.restore(snap);

  CHECK(world.get<Health>(e).hp == 100);

  world.get<Health>(e).hp = 75;
  CHECK(world.get<Health>(e).hp == 75);
}

TEST_CASE("Multiple snapshots independent") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Counter>(e, 1);

  auto snap1 = world.snapshot();

  world.get<Counter>(e).value = 2;
  auto snap2 = world.snapshot();

  world.get<Counter>(e).value = 3;

  world.restore(snap1);
  CHECK(world.get<Counter>(e).value == 1);

  world.restore(snap2);
  CHECK(world.get<Counter>(e).value == 2);
}

TEST_CASE("Destroy removes all components") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 1, 2);
  world.add<Health>(e, 100);
  world.add<Velocity>(e, 3.0f, 4.0f);

  world.destroy(e);

  int pos_count = 0, hp_count = 0, vel_count = 0;
  world.each<Position>([&](ecs_lab::Entity, Position&) { ++pos_count; });
  world.each<Health>([&](ecs_lab::Entity, Health&) { ++hp_count; });
  world.each<Velocity>([&](ecs_lab::Entity, Velocity&) { ++vel_count; });

  CHECK(pos_count == 0);
  CHECK(hp_count == 0);
  CHECK(vel_count == 0);
}

TEST_CASE("Add existing component returns reference to existing") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 1, 2);

  auto& pos = world.add<Position>(e, 99, 99);
  CHECK(pos.x == 1);
  CHECK(pos.y == 2);
}

TEST_CASE("Remove non-existent component is no-op") {
  ecs_lab::World world;
  auto e = world.create();
  world.add<Position>(e, 1, 2);

  world.remove<Health>(e);

  CHECK(world.has<Position>(e));
  CHECK(!world.has<Health>(e));
}

TEST_CASE("try_get returns nullptr for non-existent component") {
  ecs_lab::World world;
  auto e = world.create();

  CHECK(world.try_get<Position>(e) == nullptr);
  CHECK(world.try_get<Health>(e) == nullptr);
}

TEST_CASE("Large number of entities stress test") {
  ecs_lab::World world;
  constexpr int N = 1000;

  std::vector<ecs_lab::Entity> entities;
  entities.reserve(N);

  for (int i = 0; i < N; ++i) {
    auto e = world.create();
    world.add<Counter>(e, i);
    entities.push_back(e);
  }

  for (int i = 0; i < N; i += 2) {
    world.destroy(entities[i]);
  }

  int sum = 0;
  int count = 0;
  world.each<Counter>([&](ecs_lab::Entity, Counter& ct) {
    sum += ct.value;
    ++count;
  });

  CHECK(count == N / 2);

  int expected_sum = 0;
  for (int i = 1; i < N; i += 2) {
    expected_sum += i;
  }
  CHECK(sum == expected_sum);
}

TEST_CASE("Entity ID strictly increases") {
  ecs_lab::World world;

  auto e1 = world.create();
  auto e2 = world.create();
  world.destroy(e1);
  auto e3 = world.create();
  auto e4 = world.create();

  CHECK(e1.entity_id < e2.entity_id);
  CHECK(e2.entity_id < e3.entity_id);
  CHECK(e3.entity_id < e4.entity_id);
}

TEST_CASE("Zero-size tag component") {
  ecs_lab::World world;
  auto e = world.create();

  CHECK(!world.has<Tag>(e));
  world.add<Tag>(e);
  CHECK(world.has<Tag>(e));

  int count = 0;
  world.each<Tag>([&](ecs_lab::Entity, Tag&) { ++count; });
  CHECK(count == 1);

  world.remove<Tag>(e);
  CHECK(!world.has<Tag>(e));
}

TEST_CASE("add_missing_components does not overwrite existing") {
  ecs_lab::World world;
  auto src = world.create();
  auto dst = world.create();

  world.add<Position>(src, 100, 200);
  world.add<Health>(src, 999);

  world.add<Position>(dst, 1, 2);

  world.add_missing_components(dst, src);

  CHECK(world.get<Position>(dst).x == 1);
  CHECK(world.get<Position>(dst).y == 2);
  CHECK(world.get<Health>(dst).hp == 999);
}

TEST_CASE("add_missing_components with destroyed entities is no-op") {
  ecs_lab::World world;
  auto src = world.create();
  auto dst = world.create();

  world.add<Position>(src, 10, 20);
  world.destroy(src);

  world.add_missing_components(dst, src);
  CHECK(!world.has<Position>(dst));
}
