#include "tcpch.h"
#include "OpenGLFramebuffer.h"

#include <glad/glad.h>
#include <stdexcept>

namespace TomCat {

	namespace Utils {
		class ScopedFramebufferState
		{
		public:
			ScopedFramebufferState()
			{
				glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_DrawFramebuffer);
				glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_ReadFramebuffer);
				glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_Texture2D);
				glGetIntegerv(GL_TEXTURE_BINDING_2D_MULTISAMPLE, &m_Texture2DMultisample);
			}

			~ScopedFramebufferState()
			{
				Restore();
			}

			void MapFramebuffer(uint32_t oldID, uint32_t newID)
			{
				if (!oldID)
					return;
				if (static_cast<uint32_t>(m_DrawFramebuffer) == oldID)
					m_DrawFramebuffer = static_cast<GLint>(newID);
				if (static_cast<uint32_t>(m_ReadFramebuffer) == oldID)
					m_ReadFramebuffer = static_cast<GLint>(newID);
			}

			void MapTexture(uint32_t oldID, uint32_t newID)
			{
				if (!oldID)
					return;
				if (static_cast<uint32_t>(m_Texture2D) == oldID)
					m_Texture2D = static_cast<GLint>(newID);
				if (static_cast<uint32_t>(m_Texture2DMultisample) == oldID)
					m_Texture2DMultisample = static_cast<GLint>(newID);
			}

			void Restore()
			{
				if (m_Restored)
					return;

				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<uint32_t>(m_DrawFramebuffer));
				glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<uint32_t>(m_ReadFramebuffer));
				glBindTexture(GL_TEXTURE_2D, static_cast<uint32_t>(m_Texture2D));
				glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, static_cast<uint32_t>(m_Texture2DMultisample));
				m_Restored = true;
			}

		private:
			GLint m_DrawFramebuffer = 0;
			GLint m_ReadFramebuffer = 0;
			GLint m_Texture2D = 0;
			GLint m_Texture2DMultisample = 0;
			bool m_Restored = false;
		};

		struct PendingFramebufferResources
		{
			explicit PendingFramebufferResources(size_t colorAttachmentCount)
				: ColorAttachments(colorAttachmentCount)
			{
			}

			~PendingFramebufferResources()
			{
				if (RendererID)
					glDeleteFramebuffers(1, &RendererID);
				if (!ColorAttachments.empty())
					glDeleteTextures(static_cast<GLsizei>(ColorAttachments.size()), ColorAttachments.data());
				if (DepthAttachment)
					glDeleteTextures(1, &DepthAttachment);
			}

			void Release()
			{
				RendererID = 0;
				ColorAttachments.clear();
				DepthAttachment = 0;
			}

			uint32_t RendererID = 0;
			std::vector<uint32_t> ColorAttachments;
			uint32_t DepthAttachment = 0;
		};

		static GLenum TextureTarget(bool multisampled)
		{
			return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
		}

		static void CreateTextures(bool multisampled, uint32_t* outID, uint32_t count)
		{
			glCreateTextures(TextureTarget(multisampled), count, outID);
		}

		static void BindTexture(bool multisampled, uint32_t id)
		{
			glBindTexture(TextureTarget(multisampled), id);
		}

		static void AttachColorTexture(uint32_t id, int samples, GLenum internalFormat, GLenum format, uint32_t width, uint32_t height, int index)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, internalFormat, width, height, GL_FALSE);
			}
			else
			{
				const GLenum dataType = format == GL_RED_INTEGER ? GL_INT : GL_UNSIGNED_BYTE;
				glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, dataType, nullptr);

				const GLenum filter = format == GL_RED_INTEGER ? GL_NEAREST : GL_LINEAR;
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}

			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + index, TextureTarget(multisampled), id, 0);
		}

		static void AttachDepthTexture(uint32_t id, int samples, GLenum format, GLenum attachmentType, uint32_t width, uint32_t height)
		{
			bool multisampled = samples > 1;
			if (multisampled)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, format, width, height, GL_FALSE);
			}
			else
			{
				glTexStorage2D(GL_TEXTURE_2D, 1, format, width, height);

				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}

			glFramebufferTexture2D(GL_FRAMEBUFFER, attachmentType, TextureTarget(multisampled), id, 0);
		}

		static bool IsDepthFormat(FramebufferTextureFormat format)
		{
			switch (format)
			{
			case FramebufferTextureFormat::DEPTH24STENCIL8:  return true;
			}

			return false;
		}

	}

	OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec)
		: m_Specification(spec)
	{
		for (auto spec : m_Specification.Attachments.Attachments)
		{
			if (spec.TextureFormat == FramebufferTextureFormat::None)
				continue;
			if (!Utils::IsDepthFormat(spec.TextureFormat))
				m_ColorAttachmentSpecifications.emplace_back(spec);
			else
				m_DepthAttachmentSpecification = spec;
		}

		if (!Invalidate())
			throw std::runtime_error("Failed to create OpenGL framebuffer");
	}

	OpenGLFramebuffer::~OpenGLFramebuffer()
	{
		if (m_RendererID)
			glDeleteFramebuffers(1, &m_RendererID);
		if (!m_ColorAttachments.empty())
			glDeleteTextures(static_cast<GLsizei>(m_ColorAttachments.size()), m_ColorAttachments.data());
		if (m_DepthAttachment)
			glDeleteTextures(1, &m_DepthAttachment);
	}

	bool OpenGLFramebuffer::Invalidate()
	{
		if (m_Specification.Samples == 0 || m_Specification.Width == 0 || m_Specification.Height == 0 ||
			m_Specification.Width > Framebuffer::MaxFramebufferSize ||
			m_Specification.Height > Framebuffer::MaxFramebufferSize)
		{
			TC_Core_Error("Invalid framebuffer size: {0}x{1}", m_Specification.Width, m_Specification.Height);
			return false;
		}

		if (m_ColorAttachmentSpecifications.size() > 4)
		{
			TC_Core_Error("Framebuffer supports at most four color attachments (received {0})", m_ColorAttachmentSpecifications.size());
			return false;
		}

		Utils::ScopedFramebufferState framebufferState;
		Utils::PendingFramebufferResources pending(m_ColorAttachmentSpecifications.size());

		glCreateFramebuffers(1, &pending.RendererID);
		if (!pending.RendererID)
		{
			TC_Core_Error("OpenGL could not create a framebuffer");
			return false;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, pending.RendererID);

		bool multisample = m_Specification.Samples > 1;

		// Attachments
		if (!pending.ColorAttachments.empty())
		{
			Utils::CreateTextures(multisample, pending.ColorAttachments.data(), static_cast<uint32_t>(pending.ColorAttachments.size()));

			for (size_t i = 0; i < pending.ColorAttachments.size(); i++)
			{
				Utils::BindTexture(multisample, pending.ColorAttachments[i]);
				switch (m_ColorAttachmentSpecifications[i].TextureFormat)
				{
				case FramebufferTextureFormat::RGBA8:
					Utils::AttachColorTexture(pending.ColorAttachments[i], m_Specification.Samples, GL_RGBA8, GL_RGBA, m_Specification.Width, m_Specification.Height, static_cast<int>(i));
					break;
				case FramebufferTextureFormat::RED_INTEGER:
					Utils::AttachColorTexture(pending.ColorAttachments[i], m_Specification.Samples, GL_R32I, GL_RED_INTEGER, m_Specification.Width, m_Specification.Height, static_cast<int>(i));
					break;
				}
			}
		}

		if (m_DepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None)
		{
			Utils::CreateTextures(multisample, &pending.DepthAttachment, 1);
			Utils::BindTexture(multisample, pending.DepthAttachment);
			switch (m_DepthAttachmentSpecification.TextureFormat)
			{
			case FramebufferTextureFormat::DEPTH24STENCIL8:
				Utils::AttachDepthTexture(pending.DepthAttachment, m_Specification.Samples, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL_ATTACHMENT, m_Specification.Width, m_Specification.Height);
				break;
			}
		}

		if (pending.ColorAttachments.size() > 1)
		{
			GLenum buffers[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
			glDrawBuffers(static_cast<GLsizei>(pending.ColorAttachments.size()), buffers);
		}
		else if (pending.ColorAttachments.empty())
		{
			// Only depth-pass
			glDrawBuffer(GL_NONE);
		}

		const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE)
		{
			TC_Core_Error("Framebuffer is incomplete (OpenGL status 0x{0:X})", framebufferStatus);
			return false;
		}

		const uint32_t oldRendererID = m_RendererID;
		const uint32_t newRendererID = pending.RendererID;
		std::vector<uint32_t> oldColorAttachments = std::move(m_ColorAttachments);
		const uint32_t oldDepthAttachment = m_DepthAttachment;

		m_RendererID = newRendererID;
		m_ColorAttachments = std::move(pending.ColorAttachments);
		m_DepthAttachment = pending.DepthAttachment;
		pending.Release();

		framebufferState.MapFramebuffer(oldRendererID, m_RendererID);
		for (size_t i = 0; i < oldColorAttachments.size(); ++i)
			framebufferState.MapTexture(oldColorAttachments[i], m_ColorAttachments[i]);
		framebufferState.MapTexture(oldDepthAttachment, m_DepthAttachment);
		framebufferState.Restore();

		if (oldRendererID)
			glDeleteFramebuffers(1, &oldRendererID);
		if (!oldColorAttachments.empty())
			glDeleteTextures(static_cast<GLsizei>(oldColorAttachments.size()), oldColorAttachments.data());
		if (oldDepthAttachment)
			glDeleteTextures(1, &oldDepthAttachment);
		return true;
	}

	void OpenGLFramebuffer::Bind()
	{
		if (!m_RendererID)
		{
			TC_Core_Error("Cannot bind an invalid framebuffer");
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
		glViewport(0, 0, m_Specification.Width, m_Specification.Height);
	}

	void OpenGLFramebuffer::Unbind()
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	bool OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0 || width > Framebuffer::MaxFramebufferSize ||
			height > Framebuffer::MaxFramebufferSize)
		{
			TC_Core_Warn("Ignoring invalid framebuffer resize: {0}x{1}", width, height);
			return false;
		}
		if (width == m_Specification.Width && height == m_Specification.Height && m_RendererID)
			return true;

		const uint32_t previousWidth = m_Specification.Width;
		const uint32_t previousHeight = m_Specification.Height;
		m_Specification.Width = width;
		m_Specification.Height = height;

		try
		{
			if (!Invalidate())
			{
				m_Specification.Width = previousWidth;
				m_Specification.Height = previousHeight;
				return false;
			}
		}
		catch (...)
		{
			m_Specification.Width = previousWidth;
			m_Specification.Height = previousHeight;
			throw;
		}
		return true;
	}

	int OpenGLFramebuffer::ReadPixel(uint32_t attachmentIndex, int x, int y)
	{
		if (!m_RendererID || attachmentIndex >= m_ColorAttachments.size() ||
			x < 0 || y < 0 || x >= static_cast<int>(m_Specification.Width) || y >= static_cast<int>(m_Specification.Height))
		{
			TC_Core_Warn("Invalid framebuffer pixel read (attachment {0}, position {1},{2})", attachmentIndex, x, y);
			return -1;
		}
		if (m_Specification.Samples > 1)
		{
			TC_Core_Warn("ReadPixel requires a resolved single-sample framebuffer");
			return -1;
		}
		if (m_ColorAttachmentSpecifications[attachmentIndex].TextureFormat != FramebufferTextureFormat::RED_INTEGER)
		{
			TC_Core_Warn("ReadPixel only supports RED_INTEGER attachments");
			return -1;
		}

		glReadBuffer(GL_COLOR_ATTACHMENT0 + attachmentIndex);
		int pixelData = -1;
		glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &pixelData);
		return pixelData;

	}

	void OpenGLFramebuffer::ClearAttachment(uint32_t attachmentIndex, int value)
	{
		if (attachmentIndex >= m_ColorAttachments.size())
		{
			TC_Core_Warn("Invalid framebuffer attachment clear: {0}", attachmentIndex);
			return;
		}

		auto& spec = m_ColorAttachmentSpecifications[attachmentIndex];
		if (spec.TextureFormat == FramebufferTextureFormat::RGBA8)
		{
			const uint8_t channel = static_cast<uint8_t>(std::clamp(value, 0, 255));
			const uint8_t clearValue[4] = { channel, channel, channel, channel };
			glClearTexImage(m_ColorAttachments[attachmentIndex], 0, GL_RGBA, GL_UNSIGNED_BYTE, clearValue);
		}
		else if (spec.TextureFormat == FramebufferTextureFormat::RED_INTEGER)
		{
			glClearTexImage(m_ColorAttachments[attachmentIndex], 0, GL_RED_INTEGER, GL_INT, &value);
		}
		else
		{
			TC_Core_Warn("Unsupported framebuffer attachment format for clearing");
		}
	}

	uint32_t OpenGLFramebuffer::GetColorAttachmentRendererID(uint32_t index) const
	{
		if (index >= m_ColorAttachments.size())
		{
			TC_Core_Warn("Invalid framebuffer color attachment index: {0}", index);
			return 0;
		}

		return m_ColorAttachments[index];
	}
}
