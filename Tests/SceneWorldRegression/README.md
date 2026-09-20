# SceneWorld reference iteration

This header-only regression target needs no renderer or third-party dependencies
beyond the vendored ekit headers. It is included in Tests/premake5.lua and the
native regression runner, and can also be built independently:

```sh
cmake -S Tests/SceneWorldRegression -B build/scene-world -DTOMCAT_BUILD_SCENE_WORLD_BENCHMARK=ON
cmake --build build/scene-world --config Release
ctest --test-dir build/scene-world -C Release --output-on-failure
./build/scene-world/Release/SceneWorldBenchmark
```

Set `TOMCAT_EKIT_INCLUDE_DIR` to validate another ekit include directory. The default
is the vendored version; this change does not alter its pin or sources.

## Contract coverage

Tests verify mutable and const callbacks (with and without Entity), compile-time
const reference types, empty pools, incomplete intersections, duplicate component
types, changing the smallest pool between calls, nested read iteration, stable
references after growth, component removal/re-addition, destruction and reused
entity slots, stale picking IDs, owning component destruction, and world Swap.
Unregistered ForEach types throw before any callback; unregistered TryGet still
returns null. Missing components and stale handles return null from TryGet.

## Local validation, 2026-09-21

- Release CMake/CTest passed against the pinned ekit `82d4de67` headers.
- Release CMake/CTest passed against the current optimized ekit working tree
  (based on `3e0daa8`).
- The modified Scene.cpp translation unit compiled in Release using the pinned
  ekit and locally available dependency headers from the main TomCat checkout.
- The entire engine, application linking, and full native/managed regression runner
  were not executed for this isolated change.

## Adapter benchmark

MSVC 19.50.35724, Windows x64, Release. Both modes use the same pinned ekit and
SceneWorld. 100,000 entities have an 8-byte Position; 1%, 10% or 100% also have an
8-byte Velocity. These are surrogate data types, not actual engine components.
Creation is outside timing. Each measurement performs 200 component updates.
Six rounds, discard round 0 and report medians of the remaining five; mode order
alternates each round. The View mode reuses its range; ForEach binds pools per call.

| Coverage | View + Get ms | ForEach references ms | Reduction |
| --- | ---: | ---: | ---: |
| 1% | 5.9124 | 0.5983 | 89.9% |
| 10% | 60.3675 | 5.2575 | 91.3% |
| 100% | 588.2810 | 45.6062 | 92.2% |

Every mode/run verifies its expected final component sum and entity count through
const iteration. [Raw output](benchmark_results.csv). This isolates iteration
overhead and is not an engine frame-time or EnTT comparison. Shared-machine timing
noise remains possible. Structural storage operations and Transform storage policy
are unchanged.
