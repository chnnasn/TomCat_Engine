#pragma once

#include "Asset.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	enum class ShaderArtifactTarget : uint32_t
	{
		OpenGL = 1,
		Vulkan = 2
	};

	enum class ShaderArtifactStage : uint32_t
	{
		Vertex = 1,
		Fragment = 2
	};

	enum class ShaderResourceKind : uint32_t
	{
		UniformBuffer = 1,
		StorageBuffer,
		SampledImage,
		SeparateImage,
		SeparateSampler,
		StorageImage,
		PushConstant,
		StageInput,
		StageOutput,
		PlainUniform
	};

	struct ShaderArtifactStageView
	{
		ShaderArtifactStage Stage = ShaderArtifactStage::Vertex;
		std::string_view EntryPoint;
		std::span<const uint8_t> Spirv;
	};

	struct ShaderResourceView
	{
		ShaderResourceKind Kind = ShaderResourceKind::UniformBuffer;
		uint32_t StageMask = 0;
		uint32_t DescriptorSet = UINT32_MAX;
		uint32_t Binding = UINT32_MAX;
		uint32_t Location = UINT32_MAX;
		uint32_t ArraySize = 1;
		uint32_t ByteSize = 0;
		std::string_view Name;
	};

	struct ShaderArtifactView
	{
		ShaderArtifactTarget Target = ShaderArtifactTarget::OpenGL;
		std::vector<ShaderArtifactStageView> Stages;
		std::vector<ShaderResourceView> Resources;
	};

	[[nodiscard]] bool IsShaderArtifact(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ParseShaderArtifact(std::span<const uint8_t> bytes,
		ShaderArtifactView& artifact, std::string& error);
	[[nodiscard]] bool BuildShaderArtifact(std::span<const uint8_t> source,
		const std::filesystem::path& sourcePath,
		const AssetImportSettings& settings, std::string_view backend,
		std::vector<uint8_t>& artifact, std::string& error);

}
