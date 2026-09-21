#pragma once
#ifndef TC_PLATFORM_WEB
#include <glad/glad.h>
#else
#include <GLES3/gl3.h>

// DSA operations used by the engine, implemented with scoped GLES bindings.
inline void glCreateBuffers(GLsizei count, GLuint* ids) { glGenBuffers(count, ids); }
inline void glCreateVertexArrays(GLsizei count, GLuint* ids) { glGenVertexArrays(count, ids); }
inline void glCreateTextures(GLenum, GLsizei count, GLuint* ids) { glGenTextures(count, ids); }
inline void glNamedBufferData(GLuint id, GLsizeiptr size, const void* data, GLenum usage) {
  GLint old; glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &old);
  glBindBuffer(GL_COPY_WRITE_BUFFER, id); glBufferData(GL_COPY_WRITE_BUFFER, size, data, usage);
  glBindBuffer(GL_COPY_WRITE_BUFFER, old);
}
inline void glNamedBufferSubData(GLuint id, GLintptr offset, GLsizeiptr size, const void* data) {
  GLint old; glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &old);
  glBindBuffer(GL_COPY_WRITE_BUFFER, id); glBufferSubData(GL_COPY_WRITE_BUFFER, offset, size, data);
  glBindBuffer(GL_COPY_WRITE_BUFFER, old);
}
struct ScopedWebTexture {
  GLint old;
  explicit ScopedWebTexture(GLuint id) { glGetIntegerv(GL_TEXTURE_BINDING_2D, &old); glBindTexture(GL_TEXTURE_2D, id); }
  ~ScopedWebTexture() { glBindTexture(GL_TEXTURE_2D, old); }
};
inline void glTextureStorage2D(GLuint id, GLsizei levels, GLenum format, GLsizei width, GLsizei height) {
  ScopedWebTexture binding(id); glTexStorage2D(GL_TEXTURE_2D, levels, format, width, height);
}
inline void glTextureParameteri(GLuint id, GLenum parameter, GLint value) {
  ScopedWebTexture binding(id); glTexParameteri(GL_TEXTURE_2D, parameter, value);
}
inline void glTextureSubImage2D(GLuint id, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* data) {
  ScopedWebTexture binding(id); glTexSubImage2D(GL_TEXTURE_2D, level, x, y, width, height, format, type, data);
}
inline void glCompressedTextureSubImage2D(GLuint id, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLsizei size, const void* data) {
  ScopedWebTexture binding(id); glCompressedTexSubImage2D(GL_TEXTURE_2D, level, x, y, width, height, format, size, data);
}
inline void glBindTextureUnit(GLuint unit, GLuint id) {
  GLint old; glGetIntegerv(GL_ACTIVE_TEXTURE, &old);
  glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, id); glActiveTexture(old);
}
#endif
