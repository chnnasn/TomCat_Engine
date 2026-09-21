# TomCat Engine — Build_System

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Develops the desktop build and distribution workflow, script diagnostics, reload feedback and external IDE debugging. This branch adds managed NuGet packages, project references and local managed DLLs through `TomCat.Dependencies.csproj`, with runtime dependencies bundled for Editor and cooked Player use. See the [dependency guide](docs/CSHARP_DEPENDENCIES.md). Native package assets and Play Mode hot replacement are outside this support scope.

## Current Progress

- [x] Managed dependency restore and bundling: pure managed NuGet packages, transitive dependencies, project references and local DLLs. Runtime dependencies travel inside `Assembly-CSharp.dll` into Editor domains and cooked Players.
- [x] Script diagnostics and reload feedback, portable PDB loading, and a generated VS Code attach workspace. Failed builds retain the last successful assembly.

## Getting Started

- [Dependency setup and scope](docs/CSHARP_DEPENDENCIES.md): copy [TomCat.Dependencies.csproj](Managed/Templates/TomCat.Dependencies.csproj) beside the game project `.tcproj`, then stop Play and compile.
- [Debugger setup](docs/DEBUGGING_AND_PROFILING.md) and [managed API/build guide](Managed/README.md).

## Validation Status

The dependency guide records regression coverage for restore, dependency updates, isolated loading, failure fallback and cooked dependency calls. The debugging guide records compiler/source-line regression coverage; interactive IDE navigation and breakpoint acceptance still require desktop checks.

## Limits and Remaining Work

- Native libraries and package content assets are unsupported; not every NuGet package is compatible.
- No Play Mode hot replacement, built-in NuGet UI or built-in C# debugger. Referenced files outside watched project paths require explicit recompilation.
