#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Components.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	inline constexpr uint32_t Advanced2DAuthoringAssetSchemaVersion = 1;

	struct AnimationClipAsset
	{
		uint32_t Version = Advanced2DAuthoringAssetSchemaVersion;
		float SampleRate = 12.0f;
		SpriteAnimationClip Clip;
	};

	struct AnimatorControllerAsset
	{
		uint32_t Version = Advanced2DAuthoringAssetSchemaVersion;
		std::string InitialState;
		std::vector<AnimatorParameter> Parameters;
		struct State
		{
			std::string Name;
			AssetHandle ClipHandle = AssetHandle(0);
			float Speed = 1.0f;
		};
		std::vector<State> States;
		std::vector<AnimatorTransition> Transitions;
	};

	struct TilePaletteEntry
	{
		glm::ivec2 Coordinate{ 0, 0 };
		AssetHandle SpriteHandle = AssetHandle(0);
	};

	struct TilePaletteAsset
	{
		uint32_t Version = Advanced2DAuthoringAssetSchemaVersion;
		glm::vec2 CellSize{ 1.0f, 1.0f };
		glm::vec2 CellGap{ 0.0f, 0.0f };
		std::vector<TilePaletteEntry> Tiles;
	};

	enum class AuthoringAssetReferenceKind : uint8_t
	{
		AnimationFrame,
		AnimatorStateMotion,
		TilePaletteEntry
	};

	struct AuthoringAssetReference
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType ExpectedType = AssetType::None;
		AuthoringAssetReferenceKind Kind =
			AuthoringAssetReferenceKind::AnimationFrame;
		std::string PropertyPath;
		bool Required = true;
	};

	using AuthoringAssetReferenceVisitor =
		std::function<bool(const AuthoringAssetReference&)>;

	class AnimationClipAssetCodec final
	{
	public:
		static bool Encode(const AnimationClipAsset& asset, std::string& document,
			std::string& error);
		static bool Decode(std::string_view document, AnimationClipAsset& asset,
			std::string& error);
		static bool Decode(std::span<const uint8_t> bytes, AnimationClipAsset& asset,
			std::string& error);
		static bool Validate(const AnimationClipAsset& asset, std::string& error);
		static bool VisitAssetReferences(const AnimationClipAsset& asset,
			const AuthoringAssetReferenceVisitor& visitor, std::string& error);
	};

	class AnimatorControllerAssetCodec final
	{
	public:
		static bool Encode(const AnimatorControllerAsset& asset,
			std::string& document, std::string& error);
		static bool Decode(std::string_view document, AnimatorControllerAsset& asset,
			std::string& error);
		static bool Decode(std::span<const uint8_t> bytes,
			AnimatorControllerAsset& asset, std::string& error);
		static bool Validate(const AnimatorControllerAsset& asset,
			std::string& error);
		static bool VisitAssetReferences(const AnimatorControllerAsset& asset,
			const AuthoringAssetReferenceVisitor& visitor, std::string& error);
	};

	class TilePaletteAssetCodec final
	{
	public:
		static bool Encode(const TilePaletteAsset& asset, std::string& document,
			std::string& error);
		static bool Decode(std::string_view document, TilePaletteAsset& asset,
			std::string& error);
		static bool Decode(std::span<const uint8_t> bytes, TilePaletteAsset& asset,
			std::string& error);
		static bool Validate(const TilePaletteAsset& asset, std::string& error);
		static bool VisitAssetReferences(const TilePaletteAsset& asset,
			const AuthoringAssetReferenceVisitor& visitor, std::string& error);
	};

}
