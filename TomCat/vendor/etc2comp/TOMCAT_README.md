# etc2comp

Upstream: [Google etc2comp](https://github.com/google/etc2comp)
Version: master snapshot downloaded 2026-10-01

Vendored source is compiled locally; no build-time downloads.
[SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) records SHA-256 hashes of the retained
upstream files. See [LICENSE](LICENSE) for upstream terms.

TomCat uses this library in [MobileTextureCodec.cpp](../../src/TomCat/Asset/MobileTextureCodec.cpp)
to encode ETC2 RGBA8/sRGBA8 textures and decode ETC2 RGBA8 blocks for CPU fallback.
Both the [desktop build](../../premake5.lua) and [Web build](../../../Web/CMakeLists.txt)
compile the retained `EtcLib` sources.

Import and round-trip checks live in [ImporterRegression](../../../Tests/ImporterRegression/src/ImporterRegression.cpp).
Cross-backend texture upload checks are documented in the [RHI guide](../../../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16).
