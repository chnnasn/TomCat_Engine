# ekit scene storage

TomCat uses ekit for entity generations, component ownership, sparse storage,
and component access. SceneWorld adds const sparse intersection ranges and the
editor's 32-bit picking IDs. It does not implement a second component store.
The dependency is pinned in TomCat/vendor/ekit/README.tomcat.md.

Scene components use RegisterSparseComponent because they include strings,
containers, and resource owners, and callers retain references while adding
other entities. Appending keeps references valid; removing a component can
invalidate references to the removed and moved-last components. Structural
changes must occur outside View iteration. Existing runtime safe points remain
responsible for deferred entity creation and physics changes.

## Reference callbacks for hot loops

Use `SceneWorld::ForEach<Ts...>` when the loop needs the queried components:

```cpp
registry.ForEach<Transform, Rigidbody2D>(
    [](ekit::Entity entity, Transform& transform, Rigidbody2D& body) {
        // Component edits are allowed. Defer structural changes until after ForEach.
    });
const SceneWorld& readOnly = registry;
readOnly.ForEach<Transform>([](const Transform& transform) {
    // The const overload never exposes mutable component references.
});
```

The Entity argument is optional. Every required type must already be registered,
just as for View; all registrations are validated before any callback runs, even
when an earlier component pool is empty. Missing components simply exclude the
entity. A callback exception propagates to its caller. Iteration order is not an
API guarantee, and entities/components must not be added or removed in callbacks.
References have the same lifetime rules as Get; the callback does not own them.

ForEach binds typed ekit pools once per call, chooses the smallest required pool,
reads that pool's component directly, and looks up other required components once.
It passes those references to the callback without HasAll followed by Get. Both
overloads use the existing ekit storage; there is no second component store or
cross-call component-pointer cache. This works with the currently pinned dependency
and does not require updating the vendored library to obtain this adapter benefit.

Animation initialization/update/reset and particle initialization/update/reset/
editor-preview loops use this path. View remains available for range-style and
entity-only consumers. TryGet still returns null for unregistered types, missing
components and dead handles, but no longer repeats a full Has before TryGet.

`Tests/SceneWorldRegression` supplies a standalone CMake/CTest suite, is also part
of the native Premake regression workspace, and is invoked by Run-Regressions.ps1.
Its [benchmark and validation notes](../Tests/SceneWorldRegression/README.md)
describe the measured scope and reproduction commands.

Entity handles retain ekit's complete 64-bit index/generation. Scene UUIDs and
scene/prefab serialization formats are unchanged. Framebuffer picking uses a
separate non-recycled integer ID; FindEntityByPickingID validates the original
handle, so a deleted entity cannot select a newly recycled slot. Entity::GetPickingID
is the explicit rendering interface. Old integer casts remain supported.

World itself is immovable. SceneWorld owns it by unique_ptr and deserialization
swaps validated worlds, leaving both scenes safe to destroy.

## Plugin components

Declare component types with EKIT_COMPONENT(MyComponent). A component descriptor
can provide RegisterStorage to register storage in every new scene, including
copy/deserialize staging scenes:

```cpp
descriptor.RegisterStorage = [](TomCat::Scene& scene) {
    scene.RegisterComponent<MyComponent>();
};
```

Register descriptors at plugin startup before creating scenes. Descriptor Add/Copy callbacks
also invoke the hook for scenes created before the plugin was registered.
For direct Entity::AddComponent calls on an existing scene, first call
scene.RegisterComponent<MyComponent>(). HasComponent returns false for storage
not registered in that scene; adding an unregistered type reports an ekit error.

## Validation

The ekit PR adds multi-translation-unit and owning-component lifetime tests.
Advanced2DRegression covers reference stability, stale handles and picking IDs,
const sparse intersections, and world ownership transfer. PhysicsRegression's
plugin reference remapping fixture declares its ekit storage explicitly.
Run the native regression solution, then build Editor, Player, Hub, and Tools.

Verified on Windows x64 with MSVC Release: all ten native regression executables,
Editor, Player, Hub, and CLI builds, CLI --help, and release script tests.
The vendored ekit suite passes 4,417 checks. Web include paths and picking code
are migrated; the WebAssembly build was not run because Emscripten is not installed.
