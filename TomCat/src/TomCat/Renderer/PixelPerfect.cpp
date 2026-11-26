#include "tcpch.h"
#include "PixelPerfect.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace TomCat {

	glm::vec3 SnapToPixelGrid(const glm::vec3& position, float unitsPerPixel)
	{
		if (!std::isfinite(unitsPerPixel) || unitsPerPixel <= 0.0f)
			return position;
		return { std::round(position.x / unitsPerPixel) * unitsPerPixel,
			std::round(position.y / unitsPerPixel) * unitsPerPixel, position.z };
	}

	PixelPerfectFrame ComputePixelPerfectFrame(uint32_t viewportWidth,
		uint32_t viewportHeight, uint32_t referenceWidth, uint32_t referenceHeight,
		float pixelsPerUnit, const glm::mat4& cameraTransform, float nearClip,
		float farClip)
	{
		PixelPerfectFrame frame;
		if (viewportWidth == 0 || viewportHeight == 0 || referenceWidth == 0
			|| referenceHeight == 0 || !std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0.0f
			|| !std::isfinite(nearClip) || !std::isfinite(farClip) || farClip <= nearClip)
			return frame;

		frame.Scale = std::max(1u, std::min(viewportWidth / referenceWidth,
			viewportHeight / referenceHeight));
		frame.ContentWidth = std::min(viewportWidth, referenceWidth * frame.Scale);
		frame.ContentHeight = std::min(viewportHeight, referenceHeight * frame.Scale);
		frame.ContentX = (viewportWidth - frame.ContentWidth) / 2;
		frame.ContentY = (viewportHeight - frame.ContentHeight) / 2;
		frame.WorldUnitsPerPixel = 1.0f / pixelsPerUnit;

		// The projection spans the whole viewport at Scale screen pixels per
		// reference pixel. Offsets keep the content rectangle on whole pixels.
		const float unitsPerScreenPixel = frame.WorldUnitsPerPixel
			/ static_cast<float>(frame.Scale);
		const float left = -(static_cast<float>(frame.ContentX)
			+ frame.ContentWidth * 0.5f) * unitsPerScreenPixel;
		const float right = left + viewportWidth * unitsPerScreenPixel;
		const float bottom = -(static_cast<float>(frame.ContentY)
			+ frame.ContentHeight * 0.5f) * unitsPerScreenPixel;
		const float top = bottom + viewportHeight * unitsPerScreenPixel;
		// Same handedness and depth convention as SceneCamera's orthographic path.
		frame.Projection = glm::orthoLH_NO(left, right, bottom, top, nearClip, farClip);

		frame.CameraTransform = cameraTransform;
		const glm::vec3 snapped = SnapToPixelGrid(glm::vec3(cameraTransform[3]),
			frame.WorldUnitsPerPixel);
		frame.CameraTransform[3] = glm::vec4(snapped, cameraTransform[3].w);
		frame.Valid = true;
		return frame;
	}

}
