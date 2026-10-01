# CoinRunner

A small complete-game sample exercising C# scripts, Box2D triggers, score UI,
engine save slots and independent cooked Player execution. Open `Project.tcproj`
in the desktop Editor to play. The headless Player automatically collects four
coins, reports WIN, saves progress and exits with code 0; a failed run exits with
code 1.

## Build and verify

From the repository root, after installing the documented native build tools
and .NET 10 SDK:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1 -Log
```

This builds native and Managed outputs and runs the complete suite including
CoinRunner. To repeat only the game smoke after building:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/Run-CoinRunnerSmoke.ps1
```

The smoke stages current Managed outputs and a private .NET runtime through
`Build-PlayerTemplate.ps1`, cooks the project, then verifies a fresh win/save,
save reload, and recovery after corrupting the primary save. It preserves and
restores any existing `progress.tcsav` and backup. Logs are written to the OS
temporary directory as `coinrunner-run1.log` through `coinrunner-run3.log`.
No manual DLL copies or runtime junctions are required.

`Tools/CoinRunnerGen` generates the scene using engine APIs and copies the
canonical source scripts from `Samples/CoinRunnerScripts`. The checked-in sample
is ready to use; the generator leaves an existing project untouched.

## Interactive acceptance still required

Open the sample in Editor, enter Play, attach the external IDE to the Editor PID,
verify a breakpoint and variables, then stop Play, edit a script, wait for
successful compilation and play again. Automated headless smoke does not prove
visual layout, keyboard interaction or IDE breakpoint behavior. See
`docs/DEBUGGING_AND_PROFILING.md` for the IDE workflow.
