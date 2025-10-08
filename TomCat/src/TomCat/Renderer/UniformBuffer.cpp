#include "tcpch.h"
#include "UniformBuffer.h"

#include "TomCat/Renderer/Renderer.h"
#include "platform/OpenGL/OpenGLUniformBuffer.h"

namespace TomCat {

	Ref<UniformBuffer> UniformBuffer::Create(uint32_t size, uint32_t binding)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    TC_Core_Assert(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return CreateRef<OpenGLUniformBuffer>(size, binding);
		}

		TC_Core_Assert(false, "Unknown RendererAPI!");
		return nullptr;
	}

}