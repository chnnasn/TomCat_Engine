# TomCat Engine

A **C++20 game engine for 2D development**, with a visual editor, a project hub,
and a standalone game runtime.

TomCat brings scene composition, asset management, C# gameplay scripting, and
Box2D physics into one development environment. Its engine library provides the
rendering, ECS scene model, and runtime systems; the Editor exposes those systems
through visual authoring tools, while the Player runs packaged games independently.

**C++20 · OpenGL · ImGui · Box2D · .NET 10 · Windows x64 · MIT**

**Languages**: English | [简体中文](README.zh-CN.md)

---

## Project Overview

| Component | Role |
| --- | --- |
| **Engine** | C++ library for rendering, ECS scenes, assets, physics, input, audio, and UI |
| **Editor** | Visual scene composition, component editing, resource browsing, and Play Mode |
| **Hub** | Project templates, project discovery, and Editor version selection |
| **Managed** | C# gameplay API, script compilation, and runtime hosting |
| **Player** | Independent game executable with packaged assets and a private .NET runtime |
| **[MCP + Skills](Tools/TomCatMCP/README.md)** | Optional local Agent interface for scene/component editing, runtime control, diagnostics and undo |

## Showcase

### Projects and workspace

The Hub organizes projects and discovers installed Editors from their compiled
product identity, so version selection and launching do not depend on folder or
executable names. The Editor brings Scene, Game, Hierarchy, Inspector, Project,
and Console panels together in a responsive dockable workspace whose panel
visibility and layout are saved as they change.

![Full-screen Hub project creation and Editor project opening](docs/portfolio/01-hub-project.gif)

### Visual 2D authoring

Entities are assembled from components. Built-in Sprite primitives, hierarchy
organization, Scene-only visibility controls, and editable transforms make it easy
to compose a scene and adjust each object's position, rotation, and scale.

![Full-screen Sprite creation and Transform editing](docs/portfolio/02-sprite-transform.gif)

### Component-based physics

Rigidbody2D and collider components connect scene objects to Box2D. The Inspector
exposes body types and collision properties, while Scene overlays show collider
bounds alongside the artwork.

![Full-screen rigidbody and floor collider authoring](docs/portfolio/03-physics-authoring.gif)

### Live simulation

Play Mode runs the scene with fixed-step physics. The combined Play/Stop control,
Pause, and single-frame Step support runtime inspection and return to the authored
scene when playback stops.

![Full-screen live Box2D simulation and playback controls](docs/portfolio/04-play-pause-step-stop.gif)

Explore [PhysicsPlayground](Samples/PhysicsPlayground/README.md), a small 2D sample
with a dynamic rectangle and a static floor, or view the
[Editor screenshot](docs/portfolio/editor-fullscreen.png).

## Features

- **2D rendering**: OpenGL batched sprites, lines, circles, cameras, framebuffer-based Scene/Game views, entity picking, stable Sprite Atlas subassets with Rect/Pivot/PPU/Border semantics, deterministic sprite sorting, animation clips, and a parameter-driven Animator state machine
- **Scene system**: ECS entities, parent/child hierarchy, stable UUIDs, strict YAML scene serialization, ordered Build Settings, and frame-end single-scene replacement
- **Asset identity workflow**: stable `AssetHandle` references, `.tcmeta` schema-v2 sidecars, ImporterRegistry, SHA-256 artifact keys, a derived-data cache, dependency tracking, and a background ImportCoordinator with debounced content monitoring, reverse-dependent reimport, and main-thread publication
- **2D physics**: fixed 60 Hz Box2D runtime, explicit and implicit-static bodies, Box/Circle colliders, triggers, filtering, ray/AABB queries, forces, impulses, and `DistanceJoint2D`
- **Physics authoring**: Scene-view collider overlays, collider handles, project Tags/Layers and a Physics 2D collision matrix, combined Play/Stop plus Pause/Step controls, and deferred C# Collision/Trigger callbacks
- **C# scripting**: .NET 10 project compilation, serialized Inspector fields, collectible Play domains, lifecycle callbacks, Entity/Transform/Input/Physics/Scene APIs, diagnostics, last-good assemblies, and cooked managed payloads
- **Snapshot Prefabs**: `.tcprefab` entity-subtree snapshots with stable LocalIDs, reference remapping, runtime C# `Instantiate`, and ordinary unlinked instances
- **Input**: action maps, keyboard/mouse/gamepad bindings, contexts, and runtime rebinding
- **Runtime text and UI**: TTF/OTF/TTC fonts, deterministic on-demand glyph atlases, strict UTF-8 with explicit primary/CJK/emoji fallback chains and a final replacement glyph, world text, and Canvas/RectTransform/Image/Text/Button/EventSystem/LayoutGroup components with DPI-aware layout, clipping, raycast targeting, navigation, and per-interaction gameplay-input capture
- **Audio**: in-memory WAV clips, bounded PCM WAV streaming, 2D spatial audio, AudioSource/AudioListener, Null and XAudio2 backends, device-loss fallback, and Master/Music/SFX buses
- **Editor**: ImGui Scene, Game, Hierarchy, Inspector, Project, and Console panels with responsive docking, actively persisted panel visibility/layout, Hierarchy Scene visibility controls, collapsed and filtered diagnostics, Undo/Redo, autosave/recovery, project locking, and user settings
- **Standalone Player**: path-free `.tcpak` v6 packages with v5/v6 Player compatibility, an independent non-Editor executable, versioned PlayerSettings/BootManifest data, fixed hashed win-x64 Player Templates, and a bundled private .NET runtime
- **Hub**: project creation and discovery, identity-based Editor installation scanning and version selection, exact executable-path launching, and per-user recent-project state
- **Localization**: dynamically generated Chinese glyph ranges, with Chinese/English UI switching in the Hub
- **Regression coverage**: one Release entry point for managed ABI/lifecycle, physics, Sprite assets, script compilation, SceneManager, Prefab, Cook, and isolated Player startup

## Current Scope

- Supported development platform: **Windows x64**
- Rendering backend: **OpenGL 4.6**
- Primary engine scope: **2D**
- The experimental 3D template configures a perspective camera; a production 3D renderer is not implemented yet
- Editor-side C# compilation requires the **.NET 10 SDK**
- Exported Players carry a fixed private .NET runtime and the required C++ runtime DLLs, without requiring global .NET or Visual Studio
- V1 deliberately excludes NuGet/third-party managed DLLs, Play Mode hot reload, script debugging, additive/asynchronous scenes, linked Prefab updates, overrides, nested Prefabs, and variants

Known issue: Hub project loading can fail during migration checks on Windows
extended paths. Open the project through the Editor's **File → Open Project**
using a normal Windows path as a workaround.

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
- The official Editor download is one EVB-compressed `TomCat.exe`. Editor resources stay in its virtual `Packages` tree; the embedded manifest, Managed toolchain, headless CLI, and win-x64 Player Template are extracted only when the Editor runtime is needed, then verified and atomically cached below `%LOCALAPPDATA%\TomCat\Editor\Runtime` for reuse.
- Run packaged headless builds through `TomCat.exe --cli cook ...` or `TomCat.exe --cli build ...`. The wrapper verifies the same embedded runtime and forwards the command to its cached `TomCatCLI.exe`.
- User settings, layouts, logs, project source, and authoring JSON remain outside the executable and its immutable runtime cache.
- Editor **Build** / **Build And Run** cooks enabled Build Settings scenes into `Game.tcpak`, copies the strict Player Template through staging, verifies every SHA-256 and compatibility version, then atomically publishes the build.

## Status and Roadmap

### Completed 2D Physics Milestone

- [x] Fixed-step Play / Pause / Step / Stop scene state model with a combined Play/Stop toolbar control (no separate Simulate state)
- [x] Scene-only Box/Circle collider visualization and Edit Collider handles
- [x] CircleCollider2D, implicit static bodies, triggers, per-fixture and project-layer collision filtering, queries, motion API, and DistanceJoint2D
- [x] Deferred engine/native-script Collision and Trigger callbacks
- [x] Project Settings for Tags, 16 stable Layers, and the symmetric Physics 2D collision matrix
- [x] Scene schema v11 writing with v9-v11 reading, registry-backed components, and cooked-package v6 physics round trips
- [x] Physics regression suite (`Scripts\Run-PhysicsRegression.ps1`)

### Completed V1 C#, Player, Scene, and Prefab Milestones

- [x] Managed runtime, source generator, Inspector fields, last-good compilation, deterministic lifecycle/physics callbacks, exception isolation, and collectible Play domains
- [x] Independent win-x64 Player, private .NET runtime, strict versioned/hash-checked template, Build / Build And Run, and Player process smoke coverage
- [x] Shared `ProjectSettings/BuildSettings.json`, `.tcpak` v6 ordered scenes with v5/v6 Player reading, synchronous safe frame-end SceneManager transitions, and C# SceneManager API
- [x] Snapshot Prefab V1 with stable LocalIDs, hierarchy/Joint/C# Entity remapping, fresh AttachmentIDs, deferred C# creation, Editor creation/drop workflows, and Cook dependency traversal
- [x] Versioned `PlayerSettings.json` for product/icon/display/directory settings, embedded in the v6 BootManifest
- [ ] Additive/asynchronous scenes, linked/nested Prefabs, overrides/variants, and save data

### Asset Pipeline and 2D Production

- [x] ImporterRegistry, SHA-256 ArtifactKey generation, derived-data cache, `.tcmeta` schema v2, and dependency graph
- [x] Background ImportCoordinator with content-hash verification, debounce/coalescing, changed-asset plus transitive reverse-dependent reimport, and main-thread registry/resource publication
- [ ] Production format transcoding, platform texture compression, and mipmap generation
- [x] Stable Sprite Atlas subassets with a list-based slice editor, Rect/Pivot/Pixels Per Unit/Border metadata, and self-contained Cook/Player payloads
- [x] Deterministic sprite sorting, animation clips, and Animator states/transitions with Bool/Int/Float/Trigger parameters, AnyState, and exit time
- [x] TTF/OTF/TTC Font import, primary/fallback/emoji runtime glyph chains and world Text, plus Canvas/RectTransform/Image/Text/Button/EventSystem/LayoutGroup UI with anchors, pivot, layout, clipping, raycast targeting, DPI scaling, mouse/keyboard/gamepad control, and per-interaction gameplay-input consumption
- [ ] Automatic atlas packing/slicing tools and a visual Animator graph editor (outside P0)
- [ ] Tilemaps, particles, and 2D lighting

### Engine Systems and Tooling

- [x] Input Actions, keyboard/mouse/gamepad bindings, contexts, and rebinding
- [x] WAV playback and bounded PCM WAV streaming, 2D spatial audio, device-loss recovery, AudioSource/AudioListener, Null/XAudio2 backends, and Master/Music/SFX buses
- [ ] OGG/Vorbis decoding (unsupported inputs are rejected explicitly; no decoder is bundled)

PCM WAV streaming reads a registry-resolved source range in authoring mode because the P0 audio importer is byte-for-byte passthrough, and reads a validated TCPAK range in cooked Players. If the authoring importer later transcodes audio, it must expose and use a validated DDC payload range instead of the source range.

- [x] Undo/Redo, autosave/recovery, and project locking
- [x] Editor Console with severity counts/filtering, duplicate collapsing, Clear-on-Play, and structured script/runtime diagnostics
- [ ] Editor Profiler
- [x] Run managed/native/Player Release regressions on push/PR CI
- [x] Component registry/reflection, opaque missing-component preservation, and the SCB/ComponentApiV1 bridge
- [ ] Scene/project schema migration tools and a plugin/module SDK
- [ ] Additional platforms and rendering backends after the Windows/OpenGL 2D workflow is mature

### Future 3D Scope

- [ ] Mesh/model import, materials, lights, PBR, shadows, environment rendering, and skeletal animation
- [ ] Promote the current camera-only 3D template after the 3D runtime and authoring workflow exist

## Related Projects

- [Ekit](https://github.com/chnnasn/ekit): a friendly, caller-focused header-only ECS library
- [VulkanSDK-Windows](https://github.com/chnnasn/VulkanSDK-Windows): prebuilt Vulkan SDK subset (submodule, no env vars)

## License

[MIT](LICENSE) (c) 2026 chnnasn
