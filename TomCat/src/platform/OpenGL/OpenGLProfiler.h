#pragma once

#include <cstdint>

namespace TomCat {

	// Called only on the main graphics context. Four pending queries are polled
	// without waiting; full rings skip a measurement rather than stalling rendering.
	class OpenGLProfiler
	{
	public:
		static void BeginFrame(uint64_t frame);
		static void EndFrame();
		static void Shutdown();
		static bool IsSupported();
	};
}
