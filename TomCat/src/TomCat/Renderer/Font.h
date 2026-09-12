#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Texture.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace TomCat {

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
		RuntimeFont(AssetHandle handle,
			std::vector<std::vector<uint8_t>> sourceChain);

		bool EnsureText(std::string_view utf8);
		AssetHandle GetHandle() const { return m_Handle; }
		const FontAtlasData& GetAtlas() const { return m_Atlas; }
		const Ref<Texture2D>& GetTexture() const { return m_Texture; }

	private:
		AssetHandle m_Handle{ 0 };
		std::vector<std::vector<uint8_t>> m_SourceChain;
		std::set<uint32_t> m_Codepoints;
		FontAtlasData m_Atlas;
		Ref<Texture2D> m_Texture;
	};

	class FontManager final
	{
	public:
		static FontManager& Get();
		Ref<RuntimeFont> Load(AssetHandle handle, std::string_view requiredText,
			AssetHandle fallbackFont = AssetHandle(0),
			AssetHandle emojiFont = AssetHandle(0));
		void Release(AssetHandle handle);
		void ReleaseAll();

	private:
		using FontChainKey = std::array<uint64_t, 3>;
		std::map<FontChainKey, Ref<RuntimeFont>> m_Fonts;
	};

}
