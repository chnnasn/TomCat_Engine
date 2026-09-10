#include "tcpch.h"
#include "Platform/OpenGL/OpenGLShader.h"

#include <fstream>
#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>

#include <shaderc/shaderc.hpp>
#include <spirv_cross/spirv_cross.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <filesystem>
#include <iomanip>
#include <limits>
#include <stdexcept>

#include "TomCat/Core/Timer.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

namespace TomCat {

	namespace Utils {
		static constexpr uint64_t FNVOffsetBasis = 14695981039346656037ull;
		static constexpr uint64_t FNVPrime = 1099511628211ull;
		static constexpr uint32_t ShaderCacheMagic = 0x43534354u; // "TCSC" in little-endian files
		static constexpr uint32_t ShaderCacheFormatVersion = 1;
		static constexpr uint64_t ShaderCacheHeaderSize = sizeof(uint32_t) * 4 + sizeof(uint64_t);
		static constexpr uint64_t MaxShaderCacheSize = 64ull * 1024ull * 1024ull;

		static uint64_t HashBytes(const void* bytes, size_t size, uint64_t hash = FNVOffsetBasis)
		{
			const auto* data = static_cast<const uint8_t*>(bytes);
			for (size_t i = 0; i < size; ++i)
			{
				hash ^= data[i];
				hash *= FNVPrime;
			}
			return hash;
		}

		static GLenum ShaderTypeFromString(const std::string& type)
		{
			if (type == "vertex")
				return GL_VERTEX_SHADER;
			if (type == "fragment" || type == "pixel")
				return GL_FRAGMENT_SHADER;

			return 0;
		}

		static shaderc_shader_kind GLShaderStageToShaderC(GLenum stage)
		{
			switch (stage)
			{
			case GL_VERTEX_SHADER:   return shaderc_glsl_vertex_shader;
			case GL_FRAGMENT_SHADER: return shaderc_glsl_fragment_shader;
			}
			TC_Core_Assert(false);
			return (shaderc_shader_kind)0;
		}

		static const char* GLShaderStageToString(GLenum stage)
		{
			switch (stage)
			{
			case GL_VERTEX_SHADER:   return "GL_VERTEX_SHADER";
			case GL_FRAGMENT_SHADER: return "GL_FRAGMENT_SHADER";
			}
			TC_Core_Assert(false);
			return nullptr;
		}

		static std::filesystem::path GetCacheDirectory()
		{
			// Packages is virtualized by the boxed distribution.  Keeping a
			// writable shader cache below it makes the virtual folder materialize
			// next to the executable (and fails on a read-only virtual tree).
			// Store generated binaries in the system temporary directory instead;
			// source shaders and all other packaged assets remain under Packages/.
			std::error_code error;
			const auto tempDirectory = std::filesystem::temp_directory_path(error);
			if (!error && !tempDirectory.empty())
				return tempDirectory / "TomCat" / "cache" / "shader" / "opengl";

			return {};
		}

		static void CreateCacheDirectoryIfNeeded()
		{
			const auto cacheDirectory = GetCacheDirectory();
			if (cacheDirectory.empty())
				return;

			std::error_code error;
			std::filesystem::create_directories(cacheDirectory, error);
			if (error)
				TC_Core_Warn("Could not create shader cache directory '{0}': {1}", PathToUTF8(cacheDirectory), error.message());
		}

		static const char* GLShaderStageCachedOpenGLFileExtension(uint32_t stage)
		{
			switch (stage)
			{
			case GL_VERTEX_SHADER:    return ".cached_opengl.vert";
			case GL_FRAGMENT_SHADER:  return ".cached_opengl.frag";
			}
			TC_Core_Assert(false);
			return "";
		}

		static const char* GLShaderStageCachedVulkanFileExtension(uint32_t stage)
		{
			switch (stage)
			{
			case GL_VERTEX_SHADER:    return ".cached_vulkan.vert";
			case GL_FRAGMENT_SHADER:  return ".cached_vulkan.frag";
			}
			TC_Core_Assert(false);
			return "";
		}

		static std::filesystem::path GetCachePath(const std::string& identity, GLenum stage,
			const void* content, size_t contentSize, const char* extension)
		{
			const auto cacheDirectory = GetCacheDirectory();
			if (cacheDirectory.empty())
				return {};

			uint64_t hash = HashBytes(&ShaderCacheFormatVersion, sizeof(ShaderCacheFormatVersion));
			hash = HashBytes(identity.data(), identity.size(), hash);
			hash = HashBytes(&stage, sizeof(stage), hash);
			hash = HashBytes(content, contentSize, hash);

			std::ostringstream hashStream;
			hashStream << std::hex << std::setw(16) << std::setfill('0') << hash;

			return cacheDirectory / ("shader.v" + std::to_string(ShaderCacheFormatVersion) + "." +
				hashStream.str() + extension);
		}

		static void RemoveCachedSPIRV(const std::filesystem::path& path)
		{
			if (path.empty())
				return;

			std::error_code error;
			std::filesystem::remove(path, error);
			if (error)
				TC_Core_Warn("Could not remove shader cache '{0}': {1}", PathToUTF8(path), error.message());
		}

		static bool ReadCachedSPIRV(const std::filesystem::path& path, GLenum stage, std::vector<uint32_t>& data)
		{
			std::ifstream input(path, std::ios::in | std::ios::binary | std::ios::ate);
			if (!input)
				return false;

			const std::streamoff size = input.tellg();
			input.seekg(0, std::ios::beg);

			uint32_t magic = 0;
			uint32_t version = 0;
			uint32_t cachedStage = 0;
			uint32_t wordCount = 0;
			uint64_t payloadHash = 0;
			if (size < static_cast<std::streamoff>(ShaderCacheHeaderSize) ||
				size > static_cast<std::streamoff>(MaxShaderCacheSize) ||
				!input.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
				!input.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
				!input.read(reinterpret_cast<char*>(&cachedStage), sizeof(cachedStage)) ||
				!input.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount)) ||
				!input.read(reinterpret_cast<char*>(&payloadHash), sizeof(payloadHash)))
			{
				input.close();
				data.clear();
				RemoveCachedSPIRV(path);
				return false;
			}

			const uint64_t expectedSize = ShaderCacheHeaderSize + static_cast<uint64_t>(wordCount) * sizeof(uint32_t);
			if (magic != ShaderCacheMagic || version != ShaderCacheFormatVersion ||
				cachedStage != static_cast<uint32_t>(stage) || wordCount < 5 ||
				expectedSize != static_cast<uint64_t>(size))
			{
				input.close();
				data.clear();
				RemoveCachedSPIRV(path);
				return false;
			}

			data.resize(wordCount);
			const auto payloadSize = static_cast<std::streamsize>(static_cast<uint64_t>(wordCount) * sizeof(uint32_t));
			if (!input.read(reinterpret_cast<char*>(data.data()), payloadSize) ||
				data.front() != 0x07230203u ||
				HashBytes(data.data(), static_cast<size_t>(payloadSize)) != payloadHash)
			{
				input.close();
				data.clear();
				RemoveCachedSPIRV(path);
				return false;
			}

			return true;
		}

		static bool ReplaceCacheFile(const std::filesystem::path& temporaryPath, const std::filesystem::path& path)
		{
			std::string error;
			if (FileSystem::InstallTemporaryFileAtomically(temporaryPath, path, error))
				return true;
			TC_Core_Warn("Could not replace shader cache '{0}': {1}", PathToUTF8(path), error);
			return false;
		}

		static void WriteCachedSPIRV(const std::filesystem::path& path, GLenum stage, const std::vector<uint32_t>& data)
		{
			if (path.empty() || data.size() < 5 || data.size() > std::numeric_limits<uint32_t>::max() ||
				data.size() * sizeof(uint32_t) > MaxShaderCacheSize - ShaderCacheHeaderSize)
				return;

			const std::filesystem::path temporaryPath = FileSystem::MakeTemporarySiblingPath(path);
			if (temporaryPath.empty())
			{
				TC_Core_Warn("Could not allocate a temporary shader cache path for '{0}'", PathToUTF8(path));
				return;
			}

			const uint32_t magic = ShaderCacheMagic;
			const uint32_t version = ShaderCacheFormatVersion;
			const uint32_t cachedStage = static_cast<uint32_t>(stage);
			const uint32_t wordCount = static_cast<uint32_t>(data.size());
			const auto payloadSize = static_cast<std::streamsize>(data.size() * sizeof(uint32_t));
			const uint64_t payloadHash = HashBytes(data.data(), static_cast<size_t>(payloadSize));

			std::ofstream output(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!output ||
				!output.write(reinterpret_cast<const char*>(&magic), sizeof(magic)) ||
				!output.write(reinterpret_cast<const char*>(&version), sizeof(version)) ||
				!output.write(reinterpret_cast<const char*>(&cachedStage), sizeof(cachedStage)) ||
				!output.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount)) ||
				!output.write(reinterpret_cast<const char*>(&payloadHash), sizeof(payloadHash)) ||
				!output.write(reinterpret_cast<const char*>(data.data()), payloadSize))
			{
				output.close();
				std::error_code ignored;
				std::filesystem::remove(temporaryPath, ignored);
				TC_Core_Warn("Could not write shader cache '{0}'", PathToUTF8(path));
				return;
			}

			output.close();
			if (!output || !ReplaceCacheFile(temporaryPath, path))
			{
				std::error_code ignored;
				std::filesystem::remove(temporaryPath, ignored);
			}
		}


	}

	OpenGLShader::OpenGLShader(const std::filesystem::path& filepath)
		: m_Identity(PathToUTF8(filepath))
	{
		TC_PROFILE_FUNCTION();

		Utils::CreateCacheDirectoryIfNeeded();

		std::string source = ReadFile(filepath);
		auto shaderSources = PreProcess(source);

		{
			Timer timer;
			BuildProgram(shaderSources);
			TC_Core_Warn("Shader creation took {0} ms", timer.ElapsedMillis());
			
		}

		m_Name = PathToUTF8(filepath.stem());
	}

	OpenGLShader::OpenGLShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
		: m_Identity(name), m_Name(name)
	{
		TC_PROFILE_FUNCTION();
		Utils::CreateCacheDirectoryIfNeeded();

		std::unordered_map<GLenum, std::string> sources;
		sources[GL_VERTEX_SHADER] = vertexSrc;
		sources[GL_FRAGMENT_SHADER] = fragmentSrc;

		BuildProgram(sources);
	}

	OpenGLShader::~OpenGLShader()
	{
		TC_PROFILE_FUNCTION();

		if (m_RendererID)
			glDeleteProgram(m_RendererID);
	}

	std::string OpenGLShader::ReadFile(const std::filesystem::path& filepath)
	{
		TC_PROFILE_FUNCTION();

		const std::string displayPath = PathToUTF8(filepath);
		std::ifstream in(filepath, std::ios::in | std::ios::binary | std::ios::ate);
		if (!in)
		{
			TC_Core_Error("Could not open shader file '{0}'", displayPath);
			throw std::runtime_error("Could not open shader file: " + displayPath);
		}

		const std::streamoff fileSize = in.tellg();
		if (fileSize <= 0)
		{
			TC_Core_Error("Shader file is empty or unreadable: '{0}'", displayPath);
			throw std::runtime_error("Shader file is empty or unreadable: " + displayPath);
		}

		std::string result(static_cast<size_t>(fileSize), '\0');
		in.seekg(0, std::ios::beg);
		if (!in.read(result.data(), fileSize))
		{
			TC_Core_Error("Could not read shader file '{0}'", displayPath);
			throw std::runtime_error("Could not read shader file: " + displayPath);
		}
		return result;
	}

	std::unordered_map<GLenum, std::string> OpenGLShader::PreProcess(const std::string& source)
	{
		TC_PROFILE_FUNCTION();

		std::unordered_map<GLenum, std::string> shaderSources;

		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = source.find(typeToken, 0); //Start of shader type declaration line
		if (pos == std::string::npos)
			throw std::runtime_error("Shader source contains no #type declarations: " + m_Identity);

		while (pos != std::string::npos)
		{
			size_t eol = source.find_first_of("\r\n", pos); //End of shader type declaration line
			if (eol == std::string::npos)
				throw std::runtime_error("Malformed #type declaration in shader: " + m_Identity);

			size_t begin = source.find_first_not_of(" \t", pos + typeTokenLength);
			if (begin == std::string::npos || begin >= eol)
				throw std::runtime_error("Missing shader stage after #type in: " + m_Identity);
			size_t typeEnd = source.find_last_not_of(" \t\r", eol - 1);
			std::string type = source.substr(begin, typeEnd - begin + 1);
			const GLenum stage = Utils::ShaderTypeFromString(type);
			if (!stage)
				throw std::runtime_error("Unknown shader stage '" + type + "' in: " + m_Identity);

			size_t nextLinePos = source.find_first_not_of("\r\n", eol); //Start of shader code after shader type declaration line
			if (nextLinePos == std::string::npos)
				throw std::runtime_error("Shader stage has no source in: " + m_Identity);
			pos = source.find(typeToken, nextLinePos); //Start of next shader type declaration line

			std::string stageSource = (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
			if (stageSource.empty())
				throw std::runtime_error("Shader stage has empty source in: " + m_Identity);
			if (!shaderSources.emplace(stage, std::move(stageSource)).second)
				throw std::runtime_error("Duplicate shader stage in: " + m_Identity);
		}

		return shaderSources;
	}

	void OpenGLShader::BuildProgram(const std::unordered_map<GLenum, std::string>& shaderSources)
	{
		m_UsedCachedBinaries = false;
		try
		{
			CompileOrGetVulkanBinaries(shaderSources);
			CompileOrGetOpenGLBinaries();
			CreateProgram();
		}
		catch (const std::exception& error)
		{
			if (!m_UsedCachedBinaries)
				throw;

			TC_Core_Warn("Shader cache failed for '{0}' ({1}); discarding it and recompiling once",
				m_Identity, error.what());
			m_UsedCachedBinaries = false;
			CompileOrGetVulkanBinaries(shaderSources, true);
			CompileOrGetOpenGLBinaries(true);
			CreateProgram();
		}
	}

	void OpenGLShader::CompileOrGetVulkanBinaries(const std::unordered_map<GLenum, std::string>& shaderSources, bool forceCompile)
	{
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
		const bool optimize = true;
		if (optimize)
			options.SetOptimizationLevel(shaderc_optimization_level_performance);

		auto& shaderData = m_VulkanSPIRV;
		shaderData.clear();
		for (auto&& [stage, source] : shaderSources)
		{
			const auto cachedPath = Utils::GetCachePath(m_Identity, stage, source.data(), source.size(),
				Utils::GLShaderStageCachedVulkanFileExtension(stage));
			auto& data = shaderData[stage];
			if (forceCompile)
				Utils::RemoveCachedSPIRV(cachedPath);

			if (!forceCompile && Utils::ReadCachedSPIRV(cachedPath, stage, data))
			{
				m_UsedCachedBinaries = true;
			}
			else
			{
				shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::GLShaderStageToShaderC(stage), m_Identity.c_str(), options);
				if (module.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					TC_Core_Error("Vulkan shader compilation failed ({0}):\n{1}", m_Identity, module.GetErrorMessage());
					throw std::runtime_error("Vulkan shader compilation failed: " + m_Identity);
				}

				data.assign(module.cbegin(), module.cend());
				if (data.empty())
					throw std::runtime_error("Shader compiler returned no Vulkan SPIR-V: " + m_Identity);
				Utils::WriteCachedSPIRV(cachedPath, stage, data);
			}
		}

		for (auto&& [stage, data] : shaderData)
			Reflect(stage, data);
	}

	void OpenGLShader::CompileOrGetOpenGLBinaries(bool forceCompile)
	{
		auto& shaderData = m_OpenGLSPIRV;

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;
		options.SetTargetEnvironment(shaderc_target_env_opengl, shaderc_env_version_opengl_4_5);
		const bool optimize = false;
		if (optimize)
			options.SetOptimizationLevel(shaderc_optimization_level_performance);

		shaderData.clear();
		m_OpenGLSourceCode.clear();
		for (auto&& [stage, spirv] : m_VulkanSPIRV)
		{
			const auto cachedPath = Utils::GetCachePath(m_Identity, stage, spirv.data(), spirv.size() * sizeof(uint32_t),
				Utils::GLShaderStageCachedOpenGLFileExtension(stage));
			auto& data = shaderData[stage];
			if (forceCompile)
				Utils::RemoveCachedSPIRV(cachedPath);

			if (!forceCompile && Utils::ReadCachedSPIRV(cachedPath, stage, data))
			{
				m_UsedCachedBinaries = true;
			}
			else
			{
				spirv_cross::CompilerGLSL glslCompiler(spirv);
				m_OpenGLSourceCode[stage] = glslCompiler.compile();
				auto& source = m_OpenGLSourceCode[stage];

				shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(source, Utils::GLShaderStageToShaderC(stage), m_Identity.c_str(), options);
				if (module.GetCompilationStatus() != shaderc_compilation_status_success)
				{
					TC_Core_Error("OpenGL shader compilation failed ({0}):\n{1}", m_Identity, module.GetErrorMessage());
					throw std::runtime_error("OpenGL shader compilation failed: " + m_Identity);
				}

				data.assign(module.cbegin(), module.cend());
				if (data.empty())
					throw std::runtime_error("Shader compiler returned no OpenGL SPIR-V: " + m_Identity);
				Utils::WriteCachedSPIRV(cachedPath, stage, data);
			}
		}
	}

	void OpenGLShader::CreateProgram()
	{
		if (m_OpenGLSPIRV.empty())
			throw std::runtime_error("Cannot create an OpenGL program without shader stages: " + m_Identity);
		if (!glSpecializeShader)
			throw std::runtime_error("OpenGL SPIR-V specialization is unavailable; OpenGL 4.6 is required");

		GLuint program = glCreateProgram();
		if (!program)
			throw std::runtime_error("OpenGL failed to create a shader program: " + m_Identity);

		std::vector<GLuint> shaderIDs;
		for (auto&& [stage, spirv] : m_OpenGLSPIRV)
		{
			GLuint shaderID = shaderIDs.emplace_back(glCreateShader(stage));
			glShaderBinary(1, &shaderID, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv.data(), static_cast<GLsizei>(spirv.size() * sizeof(uint32_t)));
			glSpecializeShader(shaderID, "main", 0, nullptr, nullptr);

			GLint isCompiled = GL_FALSE;
			glGetShaderiv(shaderID, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE)
			{
				GLint maxLength = 0;
				glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &maxLength);
				std::vector<GLchar> infoLog(static_cast<size_t>(std::max(maxLength, 1)), '\0');
				glGetShaderInfoLog(shaderID, maxLength, &maxLength, infoLog.data());
				TC_Core_Error("Shader specialization failed ({0}, {1}):\n{2}", m_Identity,
					Utils::GLShaderStageToString(stage), infoLog.data());
				for (GLuint id : shaderIDs)
					glDeleteShader(id);
				glDeleteProgram(program);
				throw std::runtime_error("Shader specialization failed: " + m_Identity);
			}
			glAttachShader(program, shaderID);
		}

		glLinkProgram(program);

		GLint isLinked;
		glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
		if (isLinked == GL_FALSE)
		{
			GLint maxLength;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			std::vector<GLchar> infoLog(static_cast<size_t>(std::max(maxLength, 1)), '\0');
			glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());
			TC_Core_Error("Shader linking failed ({0}):\n{1}", m_Identity, infoLog.data());

			glDeleteProgram(program);

			for (auto id : shaderIDs)
				glDeleteShader(id);

			throw std::runtime_error("Shader linking failed: " + m_Identity);
		}

		for (auto id : shaderIDs)
		{
			glDetachShader(program, id);
			glDeleteShader(id);
		}

		m_RendererID = program;
	}

	void OpenGLShader::Reflect(GLenum stage, const std::vector<uint32_t>& shaderData)
	{
		spirv_cross::Compiler compiler(shaderData);
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		TC_Core_Trace("OpenGLShader::Reflect - {0} {1}", Utils::GLShaderStageToString(stage), m_Identity);
		TC_Core_Trace("    {0} uniform buffers", resources.uniform_buffers.size());
		TC_Core_Trace("    {0} resources", resources.sampled_images.size());

		TC_Core_Trace("Uniform buffers:");
		for (const auto& resource : resources.uniform_buffers)
		{
			const auto& bufferType = compiler.get_type(resource.base_type_id);
			size_t bufferSize = compiler.get_declared_struct_size(bufferType);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			size_t memberCount = bufferType.member_types.size();

			TC_Core_Trace("  {0}", resource.name);
			TC_Core_Trace("    Size = {0}", bufferSize);
			TC_Core_Trace("    Binding = {0}", binding);
			TC_Core_Trace("    Members = {0}", memberCount);
		}
	}

	void OpenGLShader::Bind() const
	{
		TC_PROFILE_FUNCTION();

		glUseProgram(m_RendererID);
	}

	void OpenGLShader::Unbind() const
	{
		TC_PROFILE_FUNCTION();

		glUseProgram(0);
	}

	void OpenGLShader::SetInt(const std::string& name, int value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformInt(name, value);
	}

	void OpenGLShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		UploadUniformIntArray(name, values, count);
	}

	void OpenGLShader::SetFloat(const std::string& name, float value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformFloat(name, value);
	}

	void OpenGLShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformFloat2(name, value);
	}

	void OpenGLShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformFloat3(name, value);
	}

	void OpenGLShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformFloat4(name, value);
	}

	void OpenGLShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		TC_PROFILE_FUNCTION();

		UploadUniformMat4(name, value);
	}

	void OpenGLShader::UploadUniformInt(const std::string& name, int value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1i(location, value);
	}

	void OpenGLShader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1iv(location, count, values);
	}

	void OpenGLShader::UploadUniformFloat(const std::string& name, float value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform1f(location, value);
	}

	void OpenGLShader::UploadUniformFloat2(const std::string& name, const glm::vec2& value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform2f(location, value.x, value.y);
	}

	void OpenGLShader::UploadUniformFloat3(const std::string& name, const glm::vec3& value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform3f(location, value.x, value.y, value.z);
	}

	void OpenGLShader::UploadUniformFloat4(const std::string& name, const glm::vec4& value)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniform4f(location, value.x, value.y, value.z, value.w);
	}

	void OpenGLShader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}

	void OpenGLShader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix)
	{
		GLint location = glGetUniformLocation(m_RendererID, name.c_str());
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}

}
