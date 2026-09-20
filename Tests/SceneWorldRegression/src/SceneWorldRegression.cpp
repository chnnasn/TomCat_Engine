#include "TomCat/Scene/SceneWorld.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Position { int value = 0; EKIT_COMPONENT(Position); };
struct Velocity { int value = 1; EKIT_COMPONENT(Velocity); };
struct Missing { EKIT_COMPONENT(Missing); };
struct Owner { std::shared_ptr<int> value; EKIT_COMPONENT(Owner); };

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<typename F> void RequireThrows(F&& fn) {
    bool caught = false;
    try { fn(); } catch (const ekit::EkitException&) { caught = true; }
    Require(caught, "Expected registration error");
}

int main() {
    try {
        TomCat::SceneWorld world;
        world.RegisterSparseComponent<Position>();
        world.RegisterSparseComponent<Velocity>();
        const auto& read_only = world;
        int visits = 0;
        world.ForEach<Position, Velocity>([&](Position&, Velocity&) { ++visits; });
        Require(visits == 0, "Empty pools must not invoke callback");
        auto first = world.Create();
        auto* stable = &world.Add<Position>(first, 7);
        world.Add<Velocity>(first, 2);
        Require(world.TryGet<Missing>(first) == nullptr, "Unregistered TryGet must return null");
        Require(read_only.TryGet<Missing>(first) == nullptr, "Const unregistered TryGet must return null");
        Require(!world.Has<Missing>(first), "Unregistered Has must return false");
        RequireThrows([&] { world.View<Position, Missing>(); });
        RequireThrows([&] { world.ForEach<Position, Missing>([&](Position&, Missing&) { ++visits; }); });
        RequireThrows([&] { read_only.ForEach<Position, Missing>([&](const Position&, const Missing&) { ++visits; }); });
        Require(visits == 0, "Validate registrations before invoking callbacks");
        std::vector<ekit::Entity> entities{first};
        for (int i = 0; i < 1024; ++i) {
            auto e = world.Create(); entities.push_back(e);
            world.Add<Position>(e, i);
            if (i % 2 == 0) world.Add<Velocity>(e, 1);
        }
        Require(world.TryGet<Position>(first) == stable, "Growth invalidated reference");
        world.ForEach<Position, Velocity>([&](ekit::Entity e, auto& p, auto& v) {
            static_assert(std::is_same_v<decltype(p), Position&>);
            static_assert(std::is_same_v<decltype(v), Velocity&>);
            Require(&p == world.TryGet<Position>(e), "Position reference mismatch");
            Require(&v == world.TryGet<Velocity>(e), "Velocity reference mismatch");
            p.value += v.value; ++visits;
        });
        Require(visits == 513, "Intersection count mismatch");
        int const_count = 0;
        read_only.ForEach<Velocity, Position>([&](ekit::Entity e, auto& v, auto& p) {
            static_assert(std::is_same_v<decltype(p), const Position&>);
            static_assert(std::is_same_v<decltype(v), const Velocity&>);
            Require(&p == read_only.TryGet<Position>(e), "Const reference mismatch");
            ++const_count;
        });
        Require(const_count == visits, "Const iteration mismatch");
        read_only.ForEach<Position>([](const Position&) {});
        world.ForEach<Position, Position>([](Position& a, Position& b) {
            Require(&a == &b, "Duplicate required type must alias");
        });
        int duplicate_count = 0;
        world.ForEach<Velocity, Velocity>([&](Velocity&, Velocity&) { ++duplicate_count; });
        Require(duplicate_count == visits, "Duplicate drivers must not double-visit");
        for (std::size_t i = 1; i < entities.size(); ++i) world.Remove<Position>(entities[i]);
        int switched_count = 0;
        world.ForEach<Position, Velocity>([&](Position& p, Velocity&) {
            Require(&p == stable, "Driver switching lost component reference"); ++switched_count;
        });
        Require(switched_count == 1, "Smallest pool must be selected per call");
        // Same-world nested reads do not share mutable iteration state.
        world.ForEach<Position>([&](Position&) {
            int nested = 0;
            read_only.ForEach<Velocity>([&](const Velocity&) { ++nested; });
            Require(nested == 513, "Nested iteration mismatch");
        });
        int pick = world.GetPickingID(first);
        world.Destroy(first);
        auto replacement = world.Create();
        world.Add<Position>(replacement, 99);
        Require(!world.IsAlive(first), "Stale handle alive");
        Require(world.TryGet<Position>(first) == nullptr && read_only.TryGet<Position>(first) == nullptr,
            "Stale handle TryGet must return null");
        Require(world.FindPickingEntity(pick) == ekit::Entity::Null, "Stale picking ID reused");
        Require(world.TryGet<Velocity>(replacement) == nullptr, "Missing component must return null");
        world.ForEach<Position, Velocity>([](Position&, Velocity&) {
            throw std::runtime_error("Destroyed component was visited");
        });
        world.Add<Velocity>(replacement, 3);
        int restored = 0;
        world.ForEach<Position, Velocity>([&](Position&, Velocity&) { ++restored; });
        Require(restored == 1, "Re-added components not visited");
        world.RegisterSparseComponent<Owner>();
        auto resource = std::make_shared<int>(42);
        std::weak_ptr<int> weak = resource;
        world.Add<Owner>(replacement, resource); resource.reset();
        read_only.ForEach<Owner>([](const Owner& owner) { Require(*owner.value == 42, "Owning component mismatch"); });
        world.Remove<Owner>(replacement);
        Require(weak.expired(), "Iteration retained component ownership");
        TomCat::SceneWorld adopted;
        adopted.Swap(world);
        int adopted_count = 0;
        adopted.ForEach<Position>([&](Position& p) { Require(p.value == 99, "Swap lost values"); ++adopted_count; });
        Require(adopted_count == 1, "Swap lost entities");
        RequireThrows([&] { world.ForEach<Position>([](Position&) {}); });
        std::cout << "SceneWorldRegression: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "SceneWorldRegression: FAIL: " << error.what() << '\n';
        return 1;
    }
}
