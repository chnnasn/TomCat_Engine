#include "tcpch.h"
#include "Buffer.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLBuffer.h"

namespace TomCat {

	VertexBuffer* VertexBuffer::Create(float* vertices, uint32_t  size)
	{
		switch (Renderer::GetAPI()) 
		{
			case RendererAPI::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::OpenGL: return new OpenGLVertexBuffer(vertices,size);
		}
		TC_Core_Assert(false,"unknown rendererapi")
		return nullptr;
	}


	IndexBuffer* IndexBuffer::Create(uint32_t* indices, uint32_t size)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::OpenGL: return new OpenGLIndexBuffer(indices, size);
		}
		TC_Core_Assert(false, "unknown rendererapi")
		return nullptr;
	}

}