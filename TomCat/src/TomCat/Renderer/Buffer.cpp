#include "tcpch.h"
#include "Buffer.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLBuffer.h"

namespace TomCat {

	Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t  size)
	{
		switch (Renderer::GetAPI()) 
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return  CreateRef< OpenGLVertexBuffer>(vertices,size);
		}
		TC_Core_Assert(false,"unknown rendererapi")
		return nullptr;
	}


	Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t size)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return CreateRef <OpenGLIndexBuffer>(indices, size);
		}
		TC_Core_Assert(false, "unknown rendererapi")
		return nullptr;
	}

}