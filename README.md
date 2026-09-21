# TomCat Engine — dev_ekit

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Replaces EnTT with ekit and adapts component registration, storage and SceneWorld reference iteration, including const-correct hot-loop access. Use it for ECS integration and compatibility work. See the [migration guide](docs/EKIT_MIGRATION.md); Web migration does not by itself establish browser runtime validation.

## Current Progress

- [x] Replaced EnTT with ekit, using explicit sparse component registration, including components owning strings, containers and resource references.
- [x] Preserved 64-bit entity index/generation and scene UUID/serialization contracts; editor picking uses separate integer IDs. SceneWorld supports const-correct reference iteration for hot loops.

## Getting Started

- [Migration and component registration](docs/EKIT_MIGRATION.md).
- [Dependency provenance](TomCat/vendor/ekit/README.tomcat.md); integration fixes cover owning components, stable references during growth, registration conflicts and multi-translation-unit linkage.

## Validation Status

The previous branch README records Windows x64 MSVC Release success for ten native regression executables, Editor/Player/Hub/CLI builds, CLI smoke and release script tests; the ekit suite passed 4,417 checks. This preserves the recorded migration baseline, not a fresh run of the later iteration change.

## Limits and Remaining Work

- Web includes and code were migrated, but the WebAssembly build was not verified because Emscripten was unavailable.
- Further component/plugin work must preserve registered storage and entity lifetime rules; see the migration guide.
