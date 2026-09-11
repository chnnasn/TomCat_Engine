# TomCat Engine

A **C++20 2D game engine + editor + project hub**, built on OpenGL / ImGui / Box2D.

**Languages**: English | [简体中文](README.zh-CN.md)

---

## Features

- **2D rendering pipeline**: OpenGL batched rendering (Renderer2D) with textures, rotation, cameras
- **ECS scene system**: Entity / Component / Scene with YAML scene serialization
- **2D physics**: built-in Box2D (rigid bodies, colliders)
- **Editor**: ImGui-powered ContentBrowser and SceneHierarchy panels with persisted layouts
- **Hub project manager**: multi-project management, virtual-path mounting, sort by last opened
- **Chinese support**: dynamically generated Chinese glyph set for ImGui, CN/EN toggle
- **One-click packaging**: GitHub Actions 3-mode (Editor / Hub / Both); boxed executables with external Editor Packages

## Screenshots

> TODO (add editor/demo screenshots here)

## Building

### Dependencies

- Visual Studio 2022+
- premake5 (auto-downloaded by the setup script)

> The Vulkan SDK is provided automatically via a git submodule (`vendor/VulkanSDK`, based on VulkanSDK-Windows) - no manual install or environment variables needed.

### Steps

1. Clone the repo with submodules: `git clone --recurse-submodules ...`
2. Run `Scripts\Setup.bat` (auto-downloads premake5)
3. Run `Scripts\Win_GenProjects.bat` to generate the VS projects
4. Open the generated `.sln` and build (Release x64)

## Directory Layout

```
TomCat/            Engine core (rendering, ECS, physics, ImGui integration)
Editor/TomCatInut/ Editor application
Builder/Manager/   Hub (project center)
Scripts/           Build & packaging scripts
vendor/            premake and third-party dependencies
```

## Packaging

- Local: `Scripts\Package-Editor.ps1` / `Scripts\Package-Hub.ps1`
- CI: GitHub Actions (`workflow_dispatch` with editor / hub / both); the Editor is published as `TomCat.zip` (`TomCat.exe` + external `Packages/`), while the Hub remains a boxed executable

## Roadmap

### Scene runtime & physics

- [ ] Scene run control: pause / reset (Play / Stop already exist)
- [ ] Editor physics visualization: circles / lines (collider gizmos)
- [ ] CircleCollider2D component
- [ ] Physics simulation mode (simulate without mutating scene data)
- [ ] Physics regression tests

### Scripting (C#)

- [ ] C# scripting system (Mono integration)
- [ ] Call C++ engine API from C#
- [ ] Attach C# script components in ECS
- [ ] Read/write C# fields in the editor
- [ ] C# script field workflow
- [ ] Script data serialization & management

## Related Projects

- [Ekit](https://github.com/chnnasn/ekit): a friendly, caller-focused header-only ECS library
- [VulkanSDK-Windows](https://github.com/chnnasn/VulkanSDK-Windows): prebuilt Vulkan SDK subset (submodule, no env vars)

## License

[MIT](LICENSE) (c) 2026 chnnasn
