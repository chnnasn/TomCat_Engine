#pragma once

#include "Asset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace TomCat {

	inline constexpr uint32_t MeshArtifactVertexStride = sizeof(float) * 8;

	struct MeshArtifactVertex
	{
		std::array<float, 3> Position{};
		std::array<float, 3> Normal{};
		std::array<float, 2> TexCoord{};
	};

	struct MeshArtifactView
	{
		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;
		std::span<const uint8_t> PackedVertices;
		std::span<const uint8_t> PackedIndices;

		[[nodiscard]] bool DecodeVertex(uint32_t index,
			MeshArtifactVertex& vertex) const noexcept;
		[[nodiscard]] bool DecodeIndex(uint32_t index,
			uint32_t& value) const noexcept;
	};

	struct MeshArtifactPart
	{
		uint32_t FirstIndex = 0;
		uint32_t IndexCount = 0;
		std::array<float, 4> Color{1, 1, 1, 1};
		std::vector<uint8_t> Texture;
	};

	// Owning packed mesh. Vertex/index spans returned by this object remain valid
	// until the object is moved, assigned or destroyed.
	class MeshArtifact
	{
	public:
		const std::vector<MeshArtifactPart>& GetParts() const { return m_Parts; }
		[[nodiscard]] uint32_t GetVertexCount() const noexcept { return m_VertexCount; }
		[[nodiscard]] uint32_t GetIndexCount() const noexcept { return m_IndexCount; }
		[[nodiscard]] std::span<const uint8_t> GetPackedVertices() const noexcept;
		[[nodiscard]] std::span<const uint8_t> GetPackedIndices() const noexcept;
		[[nodiscard]] bool DecodeVertex(uint32_t index,
			MeshArtifactVertex& vertex) const noexcept;
		[[nodiscard]] bool DecodeIndex(uint32_t index,
			uint32_t& value) const noexcept;

	private:
		friend bool DecodeMeshArtifact(std::vector<uint8_t> bytes,
			MeshArtifact& artifact, std::string& error);
		std::vector<uint8_t> m_Bytes;
		std::vector<MeshArtifactPart> m_Parts;
		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;
		uint32_t m_VertexOffset = 0;
		uint32_t m_IndexOffset = 0;
	};

	[[nodiscard]] bool IsMeshArtifact(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ParseMeshArtifact(std::span<const uint8_t> bytes,
		MeshArtifactView& artifact, std::string& error);
	// Takes the artifact bytes by value so async loaders can move their buffer
	// directly into the owning mesh without a second bulk allocation.
	[[nodiscard]] bool DecodeMeshArtifact(std::vector<uint8_t> bytes,
		MeshArtifact& artifact, std::string& error);
	[[nodiscard]] bool CollectModelDependencies(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath, std::vector<std::filesystem::path>& paths, std::string& error);
	[[nodiscard]] bool BuildMeshArtifact(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath,
		std::vector<uint8_t>& artifact, std::string& error);

}
