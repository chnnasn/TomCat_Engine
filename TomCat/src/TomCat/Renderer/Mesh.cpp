#include "tcpch.h"
#include "TomCat/Renderer/Mesh.h"

#include <array>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace TomCat {

	static glm::vec3 ResolvePosition(const std::vector<glm::vec3>& list, int index)
	{
		if (index > 0) return list[(size_t)(index - 1)];
		if (index < 0) return list[list.size() + index];
		return glm::vec3(0.0f);
	}

	static glm::vec2 ResolveTexCoord(const std::vector<glm::vec2>& list, int index)
	{
		if (index > 0) return list[(size_t)(index - 1)];
		if (index < 0) return list[list.size() + index];
		return glm::vec2(0.0f);
	}

	static glm::vec3 ResolveNormal(const std::vector<glm::vec3>& list, int index)
	{
		if (index > 0) return list[(size_t)(index - 1)];
		if (index < 0) return list[list.size() + index];
		return glm::vec3(0.0f, 0.0f, 1.0f);
	}

	Ref<Mesh> Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		return CreateRef<Mesh>(vertices, indices);
	}

	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		m_IndexCount = (uint32_t)indices.size();

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
		std::ifstream file(filepath);
		if (!file.is_open())
		{
			TC_Core_Error("Mesh::LoadOBJ: failed to open '{0}'", filepath);
			return nullptr;
		}

		std::vector<glm::vec3> positions;
		std::vector<glm::vec2> texcoords;
		std::vector<glm::vec3> normals;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::unordered_map<std::string, uint32_t> indexCache;

		std::string line;
		std::string name;

		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			if (line.empty() || line[0] == '#')
				continue;

			std::istringstream iss(line);
			std::string type;
			iss >> type;

			if (type == "v")
			{
				glm::vec3 p{ 0.0f };
				iss >> p.x >> p.y >> p.z;
				positions.push_back(p);
			}
			else if (type == "vt")
			{
				glm::vec2 t{ 0.0f };
				iss >> t.x >> t.y;
				texcoords.push_back(t);
			}
			else if (type == "vn")
			{
				glm::vec3 n{ 0.0f };
				iss >> n.x >> n.y >> n.z;
				normals.push_back(n);
			}
			else if (type == "f")
			{
				std::vector<std::string> faceTokens;
				std::string token;
				while (iss >> token)
					faceTokens.push_back(token);

				std::vector<std::array<int, 3>> face;
				for (const auto& ft : faceTokens)
				{
					std::array<int, 3> idx = { 0, 0, 0 };
					std::vector<std::string> parts;
					std::stringstream fss(ft);
					std::string part;
					while (std::getline(fss, part, '/'))
						parts.push_back(part);

					if (parts.empty() || parts[0].empty())
						continue;

					idx[0] = std::stoi(parts[0]);
					if (parts.size() >= 2 && !parts[1].empty()) idx[1] = std::stoi(parts[1]);
					if (parts.size() >= 3 && !parts[2].empty()) idx[2] = std::stoi(parts[2]);
					face.push_back(idx);
				}

				for (size_t i = 1; i + 1 < face.size(); i++)
				{
					std::array<std::array<int, 3>, 3> tri = { face[0], face[i], face[i + 1] };

					glm::vec3 faceNormal{ 0.0f, 0.0f, 1.0f };
					if (normals.empty())
					{
						glm::vec3 p0 = ResolvePosition(positions, tri[0][0]);
						glm::vec3 p1 = ResolvePosition(positions, tri[1][0]);
						glm::vec3 p2 = ResolvePosition(positions, tri[2][0]);
						glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
						if (glm::dot(cross, cross) > 0.000001f)
							faceNormal = glm::normalize(cross);
					}

					uint32_t triIndices[3];
					for (int k = 0; k < 3; k++)
					{
						Vertex v;
						v.Position = ResolvePosition(positions, tri[k][0]);
						v.TexCoord = texcoords.empty() ? glm::vec2(0.0f) : ResolveTexCoord(texcoords, tri[k][1]);
						v.Normal = normals.empty() ? faceNormal : ResolveNormal(normals, tri[k][2]);

						std::string key = std::to_string(tri[k][0]) + "/" + std::to_string(tri[k][1]) + "/" + std::to_string(tri[k][2]);
						auto it = indexCache.find(key);
						if (it != indexCache.end())
						{
							triIndices[k] = it->second;
						}
						else
						{
							uint32_t vertexIndex = (uint32_t)vertices.size();
							vertices.push_back(v);
							indexCache[key] = vertexIndex;
							triIndices[k] = vertexIndex;
						}
					}

					indices.push_back(triIndices[0]);
					indices.push_back(triIndices[1]);
					indices.push_back(triIndices[2]);
				}
			}
			else if (type == "o" || type == "g")
			{
				std::string n;
				iss >> n;
				if (name.empty() && !n.empty())
					name = n;
			}
		}

		if (vertices.empty())
		{
			TC_Core_Error("Mesh::LoadOBJ: no geometry loaded from '{0}'", filepath);
			return nullptr;
		}

		Ref<Mesh> mesh = Create(vertices, indices);
		mesh->SetName(name);
		return mesh;
	}

	Ref<Mesh> Mesh::CreateCube(float size)
	{
		const float h = size * 0.5f;

		std::vector<Vertex> vertices = {
			// +X
			Vertex(glm::vec3( h, -h, -h), glm::vec3( 1, 0, 0), glm::vec2(0, 0)),
			Vertex(glm::vec3( h,  h, -h), glm::vec3( 1, 0, 0), glm::vec2(1, 0)),
			Vertex(glm::vec3( h,  h,  h), glm::vec3( 1, 0, 0), glm::vec2(1, 1)),
			Vertex(glm::vec3( h, -h,  h), glm::vec3( 1, 0, 0), glm::vec2(0, 1)),
			// -X
			Vertex(glm::vec3(-h, -h,  h), glm::vec3(-1, 0, 0), glm::vec2(0, 0)),
			Vertex(glm::vec3(-h,  h,  h), glm::vec3(-1, 0, 0), glm::vec2(1, 0)),
			Vertex(glm::vec3(-h,  h, -h), glm::vec3(-1, 0, 0), glm::vec2(1, 1)),
			Vertex(glm::vec3(-h, -h, -h), glm::vec3(-1, 0, 0), glm::vec2(0, 1)),
			// +Y
			Vertex(glm::vec3(-h,  h, -h), glm::vec3(0,  1, 0), glm::vec2(0, 0)),
			Vertex(glm::vec3(-h,  h,  h), glm::vec3(0,  1, 0), glm::vec2(1, 0)),
			Vertex(glm::vec3( h,  h,  h), glm::vec3(0,  1, 0), glm::vec2(1, 1)),
			Vertex(glm::vec3( h,  h, -h), glm::vec3(0,  1, 0), glm::vec2(0, 1)),
			// -Y
			Vertex(glm::vec3(-h, -h,  h), glm::vec3(0, -1, 0), glm::vec2(0, 0)),
			Vertex(glm::vec3(-h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(1, 0)),
			Vertex(glm::vec3( h, -h, -h), glm::vec3(0, -1, 0), glm::vec2(1, 1)),
			Vertex(glm::vec3( h, -h,  h), glm::vec3(0, -1, 0), glm::vec2(0, 1)),
			// +Z
			Vertex(glm::vec3(-h, -h,  h), glm::vec3(0, 0,  1), glm::vec2(0, 0)),
			Vertex(glm::vec3( h, -h,  h), glm::vec3(0, 0,  1), glm::vec2(1, 0)),
			Vertex(glm::vec3( h,  h,  h), glm::vec3(0, 0,  1), glm::vec2(1, 1)),
			Vertex(glm::vec3(-h,  h,  h), glm::vec3(0, 0,  1), glm::vec2(0, 1)),
			// -Z
			Vertex(glm::vec3( h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 0)),
			Vertex(glm::vec3(-h, -h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 0)),
			Vertex(glm::vec3(-h,  h, -h), glm::vec3(0, 0, -1), glm::vec2(1, 1)),
			Vertex(glm::vec3( h,  h, -h), glm::vec3(0, 0, -1), glm::vec2(0, 1)),
		};

		std::vector<uint32_t> indices;
		for (uint32_t face = 0; face < 6; face++)
		{
			uint32_t base = face * 4;
			uint32_t quad[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
			for (uint32_t i = 0; i < 6; i++)
				indices.push_back(quad[i]);
		}

		return Create(vertices, indices);
	}

	Ref<Mesh> Mesh::CreatePlane(float width, float height)
	{
		const float hw = width * 0.5f;
		const float hh = height * 0.5f;

		std::vector<Vertex> vertices = {
			Vertex(glm::vec3(-hw, -hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(0, 0)),
			Vertex(glm::vec3( hw, -hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(1, 0)),
			Vertex(glm::vec3( hw,  hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(1, 1)),
			Vertex(glm::vec3(-hw,  hh, 0.0f), glm::vec3(0, 0, 1), glm::vec2(0, 1)),
		};

		std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };

		return Create(vertices, indices);
	}

}
