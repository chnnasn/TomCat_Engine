# CoinRunner

Verified against main: 2026-10-16 · [Documentation index](../../docs/README.md)

A small complete-game sample exercising C# scripts, Box2D triggers, score UI,
engine save slots and independent cooked Player execution. Open `Project.tcproj`
in the desktop Editor to play. The headless Player automatically collects four
coins, reports WIN, saves progress and exits with code 0; a failed run exits with
code 1.

## Build and verify

From the repository root, after installing the [native build tools](../../README.md#building)
and .NET 10 SDK:

```powershell
$env:TC_RENDERER = 'opengl'
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1 -Configuration Release -Log
```

This builds native and Managed outputs and runs the complete suite including
CoinRunner. To repeat only the game smoke after building:

```powershell
$env:TC_RENDERER = 'opengl'
powershell -ExecutionPolicy Bypass -File Scripts/Run-CoinRunnerSmoke.ps1 -Configuration Release
```

The smoke stages current Managed outputs and a private .NET runtime through
[Build-PlayerTemplate.ps1](../../Scripts/Build-PlayerTemplate.ps1), cooks a clean
copy of the project, then verifies a fresh win/save,
save reload, and recovery after corrupting the primary save. It preserves and
restores any existing `progress.tcsav` and backup. Logs are written to the OS
temporary directory as `coinrunner-run1.log` through `coinrunner-run3.log`
(with corresponding `.log.err` files for stderr). The clean copy also verifies
that cooking publishes a Production script build without relying on a local
Development cache.
No manual DLL copies or runtime junctions are required.

[Tools/CoinRunnerGen](../../Tools/CoinRunnerGen) generates the scene using engine APIs and copies the
canonical source scripts from [Samples/CoinRunnerScripts](../CoinRunnerScripts). The checked-in sample
is ready to use; the generator leaves an existing project untouched by default.
`CoinRunnerGen --refresh-scene` explicitly regenerates the scene after closing
the Editor and preserves existing C# sources. The HUD uses a top-left anchor so
DPI scaling cannot push a fixed center offset beyond the viewport.

The complete Release suite, including this three-run CoinRunner smoke, passed
on 2026-10-16 after the Vulkan merge. See the [integration verification record](../../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16)
for the separately verified GPU, Editor, Player and Web workflows.

## Desktop renderer

OpenGL remains the default. To open the sample with Vulkan, launch Editor from
the repository root, then open `Samples/CoinRunner/Project.tcproj`:

```powershell
$env:TC_RENDERER = 'vulkan'
& ./Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe
```

Vulkan requires a Vulkan 1.2 driver. Set `TC_RENDERER` before starting Editor,
CLI or Player; a process cannot switch renderers in place. Cook and run a
packaged game with the same renderer, and recook after changing it. Use
`$env:TC_RENDERER = 'opengl'` for subsequent OpenGL launches. The headless
CoinRunner smoke verifies gameplay and saves; use the [RHI regression commands](../../docs/RHI_VULKAN.zh-CN.md#启动与构建)
to verify GPU rendering.

## Interactive acceptance still required

Open the sample in Editor, enter Play, attach the external IDE to the Editor PID,
verify a breakpoint and variables, then stop Play, edit a script, wait for
successful compilation and play again. Automated headless smoke does not prove
visual layout, keyboard interaction or IDE breakpoint behavior. See
[Debugging and profiling](../../docs/DEBUGGING_AND_PROFILING.md) for the IDE workflow.

The Player Controller exposes `MoveSpeed` as a public instance field (default
3.5). Select Player to edit **Move Speed** in the Inspector; `const`, `static`
and `readonly` fields are not serialized Inspector parameters. In Play mode,
gameplay keyboard/mouse input belongs to the focused Game view. Clicking Scene,
Hierarchy, Inspector or Console returns input ownership to the editor.
Use **A/D** or **Left/Right** to move, and **Space**, **W** or **Up** to jump.
