#include "tcpch.h"
#include "TomCat/Renderer/GraphicsContext.h"

#include "TomCat/Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLContext.h"

namespace TomCat {

	Scope<GraphicsContext> GraphicsContext::Create(void* window)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    TC_Core_Assert(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return CreateScope<OpenGLContext>(static_cast<GLFWwindow*>(window));
		}

		TC_Core_Assert(false, "Unknown RendererAPI!");
		return nullptr;
	}

}