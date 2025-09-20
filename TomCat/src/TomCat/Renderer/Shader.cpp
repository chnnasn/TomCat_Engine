#include "tcpch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "platform/OpenGL/OpenGLShader.h"

namespace TomCat {


	Shader* Shader::Create(const std::string& filepath)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return new OpenGLShader(filepath);
		}
		TC_Core_Assert(false, "unknown rendererapi")
		return nullptr;
	}

	Shader* Shader::Create(const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return new OpenGLShader(vertexSrc, fragmentSrc);
		}
		TC_Core_Assert(false, "unknown rendererapi")
		return nullptr;
	}

}