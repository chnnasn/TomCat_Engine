#include "tcpch.h"
#include "Font.h"

#include "TomCat/Asset/AssetManager.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4701)
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace TomCat {

	namespace {

		struct RasterGlyph
		{
			uint32_t Codepoint = 0;
			int Width = 0;
			int Height = 0;
			int OffsetX = 0;
			int OffsetY = 0;
			float Advance = 0.0f;
			std::vector<uint8_t> Alpha;
			bool Procedural = false;
			uint32_t SourceIndex = 0;
		};

		uint32_t NextPowerOfTwo(uint32_t value)
		{
			value = std::max(1u, value);
			--value;
			value |= value >> 1;
			value |= value >> 2;
			value |= value >> 4;
			value |= value >> 8;
			value |= value >> 16;
			return value + 1;
		}

		RasterGlyph MakeProceduralFallback(float pixelHeight)
		{
			RasterGlyph glyph;
			glyph.Codepoint = FontAtlasBuilder::ReplacementCodepoint;
			glyph.Width = std::max(8, static_cast<int>(std::round(pixelHeight * 0.55f)));
			glyph.Height = std::max(10, static_cast<int>(std::round(pixelHeight * 0.76f)));
			glyph.OffsetX = 1;
			glyph.OffsetY = -glyph.Height + std::max(1,
				static_cast<int>(std::round(pixelHeight * 0.12f)));
			glyph.Advance = static_cast<float>(glyph.Width + 3);
			glyph.Alpha.assign(static_cast<size_t>(glyph.Width) * glyph.Height, 0);
			const int stroke = std::max(1, glyph.Width / 12);
			for (int y = 0; y < glyph.Height; ++y)
			{
				for (int x = 0; x < glyph.Width; ++x)
				{
					const bool border = x < stroke || y < stroke
						|| x >= glyph.Width - stroke || y >= glyph.Height - stroke;
					const int diagonal = (x * glyph.Height) / std::max(1, glyph.Width);
					const bool cross = std::abs(y - diagonal) < stroke
						|| std::abs((glyph.Height - 1 - y) - diagonal) < stroke;
					if (border || cross)
						glyph.Alpha[static_cast<size_t>(y) * glyph.Width + x] = 255;
				}
			}
			glyph.Procedural = true;
			glyph.SourceIndex = (std::numeric_limits<uint32_t>::max)();
			return glyph;
		}

		bool Rasterize(stbtt_fontinfo& info, float scale, uint32_t codepoint,
			uint32_t sourceIndex, RasterGlyph& glyph)
		{
			if (stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint)) == 0)
				return false;
			int advance = 0;
			int bearing = 0;
			stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint),
				&advance, &bearing);
			(void)bearing;
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			stbtt_GetCodepointBitmapBox(&info, static_cast<int>(codepoint), scale,
				scale, &x0, &y0, &x1, &y1);
			const int64_t rasterWidth = static_cast<int64_t>(x1) - x0;
			const int64_t rasterHeight = static_cast<int64_t>(y1) - y0;
			// Font files are untrusted project input. Reject pathological bitmap
			// boxes before narrowing or multiplying allocation dimensions.
			if (rasterWidth < 0 || rasterHeight < 0
				|| rasterWidth > 2048 || rasterHeight > 2048)
				return false;
			glyph.Codepoint = codepoint;
			glyph.Width = static_cast<int>(rasterWidth);
			glyph.Height = static_cast<int>(rasterHeight);
			glyph.OffsetX = x0;
			glyph.OffsetY = -y1;
			glyph.Advance = std::max(0.0f, static_cast<float>(advance) * scale);
			glyph.SourceIndex = sourceIndex;
			if (glyph.Width == 0 || glyph.Height == 0)
				return true;
			glyph.Alpha.assign(static_cast<size_t>(glyph.Width) * glyph.Height, 0);
			stbtt_MakeCodepointBitmap(&info, glyph.Alpha.data(), glyph.Width,
				glyph.Height, glyph.Width, scale, scale, static_cast<int>(codepoint));
			return true;
		}

		uint64_t HashAtlas(const FontAtlasData& atlas)
		{
			uint64_t hash = 1469598103934665603ULL;
			auto append = [&hash](const void* data, size_t size)
			{
				const auto* bytes = static_cast<const uint8_t*>(data);
				for (size_t index = 0; index < size; ++index)
				{
					hash ^= bytes[index];
					hash *= 1099511628211ULL;
				}
			};
			append(&atlas.Width, sizeof(atlas.Width));
			append(&atlas.Height, sizeof(atlas.Height));
			for (const auto& [codepoint, glyph] : atlas.Glyphs)
			{
				append(&codepoint, sizeof(codepoint));
				append(&glyph.Advance, sizeof(glyph.Advance));
				append(&glyph.OffsetX, sizeof(glyph.OffsetX));
				append(&glyph.OffsetY, sizeof(glyph.OffsetY));
				append(&glyph.Width, sizeof(glyph.Width));
				append(&glyph.Height, sizeof(glyph.Height));
				append(&glyph.UVMin, sizeof(glyph.UVMin));
				append(&glyph.UVMax, sizeof(glyph.UVMax));
				append(&glyph.UsesFallback, sizeof(glyph.UsesFallback));
				append(&glyph.SourceIndex, sizeof(glyph.SourceIndex));
				append(&glyph.IsProceduralFallback,
					sizeof(glyph.IsProceduralFallback));
				append(&glyph.AlphaCoverage, sizeof(glyph.AlphaCoverage));
			}
			append(atlas.PixelsRGBA.data(), atlas.PixelsRGBA.size());
			return hash;
		}

	}

	const FontGlyph* FontAtlasData::Find(uint32_t codepoint) const
	{
		auto found = Glyphs.find(codepoint);
		if (found != Glyphs.end())
			return &found->second;
		found = Glyphs.find(FontAtlasBuilder::ReplacementCodepoint);
		return found == Glyphs.end() ? nullptr : &found->second;
	}

	std::vector<uint32_t> FontAtlasBuilder::DecodeUTF8(std::string_view text,
		bool* wasValid)
	{
		std::vector<uint32_t> result;
		result.reserve(text.size());
		bool valid = true;
		for (size_t index = 0; index < text.size();)
		{
			const uint8_t first = static_cast<uint8_t>(text[index]);
			uint32_t codepoint = 0;
			size_t count = 0;
			if (first < 0x80u)
			{
				codepoint = first;
				count = 1;
			}
			else if (first >= 0xc2u && first <= 0xdfu)
			{
				codepoint = first & 0x1fu;
				count = 2;
			}
			else if (first >= 0xe0u && first <= 0xefu)
			{
				codepoint = first & 0x0fu;
				count = 3;
			}
			else if (first >= 0xf0u && first <= 0xf4u)
			{
				codepoint = first & 0x07u;
				count = 4;
			}

			bool sequenceValid = count != 0 && index + count <= text.size();
			for (size_t continuation = 1; sequenceValid && continuation < count;
				++continuation)
			{
				const uint8_t value = static_cast<uint8_t>(text[index + continuation]);
				sequenceValid = (value & 0xc0u) == 0x80u;
				codepoint = (codepoint << 6) | (value & 0x3fu);
			}
			if (sequenceValid)
			{
				const uint32_t minimum = count == 1 ? 0u : count == 2 ? 0x80u
					: count == 3 ? 0x800u : 0x10000u;
				sequenceValid = codepoint >= minimum && codepoint <= 0x10ffffu
					&& !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
			}
			if (!sequenceValid)
			{
				valid = false;
				result.push_back(ReplacementCodepoint);
				++index;
				continue;
			}
			result.push_back(codepoint);
			index += count;
		}
		if (wasValid)
			*wasValid = valid;
		return result;
	}

	bool FontAtlasBuilder::Build(std::span<const uint8_t> sourceBytes,
		std::span<const uint32_t> requestedCodepoints, FontAtlasData& output,
		float pixelHeight)
	{
		const std::array<std::span<const uint8_t>, 1> sourceChain = { sourceBytes };
		return Build(std::span<const std::span<const uint8_t>>(sourceChain),
			requestedCodepoints, output, pixelHeight);
	}

	bool FontAtlasBuilder::Build(
		std::span<const std::span<const uint8_t>> sourceChain,
		std::span<const uint32_t> requestedCodepoints, FontAtlasData& output,
		float pixelHeight)
	{
		if (!std::isfinite(pixelHeight) || pixelHeight < 8.0f || pixelHeight > 256.0f)
			return false;
		if (sourceChain.size() > 16)
			return false;
		std::set<uint32_t> codepoints;
		for (uint32_t value : requestedCodepoints)
		{
			if (value <= 0x10ffffu && !(value >= 0xd800u && value <= 0xdfffu))
			{
				codepoints.insert(value);
				if (codepoints.size() > 65536)
					return false;
			}
		}
		codepoints.insert(static_cast<uint32_t>('?'));
		codepoints.insert(ReplacementCodepoint);

		FontAtlasData candidate;
		candidate.PixelHeight = pixelHeight;
		candidate.Ascent = pixelHeight * 0.75f;
		candidate.Descent = -pixelHeight * 0.25f;
		candidate.LineHeight = pixelHeight;
		struct FontFace
		{
			stbtt_fontinfo Info{};
			float Scale = 1.0f;
			uint32_t SourceIndex = 0;
		};
		std::vector<FontFace> faces;
		faces.reserve(sourceChain.size());
		bool metricsAssigned = false;
		for (size_t sourceIndex = 0; sourceIndex < sourceChain.size(); ++sourceIndex)
		{
			const std::span<const uint8_t> sourceBytes = sourceChain[sourceIndex];
			if (sourceBytes.empty() || sourceBytes.size() >
				static_cast<size_t>((std::numeric_limits<int>::max)()))
				continue;
			FontFace face;
			face.SourceIndex = static_cast<uint32_t>(sourceIndex);
			const int offset = stbtt_GetFontOffsetForIndex(sourceBytes.data(), 0);
			if (offset < 0 || !stbtt_InitFont(&face.Info, sourceBytes.data(), offset))
				continue;
			face.Scale = stbtt_ScaleForPixelHeight(&face.Info, pixelHeight);
			if (!std::isfinite(face.Scale) || face.Scale <= 0.0f)
				continue;
			if (!metricsAssigned)
			{
				int ascent = 0, descent = 0, gap = 0;
				stbtt_GetFontVMetrics(&face.Info, &ascent, &descent, &gap);
				const float lineHeight = static_cast<float>(ascent - descent + gap)
					* face.Scale;
				if (std::isfinite(lineHeight) && lineHeight > 0.0f)
				{
					candidate.Ascent = static_cast<float>(ascent) * face.Scale;
					candidate.Descent = static_cast<float>(descent) * face.Scale;
					candidate.LineHeight = lineHeight;
					metricsAssigned = true;
				}
			}
			if (sourceIndex == 0)
				candidate.UsesSourceFont = true;
			faces.push_back(face);
		}

		RasterGlyph fallback = MakeProceduralFallback(pixelHeight);
		for (FontFace& face : faces)
		{
			RasterGlyph sourceFallback;
			if (Rasterize(face.Info, face.Scale, ReplacementCodepoint,
				face.SourceIndex, sourceFallback)
				|| Rasterize(face.Info, face.Scale, static_cast<uint32_t>('?'),
					face.SourceIndex, sourceFallback))
			{
				fallback = std::move(sourceFallback);
				break;
			}
		}

		std::vector<RasterGlyph> rasters;
		rasters.reserve(codepoints.size());
		rasters.push_back(fallback);
		std::map<uint32_t, size_t> rasterIndices;
		rasterIndices[ReplacementCodepoint] = 0;
		for (uint32_t codepoint : codepoints)
		{
			if (codepoint == ReplacementCodepoint)
				continue;
			bool foundGlyph = false;
			for (FontFace& face : faces)
			{
				RasterGlyph glyph;
				if (!Rasterize(face.Info, face.Scale, codepoint,
					face.SourceIndex, glyph))
					continue;
				rasterIndices[codepoint] = rasters.size();
				rasters.push_back(std::move(glyph));
				foundGlyph = true;
				break;
			}
			if (!foundGlyph)
				rasterIndices[codepoint] = 0;
		}

		uint64_t area = 0;
		for (const RasterGlyph& glyph : rasters)
			area += static_cast<uint64_t>(glyph.Width + 2) * (glyph.Height + 2);
		const uint32_t estimated = static_cast<uint32_t>(std::ceil(std::sqrt(
			static_cast<double>(std::max<uint64_t>(area, 1))) * 1.4));
		candidate.Width = std::clamp(NextPowerOfTwo(estimated), 128u, 2048u);

		struct Placement { uint32_t X = 0, Y = 0; };
		std::vector<Placement> placements(rasters.size());
		uint32_t x = 1, rowHeight = 0;
		uint64_t y = 1;
		for (size_t index = 0; index < rasters.size(); ++index)
		{
			const RasterGlyph& glyph = rasters[index];
			if (glyph.Width == 0 || glyph.Height == 0)
				continue;
			const uint32_t width = static_cast<uint32_t>(glyph.Width) + 2;
			const uint32_t height = static_cast<uint32_t>(glyph.Height) + 2;
			if (width + 2 > candidate.Width)
				return false;
			if (x + width > candidate.Width)
			{
				x = 1;
				y += rowHeight;
				rowHeight = 0;
			}
			if (y > (std::numeric_limits<uint32_t>::max)())
				return false;
			placements[index] = { x, static_cast<uint32_t>(y) };
			x += width;
			rowHeight = std::max(rowHeight, height);
		}
		if (y > (std::numeric_limits<uint64_t>::max)() - rowHeight - 1)
			return false;
		const uint64_t requiredHeight = y + rowHeight + 1;
		// Never clamp a required height down: placements below are already fixed,
		// so truncation would turn the pixel copy into an out-of-bounds write.
		if (requiredHeight > 4096)
			return false;
		candidate.Height = std::clamp(NextPowerOfTwo(
			static_cast<uint32_t>(requiredHeight)), 32u, 4096u);
		const uint64_t pixelBytes = static_cast<uint64_t>(candidate.Width)
			* candidate.Height * 4;
		if (pixelBytes > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())
			|| pixelBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)()))
			return false;
		candidate.PixelsRGBA.assign(static_cast<size_t>(pixelBytes), 0);
		for (size_t index = 0; index < rasters.size(); ++index)
		{
			const RasterGlyph& raster = rasters[index];
			if (raster.Width == 0 || raster.Height == 0)
				continue;
			const Placement placement = placements[index];
			for (int row = 0; row < raster.Height; ++row)
			{
				for (int column = 0; column < raster.Width; ++column)
				{
					const uint32_t px = placement.X + 1 + static_cast<uint32_t>(column);
					const uint32_t py = placement.Y + 1 + static_cast<uint32_t>(row);
					const size_t destination = (static_cast<size_t>(py) * candidate.Width + px) * 4;
					const size_t source = static_cast<size_t>(row)
						* raster.Width + column;
					if (source >= raster.Alpha.size()
						|| destination > candidate.PixelsRGBA.size() - 4)
						return false;
					const uint8_t alpha = raster.Alpha[source];
					candidate.PixelsRGBA[destination + 0] = 255;
					candidate.PixelsRGBA[destination + 1] = 255;
					candidate.PixelsRGBA[destination + 2] = 255;
					candidate.PixelsRGBA[destination + 3] = alpha;
				}
			}
		}

		for (uint32_t codepoint : codepoints)
		{
			const size_t rasterIndex = rasterIndices.contains(codepoint)
				? rasterIndices.at(codepoint) : 0;
			const RasterGlyph& raster = rasters[rasterIndex];
			const Placement placement = placements[rasterIndex];
			FontGlyph glyph;
			glyph.Codepoint = codepoint;
			glyph.Advance = raster.Advance;
			glyph.OffsetX = static_cast<float>(raster.OffsetX);
			glyph.OffsetY = static_cast<float>(raster.OffsetY);
			glyph.Width = static_cast<float>(raster.Width);
			glyph.Height = static_cast<float>(raster.Height);
			glyph.SourceIndex = raster.SourceIndex;
			glyph.IsProceduralFallback = raster.Procedural;
			glyph.AlphaCoverage = static_cast<uint32_t>(std::count_if(
				raster.Alpha.begin(), raster.Alpha.end(),
				[](uint8_t alpha) { return alpha != 0; }));
			glyph.UsesFallback = raster.SourceIndex != 0 || raster.Procedural
				|| (rasterIndex == 0 && codepoint != ReplacementCodepoint);
			if (raster.Width > 0 && raster.Height > 0)
			{
				const float left = static_cast<float>(placement.X + 1);
				const float top = static_cast<float>(placement.Y + 1);
				glyph.UVMin = { left / candidate.Width,
					1.0f - (top + raster.Height) / candidate.Height };
				glyph.UVMax = { (left + raster.Width) / candidate.Width,
					1.0f - top / candidate.Height };
			}
			candidate.Glyphs.emplace(codepoint, glyph);
		}
		candidate.DeterministicHash = HashAtlas(candidate);
		output = std::move(candidate);
		return true;
	}

	RuntimeFont::RuntimeFont(AssetHandle handle,
		std::vector<std::vector<uint8_t>> sourceChain)
		: m_Handle(handle), m_SourceChain(std::move(sourceChain))
	{
		for (uint32_t codepoint = 32; codepoint <= 126; ++codepoint)
			m_Codepoints.insert(codepoint);
		m_Codepoints.insert(FontAtlasBuilder::ReplacementCodepoint);
	}

	bool RuntimeFont::EnsureText(std::string_view utf8)
	{
		const std::vector<uint32_t> decoded = FontAtlasBuilder::DecodeUTF8(utf8);
		std::set<uint32_t> candidateCodepoints = m_Codepoints;
		bool changed = m_Atlas.Glyphs.empty();
		for (uint32_t codepoint : decoded)
			changed = candidateCodepoints.emplace(codepoint).second || changed;
		if (!changed)
			return m_Texture && m_Texture->IsLoaded();

		const std::vector<uint32_t> requested(candidateCodepoints.begin(),
			candidateCodepoints.end());
		std::vector<std::span<const uint8_t>> sources;
		sources.reserve(m_SourceChain.size());
		for (const std::vector<uint8_t>& source : m_SourceChain)
			sources.emplace_back(source.data(), source.size());
		FontAtlasData atlas;
		if (!FontAtlasBuilder::Build(sources, requested, atlas))
			return false;
		Ref<Texture2D> texture = Texture2D::Create(atlas.Width, atlas.Height);
		if (!texture || !texture->IsLoaded()
			|| atlas.PixelsRGBA.size() > std::numeric_limits<uint32_t>::max())
			return false;
		texture->SetData(atlas.PixelsRGBA.data(),
			static_cast<uint32_t>(atlas.PixelsRGBA.size()));
		// Commit the requested codepoint set only after both atlas construction
		// and GPU publication succeed. A failed growth attempt must remain
		// retryable and must never make an older atlas appear complete.
		m_Codepoints = std::move(candidateCodepoints);
		m_Atlas = std::move(atlas);
		m_Texture = std::move(texture);
		return true;
	}

	FontManager& FontManager::Get()
	{
		static FontManager manager;
		return manager;
	}

	Ref<RuntimeFont> FontManager::Load(AssetHandle handle,
		std::string_view requiredText, AssetHandle fallbackFont,
		AssetHandle emojiFont)
	{
		const FontChainKey key = { static_cast<uint64_t>(handle),
			static_cast<uint64_t>(fallbackFont), static_cast<uint64_t>(emojiFont) };
		auto found = m_Fonts.find(key);
		if (found == m_Fonts.end())
		{
			const std::array<AssetHandle, 3> handles = {
				handle, fallbackFont, emojiFont
			};
			std::vector<std::vector<uint8_t>> sources(handles.size());
			for (size_t index = 0; index < handles.size(); ++index)
			{
				if (static_cast<uint64_t>(handles[index]) == 0)
					continue;
				bool duplicate = false;
				for (size_t previous = 0; previous < index; ++previous)
					duplicate = duplicate || handles[previous] == handles[index];
				if (duplicate)
					continue;
				AssetLoadResult loaded = AssetManager::Get().LoadImportedArtifact(
					handles[index]);
				if (loaded.Succeeded() && loaded.Artifact.Type == AssetType::Font)
					sources[index] = std::move(loaded.Artifact.Bytes);
				else
					TC_Core_Warn("Font chain entry {0} is unavailable: {1}", index,
						static_cast<uint64_t>(handles[index]));
			}
			found = m_Fonts.emplace(key,
				CreateRef<RuntimeFont>(handle, std::move(sources))).first;
		}
		if (!found->second->EnsureText(requiredText))
			return nullptr;
		return found->second;
	}

	void FontManager::Release(AssetHandle handle)
	{
		const uint64_t raw = static_cast<uint64_t>(handle);
		for (auto iterator = m_Fonts.begin(); iterator != m_Fonts.end();)
		{
			if (std::find(iterator->first.begin(), iterator->first.end(), raw)
				!= iterator->first.end())
				iterator = m_Fonts.erase(iterator);
			else
				++iterator;
		}
	}

	void FontManager::ReleaseAll()
	{
		m_Fonts.clear();
	}

}
