# TomCatCLI

English | [简体中文](README.zh-CN.md) · Reviewed 2026-10-22 · [All documentation](../../docs/README.md)

`TomCatCLI` is the window-free entry point for CI cooks and native Windows/Linux Player builds. Each host exports its own platform's Player; cross-platform export is not provided. The commands below build the Windows CLI.

```powershell
vendor/premake/bin/premake5.exe --file=Tools/premake5.lua vs2022
msbuild Tools/Tools.sln -p:Configuration=Release -p:Platform=x64
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj
```

Both commands perform a fresh managed compile using the Production profile by default, validate assets, and use
the project's enabled entry scene. Only current project, scene, component and package formats are supported; unsupported versions are rejected without rewriting the inputs.

Windows Editor releases contain the CLI, Managed toolchain, and Player
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

## Linux build and export

Linux support is integrated into `main`. Install the .NET 10 SDK and system dependencies from the [Linux guide](../../docs/LINUX.zh-CN.md), then run from the repository root:

```bash
bash Scripts/Linux_Build.sh
TC_RENDERER=opengl ./build/linux/bin/CLI/TomCatCLI cook --project /absolute/Game/Game.tcproj
TC_RENDERER=opengl ./build/linux/bin/CLI/TomCatCLI build --project /absolute/Game/Game.tcproj \
  --template ./build/linux/bin/Editor/Packages/PlayerTemplates/linux-x64
```

The build script also stages the template. Export writes an executable ELF file, `Game.tcpak`, Managed assemblies and a private .NET runtime under the project's `Build/<ProductName>/`. The target machine needs compatible system libraries but no .NET SDK. Linux uses OpenGL 4.5+; Vulkan probing and the Windows single-file Editor wrapper are outside the current Linux scope. Test evidence and downloadable acceptance games are in the [Linux CI guide](../../docs/LINUX.zh-CN.md#ci-与验收).

## Options and prerequisites

Run Windows source-build commands from the repository root in a Visual Studio developer
shell, after `Scripts/Setup.bat`. Project compilation requires the .NET 10 SDK.
For a source-built Player template, use `Scripts/Build-PlayerTemplate.ps1 -Build` and
pass the resulting directory with `build --template <directory>`.

| Option | Scope | Behavior |
| --- | --- | --- |
| `--project <path>` | Both | Required `.tcproj` path; quote paths containing spaces. |
| `--output <path>` | `cook` only | Defaults to `<project>/Build/Game.tcpak`. |
| `--template <directory>` | `build` only | Overrides Player template discovery; build output remains in the project's Build directory. |
| `--development` | Both | Selects unoptimized Development C# instead of optimized Production C#. |

Use `TomCatCLI help` (or `--help` / `-h`) for usage. Relative paths resolve from
the caller's working directory.

Both commands take the project write lock: close any Editor or other CLI using
that project first.
`cook` writes the asset package; `build` also supplies the freshly compiled managed
assembly and manifest to PlayerBuilder and publishes the standalone Player.
TCPAK output and input use v8 with mandatory per-entry SHA-256 digests.

| Exit code | Meaning |
| --- | --- |
| 0 | Success or help |
| 2 | Invalid arguments |
| 5 | Project load failed (including unsupported format or missing settings) |
| 6–9 | Module/asset initialization, entry scene, managed compilation, or Cook failure |
| 10–12 | Managed metadata, assembly/PDB read, or Player build failure |
| 13 | Project lock unavailable |

See [project configuration](../../PROJECT_SYSTEM.md) and
[managed scripting](../../Managed/README.md) for the underlying contracts.

## Renderer selection

Desktop Editor, Hub, CLI and Player read `TC_RENDERER` once per process. On Windows, unset or
`auto` prefers Vulkan and falls back to OpenGL when Vulkan is unsupported;
`opengl` and `vulkan` force that backend, with an error if forced Vulkan is
unavailable. The headless CLI probes device support without creating a window.
On Linux, use `TC_RENDERER=opengl`; Vulkan probing is not yet ported. Windows example:

```powershell
$env:TC_RENDERER = 'auto'
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj --template C:/Game/PlayerTemplate
```

New desktop cook/build outputs contain precompiled OpenGL and Vulkan shader
variants, so the Player selects its backend independently on its own machine.
Shader source is not shipped. Older single-backend packages still require their
original backend or a recook. Vulkan execution requires a Vulkan 1.2 driver and
loader; automatic selection safely falls back when they are unavailable.
Rebuild Player Templates using `Scripts/Build-PlayerTemplate.ps1 -Build` before
publishing. The builder requires template format 2 and rejects older templates,
whose Player cannot read the new shader bundles; TCPAK remains version 8.
See [RHI/Vulkan integration](../../docs/RHI_VULKAN.zh-CN.md) for validation-layer
and device-selection options. The browser target continues to use WebGL 2;
its build entry point is in the [Web guide](../../Web/README.md).

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

The 2026-10-16 mainline integration passed the full Release regressions, CLI and
Player builds, Player Template and C# Player smoke. Vulkan cook/build and a real
Player script also passed, with the expected smoke-test exit code 73. See the
[integration record](../../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16) for scope
and logs, and the [root build guide](../../README.md#building) for the full
regression entry point. Run build/regression scripts sequentially to avoid
replacing managed compilation inputs during a test.

To test a package in real graphics windows, first rebuild Managed Release,
`ScriptCompilerRegression` and `TomCatCLI` from the current sources, then run:

```powershell
./Scripts/Build-PlayerTemplate.ps1 -Configuration Release -Build -Destination ./Tests/bin/Release-windows-x86_64/PlayerTemplateRegression
./Scripts/Run-CSharpPlayerSmoke.ps1 -Configuration Release -RequirePlayer -Graphics
```

The default smoke mode is headless. `-Graphics` runs the same package first with
automatic backend selection, then with an unavailable Vulkan device filter to
exercise OpenGL fallback. It checks the backend in the game log and the C#
script's completion exit code 73.

## Script build profile

`cook` / `build` default to optimized Production C#. Add `--development` for
unoptimized Development C#. Both retain portable PDBs in isolated Library build
directories. `cook` includes the selected build's PDB in its managed package
payload; `build` currently omits it from the published Player package. See
[production acceptance](../../docs/PRODUCTION_ACCEPTANCE.md).
