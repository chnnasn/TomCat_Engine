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

    // Persisted values: 2 retains the original XY "Plane" geometry in old scenes.
    enum class MeshPrimitive : int32_t { None = 0, Cube = 1, Quad = 2, Sphere = 3, Capsule = 4, Cylinder = 5, Plane = 6 };

	class Mesh
	{
	public:
		static Ref<Mesh> Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		static Ref<Mesh> LoadOBJ(const std::string& filepath);
        static bool GeneratePrimitive(MeshPrimitive type, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
        static Ref<Mesh> CreatePrimitive(MeshPrimitive type);
        static Ref<Mesh> CreateSphere();
        static Ref<Mesh> CreateCapsule();
        static Ref<Mesh> CreateCylinder();
        static Ref<Mesh> CreateQuad(float width = 1.0f, float height = 1.0f);
        static Ref<Mesh> CreateGroundPlane(float width = 10.0f, float depth = 10.0f);
		static Ref<Mesh> CreateCube(float size = 1.0f);
        // Legacy XY plane API. Prefer CreateQuad or CreateGroundPlane in new code.
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
