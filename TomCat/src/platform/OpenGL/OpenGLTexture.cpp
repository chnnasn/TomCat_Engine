#include "tcpch.h"
#include "Platform/OpenGL/OpenGLTexture.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Utils/PathUtils.h"

#include <stb_image.h>

#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

namespace TomCat {
	namespace {
		constexpr GLenum GLCompressedRGBA_S3TCDXT5 = 0x83F3;
		constexpr GLenum GLCompressedSRGBAlpha_S3TCDXT5 = 0x8C4F;

		bool SupportsBC3Textures()
		{
			if (!glGetStringi)
				return false;
			GLint extensionCount = 0;
			glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
			for (GLint index = 0; index < extensionCount; ++index)
			{
				const auto* raw = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index));
				if (!raw)
					continue;
				const std::string_view extension(reinterpret_cast<const char*>(raw));
				if (extension == "GL_EXT_texture_compression_s3tc"
					|| extension == "GL_EXT_texture_compression_dxt5"
					|| extension == "GL_NV_texture_compression_vtc")
					return true;
			}
			return false;
		}
	}

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
		m_ProfileBytes = static_cast<uint64_t>(m_Width) * m_Height * 4;
		ProfileResourceTracker::Get().Texture(1, static_cast<int64_t>(m_ProfileBytes));
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

		LoadEncodedImage(encoded.data(), encoded.size());
	}

	OpenGLTexture2D::OpenGLTexture2D(const void* encodedData, size_t encodedSize,
		const std::filesystem::path& sourcePath)
		: m_Path(sourcePath.lexically_normal())
	{
		TC_PROFILE_FUNCTION();
		LoadEncodedImage(encodedData, encodedSize);
	}

	bool OpenGLTexture2D::LoadEncodedImage(const void* encodedData, size_t encodedSize)
	{
		if (!encodedData || encodedSize == 0)
		{
			TC_Core_Error("Cannot decode texture '{0}': invalid encoded byte buffer",
				m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path));
			return false;
		}

		const auto bytes = std::span<const uint8_t>(
			static_cast<const uint8_t*>(encodedData), encodedSize);
		if (IsTextureArtifact(bytes))
			return LoadArtifact(bytes);
		if (encodedSize > static_cast<size_t>((std::numeric_limits<int>::max)()))
		{
			TC_Core_Error("Cannot decode texture '{0}': encoded image exceeds the decoder limit",
				m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path));
			return false;
		}

		int width = 0, height = 0;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = nullptr;
		{
			TC_PROFILE_SCOPE("stbi_load_from_memory - OpenGLTexture2D");
			data = stbi_load_from_memory(static_cast<const stbi_uc*>(encodedData),
				static_cast<int>(encodedSize), &width, &height, nullptr, STBI_rgb_alpha);
		}

		if (!data || width <= 0 || height <= 0)
		{
			TC_Core_Error("Failed to decode texture '{0}': {1}",
				m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path),
				stbi_failure_reason() ? stbi_failure_reason() : "unknown stb_image error");
			if (data)
				stbi_image_free(data);
			return false;
		}

		m_Width = static_cast<uint32_t>(width);
		m_Height = static_cast<uint32_t>(height);
		CreateStorageAndUpload(data);
		stbi_image_free(data);
		return m_IsLoaded;
	}

	bool OpenGLTexture2D::LoadArtifact(std::span<const uint8_t> bytes)
	{
		TextureArtifactView artifact;
		std::string error;
		if (!ParseTextureArtifact(bytes, artifact, error))
		{
			TC_Core_Error("Failed to load texture artifact '{0}': {1}",
				m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path), error);
			return false;
		}

		m_Width = artifact.Width;
		m_Height = artifact.Height;
		m_DataFormat = GL_RGBA;
		m_Compressed = artifact.Format == TextureArtifactFormat::BC3
			&& SupportsBC3Textures();
		m_InternalFormat = m_Compressed
			? (artifact.SRGB ? GLCompressedSRGBAlpha_S3TCDXT5
				: GLCompressedRGBA_S3TCDXT5)
			: (artifact.SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		if (!m_RendererID)
		{
			TC_Core_Error("Failed to allocate texture artifact '{0}'",
				m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path));
			return false;
		}
		glTextureStorage2D(m_RendererID, static_cast<GLsizei>(artifact.Mips.size()),
			m_InternalFormat, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));

		std::vector<uint8_t> decoded;
		for (size_t level = 0; level < artifact.Mips.size(); ++level)
		{
			const TextureArtifactMip& mip = artifact.Mips[level];
			if (m_Compressed)
			{
				glCompressedTextureSubImage2D(m_RendererID, static_cast<GLint>(level),
					0, 0, static_cast<GLsizei>(mip.Width), static_cast<GLsizei>(mip.Height),
					m_InternalFormat, static_cast<GLsizei>(mip.Bytes.size()), mip.Bytes.data());
				continue;
			}

			const uint8_t* pixels = mip.Bytes.data();
			if (artifact.Format == TextureArtifactFormat::BC3)
			{
				if (!DecompressTextureMip(mip, artifact.Format, decoded, error))
				{
					TC_Core_Error("Failed to decompress texture artifact '{0}': {1}",
						m_Path.empty() ? std::string("<memory>") : PathToUTF8(m_Path), error);
					glDeleteTextures(1, &m_RendererID);
					m_RendererID = 0;
					return false;
				}
				pixels = decoded.data();
			}
			glTextureSubImage2D(m_RendererID, static_cast<GLint>(level), 0, 0,
				static_cast<GLsizei>(mip.Width), static_cast<GLsizei>(mip.Height),
				m_DataFormat, GL_UNSIGNED_BYTE, pixels);
		}

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER,
			artifact.Mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAX_LEVEL,
			static_cast<GLint>(artifact.Mips.size() - 1));
		m_IsLoaded = true;
		for (const auto& mip : artifact.Mips)
			m_ProfileBytes += m_Compressed ? mip.Bytes.size() : static_cast<uint64_t>(mip.Width) * mip.Height * 4;
		ProfileResourceTracker::Get().Texture(1, static_cast<int64_t>(m_ProfileBytes));
		return true;
	}

	void OpenGLTexture2D::CreateStorageAndUpload(const void* rgbaPixels)
	{
		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;
		m_Compressed = false;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
		if (rgbaPixels)
		{
			glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height,
				m_DataFormat, GL_UNSIGNED_BYTE, rgbaPixels);
		}
		m_IsLoaded = m_RendererID != 0;
		if (m_IsLoaded)
		{
			m_ProfileBytes = static_cast<uint64_t>(m_Width) * m_Height * 4;
			ProfileResourceTracker::Get().Texture(1, static_cast<int64_t>(m_ProfileBytes));
		}
	}

	OpenGLTexture2D::~OpenGLTexture2D()
	{
		TC_PROFILE_FUNCTION();

		if (m_RendererID)
			glDeleteTextures(1, &m_RendererID);
		if (m_ProfileBytes)
			ProfileResourceTracker::Get().Texture(-1, -static_cast<int64_t>(m_ProfileBytes));
	}

	void OpenGLTexture2D::SetData(const void* data, uint32_t size)
	{
		TC_PROFILE_FUNCTION();

		const uint64_t bpp = m_DataFormat == GL_RGBA ? 4ULL : 3ULL;
		const uint64_t expectedSize = static_cast<uint64_t>(m_Width) * m_Height * bpp;
		if (m_Compressed)
		{
			TC_Core_Error("Cannot update a block-compressed texture with raw pixel data");
			return;
		}
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
