#include"tcpch.h"
#include "Texture.h"
#include "Renderer.h"
#include "platform/OpenGL/OpenGLTexture.h"


namespace TomCat {

	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return CreateRef<OpenGLTexture2D>(width,height);
		}
		TC_Core_Assert(false, "unknown rendererapi");
			return nullptr;
	}



	Ref<Texture2D> Texture2D::Create(const std::filesystem::path& path)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return CreateRef<OpenGLTexture2D>(path);
		}
		TC_Core_Assert(false, "unknown rendererapi");
			return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const void* encodedData, size_t encodedSize,
		const std::filesystem::path& sourcePath)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL:
				return CreateRef<OpenGLTexture2D>(encodedData, encodedSize, sourcePath);
		}
		TC_Core_Assert(false, "unknown rendererapi");
		return nullptr;
	}
}
