#include "TomCat/Scene/SceneWorld.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

struct Position { std::uint64_t value = 0; EKIT_COMPONENT(Position); };
struct Velocity { std::uint64_t value = 1; EKIT_COMPONENT(Velocity); };

int main() {
    constexpr std::size_t total = 100000, iterations = 200;
    std::cout << "run,coverage_percent,mode,ms,checksum\n";
    for (int run = 0; run < 6; ++run) {
        for (std::size_t percent : {1u, 10u, 100u}) {
            // Alternate mode order to reduce systematic warm-cache/order bias.
            for (int order = 0; order < 2; ++order) {
                const bool callback = (order + run) % 2 != 0;
                TomCat::SceneWorld scene;
                scene.RegisterSparseComponent<Position>();
                scene.RegisterSparseComponent<Velocity>();
                std::uint64_t expected = 0;
                for (std::size_t i = 0; i < total; ++i) {
                    auto e = scene.Create();
                    scene.Add<Position>(e, i);
                    if (i % 100 < percent) {
                        scene.Add<Velocity>(e, std::uint64_t{1});
                        expected += i + iterations;
                    }
                }
                auto view = scene.View<Position, Velocity>();
                const auto begin = std::chrono::steady_clock::now();
                for (std::size_t i = 0; i < iterations; ++i) {
                    if (callback) {
                        scene.ForEach<Position, Velocity>([](Position& p, Velocity& v) { p.value += v.value; });
                    } else {
                        for (auto e : view) {
                            auto [p, v] = view.Get<Position, Velocity>(e);
                            p.value += v.value;
                        }
                    }
                }
                const auto ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - begin).count();
                std::uint64_t checksum = 0;
                std::size_t count = 0;
                const auto& read_only = scene;
                read_only.ForEach<Position, Velocity>([&](const Position& p, const Velocity&) {
                    checksum += p.value; ++count;
                });
                if (checksum != expected || count != total * percent / 100)
                    throw std::runtime_error("SceneWorld benchmark result mismatch");
                std::cout << run << ',' << percent << ',' << (callback ? "foreach" : "view_get")
                          << ',' << ms << ',' << checksum << '\n';
            }
        }
    }
}
