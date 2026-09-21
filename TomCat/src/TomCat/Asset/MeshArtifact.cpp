#include "tcpch.h"
#include "MeshArtifact.h"

#ifndef TC_PLATFORM_WEB
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/MemoryIOWrapper.h>
#include <assimp/DefaultIOSystem.h>
#include <fstream>
#include <glm/gtc/matrix_inverse.hpp>
#include <sstream>
#endif

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <map>
#include <string_view>
#include <tuple>

namespace TomCat {

	namespace {

#ifndef TC_PLATFORM_WEB
		class ModelSourceIO : public Assimp::IOSystem {
		public:
			ModelSourceIO(std::filesystem::path path, std::span<const uint8_t> bytes, std::vector<std::filesystem::path>* paths = nullptr)
				: m_Path(path.lexically_normal()), m_Bytes(bytes), m_Paths(paths) {}
			bool Exists(const char* path) const override {
				return std::filesystem::path(path).lexically_normal() == m_Path || m_Default.Exists(path);
			}
			Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
				if (std::filesystem::path(path).lexically_normal() == m_Path)
					return new Assimp::MemoryIOStream(m_Bytes.data(), m_Bytes.size());
				auto* stream = m_Default.Open(path, mode);
				if (stream && m_Paths) m_Paths->push_back(std::filesystem::path(path).lexically_normal());
				return stream;
			}
			char getOsSeparator() const override { return m_Default.getOsSeparator(); }
			void Close(Assimp::IOStream* stream) override { delete stream; }
		private:
			Assimp::DefaultIOSystem m_Default;
			std::filesystem::path m_Path;
			std::span<const uint8_t> m_Bytes;
			std::vector<std::filesystem::path>* m_Paths;
		};
#endif
		constexpr std::array<uint8_t, 4> kMagic = { 'T', 'C', 'M', 'S' };
		constexpr uint32_t kVersion = 1;
		constexpr uint32_t kHeaderSize = 32;
		constexpr uint64_t kMaximumSourceBytes = 512ULL * 1024ULL * 1024ULL;
		constexpr uint32_t kMaximumVertices = 16'000'000;
		constexpr uint32_t kMaximumIndices = 48'000'000;

		struct Vec2 { float X = 0.0f, Y = 0.0f; };
		struct Vec3 { float X = 0.0f, Y = 0.0f, Z = 0.0f; };
		struct VertexBuild
		{
			MeshArtifactVertex Vertex;
			bool HasNormal = false;
			Vec3 AccumulatedNormal;
		};

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 4; ++index)
				value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
			return true;
		}

		void AppendFloat(std::vector<uint8_t>& output, float value)
		{
			AppendU32(output, std::bit_cast<uint32_t>(value));
		}

		bool ReadFloat(std::span<const uint8_t> bytes, size_t offset, float& value)
		{
			uint32_t raw = 0;
			if (!ReadU32(bytes, offset, raw))
				return false;
			value = std::bit_cast<float>(raw);
			return std::isfinite(value);
		}

		std::string_view Trim(std::string_view value)
		{
			while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
				value.remove_prefix(1);
			while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
				value.remove_suffix(1);
			return value;
		}

		std::vector<std::string_view> SplitWhitespace(std::string_view value)
		{
			std::vector<std::string_view> result;
			while (!value.empty())
			{
				while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
					value.remove_prefix(1);
				if (value.empty())
					break;
				const size_t length = value.find_first_of(" \t");
				result.push_back(value.substr(0, length));
				if (length == std::string_view::npos)
					break;
				value.remove_prefix(length);
			}
			return result;
		}

		bool ParseFloat(std::string_view token, float& value)
		{
			const auto parsed = std::from_chars(token.data(), token.data() + token.size(),
				value, std::chars_format::general);
			return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()
				&& std::isfinite(value);
		}

		bool ParseInt(std::string_view token, int32_t& value)
		{
			const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()
				&& value != 0;
		}

		bool ResolveIndex(int32_t value, size_t count, int32_t& index)
		{
			const int64_t resolved = value > 0 ? static_cast<int64_t>(value) - 1
				: static_cast<int64_t>(count) + value;
			if (resolved < 0 || resolved >= static_cast<int64_t>(count)
				|| resolved > INT32_MAX)
				return false;
			index = static_cast<int32_t>(resolved);
			return true;
		}

		bool ParseFaceIndex(std::string_view token, size_t positions,
			size_t texCoords, size_t normals, std::tuple<int32_t, int32_t, int32_t>& key)
		{
			std::array<std::string_view, 3> fields{};
			size_t fieldCount = 0;
			for (;;)
			{
				if (fieldCount == fields.size())
					return false;
				const size_t slash = token.find('/');
				fields[fieldCount++] = token.substr(0, slash);
				if (slash == std::string_view::npos)
					break;
				token.remove_prefix(slash + 1);
			}
			if (fields[0].empty())
				return false;
			int32_t raw = 0, position = -1, texCoord = -1, normal = -1;
			if (!ParseInt(fields[0], raw) || !ResolveIndex(raw, positions, position))
				return false;
			if (fieldCount > 1 && !fields[1].empty()
				&& (!ParseInt(fields[1], raw) || !ResolveIndex(raw, texCoords, texCoord)))
				return false;
			if (fieldCount > 2 && !fields[2].empty()
				&& (!ParseInt(fields[2], raw) || !ResolveIndex(raw, normals, normal)))
				return false;
			key = { position, texCoord, normal };
			return true;
		}

		Vec3 Cross(const Vec3& left, const Vec3& right)
		{
			return { left.Y * right.Z - left.Z * right.Y,
				left.Z * right.X - left.X * right.Z,
				left.X * right.Y - left.Y * right.X };
		}

		float LengthSquared(const Vec3& value)
		{
			return value.X * value.X + value.Y * value.Y + value.Z * value.Z;
		}

		bool Normalize(Vec3& value)
		{
			const float lengthSquared = LengthSquared(value);
			if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-20f)
				return false;
			const float inverse = 1.0f / std::sqrt(lengthSquared);
			value.X *= inverse; value.Y *= inverse; value.Z *= inverse;
			return true;
		}

		std::string LowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
			return extension;
		}

	}

	bool MeshArtifactView::DecodeVertex(uint32_t index,
		MeshArtifactVertex& vertex) const noexcept
	{
		if (index >= VertexCount || PackedVertices.size() <
			(static_cast<size_t>(index) + 1) * MeshArtifactVertexStride)
			return false;
		const size_t offset = static_cast<size_t>(index) * MeshArtifactVertexStride;
		for (size_t component = 0; component < 8; ++component)
		{
			float value = 0.0f;
			if (!ReadFloat(PackedVertices, offset + component * 4, value))
				return false;
			if (component < 3) vertex.Position[component] = value;
			else if (component < 6) vertex.Normal[component - 3] = value;
			else vertex.TexCoord[component - 6] = value;
		}
		return true;
	}

	bool MeshArtifactView::DecodeIndex(uint32_t index, uint32_t& value) const noexcept
	{
		return index < IndexCount && ReadU32(PackedIndices,
			static_cast<size_t>(index) * sizeof(uint32_t), value);
	}

	std::span<const uint8_t> MeshArtifact::GetPackedVertices() const noexcept
	{
		const size_t size = static_cast<size_t>(m_VertexCount)
			* MeshArtifactVertexStride;
		if (m_VertexOffset > m_Bytes.size() || size > m_Bytes.size() - m_VertexOffset)
			return {};
		return std::span<const uint8_t>(m_Bytes).subspan(m_VertexOffset, size);
	}

	std::span<const uint8_t> MeshArtifact::GetPackedIndices() const noexcept
	{
		const size_t size = static_cast<size_t>(m_IndexCount) * sizeof(uint32_t);
		if (m_IndexOffset > m_Bytes.size() || size > m_Bytes.size() - m_IndexOffset)
			return {};
		return std::span<const uint8_t>(m_Bytes).subspan(m_IndexOffset, size);
	}

	bool MeshArtifact::DecodeVertex(uint32_t index,
		MeshArtifactVertex& vertex) const noexcept
	{
		MeshArtifactView view;
		view.VertexCount = m_VertexCount;
		view.IndexCount = m_IndexCount;
		view.PackedVertices = GetPackedVertices();
		view.PackedIndices = GetPackedIndices();
		return view.DecodeVertex(index, vertex);
	}

	bool MeshArtifact::DecodeIndex(uint32_t index, uint32_t& value) const noexcept
	{
		MeshArtifactView view;
		view.VertexCount = m_VertexCount;
		view.IndexCount = m_IndexCount;
		view.PackedVertices = GetPackedVertices();
		view.PackedIndices = GetPackedIndices();
		return view.DecodeIndex(index, value);
	}

	bool IsMeshArtifact(std::span<const uint8_t> bytes)
	{
		return bytes.size() >= kMagic.size()
			&& std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
	}

	bool ParseMeshArtifact(std::span<const uint8_t> bytes,
		MeshArtifactView& artifact, std::string& error)
	{
		artifact = {};
		error.clear();
		uint32_t version = 0, stride = 0, vertexCount = 0, indexCount = 0;
		uint32_t vertexOffset = 0, indexOffset = 0, totalSize = 0;
		if (bytes.size() < kHeaderSize || !IsMeshArtifact(bytes)
			|| !ReadU32(bytes, 4, version) || (version != kVersion && version != 2)
			|| !ReadU32(bytes, 8, stride) || stride != MeshArtifactVertexStride
			|| !ReadU32(bytes, 12, vertexCount) || vertexCount == 0 || vertexCount > kMaximumVertices
			|| !ReadU32(bytes, 16, indexCount) || indexCount == 0
			|| indexCount > kMaximumIndices || indexCount % 3 != 0
			|| !ReadU32(bytes, 20, vertexOffset) || !ReadU32(bytes, 24, indexOffset)
			|| !ReadU32(bytes, 28, totalSize) || totalSize != bytes.size())
		{
			error = "mesh artifact header is invalid or unsupported";
			return false;
		}
		const uint64_t expectedIndex = static_cast<uint64_t>(kHeaderSize)
			+ static_cast<uint64_t>(vertexCount) * MeshArtifactVertexStride;
		const uint64_t expectedSize = expectedIndex
			+ static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
		if (vertexOffset != kHeaderSize || indexOffset != expectedIndex
			|| expectedSize > bytes.size() || (version == 1 && expectedSize != bytes.size()))
		{
			error = "mesh artifact data layout is invalid";
			return false;
		}
		if (version == 2)
		{
			size_t cursor = static_cast<size_t>(expectedSize);
			uint32_t count = 0, covered = 0;
			if (!ReadU32(bytes, cursor, count) || !count || count > indexCount / 3) { error = "invalid model part count"; return false; }
			cursor += 4;
			for (uint32_t i = 0; i < count; ++i)
			{
				uint32_t first = 0, size = 0, texture = 0;
				if (!ReadU32(bytes, cursor, first) || !ReadU32(bytes, cursor + 4, size)
					|| first != covered || !size || size % 3 || size > indexCount - covered) { error = "invalid model part range"; return false; }
				for (size_t c = 0; c < 4; ++c) { float value; if (!ReadFloat(bytes, cursor + 8 + c * 4, value)) { error = "invalid model color"; return false; } }
				if (!ReadU32(bytes, cursor + 24, texture) || texture > 64 * 1024 * 1024
					|| cursor + 28 > bytes.size() || texture > bytes.size() - cursor - 28) { error = "invalid model texture"; return false; }
				cursor += 28 + texture;
				covered += size;
			}
			if (cursor != bytes.size() || covered != indexCount) { error = "incomplete model parts"; return false; }
		}
		artifact.VertexCount = vertexCount;
		artifact.IndexCount = indexCount;
		artifact.PackedVertices = bytes.subspan(vertexOffset,
			static_cast<size_t>(vertexCount) * MeshArtifactVertexStride);
		artifact.PackedIndices = bytes.subspan(indexOffset,
			static_cast<size_t>(indexCount) * sizeof(uint32_t));
		for (uint32_t index = 0; index < vertexCount; ++index)
		{
			MeshArtifactVertex vertex;
			if (!artifact.DecodeVertex(index, vertex))
			{
				error = "mesh artifact contains a non-finite vertex";
				artifact = {};
				return false;
			}
			const float normalLength = vertex.Normal[0] * vertex.Normal[0]
				+ vertex.Normal[1] * vertex.Normal[1] + vertex.Normal[2] * vertex.Normal[2];
			if (!std::isfinite(normalLength) || normalLength < 0.25f || normalLength > 2.25f)
			{
				error = "mesh artifact contains an invalid normal";
				artifact = {};
				return false;
			}
		}
		for (uint32_t index = 0; index < indexCount; ++index)
		{
			uint32_t value = 0;
			if (!artifact.DecodeIndex(index, value) || value >= vertexCount)
			{
				error = "mesh artifact contains an out-of-range index";
				artifact = {};
				return false;
			}
		}
		return true;
	}

	bool DecodeMeshArtifact(std::vector<uint8_t> bytes,
		MeshArtifact& artifact, std::string& error)
	{
		artifact = {};
		MeshArtifactView view;
		if (!ParseMeshArtifact(bytes, view, error))
			return false;
		MeshArtifact decoded;
		decoded.m_VertexCount = view.VertexCount;
		decoded.m_IndexCount = view.IndexCount;
		decoded.m_VertexOffset = static_cast<uint32_t>(
			view.PackedVertices.data() - bytes.data());
		decoded.m_IndexOffset = static_cast<uint32_t>(
			view.PackedIndices.data() - bytes.data());
		uint32_t version = 0;
		ReadU32(bytes, 4, version);
		if (version == 2)
		{
			size_t cursor = decoded.m_IndexOffset + static_cast<size_t>(view.IndexCount) * 4;
			uint32_t count = 0;
			ReadU32(bytes, cursor, count); cursor += 4;
			for (uint32_t i = 0; i < count; ++i)
			{
				MeshArtifactPart part;
				ReadU32(bytes, cursor, part.FirstIndex);
				ReadU32(bytes, cursor + 4, part.IndexCount);
				for (size_t c = 0; c < 4; ++c) ReadFloat(bytes, cursor + 8 + c * 4, part.Color[c]);
				uint32_t size = 0; ReadU32(bytes, cursor + 24, size); cursor += 28;
				part.Texture.assign(bytes.begin() + cursor, bytes.begin() + cursor + size);
				cursor += size;
				decoded.m_Parts.push_back(std::move(part));
			}
		}
		decoded.m_Bytes = std::move(bytes);
		artifact = std::move(decoded);
		return true;
	}

	bool CollectModelDependencies(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath, std::vector<std::filesystem::path>& paths, std::string& error)
	{
		paths.clear();
#ifndef TC_PLATFORM_WEB
		const auto extension = LowerExtension(sourcePath);
		if (extension == ".obj" && std::string_view(reinterpret_cast<const char*>(source.data()), source.size()).find("mtllib") == std::string_view::npos) return true;
		Assimp::Importer importer;
		importer.SetIOHandler(new ModelSourceIO(sourcePath, source, &paths));
		const auto* scene = importer.ReadFile(sourcePath.string(), aiProcess_Triangulate);
		if (!scene) { error = importer.GetErrorString(); return false; }
		for (uint32_t m = 0; m < scene->mNumMaterials; ++m) {
			aiString texture;
			const auto* material = scene->mMaterials[m];
			if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texture) != AI_SUCCESS)
				material->GetTexture(aiTextureType_BASE_COLOR, 0, &texture);
			if (texture.length && !scene->GetEmbeddedTexture(texture.C_Str()))
				paths.push_back((sourcePath.parent_path() / texture.C_Str()).lexically_normal());
		}
		std::sort(paths.begin(), paths.end());
		paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
#endif
		return true;
	}

	bool BuildMeshArtifact(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath,
		std::vector<uint8_t>& artifact, std::string& error)
	{
		artifact.clear();
		error.clear();
#ifndef TC_PLATFORM_WEB
		const auto extension = LowerExtension(sourcePath);
		if (extension == ".fbx" || extension == ".gltf" || extension == ".glb"
			|| (extension == ".obj" && std::string_view(reinterpret_cast<const char*>(source.data()), source.size()).find("mtllib") != std::string_view::npos))
		{
			if (source.empty() || source.size() > kMaximumSourceBytes) { error = "Model source is empty or oversized"; return false; }
			Assimp::Importer importer;
			importer.SetIOHandler(new ModelSourceIO(sourcePath, source));
			// Pre-transform bakes node hierarchy into vertices, matching the original Model loader.
			const auto* scene = importer.ReadFile(sourcePath.string(), aiProcess_Triangulate
				| aiProcess_JoinIdenticalVertices | aiProcess_GenNormals | aiProcess_PreTransformVertices | aiProcess_FlipUVs);
			if (!scene || !scene->HasMeshes()) { error = importer.GetErrorString(); return false; }
			std::ostringstream obj;
			obj.imbue(std::locale::classic());
			obj.precision(9);
			uint64_t offset = 1;
			std::vector<MeshArtifactPart> parts;
			uint32_t firstIndex = 0;
			for (uint32_t m = 0; m < scene->mNumMeshes; ++m)
			{
				const aiMesh* mesh = scene->mMeshes[m];
				MeshArtifactPart part;
				part.FirstIndex = firstIndex;
				if (mesh->mMaterialIndex < scene->mNumMaterials)
				{
					const auto* material = scene->mMaterials[mesh->mMaterialIndex];
					aiColor4D color(1,1,1,1);
					material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
					part.Color = {color.r,color.g,color.b,color.a};
					aiString texture;
					if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texture) != AI_SUCCESS)
						material->GetTexture(aiTextureType_BASE_COLOR, 0, &texture);
					if (texture.length)
					{
						const aiTexture* embedded = scene->GetEmbeddedTexture(texture.C_Str());
						if (embedded && !embedded->mHeight && embedded->mWidth <= 64 * 1024 * 1024)
						{
							const auto* bytes = reinterpret_cast<const uint8_t*>(embedded->pcData);
							part.Texture.assign(bytes, bytes + embedded->mWidth);
						}
						else if (!embedded)
						{
							const auto path = sourcePath.parent_path() / texture.C_Str();
							std::error_code ec;
							const auto size = std::filesystem::file_size(path, ec);
							if (!ec && size <= 64 * 1024 * 1024) {
								std::ifstream input(path, std::ios::binary);
								part.Texture.assign(std::istreambuf_iterator<char>(input), {});
							}
						}
					}
				}
				if (offset + mesh->mNumVertices > kMaximumVertices) { error = "Model exceeds vertex limit"; return false; }
				for (uint32_t v = 0; v < mesh->mNumVertices; ++v)
				{
					const auto p = mesh->mVertices[v];
					const auto n = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0,1,0);
					const auto uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D();
					obj << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
					obj << "vn " << n.x << ' ' << n.y << ' ' << n.z << '\n';
					obj << "vt " << uv.x << ' ' << uv.y << '\n';
				}
				for (uint32_t f = 0; f < mesh->mNumFaces; ++f)
				{
					const auto& face = mesh->mFaces[f];
					if (face.mNumIndices != 3) continue;
					part.IndexCount += 3;
					obj << "f";
					for (uint32_t j = 0; j < 3; ++j) { const auto i = offset + face.mIndices[j]; obj << ' ' << i << '/' << i << '/' << i; }
					obj << '\n';
				}
				offset += mesh->mNumVertices;
				firstIndex += part.IndexCount;
				if (part.IndexCount) parts.push_back(std::move(part));
			}
			const std::string text = obj.str();
			if (!BuildMeshArtifact(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "model.obj", artifact, error)) return false;
			AppendU32(artifact, static_cast<uint32_t>(parts.size()));
			for (const auto& part : parts) {
				AppendU32(artifact, part.FirstIndex); AppendU32(artifact, part.IndexCount);
				for (float c : part.Color) AppendFloat(artifact, c);
				AppendU32(artifact, static_cast<uint32_t>(part.Texture.size()));
				artifact.insert(artifact.end(), part.Texture.begin(), part.Texture.end());
			}
			if (artifact.size() > UINT32_MAX) { error = "Model artifact too large"; artifact.clear(); return false; }
			artifact[4] = 2;
			for (uint32_t i = 0; i < 4; ++i) artifact[28 + i] = static_cast<uint8_t>(artifact.size() >> (i * 8));
			MeshArtifactView validation;
			return ParseMeshArtifact(artifact, validation, error);
		}
#endif
		if (LowerExtension(sourcePath) != ".obj")
		{
			error = "mesh format is unsupported; the built-in importer currently supports OBJ only";
			return false;
		}
		if (source.empty() || source.size() > kMaximumSourceBytes
			|| std::find(source.begin(), source.end(), uint8_t{ 0 }) != source.end())
		{
			error = "OBJ source is empty, oversized, or contains NUL bytes";
			return false;
		}

		std::vector<Vec3> positions;
		std::vector<Vec2> texCoords;
		std::vector<Vec3> normals;
		std::vector<VertexBuild> vertices;
		std::vector<uint32_t> indices;
		std::map<std::tuple<int32_t, int32_t, int32_t>, uint32_t> vertexMap;
		const std::string text(source.begin(), source.end());
		size_t cursor = 0;
		uint32_t lineNumber = 0;
		auto fail = [&](std::string_view message)
		{
			error = "OBJ line " + std::to_string(lineNumber) + ": " + std::string(message);
			return false;
		};
		while (cursor <= text.size())
		{
			++lineNumber;
			const size_t end = text.find('\n', cursor);
			std::string_view line(text.data() + cursor,
				(end == std::string::npos ? text.size() : end) - cursor);
			cursor = end == std::string::npos ? text.size() + 1 : end + 1;
			const size_t comment = line.find('#');
			if (comment != std::string_view::npos)
				line = line.substr(0, comment);
			line = Trim(line);
			if (line.empty())
				continue;
			const std::vector<std::string_view> tokens = SplitWhitespace(line);
			if (tokens.empty())
				continue;
			if (tokens[0] == "v")
			{
				if (tokens.size() != 4 && tokens.size() != 5)
					return fail("vertex position must have three coordinates and optional w");
				Vec3 value;
				if (!ParseFloat(tokens[1], value.X) || !ParseFloat(tokens[2], value.Y)
					|| !ParseFloat(tokens[3], value.Z))
					return fail("vertex position contains an invalid number");
				if (tokens.size() == 5)
				{
					float w = 0.0f;
					if (!ParseFloat(tokens[4], w) || std::abs(w) <= 1.0e-20f)
						return fail("vertex homogeneous coordinate is invalid or zero");
					value.X /= w; value.Y /= w; value.Z /= w;
				}
				positions.push_back(value);
			}
			else if (tokens[0] == "vt")
			{
				if (tokens.size() < 2 || tokens.size() > 4)
					return fail("texture coordinate must have one to three coordinates");
				Vec2 value;
				if (!ParseFloat(tokens[1], value.X)
					|| (tokens.size() >= 3 && !ParseFloat(tokens[2], value.Y)))
					return fail("texture coordinate contains an invalid number");
				float ignored = 0.0f;
				if (tokens.size() == 4 && !ParseFloat(tokens[3], ignored))
					return fail("texture coordinate contains an invalid third number");
				texCoords.push_back(value);
			}
			else if (tokens[0] == "vn")
			{
				if (tokens.size() != 4)
					return fail("normal must have three coordinates");
				Vec3 value;
				if (!ParseFloat(tokens[1], value.X) || !ParseFloat(tokens[2], value.Y)
					|| !ParseFloat(tokens[3], value.Z) || !Normalize(value))
					return fail("normal is invalid or has zero length");
				normals.push_back(value);
			}
			else if (tokens[0] == "f")
			{
				if (tokens.size() < 4 || tokens.size() > 1'000'001)
					return fail("face must contain between 3 and 1,000,000 vertices");
				std::vector<uint32_t> face;
				face.reserve(tokens.size() - 1);
				for (size_t tokenIndex = 1; tokenIndex < tokens.size(); ++tokenIndex)
				{
					std::tuple<int32_t, int32_t, int32_t> key;
					if (!ParseFaceIndex(tokens[tokenIndex], positions.size(),
						texCoords.size(), normals.size(), key))
						return fail("face contains an invalid or out-of-range index");
					const auto found = vertexMap.find(key);
					if (found != vertexMap.end())
					{
						face.push_back(found->second);
						continue;
					}
					if (vertices.size() >= kMaximumVertices)
						return fail("mesh exceeds the vertex limit");
					const auto [positionIndex, texCoordIndex, normalIndex] = key;
					VertexBuild vertex;
					const Vec3& position = positions[static_cast<size_t>(positionIndex)];
					vertex.Vertex.Position = { position.X, position.Y, position.Z };
					if (texCoordIndex >= 0)
					{
						const Vec2& texCoord = texCoords[static_cast<size_t>(texCoordIndex)];
						vertex.Vertex.TexCoord = { texCoord.X, texCoord.Y };
					}
					if (normalIndex >= 0)
					{
						const Vec3& normal = normals[static_cast<size_t>(normalIndex)];
						vertex.Vertex.Normal = { normal.X, normal.Y, normal.Z };
						vertex.HasNormal = true;
					}
					const uint32_t index = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertex);
					vertexMap.emplace(key, index);
					face.push_back(index);
				}
				for (size_t triangle = 1; triangle + 1 < face.size(); ++triangle)
				{
					if (indices.size() > kMaximumIndices - 3)
						return fail("mesh exceeds the index limit");
					const std::array<uint32_t, 3> triangleIndices = {
						face[0], face[triangle], face[triangle + 1] };
					const auto& a = vertices[triangleIndices[0]].Vertex.Position;
					const auto& b = vertices[triangleIndices[1]].Vertex.Position;
					const auto& c = vertices[triangleIndices[2]].Vertex.Position;
					const Vec3 ab{ b[0] - a[0], b[1] - a[1], b[2] - a[2] };
					const Vec3 ac{ c[0] - a[0], c[1] - a[1], c[2] - a[2] };
					const Vec3 faceNormal = Cross(ab, ac);
					if (LengthSquared(faceNormal) <= 1.0e-20f)
						return fail("face triangulation produced a degenerate triangle");
					for (uint32_t index : triangleIndices)
					{
						VertexBuild& vertex = vertices[index];
						if (!vertex.HasNormal)
						{
							vertex.AccumulatedNormal.X += faceNormal.X;
							vertex.AccumulatedNormal.Y += faceNormal.Y;
							vertex.AccumulatedNormal.Z += faceNormal.Z;
						}
					}
					indices.insert(indices.end(), triangleIndices.begin(), triangleIndices.end());
				}
			}
			// Object/group/material/smoothing declarations and non-triangle primitive
			// records do not alter packed triangle geometry and are intentionally ignored.
		}
		if (positions.empty() || vertices.empty() || indices.empty())
		{
			error = "OBJ source contains no renderable triangle geometry";
			return false;
		}
		for (VertexBuild& vertex : vertices)
		{
			if (vertex.HasNormal)
				continue;
			if (!Normalize(vertex.AccumulatedNormal))
			{
				error = "OBJ source could not generate a normal for every vertex";
				return false;
			}
			vertex.Vertex.Normal = { vertex.AccumulatedNormal.X,
				vertex.AccumulatedNormal.Y, vertex.AccumulatedNormal.Z };
		}

		const uint64_t vertexBytes = static_cast<uint64_t>(vertices.size())
			* MeshArtifactVertexStride;
		const uint64_t totalSize = kHeaderSize + vertexBytes
			+ static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
		if (totalSize > UINT32_MAX)
		{
			error = "mesh artifact exceeds the 4 GiB format limit";
			return false;
		}
		artifact.reserve(static_cast<size_t>(totalSize));
		artifact.insert(artifact.end(), kMagic.begin(), kMagic.end());
		AppendU32(artifact, kVersion);
		AppendU32(artifact, MeshArtifactVertexStride);
		AppendU32(artifact, static_cast<uint32_t>(vertices.size()));
		AppendU32(artifact, static_cast<uint32_t>(indices.size()));
		AppendU32(artifact, kHeaderSize);
		AppendU32(artifact, kHeaderSize + static_cast<uint32_t>(vertexBytes));
		AppendU32(artifact, static_cast<uint32_t>(totalSize));
		for (const VertexBuild& vertex : vertices)
		{
			for (float value : vertex.Vertex.Position) AppendFloat(artifact, value);
			for (float value : vertex.Vertex.Normal) AppendFloat(artifact, value);
			for (float value : vertex.Vertex.TexCoord) AppendFloat(artifact, value);
		}
		for (uint32_t index : indices) AppendU32(artifact, index);
		MeshArtifactView validation;
		if (!ParseMeshArtifact(artifact, validation, error))
		{
			artifact.clear();
			return false;
		}
		return true;
	}

}
