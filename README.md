# TomCat Engine — main_web

**Languages**: English | [简体中文](README.zh-CN.md)

This page covers this branch's changes and progress. See the [main branch README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.md) for the project overview, shared features, showcase and desktop build baseline; branch-specific requirements and entry points are below.

Documentation updated **2026-09-21**. Validation below cites existing records; this documentation-only update did not rerun engine tests.

## Branch Purpose

Develops Emscripten/WebGL2 targets, shared native ImGui editor panels and browser-host integration, including Scene toolbar and Game-view behavior. The host handles persistence. Use the [Web build and integration guide](Web/README.md); SharedArrayBuffer/Workers are required and C# payloads are unsupported. This branch has its own history and does not necessarily contain the latest desktop mainline changes.

## Current Progress

- [x] Emscripten/WebGL2 modules reuse the native Player runtime, TCPAK reader, 2D scenes and ImGui authoring panels; the browser host owns file selection and persistence.
- [x] Shared draggable Q/W/E/R Scene tools and Pivot/Center, Local/Global controls; Game renders the main camera before Play and after Stop. Browser toolbar placement is persisted when localStorage is available.

## Getting Started

- [Web build, host API and regression guide](Web/README.md): Emscripten, CMake, Ninja and Node.js. The build emits modules, not a complete hosted website.
- Serve with WebGL2, SharedArrayBuffer/Workers and the documented COOP/COEP headers. Host integration and tests live under [Web](Web/README.md).

## Validation Status

The Web guide records 2026-09-16 PhysicsPlayground browser cook/load/render/physics and malformed-package rejection, plus 2026-09-18 toolbar and idle Game behavior. This validates the sample and recorded flows, not arbitrary desktop games. The Windows CI does not run Web build/WASM tests.

## Limits and Remaining Work

- No C# payloads, audible WebAudio, custom cooked SPIR-V shaders, multisample framebuffers or single-thread fallback. Web authoring remains 2D.
- Host persistence and broader device/widget/IME acceptance remain integration work; this branch does not automatically track the latest desktop changes.
