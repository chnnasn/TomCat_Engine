#pragma once

#include "Asset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	enum class MaterialParameterType : uint32_t
	{
		Bool = 1,
		Int,
		Float,
		Float2,
		Float3,
		Float4
	};

	struct MaterialTextureBindingView
	{
		std::string_view Name;
		AssetHandle Texture = AssetHandle(0);
	};

	struct MaterialParameterView
	{
		std::string_view Name;
		MaterialParameterType Type = MaterialParameterType::Float;
		std::array<uint32_t, 4> RawValue{};

		[[nodiscard]] bool AsBool() const noexcept;
		[[nodiscard]] int32_t AsInt() const noexcept;
		[[nodiscard]] float AsFloat(size_t component = 0) const noexcept;
		[[nodiscard]] size_t ComponentCount() const noexcept;
	};

	struct MaterialArtifactView
	{
		AssetHandle Shader = AssetHandle(0);
		std::vector<MaterialTextureBindingView> Textures;
		std::vector<MaterialParameterView> Parameters;
	};

	struct MaterialTextureBinding
	{
		std::string Name;
		AssetHandle Texture = AssetHandle(0);
	};

	struct MaterialParameter
	{
		std::string Name;
		MaterialParameterType Type = MaterialParameterType::Float;
		std::array<uint32_t, 4> RawValue{};

		[[nodiscard]] bool AsBool() const noexcept;
		[[nodiscard]] int32_t AsInt() const noexcept;
		[[nodiscard]] float AsFloat(size_t component = 0) const noexcept;
		[[nodiscard]] size_t ComponentCount() const noexcept;
	};

	// Owning representation for runtime use. Unlike MaterialArtifactView, all
	// names remain valid after the input artifact byte buffer is released.
	struct MaterialArtifact
	{
		AssetHandle Shader = AssetHandle(0);
		std::vector<MaterialTextureBinding> Textures;
		std::vector<MaterialParameter> Parameters;
	};

	struct TypedAssetDependency
	{
		std::string Name;
		AssetHandle Handle = AssetHandle(0);
		AssetType ExpectedType = AssetType::None;
	};

	[[nodiscard]] bool IsMaterialArtifact(std::span<const uint8_t> bytes);
	[[nodiscard]] bool ParseMaterialArtifact(std::span<const uint8_t> bytes,
		MaterialArtifactView& artifact, std::string& error);
	[[nodiscard]] bool DecodeMaterialArtifact(std::span<const uint8_t> bytes,
		MaterialArtifact& artifact, std::string& error);
	[[nodiscard]] bool ParseMaterialSourceDependencies(std::span<const uint8_t> source,
		std::vector<TypedAssetDependency>& dependencies, std::string& error);
	[[nodiscard]] bool BuildMaterialArtifact(std::span<const uint8_t> source,
		std::vector<uint8_t>& artifact, std::string& error);

}
