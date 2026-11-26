#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace TomCat {

	// Integer-scale pixel-art framing for an orthographic 2D camera.
	//
	// One reference pixel (1 / PixelsPerUnit world units) maps to exactly
	// Scale screen pixels, Scale being the largest whole number for which the
	// reference resolution fits the viewport. The visible world area is the
	// reference resolution; the leftover screen is letterboxed. The camera
	// position snaps to the reference-pixel grid, and sprite translations snap
	// to the same grid, so texels never straddle screen pixels and tiles never
	// show seams.
	struct PixelPerfectFrame
	{
		bool Valid = false;
		uint32_t Scale = 1;
		// Visible content rectangle in framebuffer pixels, bottom-left origin.
		uint32_t ContentX = 0, ContentY = 0;
		uint32_t ContentWidth = 0, ContentHeight = 0;
		// Orthographic projection covering the whole viewport, so the content
		// rectangle shows exactly the reference resolution at integer scale.
		glm::mat4 Projection{ 1.0f };
		// Camera transform with its translation snapped to the pixel grid.
		glm::mat4 CameraTransform{ 1.0f };
		float WorldUnitsPerPixel = 0.01f;
	};

	// Valid=false for zero sizes, a non-positive or non-finite PPU, or invalid
	// clip planes. A viewport smaller than the reference resolution uses scale
	// 1 and crops the content to the viewport.
	[[nodiscard]] PixelPerfectFrame ComputePixelPerfectFrame(uint32_t viewportWidth,
		uint32_t viewportHeight, uint32_t referenceWidth, uint32_t referenceHeight,
		float pixelsPerUnit, const glm::mat4& cameraTransform, float nearClip,
		float farClip);

	// Rounds a world-space translation to the nearest multiple of unitsPerPixel.
	[[nodiscard]] glm::vec3 SnapToPixelGrid(const glm::vec3& position, float unitsPerPixel);

}
