# TomCat Engine — dev_vulkan

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Adds a shared RHI and a Vulkan 1.2 backend for the existing 2D renderer and editor workflow. OpenGL remains the default; set `TC_RENDERER=vulkan` before launching to select Vulkan. Use this branch for backend integration and cross-backend validation, not as evidence that the separate OpenGL 3D work is included. See the [Vulkan guide](docs/RHI_VULKAN.zh-CN.md).

## Current Progress

- [x] Added a shared RHI and Vulkan 1.2 backend for existing 2D rendering, offscreen Scene/Game targets, integer picking, ImGui textures and GPU timing.
- [x] Migrated shader artifacts, per-draw data snapshots, resource synchronization and presentation; OpenGL remains the default backend.

## Getting Started

- [Vulkan setup, requirements and validation](docs/RHI_VULKAN.zh-CN.md). Set `TC_RENDERER=vulkan` before launching; backend selection is fixed for that process.
- Run `Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport`, then `Scripts/Run-RHIRegression.ps1 -Backend opengl`. Validation mode needs the Khronos validation layer.

## Validation Status

The 2026-09-21 guide records Windows Release builds/native regressions, Intel and NVIDIA GPU validation, Editor interaction and a cooked standalone Player run without validation errors. Web build/browser acceptance was not performed.

## Limits and Remaining Work

- This is the 2D backend migration; it does not include the separate OpenGL 3D feature track.
- Conservative synchronization is not a performance advantage claim. Multi-frame/upload optimization, render graphs, MSAA, compute and storage resources are outside the current frontend scope. Auxiliary viewport input routing remains incomplete.
