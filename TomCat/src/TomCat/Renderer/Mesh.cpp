#include "tcpch.h"
#include "TomCat/Renderer/Mesh.h"
#include "TomCat/Asset/MeshArtifact.h"
#include "TomCat/Utils/PathUtils.h"

#include <array>
#include <glm/gtc/constants.hpp>
#include <fstream>
#include <sstream>

namespace TomCat {

	Ref<Mesh> Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		return CreateRef<Mesh>(vertices, indices);
	}

	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		m_IndexCount = (uint32_t)indices.size();
        if (!indices.empty())
        {
            m_BoundsMinimum = m_BoundsMaximum = vertices.at(indices.front()).Position;
            for (uint32_t index : indices)
            {
                const glm::vec3& position = vertices.at(index).Position;
                m_BoundsMinimum = glm::min(m_BoundsMinimum, position);
                m_BoundsMaximum = glm::max(m_BoundsMaximum, position);
            }
        }

		m_VertexArray = VertexArray::Create();

		Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create((float*)vertices.data(), (uint32_t)(vertices.size() * sizeof(Vertex)));
		vertexBuffer->SetLayout({
			{ ShaderDataType::Float3, "a_Position" },
			{ ShaderDataType::Float3, "a_Normal"   },
			{ ShaderDataType::Float2, "a_TexCoord" }
			});
		m_VertexArray->AddVertexBuffer(vertexBuffer);

		Ref<IndexBuffer> indexBuffer = IndexBuffer::Create((uint32_t*)indices.data(), m_IndexCount);
		m_VertexArray->SetIndexBuffer(indexBuffer);
	}

	Ref<Mesh> Mesh::LoadOBJ(const std::string& filepath)
	{
        const std::filesystem::path sourcePath = UTF8ToPath(filepath);
		std::ifstream file(sourcePath, std::ios::binary);
		if (!file)
		{
			TC_Core_Error("Mesh::LoadOBJ: failed to open '{0}'", filepath);
			return nullptr;
		}
		std::vector<uint8_t> source(std::istreambuf_iterator<char>(file), {});
		std::vector<uint8_t> bytes;
		std::string error;
		MeshArtifactView artifact;
		if (!BuildMeshArtifact(source, sourcePath, bytes, error)
			|| !ParseMeshArtifact(bytes, artifact, error))
		{
			TC_Core_Error("Mesh::LoadOBJ: {0} ({1})", filepath, error);
			return nullptr;
		}
		std::vector<Vertex> vertices(artifact.VertexCount);
		std::vector<uint32_t> indices(artifact.IndexCount);
		for (uint32_t index = 0; index < artifact.VertexCount; ++index)
		{
			MeshArtifactVertex vertex;
			if (!artifact.DecodeVertex(index, vertex)) return nullptr;
			vertices[index] = {{vertex.Position[0], vertex.Position[1], vertex.Position[2]},
				{vertex.Normal[0], vertex.Normal[1], vertex.Normal[2]},
				{vertex.TexCoord[0], vertex.TexCoord[1]}};
		}
		for (uint32_t index = 0; index < artifact.IndexCount; ++index)
			if (!artifact.DecodeIndex(index, indices[index])) return nullptr;
		auto mesh = Create(vertices, indices);
		std::istringstream text(std::string(source.begin(), source.end()));
		std::string line;
		while (std::getline(text, line))
		{
			std::istringstream fields(line);
			std::string kind, name;
			if (fields >> kind >> name && (kind == "o" || kind == "g"))
			{
				mesh->SetName(name);
				break;
			}
		}
		return mesh;
	}

    static void BuildCube(float size, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
    {
        const float h = size * 0.5f;

        vertices = {
            // +X
            Vertex(glm::vec3(h, -h, -h), glm::vec3(1, 0, 0), glm::vec2(0, 0)),
            Vertex(glm::vec3(h, h, -h), glm::vec3(1, 0, 0), glm::vec2(1, 0)),
            Vertex(glm::vec3(h, h, h), glm::vec3(1, 0, 0), glm::vec2(1, 1)),
            Vertex(glm::vec3(h, -h, h), glm::vec3(1, 0, 0), glm::vec2(0, 1)),
            // -X
            Vertex(glm::vec3(-h, -h, h), glm::vec3(-1, 0, 0), glm::vec2(0, 0)),
            Vertex(glm::vec3(-h, h, h), glm::vec3(-1, 0, 0), glm::vec2(1, 0)),
            Vertex(glm::vec3(-h, h, -h), glm::vec3(-1, 0, 0), glm::vec2(1, 1)),
            Vertex(glm::vec3(-h, -h, -h), glm::vec3(-1, 0, 0), glm::vec2(0, 1)),
            // +Y
            Vertex(glm::vec3(-h, h, -h), glm::vec3(0, 1, 0), glm::vec2(0, 0)),
            Vertex(glm::vec3(-h, h, h), glm::vec3(0, 1, 0), glm::vec2(1, 0)),
            Vertex(glm::vec3(h, h, h), glm::vec3(0, 1, 0), glm::vec2(1, 1)),
            Vertex(glm::vec3(h, h, -h), glm::vec3(0, 1, 0), glm::vec2(0, 1)),
            // -Y
            Vertex(glm::vec3(-h, -h, h), glm::vec3(0, -1, 0), glm::vec2(0, 0)),
            Vertex(glm::vec3(-h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(1, 0)),
            Vertex(glm::vec3(h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(1, 1)),
            Vertex(glm::vec3(h, -h, h), glm::vec3(0, -1, 0), glm::vec2(0, 1)),
            // +Z
            Vertex(glm::vec3(-h, -h, h), glm::vec3(0, 0, 1), glm::vec2(0, 0)),
            Vertex(glm::vec3(h, -h, h), glm::vec3(0, 0, 1), glm::vec2(1, 0)),
            Vertex(glm::vec3(h, h, h), glm::vec3(0, 0, 1), glm::vec2(1, 1)),
            Vertex(glm::vec3(-h, h, h), glm::vec3(0, 0, 1), glm::vec2(0, 1)),
            // -Z
            Vertex(glm::vec3(h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 0)),
            Vertex(glm::vec3(-h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 0)),
            Vertex(glm::vec3(-h, h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 1)),
            Vertex(glm::vec3(h, h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 1)),
        };

        indices.clear();
        for (uint32_t face = 0; face < 6; face++)
        {
            uint32_t base = face * 4;
            uint32_t quad[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
            for (uint32_t i = 0; i < 6; i++)
                indices.push_back(quad[i]);
        }
    }

    static void BuildQuad(float width, float height, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
    {
        const float hw = width * 0.5f;
        const float hh = height * 0.5f;

        vertices = {
            Vertex(glm::vec3(-hw, -hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(0, 0)),
            Vertex(glm::vec3(hw, -hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(1, 0)),
            Vertex(glm::vec3(hw, hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(1, 1)),
            Vertex(glm::vec3(-hw, hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(0, 1)),
        };

        indices = {0, 1, 2, 0, 2, 3};
    }

    namespace
    {
        constexpr uint32_t Segments = 32;
        void BuildRound(bool capsule, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
        {
            struct Ring
            {
                float Radius, Y, NormalY, NormalRadius;
            };
            std::vector<Ring> rings;
            constexpr uint32_t hemisphereSteps = 16;
            for (uint32_t i = 0; i <= hemisphereSteps * 2; ++i)
            {
                float latitude = -glm::half_pi<float>() + glm::pi<float>() * float(i) / float(hemisphereSteps * 2);
                float nr = (i == 0 || i == hemisphereSteps * 2) ? 0.0f : std::cos(latitude);
                float ny = std::sin(latitude);
                float offset = capsule ? (i <= hemisphereSteps ? -.5f : .5f) : 0.0f;
                rings.push_back({nr * .5f, ny * .5f + offset, ny, nr});
                if (capsule && i == hemisphereSteps)
                    rings.push_back({.5f, .5f, 0, 1});
            }
            for (const auto &ring : rings)
                for (uint32_t j = 0; j <= Segments; ++j)
                {
                    float angle = glm::two_pi<float>() * float(j % Segments) / float(Segments);
                    float x = std::cos(angle), z = std::sin(angle);
                    vertices.emplace_back(
                        glm::vec3(ring.Radius * x, ring.Y, ring.Radius * z),
                        glm::vec3(ring.NormalRadius * x, ring.NormalY, ring.NormalRadius * z),
                        glm::vec2(float(j) / Segments, (ring.Y + (capsule ? 1.0f : .5f)) / (capsule ? 2.0f : 1.0f)));
                }
            for (uint32_t i = 0; i + 1 < rings.size(); ++i)
                for (uint32_t j = 0; j < Segments; ++j)
                {
                    uint32_t a = i * (Segments + 1) + j, b = a + Segments + 1;
                    if (i != 0)
                        indices.insert(indices.end(), {a, b, a + 1});
                    if (i + 2 != rings.size())
                        indices.insert(indices.end(), {a + 1, b, b + 1});
                }
        }
        void BuildCylinder(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
        {
            for (int y = -1; y <= 1; y += 2)
                for (uint32_t j = 0; j <= Segments; ++j)
                {
                    float angle = glm::two_pi<float>() * float(j % Segments) / Segments;
                    glm::vec3 normal(std::cos(angle), 0, std::sin(angle));
                    vertices.emplace_back(glm::vec3(normal.x * .5f, float(y), normal.z * .5f), normal,
                                          glm::vec2(float(j) / Segments, float(y + 1) * .5f));
                }
            for (uint32_t j = 0; j < Segments; ++j)
            {
                uint32_t b = j + Segments + 1;
                indices.insert(indices.end(), {j, b, j + 1, j + 1, b, b + 1});
            }
            // Separate cap vertices retain hard rim normals and planar UVs.
            for (int y = -1; y <= 1; y += 2)
            {
                uint32_t center = static_cast<uint32_t>(vertices.size());
                vertices.emplace_back(glm::vec3(0, y, 0), glm::vec3(0, y, 0), glm::vec2(.5f));
                for (uint32_t j = 0; j <= Segments; ++j)
                {
                    float angle = glm::two_pi<float>() * float(j % Segments) / Segments;
                    float x = std::cos(angle) * .5f, z = std::sin(angle) * .5f;
                    vertices.emplace_back(glm::vec3(x, y, z), glm::vec3(0, y, 0), glm::vec2(x + .5f, z + .5f));
                }
                for (uint32_t j = 0; j < Segments; ++j)
                {
                    uint32_t a = center + 1 + j, b = a + 1;
                    indices.insert(indices.end(), {center, y < 0 ? a : b, y < 0 ? b : a});
                }
            }
        }
        void BuildGround(float width, float depth, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
        {
            constexpr uint32_t cells = 10;
            for (uint32_t z = 0; z <= cells; ++z)
                for (uint32_t x = 0; x <= cells; ++x)
                {
                    glm::vec2 uv(float(x) / cells, float(z) / cells);
                    vertices.emplace_back(glm::vec3((uv.x - .5f) * width, 0, (uv.y - .5f) * depth), glm::vec3(0, 1, 0),
                                          uv);
                }
            for (uint32_t z = 0; z < cells; ++z)
                for (uint32_t x = 0; x < cells; ++x)
                {
                    uint32_t a = z * (cells + 1) + x, b = a + cells + 1;
                    indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
                }
        }
    } // namespace

    bool Mesh::GeneratePrimitive(MeshPrimitive type, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
    {
        vertices.clear();
        indices.clear();
        switch (type)
        {
        case MeshPrimitive::Cube:
            BuildCube(1, vertices, indices);
            break;
        case MeshPrimitive::Quad:
            BuildQuad(1, 1, vertices, indices);
            break;
        case MeshPrimitive::Sphere:
            BuildRound(false, vertices, indices);
            break;
        case MeshPrimitive::Capsule:
            BuildRound(true, vertices, indices);
            break;
        case MeshPrimitive::Cylinder:
            BuildCylinder(vertices, indices);
            break;
        case MeshPrimitive::Plane:
            BuildGround(10, 10, vertices, indices);
            break;
        default:
            return false;
        }
        return true;
    }
    Ref<Mesh> Mesh::CreatePrimitive(MeshPrimitive type)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        return GeneratePrimitive(type, vertices, indices) ? Create(vertices, indices) : nullptr;
    }
    Ref<Mesh> Mesh::CreateCube(float size)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        BuildCube(size, vertices, indices);
        return Create(vertices, indices);
    }
    Ref<Mesh> Mesh::CreateQuad(float width, float height)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        BuildQuad(width, height, vertices, indices);
        return Create(vertices, indices);
    }
    Ref<Mesh> Mesh::CreatePlane(float width, float height)
    {
        return CreateQuad(width, height);
    }
    Ref<Mesh> Mesh::CreateGroundPlane(float width, float depth)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        BuildGround(width, depth, vertices, indices);
        return Create(vertices, indices);
    }
    Ref<Mesh> Mesh::CreateSphere()
    {
        return CreatePrimitive(MeshPrimitive::Sphere);
    }
    Ref<Mesh> Mesh::CreateCapsule()
    {
        return CreatePrimitive(MeshPrimitive::Capsule);
    }
    Ref<Mesh> Mesh::CreateCylinder()
    {
        return CreatePrimitive(MeshPrimitive::Cylinder);
    }
} // namespace TomCat
