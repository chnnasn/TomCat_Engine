# Texture sampling

Texture import settings support `filterMode: linear|nearest`, `wrapU: repeat|clamp`
and `wrapV: repeat|clamp` (values are case-insensitive). The texture inspector
exposes the same settings. `clamp` means clamp to edge; `nearest` selects nearest
texels and, when mipmaps exist, the nearest mip level. Linear retains trilinear
minification for mipmapped textures. Disable mipmap generation separately when
pixel art requires it.

Defaults remain linear filtering and repeat addressing on both axes. Existing
textures, tiled materials, source-image loads and runtime-created textures keep
their behavior. Cropped pixel sprites can opt into nearest/clamp; linear/clamp
also prevents the opposite edge from bleeding across an otherwise transparent
border. Clamp applies to the full texture, not each region inside an atlas;
atlas authors still need appropriate padding/extrusion for linear filtering.

TCTX v1 remains readable with its original defaults. Imports with default
sampling still emit byte-identical v1 artifacts. Non-default sampling emits
TCTX v2: the 32-byte header and mip layout are unchanged; the flags at offset24
use bit0 for sRGB, bit1 for nearest, bit2 for clamp U and bit3 for clamp V.
Unknown flags are rejected, and v1 rejects the new bits. Old readers therefore
reject v2 instead of silently ignoring its sampling. Importer version6 rebuilds
cached artifacts. This does not change the TCPAK container or managed API.

OpenGL/WebGL and Vulkan apply the artifact settings once during texture upload,
without per-draw sampler mutations. Importer regression covers all eight
combinations, invalid settings/flags and v1 compatibility. RHI regression reads
rendered pixels to preserve legacy repeat behavior, independently clamp each
axis, eliminate opposite-edge bleed and verify nearest texel magnification.
