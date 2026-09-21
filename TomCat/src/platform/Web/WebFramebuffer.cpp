#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "platform/OpenGL/OpenGLFramebuffer.h"
#include <GLES3/gl3.h>
#include <stdexcept>

namespace TomCat {
OpenGLFramebuffer::OpenGLFramebuffer(const FramebufferSpecification& spec) : m_Specification(spec) {
  if (spec.Samples != 1) throw std::invalid_argument("Web framebuffer currently supports single-sample targets only");
  for (auto attachment : spec.Attachments.Attachments)
    if (attachment.TextureFormat == FramebufferTextureFormat::DEPTH24STENCIL8) m_DepthAttachmentSpecification = attachment;
    else m_ColorAttachmentSpecifications.push_back(attachment);
  if (!spec.Width || !spec.Height || spec.Width > 8192 || spec.Height > 8192)
    throw std::invalid_argument("Invalid Web framebuffer dimensions");
  if (!Invalidate()) {
    glDeleteFramebuffers(1, &m_RendererID);
    glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
    if (m_DepthAttachment) glDeleteTextures(1, &m_DepthAttachment);
    throw std::runtime_error("Web framebuffer initialization failed");
  }
}
OpenGLFramebuffer::~OpenGLFramebuffer() {
  glDeleteFramebuffers(1, &m_RendererID);
  glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
  if (m_DepthAttachment) glDeleteTextures(1, &m_DepthAttachment);
}
bool OpenGLFramebuffer::Invalidate() {
  GLint oldDraw, oldRead, oldTexture;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
  const GLuint previous = m_RendererID;
  glDeleteFramebuffers(1, &m_RendererID);
  glDeleteTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
  if (m_DepthAttachment) { glDeleteTextures(1, &m_DepthAttachment); m_DepthAttachment = 0; }
  glGenFramebuffers(1, &m_RendererID); glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
  m_ColorAttachments.resize(m_ColorAttachmentSpecifications.size());
  glGenTextures(m_ColorAttachments.size(), m_ColorAttachments.data());
  std::vector<GLenum> buffers;
  for (size_t i = 0; i < m_ColorAttachments.size(); ++i) {
    glBindTexture(GL_TEXTURE_2D, m_ColorAttachments[i]);
    const bool integer = m_ColorAttachmentSpecifications[i].TextureFormat == FramebufferTextureFormat::RED_INTEGER;
    glTexStorage2D(GL_TEXTURE_2D, 1, integer ? GL_R32I : GL_RGBA8, m_Specification.Width, m_Specification.Height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, m_ColorAttachments[i], 0);
    buffers.push_back(GL_COLOR_ATTACHMENT0 + i);
  }
  if (m_DepthAttachmentSpecification.TextureFormat != FramebufferTextureFormat::None) {
    glGenTextures(1, &m_DepthAttachment); glBindTexture(GL_TEXTURE_2D, m_DepthAttachment);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH24_STENCIL8, m_Specification.Width, m_Specification.Height);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthAttachment, 0);
  }
  if (!buffers.empty()) glDrawBuffers(buffers.size(), buffers.data());
  else { const GLenum none = GL_NONE; glDrawBuffers(1, &none); glReadBuffer(GL_NONE); }
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindTexture(GL_TEXTURE_2D, oldTexture);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previous && oldDraw == previous ? m_RendererID : oldDraw);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, previous && oldRead == previous ? m_RendererID : oldRead);
  return ok;
}
void OpenGLFramebuffer::Bind() { glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID); glViewport(0, 0, m_Specification.Width, m_Specification.Height); }
void OpenGLFramebuffer::Unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
bool OpenGLFramebuffer::Resize(uint32_t width, uint32_t height) {
  if (!width || !height || width > 8192 || height > 8192) return false;
  if (width == m_Specification.Width && height == m_Specification.Height) return true;
  auto spec = m_Specification; spec.Width = width; spec.Height = height;
  try {
    OpenGLFramebuffer replacement(spec);
    GLint draw, read;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    const GLuint previous = m_RendererID;
    std::swap(m_RendererID, replacement.m_RendererID);
    std::swap(m_ColorAttachments, replacement.m_ColorAttachments);
    std::swap(m_DepthAttachment, replacement.m_DepthAttachment);
    std::swap(m_Specification, replacement.m_Specification);
    if (draw == previous) { glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_RendererID); glViewport(0, 0, width, height); }
    if (read == previous) glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
    return true;
  } catch (const std::exception&) { return false; }
}
uint32_t OpenGLFramebuffer::GetColorAttachmentRendererID(uint32_t index) const { return m_ColorAttachments.at(index); }
int OpenGLFramebuffer::ReadPixel(uint32_t index, int x, int y) {
  if (index >= m_ColorAttachments.size() || m_ColorAttachmentSpecifications[index].TextureFormat != FramebufferTextureFormat::RED_INTEGER || x < 0 || y < 0 || x >= m_Specification.Width || y >= m_Specification.Height) return -1;
  GLint old; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old); glBindFramebuffer(GL_READ_FRAMEBUFFER, m_RendererID);
  glReadBuffer(GL_COLOR_ATTACHMENT0 + index); int value = -1; glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &value);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, old); return value;
}
void OpenGLFramebuffer::ClearAttachment(uint32_t index, int value) {
  if (index >= m_ColorAttachments.size()) throw std::out_of_range("Framebuffer attachment");
  GLint old; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_RendererID);
  if (m_ColorAttachmentSpecifications[index].TextureFormat == FramebufferTextureFormat::RED_INTEGER) {
    GLint values[4] = {value, value, value, value}; glClearBufferiv(GL_COLOR, index, values);
  } else { GLfloat values[4] = {float(value), float(value), float(value), float(value)}; glClearBufferfv(GL_COLOR, index, values); }
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, old);
}
}
#endif // __EMSCRIPTEN__
