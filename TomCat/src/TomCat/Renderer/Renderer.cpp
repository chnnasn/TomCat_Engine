#include "tcpch.h"
#include "Renderer.h"
#include "Renderer2D.h"
#include "platform/OpenGL/OpenGLProfiler.h"

namespace TomCat {

	void Renderer::Init()
	{
		TC_PROFILE_FUNCTION();

		RenderCommand::Init();
		Renderer2D::Init();
	}

	void Renderer::Shutdown()
	{
		OpenGLProfiler::Shutdown();
		Renderer2D::Shutdown();
	}

	void Renderer::BeginProfileFrame(uint64_t frame) { OpenGLProfiler::BeginFrame(frame); }
	void Renderer::EndProfileFrame() { OpenGLProfiler::EndFrame(); }
	bool Renderer::SupportsGpuProfiling() { return OpenGLProfiler::IsSupported(); }


	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}
}
