# Vendored ekit

Upstream: https://github.com/chnnasn/ekit
Pinned commit: `e26a5327d06eb28f0564ac15ab35f2f18aae7867`
Base main: `03ba18e85e09ef7487ccf02c5bebbede82e05f57`
Previous pin: `82d4de67f37d5d146bb7287e07116dc7567af996` (ekit PR #2 head)
Merged PRs: https://github.com/chnnasn/ekit/pull/2 (merged as `c212e60a`)
License: MIT (see LICENSE).

The include directory is an unmodified copy of that commit's include directory.
Only `component.hpp`, `query.hpp` and `world.hpp` differ from the previous pin;
`LICENSE` is unchanged. Relative to the previous pin, this revision:

- Replaces the sparse `std::deque` payload with allocator-backed fixed pages
  (`PagedComponents<T>`). Growth and `Reserve` keep existing element references
  valid, and removal destroys the component while retaining capacity for reuse.
- Caches query component bindings per execution instead of re-resolving sparse
  storage and archetype columns on every entity, and dispatches the smallest
  required pool up front.
- Compares the full stored entity handle for liveness (dead slots keep their next
  generation with a cleared index) and caches the empty archetype id.
- Adds `StorageVersion()`, `EntityArchetype()`, `EntityRow()`,
  `ReserveEntities()` and `GetSparseStorage<T>().Reserve()`.

The public query/`World` surface used by the TomCat adapter is unchanged, so
`TomCat/src/TomCat/Scene/SceneWorld.h` needs no edits for this bump.

To update, copy include/ and LICENSE from a tested upstream commit and update
this pin. Run the upstream CMake/CTest suite and TomCat's native regressions.
