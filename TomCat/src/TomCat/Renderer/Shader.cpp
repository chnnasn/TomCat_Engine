#include "tcpch.h"
#include "Shader.h"
#include "TomCat/RHI/RenderDevice.h"
namespace TomCat {
Ref<Shader> Shader::Create(const std::filesystem::path& path) { return RHI::RenderDevice::Get().CreateShader(path); }
Ref<Shader> Shader::Create(const std::string& name, const std::string& vertex, const std::string& fragment) { return RHI::RenderDevice::Get().CreateShader(name, vertex, fragment); }
Ref<Shader> Shader::CreateFromArtifact(const std::string& name, std::span<const uint8_t> artifact, std::string* error) {
    if (error) error->clear();
    try { return RHI::RenderDevice::Get().CreateShader(name, artifact); }
    catch (const std::exception& e) { if (error) *error = e.what(); return nullptr; }
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
