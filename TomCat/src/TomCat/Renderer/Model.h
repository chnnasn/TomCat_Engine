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
		Ref<TomCat::Mesh> Mesh;
		Ref<Texture2D> DiffuseTexture;
		glm::vec4 DiffuseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		bool UseTexture = false;
	};

	class Model
	{
	public:
		Model() = default;

		static Ref<Model> FromArtifact(const class MeshArtifact& artifact);

		static Ref<Model> Load(const std::string& filepath);

		const std::vector<ModelSubmesh>& GetSubmeshes() const { return m_Submeshes; }
		const std::string& GetPath() const { return m_Path; }
        bool GetBounds(glm::vec3& minimum, glm::vec3& maximum) const
        {
            bool hasBounds = false;
            for (const ModelSubmesh& submesh : m_Submeshes)
            {
                glm::vec3 submeshMinimum, submeshMaximum;
                if (!submesh.Mesh || !submesh.Mesh->GetBounds(submeshMinimum, submeshMaximum))
                    continue;
                if (!hasBounds)
                {
                    minimum = submeshMinimum;
                    maximum = submeshMaximum;
                    hasBounds = true;
                }
                else
                {
                    minimum = glm::min(minimum, submeshMinimum);
                    maximum = glm::max(maximum, submeshMaximum);
                }
            }
            return hasBounds;
        }

	private:
		std::vector<ModelSubmesh> m_Submeshes;
		std::string m_Path;
	};

}
