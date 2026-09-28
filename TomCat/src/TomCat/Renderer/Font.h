#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace TomCat {

	struct BuiltInFontAsset
	{
		AssetHandle Handle = AssetHandle(0);
		std::string_view Name;
		std::string_view PackageRelativePath;
	};

	// Built-in fonts are immutable package resources with stable handles. Font=0
	// remains a backwards-compatible alias for Legacy Runtime at render time.
	[[nodiscard]] std::span<const BuiltInFontAsset> GetBuiltInFontAssets();
	[[nodiscard]] const BuiltInFontAsset* FindBuiltInFontAsset(AssetHandle handle);
	[[nodiscard]] const BuiltInFontAsset* FindBuiltInFontAsset(std::string_view name);
	[[nodiscard]] std::filesystem::path GetBuiltInFontAssetPath(AssetHandle handle);
	[[nodiscard]] AssetHandle GetDefaultRuntimeFontHandle();

	struct FontGlyph
	{
		uint32_t Codepoint = 0;
		float Advance = 0.0f;
		float OffsetX = 0.0f;
		float OffsetY = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
		glm::vec2 UVMin{ 0.0f };
		glm::vec2 UVMax{ 0.0f };
		bool UsesFallback = false;
		uint32_t SourceIndex = 0;
		bool IsProceduralFallback = false;
		uint32_t AlphaCoverage = 0;
	};

	struct FontAtlasData
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		float PixelHeight = 64.0f;
		float Ascent = 48.0f;
		float Descent = -16.0f;
		float LineHeight = 64.0f;
		bool UsesSourceFont = false;
		std::vector<uint8_t> PixelsRGBA;
		std::map<uint32_t, FontGlyph> Glyphs;
		uint64_t DeterministicHash = 0;

		const FontGlyph* Find(uint32_t codepoint) const;
	};

	class FontAtlasBuilder final
	{
	public:
		static constexpr uint32_t ReplacementCodepoint = 0xfffdu;
		static constexpr float DefaultPixelHeight = 64.0f;

		// Invalid UTF-8 is replaced one byte at a time with U+FFFD. This keeps
		// gameplay text renderable without accepting overlong sequences, UTF-16
		// surrogate values, or codepoints above U+10FFFF.
		static std::vector<uint32_t> DecodeUTF8(std::string_view text,
			bool* wasValid = nullptr);
		static bool Build(std::span<const uint8_t> sourceBytes,
			std::span<const uint32_t> requestedCodepoints, FontAtlasData& output,
			float pixelHeight = DefaultPixelHeight);
		static bool Build(
			std::span<const std::span<const uint8_t>> sourceChain,
			std::span<const uint32_t> requestedCodepoints, FontAtlasData& output,
			float pixelHeight = DefaultPixelHeight);
	};

	class RuntimeFont final
	{
	public:
		RuntimeFont(AssetHandle handle, FontAtlasData atlas,
			Ref<Texture2D> texture);

		AssetHandle GetHandle() const { return m_Handle; }
		const FontAtlasData& GetAtlas() const { return m_Atlas; }
		const Ref<Texture2D>& GetTexture() const { return m_Texture; }

	private:
		AssetHandle m_Handle{ 0 };
		FontAtlasData m_Atlas;
		Ref<Texture2D> m_Texture;
	};

	struct FontStreamingStats
	{
		size_t BacklogCount = 0;
		size_t JobsInFlight = 0;
		size_t PreparedCount = 0;
		uint64_t PreparedBytes = 0;
		size_t PublishedCount = 0;
	};

	class FontManager final
	{
	public:
		static FontManager& Get();
		Ref<RuntimeFont> Load(AssetHandle handle, std::string_view requiredText,
			AssetHandle fallbackFont = AssetHandle(0),
			AssetHandle emojiFont = AssetHandle(0));
		// Workers read imported font artifacts and rasterize glyphs. The application
		// thread calls this once per frame to meter Texture2D creation and atomically
		// publish completed atlases. A growing font keeps its previous atlas alive.
		size_t PumpPublishes(uint32_t maximumUploads = 2,
			uint64_t maximumUploadBytes = 16ULL * 1024ULL * 1024ULL);
		FontStreamingStats GetStreamingStats() const;
		void Release(AssetHandle handle);
		void ReleaseAll();

	private:
		FontManager();
		~FontManager();
		FontManager(const FontManager&) = delete;
		FontManager& operator=(const FontManager&) = delete;

		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

}
