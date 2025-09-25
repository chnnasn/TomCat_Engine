#include "tcpch.h"
#include "Framebuffer.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLFramebuffer.h"

namespace TomCat {

	Ref<Framebuffer> Framebuffer::Create(const FramebufferSpecification& spec)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

		case RendererAPI::API::OpenGL: return  CreateRef< OpenGLFramebuffer>(spec);
		}
		TC_Core_Assert(false, "unknown rendererapi")
			return nullptr;
	}

}