#include "tcpch.h"
#ifdef __EMSCRIPTEN__
#include "platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace TomCat {
namespace {
// Only the engine's embedded infrastructure shaders use this conversion.
// Project SPIR-V artifacts must be cooked to GLES before Web publication.
std::string ToGLES(std::string source, GLenum stage) {
  source.erase(0, source.find_first_not_of(" \t\r\n"));
  source = std::regex_replace(source, std::regex(R"(#version 450 core)"), "#version 300 es\nprecision highp float;\nprecision highp int;");
  source = std::regex_replace(source, std::regex(R"(layout\(std140,\s*binding\s*=\s*[0-9]+\))"), "layout(std140)");
  source = std::regex_replace(source, std::regex(R"(layout\((location\s*=\s*[0-9]+,\s*)?binding\s*=\s*[0-9]+\)\s*)"), "");
  source = std::regex_replace(source, std::regex(stage == GL_VERTEX_SHADER ? R"(layout\(location\s*=\s*[0-9]+\) out)" : R"(layout\(location\s*=\s*[0-9]+\) in)"), stage == GL_VERTEX_SHADER ? "out" : "in");
  source = std::regex_replace(source, std::regex(R"(u_Textures\[32\])"), "u_Textures[16]");
  source = std::regex_replace(source, std::regex(R"(\b(out|in) flat\b)"), "flat $1");
  source = std::regex_replace(source, std::regex(R"(\b(Input|Output)\b)"), "v_Data");
  source = std::regex_replace(source, std::regex(R"(case (1[6-9]|2[0-9]|3[01]):[^\n]*)"), "");
  source = std::regex_replace(source, std::regex("gl_VertexIndex"), "gl_VertexID");
  return source;
}
GLuint Compile(GLenum stage, const std::string& source) {
  GLuint shader = glCreateShader(stage);
  const auto glsl = ToGLES(source, stage);
  const char* pointer = glsl.c_str();
  glShaderSource(shader, 1, &pointer, nullptr); glCompileShader(shader);
  GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096]{}; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); glDeleteShader(shader);
    throw std::runtime_error(std::string("WebGL shader compilation failed: ") + log);
  }
  return shader;
}
}
OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertex, const std::string& fragment) : m_Name(name) {
  GLuint vs = Compile(GL_VERTEX_SHADER, vertex), fs = 0;
  try {
    fs = Compile(GL_FRAGMENT_SHADER, fragment);
    m_RendererID = glCreateProgram(); glAttachShader(m_RendererID, vs); glAttachShader(m_RendererID, fs); glLinkProgram(m_RendererID);
    GLint ok = 0; glGetProgramiv(m_RendererID, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]{}; glGetProgramInfoLog(m_RendererID, sizeof(log), nullptr, log); throw std::runtime_error(log); }
    const GLuint camera = glGetUniformBlockIndex(m_RendererID, "Camera");
    if (camera != GL_INVALID_INDEX) glUniformBlockBinding(m_RendererID, camera, 0);
    const GLuint material = glGetUniformBlockIndex(m_RendererID, "Material");
    if (material != GL_INVALID_INDEX) glUniformBlockBinding(m_RendererID, material, 1);
      const GLuint lighting = glGetUniformBlockIndex(m_RendererID, "Lighting");
      if (lighting != GL_INVALID_INDEX) glUniformBlockBinding(m_RendererID, lighting, 2);
  } catch (...) { glDeleteShader(vs); if (fs) glDeleteShader(fs); if (m_RendererID) glDeleteProgram(m_RendererID); throw; }
  glDeleteShader(vs); glDeleteShader(fs);
}
OpenGLShader::OpenGLShader(const std::filesystem::path&) { throw std::runtime_error("Web Player requires precompiled project shaders"); }
OpenGLShader::OpenGLShader(const std::string&, std::span<const uint8_t>) { throw std::runtime_error("Desktop SPIR-V artifacts cannot run on WebGL2; a GLES cooker target is required"); }
OpenGLShader::~OpenGLShader() { if (m_RendererID) glDeleteProgram(m_RendererID); }
void OpenGLShader::Bind() const { glUseProgram(m_RendererID); }
void OpenGLShader::Unbind() const { glUseProgram(0); }
GLint OpenGLShader::GetUniformLocation(const std::string& name) {
  auto [it, inserted] = m_UniformLocations.try_emplace(name, -1);
  if (inserted) it->second = glGetUniformLocation(m_RendererID, name.c_str());
  return it->second;
}
void OpenGLShader::SetInt(const std::string& name, int value) { Bind(); glUniform1i(GetUniformLocation(name), value); }
void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count) { Bind(); glUniform1iv(GetUniformLocation(name), count, values); }
void OpenGLShader::SetFloat(const std::string& name, float value) { Bind(); glUniform1f(GetUniformLocation(name), value); }
void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value) { Bind(); glUniform2f(GetUniformLocation(name), value.x, value.y); }
void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value) { Bind(); glUniform3f(GetUniformLocation(name), value.x, value.y, value.z); }
void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value) { Bind(); glUniform4f(GetUniformLocation(name), value.x, value.y, value.z, value.w); }
void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value) { Bind(); glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value)); }
void OpenGLShader::UploadUniformInt(const std::string& name, int value) { SetInt(name, value); }
void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count) { SetIntArray(name, values, count); }
void OpenGLShader::UploadUniformFloat(const std::string& name, float value) { SetFloat(name, value); }
void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& value) { SetFloat2(name, value); }
void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& value) { SetFloat3(name, value); }
void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& value) { SetFloat4(name, value); }
void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& value) { Bind(); glUniformMatrix3fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value)); }
void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& value) { SetMat4(name, value); }
}
#endif // __EMSCRIPTEN__
