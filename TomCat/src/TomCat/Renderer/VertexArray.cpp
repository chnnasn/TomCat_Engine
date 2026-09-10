#include "tcpch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "platform/OpenGL/OpenGLVertexArray.h"

namespace TomCat {

	Ref<VertexArray> VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return std::make_shared<OpenGLVertexArray>();
		}
		TC_Core_Assert(false, "unknown rendererapi");
			return nullptr;
	}


}
