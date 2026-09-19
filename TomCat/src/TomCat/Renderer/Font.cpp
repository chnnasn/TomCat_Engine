#include "tcpch.h"
#include "Font.h"

#include "TomCat/Asset/AssetJobSystem.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/ApplicationPaths.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>

#include "TomCat/Utils/PathUtils.h"

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

		const std::array<BuiltInFontAsset, 1> kBuiltInFontAssets = {{
			{ AssetHandle(BuiltInLegacyRuntimeFontHandleValue), "Legacy Runtime",
				"fonts/opensans/OpenSans-Regular.ttf" }
		}};

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
			// OffsetY is the glyph bottom relative to the baseline. The previous
			// value placed almost the whole replacement box below the baseline,
			// so normal line clipping removed its lower half.
			glyph.OffsetY = -std::max(1,
				static_cast<int>(std::round(pixelHeight * 0.18f)));
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

	std::span<const BuiltInFontAsset> GetBuiltInFontAssets()
	{
		return kBuiltInFontAssets;
	}

	const BuiltInFontAsset* FindBuiltInFontAsset(AssetHandle handle)
	{
		const auto found = std::find_if(kBuiltInFontAssets.begin(),
			kBuiltInFontAssets.end(), [handle](const BuiltInFontAsset& asset)
			{
				return asset.Handle == handle;
			});
		return found == kBuiltInFontAssets.end() ? nullptr : &*found;
	}

	const BuiltInFontAsset* FindBuiltInFontAsset(std::string_view name)
	{
		const auto found = std::find_if(kBuiltInFontAssets.begin(),
			kBuiltInFontAssets.end(), [name](const BuiltInFontAsset& asset)
			{
				return asset.Name == name;
			});
		return found == kBuiltInFontAssets.end() ? nullptr : &*found;
	}

	std::filesystem::path GetBuiltInFontAssetPath(AssetHandle handle)
	{
		const BuiltInFontAsset* asset = FindBuiltInFontAsset(handle);
		return asset ? ApplicationPaths::ResolveRuntimePackageAsset(
			UTF8ToPath(asset->PackageRelativePath))
			: std::filesystem::path{};
	}

	AssetHandle GetDefaultRuntimeFontHandle()
	{
		return AssetHandle(BuiltInLegacyRuntimeFontHandleValue);
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
					// stb_truetype emits bitmap rows from top to bottom, while a raw
					// OpenGL upload maps the first row to v=0.  Runtime textures loaded
					// through stb_image are flipped before upload, so keep the generated
					// atlas in that same bottom-up convention.  The glyph UVs below are
					// expressed in the normal bottom-left OpenGL coordinate system.
					const uint32_t sourceY = placement.Y + 1
						+ static_cast<uint32_t>(row);
					const uint32_t py = candidate.Height - 1 - sourceY;
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

	RuntimeFont::RuntimeFont(AssetHandle handle, FontAtlasData atlas,
		Ref<Texture2D> texture)
		: m_Handle(handle), m_Atlas(std::move(atlas)),
		m_Texture(std::move(texture))
	{
	}

	struct FontManager::Impl final
	{
		using FontChainKey = std::array<uint64_t, 3>;
		static constexpr uint64_t MaximumFontChainBytes =
			64ULL * 1024ULL * 1024ULL;
		static constexpr uint64_t MaximumAtlasBytes =
			32ULL * 1024ULL * 1024ULL;
		static constexpr uint64_t MaximumRasterWorkingBytes =
			32ULL * 1024ULL * 1024ULL;

		struct FontChainState final
		{
			FontChainKey Key{};
			uint64_t Generation = 1;
			std::set<uint32_t> RequestedCodepoints;
			std::set<uint32_t> PublishedCodepoints;
			std::shared_ptr<AssetLoadCancellation> Cancellation =
				std::make_shared<AssetLoadCancellation>();
			Ref<RuntimeFont> Published;
			bool Backlogged = false;
			bool Preparing = false;
			bool Failed = false;
		};

		struct PreparedFont final
		{
			std::shared_ptr<FontChainState> State;
			uint64_t Generation = 0;
			std::set<uint32_t> Codepoints;
			FontAtlasData Atlas;
			std::string Warning;
			std::string Error;
		};

		mutable std::mutex Mutex;
		std::condition_variable PreparedCapacity;
		std::condition_variable JobsIdle;
		std::map<FontChainKey, std::shared_ptr<FontChainState>> Fonts;
		std::deque<std::shared_ptr<FontChainState>> Backlog;
		std::deque<PreparedFont> Prepared;
		uint64_t PreparedBytes = 0;
		size_t JobsInFlight = 0;

		bool IsCurrentLocked(const std::shared_ptr<FontChainState>& state,
			uint64_t generation) const
		{
			const auto found = Fonts.find(state->Key);
			return found != Fonts.end() && found->second == state
				&& state->Generation == generation;
		}

		void QueueIfNeededLocked(const std::shared_ptr<FontChainState>& state)
		{
			if (state->Failed || state->Backlogged || state->Preparing)
				return;
			const bool complete = state->Published
				&& std::includes(state->PublishedCodepoints.begin(),
					state->PublishedCodepoints.end(),
					state->RequestedCodepoints.begin(),
					state->RequestedCodepoints.end());
			if (complete)
				return;
			state->Backlogged = true;
			Backlog.push_back(state);
		}

		static void AppendWarning(std::string& target, std::string message)
		{
			if (!target.empty())
				target += "; ";
			target += std::move(message);
		}

		void FinishJob(const std::shared_ptr<FontChainState>& state,
			uint64_t generation, PreparedFont prepared)
		{
			const uint64_t byteCount = static_cast<uint64_t>(
				prepared.Atlas.PixelsRGBA.size());
			const AssetJobSystem::Limits limits = AssetJobSystem::Get().GetLimits();
			const uint64_t preparedBudget = (std::max<uint64_t>)(
				1024ULL * 1024ULL, limits.MemoryBudgetBytes / 2);
			const size_t preparedCountLimit = (std::max<size_t>)(1,
				static_cast<size_t>(limits.WorkerCount) * 2);

			std::unique_lock lock(Mutex);
			PreparedCapacity.wait(lock, [&]()
			{
				if (!IsCurrentLocked(state, generation))
					return true;
				if (Prepared.empty() && byteCount <= limits.MemoryBudgetBytes)
					return true;
				return Prepared.size() < preparedCountLimit
					&& byteCount <= preparedBudget
					&& PreparedBytes <= preparedBudget - byteCount;
			});
			if (IsCurrentLocked(state, generation))
			{
				PreparedBytes += byteCount;
				Prepared.emplace_back(std::move(prepared));
			}
			if (JobsInFlight > 0)
				--JobsInFlight;
			lock.unlock();
			JobsIdle.notify_all();
		}

		void PrepareFont(const std::shared_ptr<FontChainState>& state,
			uint64_t generation, std::set<uint32_t> codepoints,
			uint64_t taskBudget)
		{
			PreparedFont prepared;
			prepared.State = state;
			prepared.Generation = generation;
			prepared.Codepoints = std::move(codepoints);
			try
			{
				const uint64_t retainedForAtlasAndRasters = MaximumAtlasBytes
					+ MaximumRasterWorkingBytes;
				const uint64_t sourceBudget = (std::min)(MaximumFontChainBytes,
					taskBudget > retainedForAtlasAndRasters
						? taskBudget - retainedForAtlasAndRasters : taskBudget / 3);
				const std::array<AssetHandle, 3> handles = {
					AssetHandle(state->Key[0]), AssetHandle(state->Key[1]),
					AssetHandle(state->Key[2])
				};
				std::vector<std::vector<uint8_t>> sources(handles.size());
				uint64_t sourceBytes = 0;
				AssetManager& assets = AssetManager::Get();
				for (size_t index = 0; index < handles.size(); ++index)
				{
					bool current = false;
					{
						std::lock_guard lock(Mutex);
						current = IsCurrentLocked(state, generation);
					}
					if (!current)
					{
						FinishJob(state, generation, std::move(prepared));
						return;
					}
					if (static_cast<uint64_t>(handles[index]) == 0)
						continue;
					bool duplicate = false;
					for (size_t previous = 0; previous < index; ++previous)
						duplicate = duplicate || handles[previous] == handles[index];
					if (duplicate)
						continue;

					uint64_t expectedBytes = 0;
					CookedAssetRange range;
					if (assets.TryGetCookedAssetRange(handles[index], range))
					{
						if (range.Type != AssetType::Font)
						{
							AppendWarning(prepared.Warning, "font chain entry "
								+ std::to_string(index) + " has the wrong cooked type");
							continue;
						}
						expectedBytes = range.Size;
					}
					else if (const std::optional<AssetMetadata> metadata =
						assets.GetDatabase().GetMetadataSnapshot(handles[index]))
					{
						std::error_code error;
						const std::filesystem::path sourcePath =
							assets.Registry().GetAssetDirectory() / metadata->FilePath;
						expectedBytes = std::filesystem::file_size(sourcePath, error);
						if (error)
							expectedBytes = 0;
					}
					if (expectedBytes > sourceBudget - (std::min)(sourceBytes,
						sourceBudget))
					{
						AppendWarning(prepared.Warning, "font chain entry "
							+ std::to_string(index) + " exceeds the runtime memory budget");
						continue;
					}

					AssetLoadOptions options;
					options.Cancellation = state->Cancellation;
					FontLoadResult loaded = assets.LoadTypedArtifact<AssetType::Font>(
						handles[index], std::move(options));
					if (!loaded.Succeeded())
					{
						AppendWarning(prepared.Warning, "font chain entry "
							+ std::to_string(index) + " is unavailable");
						continue;
					}
					const uint64_t loadedBytes = static_cast<uint64_t>(
						loaded.Artifact.Bytes.size());
					if (loadedBytes > sourceBudget - (std::min)(sourceBytes,
						sourceBudget))
					{
						AppendWarning(prepared.Warning, "font chain entry "
							+ std::to_string(index) + " exceeds the runtime memory budget");
						continue;
					}
					sourceBytes += loadedBytes;
					sources[index] = std::move(loaded.Artifact.Bytes);
				}

				std::vector<std::span<const uint8_t>> sourceSpans;
				sourceSpans.reserve(sources.size());
				for (const std::vector<uint8_t>& source : sources)
					sourceSpans.emplace_back(source.data(), source.size());
				bool current = false;
				{
					std::lock_guard lock(Mutex);
					current = IsCurrentLocked(state, generation);
				}
				if (!current)
				{
					FinishJob(state, generation, std::move(prepared));
					return;
				}
				const std::vector<uint32_t> requested(prepared.Codepoints.begin(),
					prepared.Codepoints.end());
				if (!FontAtlasBuilder::Build(sourceSpans, requested, prepared.Atlas))
					prepared.Error = "font atlas construction failed";
				else
				{
					const uint64_t atlasBytes = static_cast<uint64_t>(
						prepared.Atlas.PixelsRGBA.size());
					if (atlasBytes > MaximumAtlasBytes || sourceBytes > taskBudget
						|| atlasBytes > taskBudget - sourceBytes)
					{
						prepared.Atlas = {};
						prepared.Error = "font atlas exceeds the runtime memory budget";
					}
				}
			}
			catch (const std::exception& exception)
			{
				prepared.Atlas = {};
				prepared.Error = exception.what();
			}
			catch (...)
			{
				prepared.Atlas = {};
				prepared.Error = "unknown font preparation failure";
			}
			FinishJob(state, generation, std::move(prepared));
		}

		void ScheduleBacklog()
		{
			const AssetJobSystem::Limits limits = AssetJobSystem::Get().GetLimits();
			const uint64_t taskBudget = (std::min)(limits.MemoryBudgetBytes,
				MaximumFontChainBytes + MaximumAtlasBytes
					+ MaximumRasterWorkingBytes);
			const size_t maximumPending = (std::max<size_t>)(1,
				static_cast<size_t>(limits.WorkerCount) * 2);
			for (;;)
			{
				std::shared_ptr<FontChainState> state;
				uint64_t generation = 0;
				std::set<uint32_t> codepoints;
				{
					std::lock_guard lock(Mutex);
					if (JobsInFlight >= maximumPending || Backlog.empty())
						return;
					state = Backlog.front();
					Backlog.pop_front();
					state->Backlogged = false;
					generation = state->Generation;
					if (!IsCurrentLocked(state, generation) || state->Preparing
						|| state->Failed)
						continue;
					state->Preparing = true;
					codepoints = state->RequestedCodepoints;
					++JobsInFlight;
				}

				const bool scheduled = AssetJobSystem::Get().TrySchedule(taskBudget,
					[this, state, generation, codepoints = std::move(codepoints),
						taskBudget]() mutable
					{
						PrepareFont(state, generation, std::move(codepoints),
							taskBudget);
					});
				if (scheduled)
					continue;

				std::lock_guard lock(Mutex);
				if (JobsInFlight > 0)
					--JobsInFlight;
				if (IsCurrentLocked(state, generation))
				{
					state->Preparing = false;
					state->Backlogged = true;
					Backlog.push_front(state);
				}
				JobsIdle.notify_all();
				return;
			}
		}
	};

	FontManager& FontManager::Get()
	{
		static FontManager manager;
		return manager;
	}

	FontManager::FontManager()
		: m_Impl(std::make_unique<Impl>())
	{
	}

	FontManager::~FontManager()
	{
		ReleaseAll();
	}

	Ref<RuntimeFont> FontManager::Load(AssetHandle handle,
		std::string_view requiredText, AssetHandle fallbackFont,
		AssetHandle emojiFont)
	{
		// Scenes authored before the built-in font handle existed serialized 0.
		// Keep those scenes readable and give every newly created text component a
		// real Latin font instead of a repeated procedural replacement box.
		if (static_cast<uint64_t>(handle) == 0)
			handle = GetDefaultRuntimeFontHandle();
		const Impl::FontChainKey key = { static_cast<uint64_t>(handle),
			static_cast<uint64_t>(fallbackFont), static_cast<uint64_t>(emojiFont) };
		const std::vector<uint32_t> decoded = FontAtlasBuilder::DecodeUTF8(
			requiredText);
		Ref<RuntimeFont> published;
		{
			std::lock_guard lock(m_Impl->Mutex);
			auto found = m_Impl->Fonts.find(key);
			if (found == m_Impl->Fonts.end())
			{
				auto state = std::make_shared<Impl::FontChainState>();
				state->Key = key;
				for (uint32_t codepoint = 32; codepoint <= 126; ++codepoint)
					state->RequestedCodepoints.insert(codepoint);
				state->RequestedCodepoints.insert(
					FontAtlasBuilder::ReplacementCodepoint);
				found = m_Impl->Fonts.emplace(key, std::move(state)).first;
			}
			for (uint32_t codepoint : decoded)
				found->second->RequestedCodepoints.insert(codepoint);
			m_Impl->QueueIfNeededLocked(found->second);
			published = found->second->Published;
		}
		// TrySchedule never waits for executor capacity. If the global queue is
		// full, the request remains in the FontManager backlog and the next frame
		// retries it from PumpPublishes.
		if (!AssetJobSystem::Get().IsWorkerThread())
			m_Impl->ScheduleBacklog();
		return published;
	}

	size_t FontManager::PumpPublishes(uint32_t maximumUploads,
		uint64_t maximumUploadBytes)
	{
		m_Impl->ScheduleBacklog();
		std::vector<Impl::PreparedFont> ready;
		uint64_t selectedBytes = 0;
		{
			std::lock_guard lock(m_Impl->Mutex);
			while (ready.size() < maximumUploads && !m_Impl->Prepared.empty())
			{
				const uint64_t nextBytes = static_cast<uint64_t>(
					m_Impl->Prepared.front().Atlas.PixelsRGBA.size());
				if (!ready.empty() && (nextBytes > maximumUploadBytes
					|| selectedBytes > maximumUploadBytes - nextBytes))
					break;
				selectedBytes += nextBytes;
				m_Impl->PreparedBytes -= nextBytes;
				ready.emplace_back(std::move(m_Impl->Prepared.front()));
				m_Impl->Prepared.pop_front();
			}
		}
		if (!ready.empty())
			m_Impl->PreparedCapacity.notify_all();

		size_t publishedCount = 0;
		for (Impl::PreparedFont& prepared : ready)
		{
			{
				std::lock_guard lock(m_Impl->Mutex);
				if (!m_Impl->IsCurrentLocked(prepared.State,
					prepared.Generation))
					continue;
			}

			Ref<Texture2D> texture;
			std::string error = std::move(prepared.Error);
			if (error.empty() && !prepared.Atlas.PixelsRGBA.empty()
				&& prepared.Atlas.PixelsRGBA.size()
					<= (std::numeric_limits<uint32_t>::max)())
			{
				try
				{
					texture = Texture2D::Create(prepared.Atlas.Width,
						prepared.Atlas.Height);
					if (texture && texture->IsLoaded())
						texture->SetData(prepared.Atlas.PixelsRGBA.data(),
							static_cast<uint32_t>(
								prepared.Atlas.PixelsRGBA.size()));
					else
						error = "font atlas texture creation failed";
				}
				catch (const std::exception& exception)
				{
					error = exception.what();
					texture.reset();
				}
				catch (...)
				{
					error = "unknown failure while publishing the font atlas";
					texture.reset();
				}
			}
			else if (error.empty())
				error = "font atlas pixels are invalid";

			Ref<RuntimeFont> runtimeFont;
			if (error.empty() && texture && texture->IsLoaded())
			{
				// Pixel staging is no longer needed once SetData returns. RuntimeFont
				// keeps only layout metadata and the GPU resource.
				std::vector<uint8_t>().swap(prepared.Atlas.PixelsRGBA);
				runtimeFont = CreateRef<RuntimeFont>(
					AssetHandle(prepared.State->Key[0]), std::move(prepared.Atlas),
					std::move(texture));
			}

			bool current = false;
			{
				std::lock_guard lock(m_Impl->Mutex);
				current = m_Impl->IsCurrentLocked(prepared.State,
					prepared.Generation);
				if (current)
				{
					prepared.State->Preparing = false;
					if (runtimeFont)
					{
						prepared.State->Published = std::move(runtimeFont);
						prepared.State->PublishedCodepoints =
							std::move(prepared.Codepoints);
						prepared.State->Failed = false;
						++publishedCount;
						m_Impl->QueueIfNeededLocked(prepared.State);
					}
					else
						prepared.State->Failed = true;
				}
			}
			if (!current)
				continue;
			if (!prepared.Warning.empty())
				TC_Core_Warn("Font {0} prepared with fallback glyphs: {1}",
					prepared.State->Key[0], prepared.Warning);
			if (!error.empty())
				TC_Core_Warn("Font {0} could not be published: {1}",
					prepared.State->Key[0], error);
		}

		m_Impl->ScheduleBacklog();
		return publishedCount;
	}

	FontStreamingStats FontManager::GetStreamingStats() const
	{
		std::lock_guard lock(m_Impl->Mutex);
		size_t publishedCount = 0;
		for (const auto& [key, state] : m_Impl->Fonts)
		{
			(void)key;
			publishedCount += state->Published ? 1u : 0u;
		}
		return { m_Impl->Backlog.size(), m_Impl->JobsInFlight,
			m_Impl->Prepared.size(), m_Impl->PreparedBytes, publishedCount };
	}

	void FontManager::Release(AssetHandle handle)
	{
		const uint64_t raw = static_cast<uint64_t>(handle) == 0
			? static_cast<uint64_t>(GetDefaultRuntimeFontHandle())
			: static_cast<uint64_t>(handle);
		std::unique_lock lock(m_Impl->Mutex);
		std::vector<std::shared_ptr<Impl::FontChainState>> removed;
		for (auto iterator = m_Impl->Fonts.begin();
			iterator != m_Impl->Fonts.end();)
		{
			if (std::find(iterator->first.begin(), iterator->first.end(), raw)
				!= iterator->first.end())
			{
				iterator->second->Cancellation->Cancel();
				++iterator->second->Generation;
				removed.push_back(iterator->second);
				iterator = m_Impl->Fonts.erase(iterator);
			}
			else
				++iterator;
		}
		const auto wasRemoved = [&removed](const auto& state)
		{
			return std::find(removed.begin(), removed.end(), state)
				!= removed.end();
		};
		std::erase_if(m_Impl->Backlog, wasRemoved);
		std::erase_if(m_Impl->Prepared,
			[&](const Impl::PreparedFont& prepared)
			{
				if (!wasRemoved(prepared.State))
					return false;
				m_Impl->PreparedBytes -= static_cast<uint64_t>(
					prepared.Atlas.PixelsRGBA.size());
				return true;
			});
		lock.unlock();
		m_Impl->PreparedCapacity.notify_all();
	}

	void FontManager::ReleaseAll()
	{
		std::unique_lock lock(m_Impl->Mutex);
		for (auto& [key, state] : m_Impl->Fonts)
		{
			(void)key;
			state->Cancellation->Cancel();
			++state->Generation;
		}
		m_Impl->Fonts.clear();
		m_Impl->Backlog.clear();
		m_Impl->Prepared.clear();
		m_Impl->PreparedBytes = 0;
		const bool hasJobs = m_Impl->JobsInFlight != 0;
		lock.unlock();
		m_Impl->PreparedCapacity.notify_all();
		if (!hasJobs || AssetJobSystem::Get().IsWorkerThread())
			return;
		lock.lock();
		m_Impl->JobsIdle.wait(lock,
			[this]() { return m_Impl->JobsInFlight == 0; });
	}

}
