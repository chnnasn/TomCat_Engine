#include "tcpch.h"
#include "Shader.h"

#include "Renderer.h"
#include "platform/OpenGL/OpenGLShader.h"

namespace TomCat {


	Ref<Shader> Shader::Create(const std::filesystem::path& filepath)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return std::make_shared< OpenGLShader>(filepath);
		}
		TC_Core_Assert(false, "unknown rendererapi");
		return nullptr;
	}

	Ref<Shader> Shader::Create(const std::string& name,const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		switch (Renderer::GetAPI())
		{
			case RendererAPI::API::None: TC_Core_Assert(false, "RendererAPI : null"); return nullptr;

			case RendererAPI::API::OpenGL: return std::make_shared< OpenGLShader>(name,vertexSrc, fragmentSrc);
		}
		TC_Core_Assert(false, "unknown rendererapi");
		return nullptr;
	}

	void ShaderLibrary:: Add(const std::string& name, const Ref<Shader>& shader)
	{
		if (!shader)
		{
			TC_Core_Error("Cannot add a null shader to the shader library");
			return;
		}
		if (Exists(name))
		{
			TC_Core_Error("Shader '{0}' already exists", name);
			return;
		}
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader)
	{
		if (!shader)
		{
			TC_Core_Error("Cannot add a null shader to the shader library");
			return;
		}
		auto& name = shader->GetName();
		Add(name,shader);
	}

	Ref<Shader> ShaderLibrary::Load(const std::filesystem::path& filepath)
	{
		auto shader = Shader::Create(filepath);

		Add(shader);

		return shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::filesystem::path& filepath)
	{
		auto shader = Shader::Create(filepath);

		Add(name,shader);

		return shader;
	}

	Ref<Shader> ShaderLibrary::Get(const std::string& name)
	{
		auto iterator = m_Shaders.find(name);
		if (iterator == m_Shaders.end())
		{
			TC_Core_Error("Shader '{0}' was not found", name);
			return nullptr;
		}
		return iterator->second;
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}

}
