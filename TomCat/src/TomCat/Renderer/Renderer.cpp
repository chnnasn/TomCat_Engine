#include "tcpch.h"
#include "Renderer.h"
#include "Renderer2D.h"

namespace TomCat {

	void Renderer::Init()
	{
		TC_PROFILE_FUNCTION();

		RenderCommand::Init();
		Renderer2D::Init();
	}

	void Renderer::Shutdown()
	{
		Renderer2D::Shutdown();
	}


	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);
	}
}
