#include "tcpch.h"
#include "TomCat/Renderer/Model.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include <filesystem>

#include <glm/gtc/matrix_inverse.hpp>

namespace TomCat {

	static glm::mat4 AssimpMatrixToGlm(const aiMatrix4x4& m)
	{
		// aiMatrix4x4 是行主序，glm::mat4 构造函数按列主序读取，因此转置
		return glm::mat4(
			m.a1, m.b1, m.c1, m.d1,
			m.a2, m.b2, m.c2, m.d2,
			m.a3, m.b3, m.c3, m.d3,
			m.a4, m.b4, m.c4, m.d4);
	}

	static void ProcessNode(aiNode* node, const aiScene* scene, const std::string& baseDir,
		const glm::mat4& parentTransform, std::vector<ModelSubmesh>& submeshes)
	{
		glm::mat4 transform = parentTransform * AssimpMatrixToGlm(node->mTransformation);
		glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));

		for (uint32_t i = 0; i < node->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			vertices.reserve(mesh->mNumVertices);

			for (uint32_t v = 0; v < mesh->mNumVertices; v++)
			{
				Vertex vertex;
				glm::vec3 pos = transform * glm::vec4(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
				vertex.Position = pos;

				if (mesh->HasNormals())
				{
					glm::vec3 n = normalMatrix * glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
					if (glm::dot(n, n) > 0.000001f)
						n = glm::normalize(n);
					vertex.Normal = n;
				}

				if (mesh->HasTextureCoords(0))
					vertex.TexCoord = { mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y };

				vertices.push_back(vertex);
			}

			for (uint32_t f = 0; f < mesh->mNumFaces; f++)
			{
				aiFace& face = mesh->mFaces[f];
				for (uint32_t j = 0; j < face.mNumIndices; j++)
					indices.push_back(face.mIndices[j]);
			}

			ModelSubmesh sub;
			sub.Mesh = Mesh::Create(vertices, indices);

			if (mesh->mMaterialIndex >= 0 && scene->HasMaterials())
			{
				aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

				aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
				if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
					sub.DiffuseColor = { color.r, color.g, color.b, color.a };

				aiString texPath;
				if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
				{
					std::filesystem::path p = std::filesystem::path(baseDir) / texPath.C_Str();
					Ref<Texture2D> tex = Texture2D::Create(p.string());
					if (tex && tex->IsLoaded())
					{
						sub.DiffuseTexture = tex;
						sub.UseTexture = true;
					}
				}
			}

			submeshes.push_back(sub);
		}

		for (uint32_t i = 0; i < node->mNumChildren; i++)
			ProcessNode(node->mChildren[i], scene, baseDir, transform, submeshes);
	}

	Ref<Model> Model::Load(const std::string& filepath)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
			aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);

		if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
		{
			TC_Core_Error("Model::Load failed: {0} ({1})", filepath, importer.GetErrorString());
			return nullptr;
		}

		Ref<Model> model = CreateRef<Model>();
		model->m_Path = filepath;
		std::string baseDir = std::filesystem::path(filepath).parent_path().string();
		ProcessNode(scene->mRootNode, scene, baseDir, glm::mat4(1.0f), model->m_Submeshes);
		return model;
	}

}
