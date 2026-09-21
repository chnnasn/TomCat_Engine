# TomCat Engine — dev_butter

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Replaces Box2D with Butter and enables 2D continuous collision detection (CCD) by default. Use it to develop and validate the physics backend, collision/trigger callbacks and existing 2D physics API compatibility. This is a 2D physics branch, with no new 3D rigid-body system.

## Current Progress

- [x] Replaced Box2D with a Butter adapter while preserving scene components and scripting physics APIs. Desktop and Web build dependencies were migrated.
- [x] Enabled dynamic-to-static/kinematic CCD by default, with collision filtering, contact aggregation, deferred callbacks, queries and distance joints.

## Getting Started

- [Migration, pinned dependency and CCD behavior](docs/Butter-Migration.md).
- [PhysicsPlayground sample](Samples/PhysicsPlayground/README.md); initialize submodules, then run `Scripts/Run-PhysicsRegression.ps1 -Configuration Release`.

## Validation Status

The 2026-09-20 migration record reports Butter Debug CTest 13/13, 87 CCD checks in both Debug and Release, and TomCat PhysicsRegression 56/56. Initial migration also records other native regressions and Editor/Player builds. These are historical local results, not browser acceptance.

## Limits and Remaining Work

- No complete WASM build/browser acceptance is recorded. Solver trajectories need not match Box2D numerically.
- Triggers retain discrete end-of-step overlap semantics; no 3D CCD. Exhausted CCD budgets may slow dense scenes. Test stacking, restitution and high-speed gameplay in actual levels.
