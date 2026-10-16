# harfbuzz

Upstream: [HarfBuzz](https://github.com/harfbuzz/harfbuzz)
Version: 10.4.0

Vendored source is compiled locally; no build-time downloads.
[SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) records SHA-256 hashes of the retained
upstream files. See [COPYING](COPYING) for upstream terms.

TomCat uses HarfBuzz in [TextShaper.cpp](../../src/TomCat/Renderer/TextShaper.cpp)
to shape source-font bytes into glyph IDs and text clusters for the shared text
layout and font atlas pipeline. Both the [desktop build](../../premake5.lua) and
[Web build](../../../Web/CMakeLists.txt) compile the `src/harfbuzz.cc` amalgamation.

Real Indic and Arabic regression fonts, their licenses and the exercised layout
checks are documented in [Shaping fixtures](../../../Tests/PhysicsRegression/Fixtures/Shaping/README.md).
GPU text rendering is covered separately by [RHIRegression](../../../docs/RHI_VULKAN.zh-CN.md#启动与构建).
