#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Buffer.h"
#include "TomCat/Renderer/VertexArray.h"

namespace TomCat {

	struct Vertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Normal{ 0.0f, 0.0f, 1.0f };
		glm::vec2 TexCoord{ 0.0f };

		Vertex() = default;
		Vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec2& texCoord)
			: Position(position), Normal(normal), TexCoord(texCoord) {}
	};

	class Mesh
	{
	public:
		static Ref<Mesh> Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		static Ref<Mesh> LoadOBJ(const std::string& filepath);
		static Ref<Mesh> CreateCube(float size = 1.0f);
		static Ref<Mesh> CreatePlane(float width = 1.0f, float height = 1.0f);

		const std::string& GetName() const { return m_Name; }
		void SetName(const std::string& name) { m_Name = name; }

		const Ref<VertexArray>& GetVertexArray() const { return m_VertexArray; }
		uint32_t GetIndexCount() const { return m_IndexCount; }

		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

	private:
		Ref<VertexArray> m_VertexArray;
		uint32_t m_IndexCount = 0;
		std::string m_Name;
	};

}
