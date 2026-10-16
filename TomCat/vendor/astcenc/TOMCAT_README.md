# astcenc

Upstream: [ARM astc-encoder](https://github.com/ARM-software/astc-encoder)
Version: 5.0.0

Vendored source is compiled locally; no build-time downloads.
[SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) records SHA-256 hashes of the retained
upstream files. See [LICENSE.txt](LICENSE.txt) for upstream terms.

TomCat uses this library in [MobileTextureCodec.cpp](../../src/TomCat/Asset/MobileTextureCodec.cpp)
to encode and decode ASTC 4×4 LDR textures, including sRGB profiles and CPU decode
fallback. Both the [desktop build](../../premake5.lua) and [Web build](../../../Web/CMakeLists.txt)
compile the retained `Source/astcenc_*.cpp` files. Desktop uses the SSE2 path;
Web disables the architecture-specific SIMD paths.

Import and round-trip checks live in [ImporterRegression](../../../Tests/ImporterRegression/src/ImporterRegression.cpp).
Cross-backend texture upload checks are documented in the [RHI guide](../../../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16).
