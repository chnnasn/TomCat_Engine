#include "tcpch.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "TomCat/Utils/PathUtils.h"

#include <stb_image.h>

#include <fstream>
#include <limits>
#include <vector>

namespace TomCat {

	OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height)
	{
		TC_PROFILE_FUNCTION();
		if (width == 0 || height == 0)
		{
			TC_Core_Error("Cannot create a zero-sized texture ({0}x{1})", width, height);
			return;
		}

		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		m_IsLoaded = true;
	}

	OpenGLTexture2D::OpenGLTexture2D(const std::filesystem::path& path)
		: m_Path(path.lexically_normal())
	{
		TC_PROFILE_FUNCTION();

		std::ifstream input(m_Path, std::ios::binary | std::ios::ate);
		std::streamoff encodedSize = -1;
		if (input)
			encodedSize = static_cast<std::streamoff>(input.tellg());
		if (encodedSize <= 0 || encodedSize > std::numeric_limits<int>::max())
		{
			TC_Core_Error("Failed to read texture '{0}'", PathToUTF8(m_Path));
			return;
		}
		std::vector<stbi_uc> encoded(static_cast<size_t>(encodedSize));
		input.seekg(0, std::ios::beg);
		if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encodedSize)))
		{
			TC_Core_Error("Failed to read texture '{0}'", PathToUTF8(m_Path));
			return;
		}

		int width = 0, height = 0;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = nullptr;
		{
			TC_PROFILE_SCOPE("stbi_load_from_memory - OpenGLTexture2D");
			data = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
				&width, &height, nullptr, STBI_rgb_alpha);
		}

		if (!data)
		{
			TC_Core_Error("Failed to decode texture '{0}': {1}", PathToUTF8(m_Path),
				stbi_failure_reason() ? stbi_failure_reason() : "unknown stb_image error");
			return;
		}

		m_Width = static_cast<uint32_t>(width);
		m_Height = static_cast<uint32_t>(height);
		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
		m_IsLoaded = true;
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		TC_PROFILE_FUNCTION();

		if (m_RendererID)
			glDeleteTextures(1, &m_RendererID);
	}

	void OpenGLTexture2D::SetData(const void* data, uint32_t size)
	{
		TC_PROFILE_FUNCTION();

		const uint64_t bpp = m_DataFormat == GL_RGBA ? 4ULL : 3ULL;
		const uint64_t expectedSize = static_cast<uint64_t>(m_Width) * m_Height * bpp;
		if (!m_IsLoaded || !m_RendererID || !data || expectedSize > std::numeric_limits<uint32_t>::max()
			|| size != expectedSize)
		{
			TC_Core_Error("Invalid texture upload: expected {0} bytes, received {1}", expectedSize, size);
			return;
		}
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	void OpenGLTexture2D::Bind(uint32_t slot) const
	{
		TC_PROFILE_FUNCTION();

		glBindTextureUnit(slot, m_RendererID);
	}
}
