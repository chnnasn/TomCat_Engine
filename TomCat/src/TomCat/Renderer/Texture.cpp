#include"tcpch.h"
#include "Texture.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLTexture.h"


namespace TomCat {

	Ref<Texture2D> Texture2D:: Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return std::make_shared<OpenGLTexture2D>(path);
		}
		TC_Core_Assert(false, "unknown rendererapi")
			return nullptr;
	}


}