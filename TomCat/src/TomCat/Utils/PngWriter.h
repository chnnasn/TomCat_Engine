#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace TomCat {

	// Encodes tightly packed 8-bit RGBA pixels as a PNG. Rows run top to bottom.
	// The image data uses zlib "stored" blocks: files are larger than a
	// compressing encoder's, but the writer has no dependency and is exact,
	// which is what screenshots and debugging dumps need. Returns an empty
	// vector when the dimensions are zero or do not match the pixel count.
	[[nodiscard]] std::vector<uint8_t> EncodePngRGBA(uint32_t width, uint32_t height,
		std::span<const uint8_t> rgba);

	// Same, for rows stored bottom to top (OpenGL/framebuffer readback order).
	[[nodiscard]] std::vector<uint8_t> EncodePngRGBABottomUp(uint32_t width,
		uint32_t height, std::span<const uint8_t> rgba);

}
