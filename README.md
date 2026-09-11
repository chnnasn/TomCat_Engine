# TomCat Engine

A **C++20 2D game engine, editor, and project hub**, built on OpenGL, ImGui, and Box2D.

TomCat is currently focused on completing a practical 2D development workflow. The
existing 3D project template is experimental: it configures a perspective camera,
but a production 3D renderer is not implemented yet.

**Languages**: English | [简体中文](README.zh-CN.md)

---

## Features

- **2D rendering**: OpenGL batched sprites, lines, circles, cameras, framebuffer-based Scene/Game views, and entity picking
- **Scene system**: ECS entities, parent/child hierarchy, stable UUIDs, strict YAML scene serialization, and atomic saves
- **Asset identity workflow**: stable `AssetHandle` references, `.tcmeta` sidecars, registry rebuilding, transactional move/delete operations, and missing-asset placeholders
- **2D physics**: fixed 60 Hz Box2D runtime, explicit and implicit-static bodies, Box/Circle colliders, triggers, filtering, ray/AABB queries, forces, impulses, and `DistanceJoint2D`
- **Physics authoring**: Scene-view collider overlays, collider handles, project Tags/Layers and a Physics 2D collision matrix, Play/Pause/Step/Stop, and deferred Collision/Trigger callbacks for native scripts
- **Editor**: ImGui Scene, Game, Hierarchy, Inspector, and Project panels with per-project layouts and user settings
- **Cooked runtime foundation**: path-free `.tcpak` v3 packages addressed by asset handle, with the project collision matrix embedded for the minimal cooked-player mode
- **Hub**: project creation and discovery, Editor version selection, and per-user recent-project state
- **Localization**: dynamically generated Chinese glyph ranges, with Chinese/English UI switching in the Hub
- **Regression coverage**: a first-party 2D physics suite covering fixed-step behavior, callbacks, runtime rebuilds, queries, joints, schema v9, and cooked-package round trips

## Current Scope

- Supported development platform: **Windows x64**
- Rendering backend: **OpenGL 4.6**
- Primary engine scope: **2D**
- The current packaging scripts publish the **Editor and Hub**, not a standalone user game
- `.tcpak` and `--play-cooked` are runtime foundations; an Editor-facing Build Game workflow and independent Player target are still planned

## Building

### Dependencies

- Windows x64 with an OpenGL 4.6-capable GPU/driver
- Visual Studio 2022+
- Python 3 with `pip` (used by the setup helper)
- premake5 (auto-downloaded by the setup script)

> The Vulkan SDK is provided automatically via a git submodule (`vendor/VulkanSDK`, based on VulkanSDK-Windows) - no manual install or environment variables needed.

### Steps

1. Clone the repo with submodules: `git clone --recurse-submodules ...`
2. Run `Scripts\Setup.bat` once to prepare Premake and the setup dependencies
3. Run `Scripts\Win_GenProjects.bat` to generate the VS projects
4. Open `Editor\Editor.sln`, `Builder\Builder.sln`, or `Tests\Tests.sln` and build the required target (Release x64)

After `Scripts\Setup.bat` has prepared Premake, run the physics and primitive-Sprite regression suites with `powershell -ExecutionPolicy Bypass -File Scripts\Run-PhysicsRegression.ps1`.

## Directory Layout

```
TomCat/            Engine core (rendering, ECS, physics, ImGui integration)
Editor/TomCatInut/ Editor application
Builder/Manager/   Hub (project center)
Tests/             Engine regression test projects
Scripts/           Build & packaging scripts
vendor/            premake and third-party dependencies
```

## Editor and Hub Packaging

- Local: `Scripts\Package-Editor.ps1` / `Scripts\Package-Hub.ps1`
- CI: GitHub Actions (`workflow_dispatch` with editor / hub / both); the Editor is published as `TomCat.zip` (`TomCat.exe` + external `Packages/`), while the Hub remains a boxed executable
- These scripts distribute TomCat itself. Exporting a project as a standalone game is not implemented yet.

## Status and Roadmap

### Completed 2D Physics Milestone

- [x] Fixed-step Play / Pause / Step / Stop scene state model (no separate Simulate state)
- [x] Scene-only Box/Circle collider visualization and Edit Collider handles
- [x] CircleCollider2D, implicit static bodies, triggers, per-fixture and project-layer collision filtering, queries, motion API, and DistanceJoint2D
- [x] Deferred engine/native-script Collision and Trigger callbacks
- [x] Project Settings for Tags, 16 stable Layers, and the symmetric Physics 2D collision matrix
- [x] Scene schema v9 persistence and cooked-package v3 physics round trips
- [x] Physics regression suite (`Scripts\Run-PhysicsRegression.ps1`)

### Next: Project Scripting (C#)

- [ ] Managed runtime integration and project assembly build/load
- [ ] Serializable C# Script component with Inspector attachment and field editing
- [ ] Entity, Transform, Input, Physics, and Scene C++/C# bindings
- [ ] Separate per-frame `OnUpdate` and fixed-step `OnFixedUpdate`
- [ ] Collision/Trigger callbacks, diagnostics, and safe hot reload
- [ ] Include compiled project assemblies in cooked builds

### Game Export and Runtime

- [ ] Independent Player target and Editor Build Game / Build & Run workflow
- [ ] Build settings for product name, icon, resolution, fullscreen, VSync, and output directory
- [ ] Start-scene dependency traversal, unused-asset stripping, and end-to-end packaged-game smoke tests
- [ ] SceneManager, runtime scene switching, additive/asynchronous loading, prefabs, and save data

### Asset Pipeline and 2D Production

- [ ] Importer/Reimport pipeline with content hashes, importer versions, derived-data cache, and dependency graph
- [ ] Typed runtime assets/loaders for audio, fonts, shaders, materials, meshes, and scripts
- [ ] Background import/file watching plus platform-aware texture settings, mipmaps, and compression
- [ ] Sprite atlases/subtextures, pivots, pixels-per-unit semantics, animation, sorting layers, text, runtime UI, tilemaps, particles, and 2D lighting

### Engine Systems and Tooling

- [ ] Input actions/axes, rebinding, contexts, and gamepad support
- [ ] Audio playback, sources/listeners, spatial audio, and mixer buses
- [ ] Undo/Redo, autosave/recovery, Editor console, and an enabled profiler
- [ ] Run regression tests on push/PR CI and extend coverage beyond physics
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
