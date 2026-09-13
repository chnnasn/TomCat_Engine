#pragma once

#include "TomCat/Core/WindowMetrics.h"
#include "TomCat/Events/ApplicationEvent.h"
#include "TomCat/Runtime/RuntimeUI.h"

#include <cmath>
#include <stdexcept>

namespace TomCat::Tests {

	inline void RunWindowMetricsRegression()
	{
		auto require = [](bool condition, const char* message)
		{
			if (!condition)
				throw std::runtime_error(message);
		};
		auto approximatelyEqual = [](float first, float second)
		{
			return std::abs(first - second) <= 1.0e-5f;
		};

		// On Windows the content scale can be 150% while screen coordinates and
		// framebuffer pixels remain 1:1. DPI must not be reused as pointer scale.
		const WindowMetrics windows = WindowMetrics::FromNative(
			1920, 1080, 1920, 1080, 1.5f, 1.5f);
		require(approximatelyEqual(windows.GetDPIScale(), 1.5f)
			&& approximatelyEqual(windows.GetScreenToFramebufferScaleX(), 1.0f)
			&& approximatelyEqual(windows.GetScreenToFramebufferScaleY(), 1.0f),
			"content DPI was incorrectly applied to framebuffer coordinates");

		const WindowMetrics pixelDense = WindowMetrics::FromNative(
			1280, 720, 2560, 1440, 2.0f, 2.0f);
		require(approximatelyEqual(pixelDense.GetScreenToFramebufferScaleX(), 2.0f)
			&& approximatelyEqual(pixelDense.GetScreenToFramebufferScaleY(), 2.0f),
			"logical-to-framebuffer pixel ratio is wrong");
		const glm::vec2 pointer = RuntimeUISystem::MapPointerToViewport(
			{ 470.0f, 350.0f }, { 400.0f, 300.0f }, { 2.0f, 1.5f });
		require(approximatelyEqual(pointer.x, 140.0f)
			&& approximatelyEqual(pointer.y, 75.0f),
			"Runtime UI pointer was not converted to framebuffer pixels once");

		const WindowMetrics invalid = WindowMetrics::FromNative(
			-1, 0, -5, 0, std::nanf(""), -2.0f);
		require(invalid.LogicalWidth == 0 && invalid.FramebufferWidth == 0
			&& approximatelyEqual(invalid.GetDPIScale(), 1.0f)
			&& approximatelyEqual(invalid.GetScreenToFramebufferScaleX(), 1.0f),
			"invalid native window metrics were not sanitized");

		const WindowResizeEvent event(pixelDense);
		require(event.GetLogicalWidth() == 1280
			&& event.GetLogicalHeight() == 720
			&& event.GetFramebufferWidth() == 2560
			&& event.GetFramebufferHeight() == 1440
			&& event.GetWidth() == event.GetFramebufferWidth()
			&& approximatelyEqual(event.GetDPIScale(), 2.0f),
			"resize event did not preserve coherent logical/framebuffer metrics");
	}

}
