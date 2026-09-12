#pragma once

#include <cmath>
#include <cstdint>

namespace TomCat {

	// GLFW reports window/cursor positions in screen coordinates while OpenGL
	// consumes framebuffer pixels. Keep those extents separate: content scale
	// controls UI sizing, whereas ScreenToFramebufferScale controls coordinate
	// conversion and may legitimately differ from the DPI scale on Windows.
	struct WindowMetrics
	{
		uint32_t LogicalWidth = 0;
		uint32_t LogicalHeight = 0;
		uint32_t FramebufferWidth = 0;
		uint32_t FramebufferHeight = 0;
		float ContentScaleX = 1.0f;
		float ContentScaleY = 1.0f;

		static WindowMetrics FromNative(int logicalWidth, int logicalHeight,
			int framebufferWidth, int framebufferHeight, float contentScaleX,
			float contentScaleY)
		{
			WindowMetrics value;
			value.LogicalWidth = logicalWidth > 0
				? static_cast<uint32_t>(logicalWidth) : 0;
			value.LogicalHeight = logicalHeight > 0
				? static_cast<uint32_t>(logicalHeight) : 0;
			value.FramebufferWidth = framebufferWidth > 0
				? static_cast<uint32_t>(framebufferWidth) : 0;
			value.FramebufferHeight = framebufferHeight > 0
				? static_cast<uint32_t>(framebufferHeight) : 0;
			value.ContentScaleX = std::isfinite(contentScaleX)
				&& contentScaleX > 0.0f ? contentScaleX : 1.0f;
			value.ContentScaleY = std::isfinite(contentScaleY)
				&& contentScaleY > 0.0f ? contentScaleY : 1.0f;
			return value;
		}

		float GetDPIScale() const
		{
			return (ContentScaleX + ContentScaleY) * 0.5f;
		}

		float GetScreenToFramebufferScaleX() const
		{
			return LogicalWidth > 0 && FramebufferWidth > 0
				? static_cast<float>(FramebufferWidth)
					/ static_cast<float>(LogicalWidth)
				: ContentScaleX;
		}

		float GetScreenToFramebufferScaleY() const
		{
			return LogicalHeight > 0 && FramebufferHeight > 0
				? static_cast<float>(FramebufferHeight)
					/ static_cast<float>(LogicalHeight)
				: ContentScaleY;
		}

		bool operator==(const WindowMetrics&) const = default;
	};

}
