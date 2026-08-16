#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Mesh.h"
#include "TomCat/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace TomCat {

	struct ModelSubmesh
	{
		Ref<Mesh> Mesh;
		Ref<Texture2D> DiffuseTexture;
		glm::vec4 DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool UseTexture = false;
	};

	class Model
	{
	public:
		Model() = default;

		static Ref<Model> Load(const std::string& filepath);

		const std::vector<ModelSubmesh>& GetSubmeshes() const { return m_Submeshes; }
		const std::string& GetPath() const { return m_Path; }

	private:
		std::vector<ModelSubmesh> m_Submeshes;
		std::string m_Path;
	};

}
