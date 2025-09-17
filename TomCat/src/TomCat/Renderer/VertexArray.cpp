#include "tcpch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "platform/OpenGL/OpenGLVertexArray.h"

namespace TomCat {

	VertexArray* VertexArray::Create()
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

		case RendererAPI::OpenGL: return new OpenGLVertexArray();
		}
		TC_Core_Assert(false, "unknown rendererapi")
			return nullptr;
	}


}