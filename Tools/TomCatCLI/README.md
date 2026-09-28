# TomCatCLI

English | [简体中文](README.zh-CN.md) · Reviewed 2026-09-20 · [All documentation](../../docs/README.md)

`TomCatCLI` is the window-free entry point for CI cooks and Player builds.

```powershell
vendor/premake/bin/premake5.exe --file=Tools/premake5.lua vs2022
msbuild Tools/Tools.sln -p:Configuration=Release -p:Platform=x64
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj
```

Both commands perform a fresh managed Release compile, validate assets, and use
the project's enabled entry scene. Migration is preview-only by default; after
reviewing the printed file list, pass `--migrate` to allow the transactional
upgrade.

Official and local Editor releases contain the CLI, Managed toolchain, and Player
Template inside the single `TomCat.exe`. Invoke that packaged CLI through the
Editor wrapper:

```powershell
TomCat.exe --cli cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
TomCat.exe --cli build --project C:/Game/Game.tcproj
```

In PowerShell, use `./TomCat.exe` for an executable in the current directory,
or supply its full path.

The wrapper validates the embedded manifest, atomically extracts or reuses the
matching runtime below `%LOCALAPPDATA%\TomCat\Editor\Runtime`, and returns the
CLI process exit code. Source builds can continue to run the executable under
`Tools/bin` directly.

## Options and prerequisites

Run source-build commands from the repository root in a Visual Studio developer
shell, after `Scripts/Setup.bat`. Project compilation requires the .NET 10 SDK.
For a source-built Player template, use `Scripts/Build-PlayerTemplate.ps1` and
pass the resulting directory with `build --template <directory>`.

| Option | Scope | Behavior |
| --- | --- | --- |
| `--project <path>` | Both | Required `.tcproj` path; quote paths containing spaces. |
| `--output <path>` | `cook` only | Defaults to `<project>/Build/Game.tcpak`. |
| `--template <directory>` | `build` only | Overrides Player template discovery; build output remains in the project's Build directory. |
| `--migrate` | Both | Explicitly allows the previewed transactional project upgrade. |

Both commands take the project write lock: close any Editor or other CLI using
that project first. Interrupted migrations must be reviewed in the Editor.
`cook` writes the asset package; `build` also supplies the freshly compiled managed
assembly and manifest to PlayerBuilder and publishes the standalone Player.
Current TCPAK output is v7 with per-entry SHA-256 digests; readers accept v5/v6/v7.

| Exit code | Meaning |
| --- | --- |
| 0 | Success or help |
| 2 | Invalid arguments |
| 3–5 | Project inspection failed, migration approval required (4), or load failed |
| 6–9 | Asset initialization, entry scene, managed compilation, or Cook failure |
| 10–12 | Managed metadata, assembly read, or Player build failure |
| 13 | Project lock unavailable |
| 14 | Interrupted migration requires Editor review |

See [project configuration and migration](../../PROJECT_SYSTEM.md) and
[managed scripting](../../Managed/README.md) for the underlying contracts.

## From editor authoring to a build

Apply pending import settings in the Project resource Inspector and save scene
edits before closing the Editor. CLI reads disk state, not unsaved UI drafts.
Configure the enabled entry and additional scenes in
`ProjectSettings/BuildSettings.json`; runtime Single/Additive loading is still
constrained by that list. See the [scene guide](../../docs/SCENE_STREAMING.zh-CN.md).

The [PhysicsPlayground sample](../../Samples/PhysicsPlayground/README.md) uses
`Assets/Scene/sample.tomcat`. After closing the Editor that owns its lock, run
from the repository root:

```powershell
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project Samples/PhysicsPlayground/Project.tcproj
```

These commands were checked against source. The
[2026-09-20 desktop recording](../../docs/portfolio/README.md) covers Hub/Editor;
it did not rerun CLI Cook or standalone Player build acceptance. See the
[root build guide](../../README.md#building) for the full regression entry point.
