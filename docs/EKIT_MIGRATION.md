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
