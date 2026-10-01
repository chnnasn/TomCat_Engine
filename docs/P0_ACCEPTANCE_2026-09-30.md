# P0 integration acceptance — 2026-09-30

Validated on Windows x64 in Release configuration. This record covers the
CoinRunner delivery, module compatibility check and persistent-format docs.

## Automated results

- `Scripts/Run-Regressions.ps1 -Log`: exit 0, final message
  `All TomCat regressions passed.`
- Managed Release build and `TomCat.Managed.Regression`: passed.
- Native regressions: Physics, SpriteAsset, Advanced2D, ScriptCompiler,
  P0Safety, SaveData, SceneMigration, ModuleSdk, EditorRecovery, Audio,
  Importer, Input and Profiler all passed (13 executables).
- Editor, Hub, independent Player, CLI and CoinRunnerGen Release builds passed.
- Player template validation and isolated C# cook/export/Player smoke passed.
- CoinRunner: CLI cook including C# compilation, automatic private-runtime
  staging, headless collection of four coins, WIN, exit 0, save reload and
  corruption recovery all passed. The three-run smoke restores pre-existing
  primary/backup slot bytes after the test.
- `Scripts/Test-ReleaseScripts.ps1`: passed.

The final local transcript is
`build/logs/regressions-20260930-201236.log` (ignored build evidence). CoinRunner
run logs are `coinrunner-run1.log` through `coinrunner-run3.log` in the OS
temporary directory. The staged Player used
`Tests/bin/Release-windows-x86_64/CoinRunnerTemplate/dotnet`, rather than the
manually staged development Player runtime.

The first full run reached CoinRunner but exposed a null PowerShell 5.1 process
ExitCode despite a successful game run. Retaining the process handle before
waiting fixed the false failure. The final full run above includes the fix.

## Changes closing the delivery gaps

CoinRunner smoke now uses `Build-PlayerTemplate.ps1` to stage current Managed
outputs, VC runtime and .NET 10 host/runtime. It bounds each child process to
60 seconds, checks WIN/save diagnostics, and touches only its progress slot.
The full regression entry now executes Advanced2D as well as building it.

Modules with a nonempty incompatible EngineBuildID are rejected before DLL
loading. Regression verifies rejection without registrations, acceptance of
an unpinned manifest, and the existing matching-build load path. The module
context's project directory string now stays alive throughout its entry call.

`PROJECT_SYSTEM.md` describes save containers, module manifests, and scene
migration backups/journals. Generated CoinRunnerGen project files are ignored;
premake scripts remain the source of truth. Shared vendor Premake output is
regeneration noise and is excluded from the delivery commits.

## Outstanding interactive acceptance

Editor opening CoinRunner, visual layout, keyboard controls, external IDE
breakpoint/variable inspection, and stopping Play followed by script editing
and recompilation have not been interactively verified in this session.
Headless smoke and compilation do not establish those results. No built-in
C# debugger or Play Mode hot replacement is required by the chosen scope.
