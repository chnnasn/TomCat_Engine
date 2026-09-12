# TomCat Engine

A **C++20 2D game engine, editor, and project hub**, built on OpenGL, ImGui, and Box2D.

TomCat is currently focused on completing a practical 2D development workflow. The
existing 3D project template is experimental: it configures a perspective camera,
but a production 3D renderer is not implemented yet.

**Languages**: English | [简体中文](README.zh-CN.md)

---

## Features

- **2D rendering**: OpenGL batched sprites, lines, circles, cameras, framebuffer-based Scene/Game views, and entity picking
- **Scene system**: ECS entities, parent/child hierarchy, stable UUIDs, strict YAML scene serialization, ordered Build Settings, and frame-end single-scene replacement
- **Asset identity workflow**: stable `AssetHandle` references, `.tcmeta` sidecars, registry rebuilding, transactional move/delete operations, and missing-asset placeholders
- **2D physics**: fixed 60 Hz Box2D runtime, explicit and implicit-static bodies, Box/Circle colliders, triggers, filtering, ray/AABB queries, forces, impulses, and `DistanceJoint2D`
- **Physics authoring**: Scene-view collider overlays, collider handles, project Tags/Layers and a Physics 2D collision matrix, Play/Pause/Step/Stop, and deferred C# Collision/Trigger callbacks
- **C# scripting**: .NET 10 project compilation, serialized Inspector fields, collectible Play domains, lifecycle callbacks, Entity/Transform/Input/Physics/Scene APIs, diagnostics, last-good assemblies, and cooked managed payloads
- **Snapshot Prefabs**: `.tcprefab` entity-subtree snapshots with stable LocalIDs, reference remapping, runtime C# `Instantiate`, and ordinary unlinked instances
- **Editor**: ImGui Scene, Game, Hierarchy, Inspector, and Project panels with per-project layouts and user settings
- **Standalone Player**: path-free `.tcpak` v5 packages, an independent non-Editor executable, fixed hashed win-x64 Player Templates, and a bundled private .NET runtime
- **Hub**: project creation and discovery, Editor version selection, and per-user recent-project state
- **Localization**: dynamically generated Chinese glyph ranges, with Chinese/English UI switching in the Hub
- **Regression coverage**: one Release entry point for managed ABI/lifecycle, physics, Sprite assets, script compilation, SceneManager, Prefab, Cook, and isolated Player startup

## Current Scope

- Supported development platform: **Windows x64**
- Rendering backend: **OpenGL 4.6**
- Primary engine scope: **2D**
- Editor-side C# compilation requires the **.NET 10 SDK**
- Exported Players carry a fixed private .NET runtime and the required C++ runtime DLLs, without requiring global .NET or Visual Studio
- V1 deliberately excludes NuGet/third-party managed DLLs, Play Mode hot reload, script debugging, additive/asynchronous scenes, linked Prefab updates, overrides, nested Prefabs, and variants

## Building

### Dependencies

- Windows x64 with an OpenGL 4.6-capable GPU/driver
- Visual Studio 2022+
- .NET 10 SDK (required to compile project C# scripts in the Editor)
- Python 3 with `pip` (used by the setup helper)
- premake5 (auto-downloaded by the setup script)

> The Vulkan SDK is provided automatically via a git submodule (`vendor/VulkanSDK`, based on VulkanSDK-Windows) - no manual install or environment variables needed.

### Steps

1. Clone the repo with submodules: `git clone --recurse-submodules ...`
2. Run `Scripts\Setup.bat` once to prepare Premake and the setup dependencies
3. Run `Scripts\Win_GenProjects.bat` to generate the VS projects
4. Open `Editor\Editor.sln`, `Builder\Builder.sln`, or `Tests\Tests.sln` and build the required target (Release x64)

After `Scripts\Setup.bat` has prepared Premake, run the complete Release suite with `powershell -ExecutionPolicy Bypass -File Scripts\Run-Regressions.ps1`.

## Directory Layout

```
TomCat/            Engine core (rendering, ECS, physics, ImGui integration)
Editor/TomCatInut/ Editor application
Player/            Independent Windows x64 game runtime
Managed/           .NET 10 runtime API, source generator, host, and regressions
Builder/Manager/   Hub (project center)
Tests/             Engine regression test projects
Scripts/           Build & packaging scripts
vendor/            premake and third-party dependencies
```

## Editor and Hub Packaging

- Local: `Scripts\Package-Editor.ps1` / `Scripts\Package-Hub.ps1`
- CI: push and pull requests run the unified Release regressions; the packaging workflow can publish Editor / Hub / both
- The Editor package keeps `Managed/` and `Packages/PlayerTemplates/win-x64/` external. User settings, project source, and authoring JSON are never embedded in the executable.
- Editor **Build** / **Build And Run** cooks enabled Build Settings scenes into `Game.tcpak`, copies the strict Player Template through staging, verifies every SHA-256 and compatibility version, then atomically publishes the build.

## Status and Roadmap

### Completed 2D Physics Milestone

- [x] Fixed-step Play / Pause / Step / Stop scene state model (no separate Simulate state)
- [x] Scene-only Box/Circle collider visualization and Edit Collider handles
- [x] CircleCollider2D, implicit static bodies, triggers, per-fixture and project-layer collision filtering, queries, motion API, and DistanceJoint2D
- [x] Deferred engine/native-script Collision and Trigger callbacks
- [x] Project Settings for Tags, 16 stable Layers, and the symmetric Physics 2D collision matrix
- [x] Scene schema v10 persistence and cooked-package v5 physics round trips
- [x] Physics regression suite (`Scripts\Run-PhysicsRegression.ps1`)

### Completed V1 C#, Player, Scene, and Prefab Milestones

- [x] Managed runtime, source generator, Inspector fields, last-good compilation, deterministic lifecycle/physics callbacks, exception isolation, and collectible Play domains
- [x] Independent win-x64 Player, private .NET runtime, strict versioned/hash-checked template, Build / Build And Run, and Player process smoke coverage
- [x] Shared `ProjectSettings/BuildSettings.json`, `.tcpak` v5 ordered scenes, synchronous frame-end SceneManager transitions, and C# SceneManager API
- [x] Snapshot Prefab V1 with stable LocalIDs, hierarchy/Joint/C# Entity remapping, fresh AttachmentIDs, deferred C# creation, Editor creation/drop workflows, and Cook dependency traversal
- [ ] Product name, icon, resolution/fullscreen/VSync authoring, additive/asynchronous scenes, linked/nested Prefabs, overrides/variants, and save data

### Asset Pipeline and 2D Production

- [ ] Importer/Reimport pipeline with content hashes, importer versions, derived-data cache, and dependency graph
- [ ] Typed runtime assets/loaders for audio, fonts, shaders, materials, meshes, and scripts
- [ ] Background import/file watching plus platform-aware texture settings, mipmaps, and compression
- [ ] Sprite atlases/subtextures, pivots, pixels-per-unit semantics, animation, sorting layers, text, runtime UI, tilemaps, particles, and 2D lighting

### Engine Systems and Tooling

- [ ] Input actions/axes, rebinding, contexts, and gamepad support
- [ ] Audio playback, sources/listeners, spatial audio, and mixer buses
- [ ] Undo/Redo, autosave/recovery, Editor console, and an enabled profiler
- [x] Run managed/native/Player Release regressions on push/PR CI
- [ ] Scene/project schema migration tools, component registration/reflection, and a plugin/module SDK
- [ ] Additional platforms and rendering backends after the Windows/OpenGL 2D workflow is mature

### Future 3D Scope

- [ ] Mesh/model import, materials, lights, PBR, shadows, environment rendering, and skeletal animation
- [ ] Promote the current camera-only 3D template after the 3D runtime and authoring workflow exist

## Related Projects

- [Ekit](https://github.com/chnnasn/ekit): a friendly, caller-focused header-only ECS library
- [VulkanSDK-Windows](https://github.com/chnnasn/VulkanSDK-Windows): prebuilt Vulkan SDK subset (submodule, no env vars)

## License

[MIT](LICENSE) (c) 2026 chnnasn
