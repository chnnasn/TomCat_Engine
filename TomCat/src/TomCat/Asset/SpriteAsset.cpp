#include "tcpch.h"
#include "SpriteAsset.h"

#include "AssetManager.h"
#include "AssetRegistry.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace TomCat {

	namespace {

		constexpr std::array<uint8_t, 8> kCookedSpriteMagic = {
			'T', 'C', 'S', 'P', 'R', '0', '0', '2' };
		const std::array<BuiltInSpriteAsset, 2> kBuiltInSpriteAssets = {{
			{ AssetHandle(BuiltInCircleSpriteHandleValue), "Circle",
				"Resources/Sprites/TomCat/Circle.tga" },
			{ AssetHandle(BuiltInSquareSpriteHandleValue), "Square",
				"Resources/Sprites/TomCat/Square.tga" }
		}};

		void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
		}

		void AppendU64(std::vector<uint8_t>& bytes, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffULL));
		}

		void AppendFloat(std::vector<uint8_t>& bytes, float value)
		{
			AppendU32(bytes, std::bit_cast<uint32_t>(value));
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t& offset, uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 4; ++index)
				value |= static_cast<uint32_t>(bytes[offset++]) << (index * 8);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> bytes, size_t& offset, uint64_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 8)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 8; ++index)
				value |= static_cast<uint64_t>(bytes[offset++]) << (index * 8);
			return true;
		}

		bool ReadFloat(std::span<const uint8_t> bytes, size_t& offset, float& value)
		{
			uint32_t bits = 0;
			if (!ReadU32(bytes, offset, bits))
				return false;
			value = std::bit_cast<float>(bits);
			return std::isfinite(value);
		}

		template<typename T>
		bool ParseNumber(std::string_view text, T& value)
		{
			if (text.empty())
				return false;
			const char* begin = text.data();
			const char* end = begin + text.size();
			const auto parsed = std::from_chars(begin, end, value);
			return parsed.ec == std::errc{} && parsed.ptr == end;
		}

		template<size_t Count, typename T>
		bool ParseTuple(std::string_view text, std::array<T, Count>& values)
		{
			size_t begin = 0;
			for (size_t index = 0; index < Count; ++index)
			{
				const size_t comma = text.find(',', begin);
				const bool final = index + 1 == Count;
				if ((final && comma != std::string_view::npos)
					|| (!final && comma == std::string_view::npos))
					return false;
				const size_t end = final ? text.size() : comma;
				if (!ParseNumber(text.substr(begin, end - begin), values[index]))
					return false;
				begin = end + 1;
			}
			return true;
		}

		bool ValidateSpriteData(const SpriteSubAssetData& value)
		{
			return value.Width != 0 && value.Height != 0
				&& std::isfinite(value.PivotX) && value.PivotX >= 0.0f
				&& value.PivotX <= 1.0f && std::isfinite(value.PivotY)
				&& value.PivotY >= 0.0f && value.PivotY <= 1.0f
				&& std::isfinite(value.PixelsPerUnit) && value.PixelsPerUnit > 0.0f
				&& std::isfinite(value.BorderLeft) && value.BorderLeft >= 0.0f
				&& std::isfinite(value.BorderBottom) && value.BorderBottom >= 0.0f
				&& std::isfinite(value.BorderRight) && value.BorderRight >= 0.0f
				&& std::isfinite(value.BorderTop) && value.BorderTop >= 0.0f
				&& value.BorderLeft + value.BorderRight <= static_cast<float>(value.Width)
				&& value.BorderBottom + value.BorderTop <= static_cast<float>(value.Height);
		}

		bool ValidateRGBA(std::span<const uint8_t> pixels, uint32_t width,
			uint32_t height, std::string& error)
		{
			if (width == 0 || height == 0)
			{
				error = "Sprite Atlas image dimensions must be non-zero";
				return false;
			}
			const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
			if (pixelCount > (std::numeric_limits<size_t>::max)() / 4
				|| pixels.size() != static_cast<size_t>(pixelCount * 4))
			{
				error = "Sprite Atlas RGBA byte count does not match its dimensions";
				return false;
			}
			return true;
		}

		uint32_t NextPowerOfTwo(uint32_t value)
		{
			if (value <= 1)
				return 1;
			--value;
			value |= value >> 1;
			value |= value >> 2;
			value |= value >> 4;
			value |= value >> 8;
			value |= value >> 16;
			return value == (std::numeric_limits<uint32_t>::max)()
				? 0 : value + 1;
		}

		uint64_t RectangleArea(const SpriteAtlasRect& rect)
		{
			return static_cast<uint64_t>(rect.Width) * rect.Height;
		}

	}

	bool SliceSpriteAtlasByAlpha(std::span<const uint8_t> rgbaPixels,
		uint32_t width, uint32_t height, const SpriteAtlasSliceOptions& options,
		std::vector<SpriteAtlasRect>& regions, std::string& error)
	{
		regions.clear();
		error.clear();
		if (!ValidateRGBA(rgbaPixels, width, height, error))
			return false;
		if (options.AlphaThreshold == 0 || options.MinimumOpaquePixels == 0)
		{
			error = "Auto Slice alpha threshold and minimum area must be non-zero";
			return false;
		}

		const size_t pixelCount = static_cast<size_t>(width) * height;
		std::vector<uint8_t> visited;
		std::vector<size_t> pending;
		try
		{
			visited.assign(pixelCount, 0);
			pending.reserve((std::min)(pixelCount, static_cast<size_t>(4096)));
		}
		catch (const std::exception&)
		{
			error = "Auto Slice could not allocate its working buffers";
			return false;
		}

		const auto opaque = [&](size_t index)
		{
			return rgbaPixels[index * 4 + 3] >= options.AlphaThreshold;
		};
		for (size_t seed = 0; seed < pixelCount; ++seed)
		{
			if (visited[seed])
				continue;
			visited[seed] = 1;
			if (!opaque(seed))
				continue;

			pending.clear();
			pending.push_back(seed);
			uint32_t minX = static_cast<uint32_t>(seed % width);
			uint32_t maxX = minX;
			uint32_t minY = static_cast<uint32_t>(seed / width);
			uint32_t maxY = minY;
			uint64_t opaqueCount = 0;
			while (!pending.empty())
			{
				const size_t index = pending.back();
				pending.pop_back();
				const uint32_t x = static_cast<uint32_t>(index % width);
				const uint32_t y = static_cast<uint32_t>(index / width);
				minX = (std::min)(minX, x); maxX = (std::max)(maxX, x);
				minY = (std::min)(minY, y); maxY = (std::max)(maxY, y);
				++opaqueCount;

				const auto enqueue = [&](size_t neighbor)
				{
					if (!visited[neighbor])
					{
						visited[neighbor] = 1;
						if (opaque(neighbor))
							pending.push_back(neighbor);
					}
				};
				if (x > 0) enqueue(index - 1);
				if (x + 1 < width) enqueue(index + 1);
				if (y > 0) enqueue(index - width);
				if (y + 1 < height) enqueue(index + width);
			}
			if (opaqueCount < options.MinimumOpaquePixels)
				continue;

			const uint32_t left = options.Padding > minX ? 0 : minX - options.Padding;
			const uint32_t top = options.Padding > minY ? 0 : minY - options.Padding;
			const uint64_t right64 = static_cast<uint64_t>(maxX) + 1 + options.Padding;
			const uint64_t bottom64 = static_cast<uint64_t>(maxY) + 1 + options.Padding;
			const uint32_t right = static_cast<uint32_t>((std::min)(right64,
				static_cast<uint64_t>(width)));
			const uint32_t bottom = static_cast<uint32_t>((std::min)(bottom64,
				static_cast<uint64_t>(height)));
			regions.push_back({ left, top, right - left, bottom - top });
		}
		std::sort(regions.begin(), regions.end(), [](const SpriteAtlasRect& left,
			const SpriteAtlasRect& right)
			{
				return std::tie(left.Y, left.X, left.Height, left.Width)
					< std::tie(right.Y, right.X, right.Height, right.Width);
			});
		regions.erase(std::unique(regions.begin(), regions.end()), regions.end());
		return true;
	}

	bool SliceSpriteAtlasGrid(uint32_t width, uint32_t height,
		const SpriteAtlasGridOptions& options, std::vector<SpriteAtlasRect>& regions,
		std::string& error)
	{
		regions.clear();
		error.clear();
		if (width == 0 || height == 0 || options.CellWidth == 0
			|| options.CellHeight == 0)
		{
			error = "Grid Slice image and cell dimensions must be non-zero";
			return false;
		}
		if (options.OffsetX >= width || options.OffsetY >= height)
		{
			error = "Grid Slice offset is outside the source image";
			return false;
		}
		const uint64_t stepX = static_cast<uint64_t>(options.CellWidth)
			+ options.SpacingX;
		const uint64_t stepY = static_cast<uint64_t>(options.CellHeight)
			+ options.SpacingY;
		for (uint64_t y = options.OffsetY; y < height; y += stepY)
		{
			const uint32_t cellHeight = static_cast<uint32_t>((std::min)(
				static_cast<uint64_t>(options.CellHeight), height - y));
			if (cellHeight != options.CellHeight && !options.IncludePartialCells)
				break;
			for (uint64_t x = options.OffsetX; x < width; x += stepX)
			{
				const uint32_t cellWidth = static_cast<uint32_t>((std::min)(
					static_cast<uint64_t>(options.CellWidth), width - x));
				if (cellWidth != options.CellWidth && !options.IncludePartialCells)
					break;
				regions.push_back({ static_cast<uint32_t>(x), static_cast<uint32_t>(y),
					cellWidth, cellHeight });
			}
		}
		if (regions.empty())
		{
			error = "Grid Slice settings do not produce any cells";
			return false;
		}
		return true;
	}

	std::vector<AssetSubAsset> ReconcileSpriteAtlasSlices(
		std::span<const SpriteAtlasRect> regions,
		std::span<const AssetSubAsset> existingSlices, float defaultPixelsPerUnit)
	{
		struct Match
		{
			size_t Region = 0;
			size_t Existing = 0;
			double Score = 0.0;
			bool Exact = false;
		};
		std::vector<Match> matches;
		for (size_t regionIndex = 0; regionIndex < regions.size(); ++regionIndex)
		{
			const SpriteAtlasRect& region = regions[regionIndex];
			for (size_t existingIndex = 0; existingIndex < existingSlices.size();
				++existingIndex)
			{
				const SpriteSubAssetData& sprite = existingSlices[existingIndex].Sprite;
				const SpriteAtlasRect old{ sprite.X, sprite.Y, sprite.Width, sprite.Height };
				if (old.Width == 0 || old.Height == 0
					|| existingSlices[existingIndex].PersistentID.empty())
					continue;
				const bool exact = old == region;
				const uint64_t left = (std::max)(old.X, region.X);
				const uint64_t top = (std::max)(old.Y, region.Y);
				const uint64_t right = (std::min)(static_cast<uint64_t>(old.X) + old.Width,
					static_cast<uint64_t>(region.X) + region.Width);
				const uint64_t bottom = (std::min)(static_cast<uint64_t>(old.Y) + old.Height,
					static_cast<uint64_t>(region.Y) + region.Height);
				const uint64_t intersection = right > left && bottom > top
					? (right - left) * (bottom - top) : 0;
				const uint64_t unionArea = RectangleArea(old) + RectangleArea(region)
					- intersection;
				const double score = unionArea == 0 ? 0.0
					: static_cast<double>(intersection) / static_cast<double>(unionArea);
				if (exact || score >= 0.25)
					matches.push_back({ regionIndex, existingIndex, score, exact });
			}
		}
		std::sort(matches.begin(), matches.end(), [](const Match& left,
			const Match& right)
			{
				if (left.Exact != right.Exact) return left.Exact > right.Exact;
				if (left.Score != right.Score) return left.Score > right.Score;
				if (left.Region != right.Region) return left.Region < right.Region;
				return left.Existing < right.Existing;
			});
		std::vector<size_t> matchedExisting(regions.size(), (std::numeric_limits<size_t>::max)());
		std::vector<uint8_t> existingUsed(existingSlices.size(), 0);
		for (const Match& match : matches)
		{
			if (matchedExisting[match.Region] != (std::numeric_limits<size_t>::max)()
				|| existingUsed[match.Existing])
				continue;
			matchedExisting[match.Region] = match.Existing;
			existingUsed[match.Existing] = 1;
		}

		std::unordered_set<std::string> usedIDs;
		for (const AssetSubAsset& existing : existingSlices)
			if (!existing.PersistentID.empty()) usedIDs.insert(existing.PersistentID);
		std::vector<AssetSubAsset> result;
		result.reserve(regions.size());
		for (size_t index = 0; index < regions.size(); ++index)
		{
			const SpriteAtlasRect& region = regions[index];
			AssetSubAsset slice;
			if (matchedExisting[index] != (std::numeric_limits<size_t>::max)())
				slice = existingSlices[matchedExisting[index]];
			else
			{
				const std::string base = "sprite:auto-" + std::to_string(region.X) + "-"
					+ std::to_string(region.Y) + "-" + std::to_string(region.Width) + "-"
					+ std::to_string(region.Height);
				slice.PersistentID = base;
				for (uint32_t suffix = 2; usedIDs.contains(slice.PersistentID); ++suffix)
					slice.PersistentID = base + "-" + std::to_string(suffix);
				usedIDs.insert(slice.PersistentID);
				slice.Name = "Sprite " + std::to_string(index + 1);
				slice.Type = AssetType::Texture2D;
				slice.Sprite.PixelsPerUnit = std::isfinite(defaultPixelsPerUnit)
					&& defaultPixelsPerUnit > 0.0f ? defaultPixelsPerUnit : 100.0f;
			}
			slice.Sprite.X = region.X;
			slice.Sprite.Y = region.Y;
			slice.Sprite.Width = region.Width;
			slice.Sprite.Height = region.Height;
			slice.Sprite.BorderLeft = (std::min)(slice.Sprite.BorderLeft,
				static_cast<float>(region.Width));
			slice.Sprite.BorderRight = (std::min)(slice.Sprite.BorderRight,
				static_cast<float>(region.Width) - slice.Sprite.BorderLeft);
			slice.Sprite.BorderBottom = (std::min)(slice.Sprite.BorderBottom,
				static_cast<float>(region.Height));
			slice.Sprite.BorderTop = (std::min)(slice.Sprite.BorderTop,
				static_cast<float>(region.Height) - slice.Sprite.BorderBottom);
			result.push_back(std::move(slice));
		}
		return result;
	}

	bool PackSpriteAtlasRects(std::span<const SpriteAtlasRect> rects,
		const SpriteAtlasPackOptions& options, SpriteAtlasPackedLayout& layout,
		std::string& error)
	{
		layout = {};
		error.clear();
		constexpr uint32_t maximumSupportedDimension = 16384;
		if (rects.empty())
		{
			error = "Atlas packing requires at least one rectangle";
			return false;
		}
		if (options.MaximumWidth == 0 || options.MaximumHeight == 0
			|| options.MaximumWidth > maximumSupportedDimension
			|| options.MaximumHeight > maximumSupportedDimension)
		{
			error = "Atlas packing dimensions must be between 1 and 16384";
			return false;
		}

		struct Item { size_t Index; uint32_t Width; uint32_t Height; };
		std::vector<Item> items;
		items.reserve(rects.size());
		uint64_t paddedArea = 0;
		uint32_t widest = 0;
		for (size_t index = 0; index < rects.size(); ++index)
		{
			const SpriteAtlasRect& rect = rects[index];
			const uint64_t paddedWidth = static_cast<uint64_t>(rect.Width)
				+ static_cast<uint64_t>(options.Padding) * 2;
			const uint64_t paddedHeight = static_cast<uint64_t>(rect.Height)
				+ static_cast<uint64_t>(options.Padding) * 2;
			if (rect.Width == 0 || rect.Height == 0
				|| paddedWidth > options.MaximumWidth
				|| paddedHeight > options.MaximumHeight)
			{
				error = "A Sprite rectangle plus padding exceeds the packing limits";
				return false;
			}
			items.push_back({ index, static_cast<uint32_t>(paddedWidth),
				static_cast<uint32_t>(paddedHeight) });
			widest = (std::max)(widest, static_cast<uint32_t>(paddedWidth));
			paddedArea += paddedWidth * paddedHeight;
		}
		std::sort(items.begin(), items.end(), [](const Item& left, const Item& right)
			{
				if (left.Height != right.Height) return left.Height > right.Height;
				if (left.Width != right.Width) return left.Width > right.Width;
				return left.Index < right.Index;
			});

		std::set<uint32_t> candidateWidths;
		if (options.PowerOfTwo)
		{
			for (uint32_t width = NextPowerOfTwo(widest); width != 0
				&& width <= options.MaximumWidth; width *= 2)
			{
				candidateWidths.insert(width);
				if (width > options.MaximumWidth / 2) break;
			}
		}
		else
		{
			candidateWidths.insert(widest);
			candidateWidths.insert(options.MaximumWidth);
			const uint32_t square = static_cast<uint32_t>(std::ceil(std::sqrt(
				static_cast<double>(paddedArea))));
			candidateWidths.insert((std::min)(options.MaximumWidth,
				(std::max)(widest, square)));
			uint64_t row = 0;
			for (const Item& item : items)
			{
				row += item.Width;
				candidateWidths.insert(static_cast<uint32_t>((std::min)(
					static_cast<uint64_t>(options.MaximumWidth),
					(std::max)(static_cast<uint64_t>(widest), row))));
			}
		}
		if (candidateWidths.empty())
		{
			error = "No power-of-two atlas width fits the requested limit";
			return false;
		}

		uint64_t bestArea = (std::numeric_limits<uint64_t>::max)();
		for (uint32_t candidateWidth : candidateWidths)
		{
			std::vector<uint32_t> skyline(candidateWidth, 0);
			std::vector<SpriteAtlasRect> placements(rects.size());
			uint32_t usedWidth = 0;
			uint32_t usedHeight = 0;
			bool fits = true;
			for (const Item& item : items)
			{
				uint32_t bestX = 0;
				uint32_t bestY = (std::numeric_limits<uint32_t>::max)();
				for (uint32_t x = 0; x <= candidateWidth - item.Width; ++x)
				{
					uint32_t y = 0;
					for (uint32_t column = x; column < x + item.Width; ++column)
						y = (std::max)(y, skyline[column]);
					if (static_cast<uint64_t>(y) + item.Height <= options.MaximumHeight
						&& (y < bestY || (y == bestY && x < bestX)))
					{
						bestX = x;
						bestY = y;
					}
				}
				if (bestY == (std::numeric_limits<uint32_t>::max)())
				{
					fits = false;
					break;
				}
				const uint32_t top = bestY + item.Height;
				for (uint32_t column = bestX; column < bestX + item.Width; ++column)
					skyline[column] = top;
				const SpriteAtlasRect& source = rects[item.Index];
				placements[item.Index] = { bestX + options.Padding,
					bestY + options.Padding, source.Width, source.Height };
				usedWidth = (std::max)(usedWidth, bestX + item.Width);
				usedHeight = (std::max)(usedHeight, top);
			}
			if (!fits) continue;
			const uint32_t outputWidth = options.PowerOfTwo ? candidateWidth : usedWidth;
			const uint32_t outputHeight = options.PowerOfTwo
				? NextPowerOfTwo(usedHeight) : usedHeight;
			if (outputHeight == 0 || outputHeight > options.MaximumHeight)
				continue;
			const uint64_t area = static_cast<uint64_t>(outputWidth) * outputHeight;
			if (area < bestArea || (area == bestArea
				&& (outputHeight < layout.Height || (outputHeight == layout.Height
					&& outputWidth < layout.Width))))
			{
				bestArea = area;
				layout.Width = outputWidth;
				layout.Height = outputHeight;
				layout.Placements = std::move(placements);
			}
		}
		if (layout.Placements.empty())
		{
			error = "Sprite rectangles do not fit within the requested atlas size";
			return false;
		}
		return true;
	}

	bool BuildPackedSpriteAtlasRGBA(std::span<const uint8_t> sourceRGBA,
		uint32_t sourceWidth, uint32_t sourceHeight,
		std::span<const SpriteAtlasRect> sourceRegions,
		const SpriteAtlasPackOptions& options, SpriteAtlasPackedLayout& layout,
		std::vector<uint8_t>& packedRGBA, std::string& error)
	{
		packedRGBA.clear();
		layout = {};
		error.clear();
		if (!ValidateRGBA(sourceRGBA, sourceWidth, sourceHeight, error))
			return false;
		for (const SpriteAtlasRect& region : sourceRegions)
		{
			if (region.Width == 0 || region.Height == 0
				|| static_cast<uint64_t>(region.X) + region.Width > sourceWidth
				|| static_cast<uint64_t>(region.Y) + region.Height > sourceHeight)
			{
				error = "A source Sprite region exceeds the source image";
				return false;
			}
		}
		if (!PackSpriteAtlasRects(sourceRegions, options, layout, error))
			return false;
		const uint64_t outputBytes = static_cast<uint64_t>(layout.Width)
			* layout.Height * 4;
		if (outputBytes > (std::numeric_limits<size_t>::max)())
		{
			error = "Packed Sprite Atlas is too large";
			layout = {};
			return false;
		}
		try { packedRGBA.assign(static_cast<size_t>(outputBytes), 0); }
		catch (const std::exception&)
		{
			error = "Packed Sprite Atlas pixel allocation failed";
			layout = {};
			return false;
		}

		for (size_t index = 0; index < sourceRegions.size(); ++index)
		{
			const SpriteAtlasRect& source = sourceRegions[index];
			const SpriteAtlasRect& destination = layout.Placements[index];
			const uint32_t outerX = destination.X - options.Padding;
			const uint32_t outerY = destination.Y - options.Padding;
			const uint32_t outerWidth = source.Width + options.Padding * 2;
			const uint32_t outerHeight = source.Height + options.Padding * 2;
			for (uint32_t y = 0; y < outerHeight; ++y)
			{
				const uint32_t localY = y < options.Padding ? 0
					: (std::min)(source.Height - 1, y - options.Padding);
				for (uint32_t x = 0; x < outerWidth; ++x)
				{
					const uint32_t localX = x < options.Padding ? 0
						: (std::min)(source.Width - 1, x - options.Padding);
					const size_t sourceOffset = (static_cast<size_t>(source.Y + localY)
						* sourceWidth + source.X + localX) * 4;
					const size_t destinationOffset = (static_cast<size_t>(outerY + y)
						* layout.Width + outerX + x) * 4;
					std::copy_n(sourceRGBA.data() + sourceOffset, 4,
						packedRGBA.data() + destinationOffset);
				}
			}
		}
		return true;
	}

	bool ParseSpriteAtlasSettings(const AssetImportSettings& settings,
		std::vector<AssetSubAsset>& sprites, std::string& error)
	{
		sprites.clear();
		error.clear();
		const auto mode = settings.find("SpriteMode");
		if (mode == settings.end() || mode->second == "Single")
			return true;
		if (mode->second != "Multiple")
		{
			error = "SpriteMode must be Single or Multiple";
			return false;
		}
		const auto schema = settings.find("SpriteAtlasSchema");
		if (schema == settings.end() || schema->second != "2")
		{
			error = "Multiple Sprite import requires SpriteAtlasSchema=2";
			return false;
		}

		struct Fields
		{
			std::string Name;
			std::string Rect;
			std::string Pivot;
			std::string PixelsPerUnit;
			std::string Border;
			uint32_t Seen = 0;
		};
		std::map<std::string, Fields> entries;
		for (const auto& [key, value] : settings)
		{
			if (!key.starts_with("Sprite."))
				continue;
			const size_t separator = key.rfind('.');
			if (separator <= 7 || separator + 1 >= key.size())
			{
				error = "invalid Sprite atlas setting key: " + key;
				return false;
			}
			const std::string id = key.substr(7, separator - 7);
			const std::string field = key.substr(separator + 1);
			Fields& target = entries[id];
			uint32_t bit = 0;
			std::string* destination = nullptr;
			if (field == "Name") { bit = 1; destination = &target.Name; }
			else if (field == "Rect") { bit = 2; destination = &target.Rect; }
			else if (field == "Pivot") { bit = 4; destination = &target.Pivot; }
			else if (field == "PixelsPerUnit") { bit = 8; destination = &target.PixelsPerUnit; }
			else if (field == "Border") { bit = 16; destination = &target.Border; }
			else
			{
				error = "unknown Sprite atlas setting field: " + field;
				return false;
			}
			if ((target.Seen & bit) != 0)
			{
				error = "duplicate Sprite atlas setting: " + key;
				return false;
			}
			target.Seen |= bit;
			*destination = value;
		}
		if (entries.empty())
		{
			error = "Multiple Sprite import requires at least one Sprite entry";
			return false;
		}

		for (const auto& [id, fields] : entries)
		{
			if (id.empty() || fields.Seen != 31 || fields.Name.empty())
			{
				error = "Sprite." + id + " is incomplete";
				return false;
			}
			std::array<uint32_t, 4> rect{};
			std::array<float, 2> pivot{};
			std::array<float, 4> border{};
			float ppu = 0.0f;
			if (!ParseTuple(fields.Rect, rect) || !ParseTuple(fields.Pivot, pivot)
				|| !ParseNumber(fields.PixelsPerUnit, ppu)
				|| !ParseTuple(fields.Border, border))
			{
				error = "Sprite." + id + " contains a malformed numeric value";
				return false;
			}
			AssetSubAsset child;
			child.PersistentID = "sprite:" + id;
			child.Name = fields.Name;
			child.Type = AssetType::Texture2D;
			child.Sprite = { rect[0], rect[1], rect[2], rect[3],
				pivot[0], pivot[1], ppu, border[0], border[1], border[2], border[3] };
			if (!ValidateSpriteData(child.Sprite))
			{
				error = "Sprite." + id + " has an invalid Rect/Pivot/PPU/Border";
				return false;
			}
			sprites.push_back(std::move(child));
		}
		return true;
	}

	std::vector<uint8_t> BuildCookedSpriteSubAsset(AssetHandle sourceTexture,
		const SpriteSubAssetData& data, std::span<const uint8_t> atlasBytes)
	{
		if (static_cast<uint64_t>(sourceTexture) == 0 || atlasBytes.empty()
			|| !ValidateSpriteData(data))
			return {};
		std::vector<uint8_t> output;
		output.reserve(72 + atlasBytes.size());
		output.insert(output.end(), kCookedSpriteMagic.begin(), kCookedSpriteMagic.end());
		AppendU64(output, static_cast<uint64_t>(sourceTexture));
		AppendU32(output, data.X); AppendU32(output, data.Y);
		AppendU32(output, data.Width); AppendU32(output, data.Height);
		AppendFloat(output, data.PivotX); AppendFloat(output, data.PivotY);
		AppendFloat(output, data.PixelsPerUnit);
		AppendFloat(output, data.BorderLeft); AppendFloat(output, data.BorderBottom);
		AppendFloat(output, data.BorderRight); AppendFloat(output, data.BorderTop);
		AppendU64(output, static_cast<uint64_t>(atlasBytes.size()));
		output.insert(output.end(), atlasBytes.begin(), atlasBytes.end());
		return output;
	}

	bool ParseCookedSpriteSubAsset(std::span<const uint8_t> bytes,
		ResolvedSpriteAsset& sprite, std::span<const uint8_t>& atlasBytes)
	{
		sprite = {};
		atlasBytes = {};
		if (bytes.size() < kCookedSpriteMagic.size()
			|| !std::equal(kCookedSpriteMagic.begin(), kCookedSpriteMagic.end(), bytes.begin()))
			return false;
		size_t offset = kCookedSpriteMagic.size();
		uint64_t source = 0;
		uint64_t payloadSize = 0;
		if (!ReadU64(bytes, offset, source) || source == 0
			|| !ReadU32(bytes, offset, sprite.Data.X)
			|| !ReadU32(bytes, offset, sprite.Data.Y)
			|| !ReadU32(bytes, offset, sprite.Data.Width)
			|| !ReadU32(bytes, offset, sprite.Data.Height)
			|| !ReadFloat(bytes, offset, sprite.Data.PivotX)
			|| !ReadFloat(bytes, offset, sprite.Data.PivotY)
			|| !ReadFloat(bytes, offset, sprite.Data.PixelsPerUnit)
			|| !ReadFloat(bytes, offset, sprite.Data.BorderLeft)
			|| !ReadFloat(bytes, offset, sprite.Data.BorderBottom)
			|| !ReadFloat(bytes, offset, sprite.Data.BorderRight)
			|| !ReadFloat(bytes, offset, sprite.Data.BorderTop)
			|| !ReadU64(bytes, offset, payloadSize)
			|| payloadSize == 0 || payloadSize != bytes.size() - offset
			|| !ValidateSpriteData(sprite.Data))
			return false;
		sprite.TextureHandle = AssetHandle(source);
		sprite.IsSubAsset = true;
		atlasBytes = bytes.subspan(offset);
		return true;
	}

	bool BuildSpriteRenderGeometry(const SpriteSubAssetData& data,
		uint32_t textureWidth, uint32_t textureHeight,
		SpriteRenderGeometry& geometry)
	{
		geometry = {};
		if (!ValidateSpriteData(data) || textureWidth == 0 || textureHeight == 0
			|| static_cast<uint64_t>(data.X) + data.Width > textureWidth
			|| static_cast<uint64_t>(data.Y) + data.Height > textureHeight)
			return false;
		const float width = static_cast<float>(data.Width);
		const float height = static_cast<float>(data.Height);
		geometry.Width = width / data.PixelsPerUnit;
		geometry.Height = height / data.PixelsPerUnit;
		geometry.OffsetX = (0.5f - data.PivotX) * geometry.Width;
		geometry.OffsetY = (0.5f - data.PivotY) * geometry.Height;
		geometry.UMin = static_cast<float>(data.X) / textureWidth;
		geometry.UMax = static_cast<float>(data.X + data.Width) / textureWidth;
		geometry.VMin = 1.0f - static_cast<float>(data.Y + data.Height) / textureHeight;
		geometry.VMax = 1.0f - static_cast<float>(data.Y) / textureHeight;
		geometry.BorderLeft = data.BorderLeft / data.PixelsPerUnit;
		geometry.BorderBottom = data.BorderBottom / data.PixelsPerUnit;
		geometry.BorderRight = data.BorderRight / data.PixelsPerUnit;
		geometry.BorderTop = data.BorderTop / data.PixelsPerUnit;
		return true;
	}

	namespace {

		constexpr const char* kUsageKey = "Usage";
		constexpr const char* kUsageSprite = "Sprite";
		constexpr const char* kPrimitiveKey = "Primitive";

		bool IsSupportedPrimitive(std::string_view primitiveName)
		{
			return primitiveName == "Circle" || primitiveName == "Square";
		}

		std::vector<uint8_t> EncodePrimitiveTga(std::string_view primitiveName)
		{
			constexpr uint16_t size = 64;
			constexpr size_t headerSize = 18;
			std::vector<uint8_t> bytes(headerSize + static_cast<size_t>(size) * size * 4, 0);
			bytes[2] = 2; // Uncompressed true-color image.
			bytes[12] = static_cast<uint8_t>(size & 0xff);
			bytes[13] = static_cast<uint8_t>(size >> 8);
			bytes[14] = static_cast<uint8_t>(size & 0xff);
			bytes[15] = static_cast<uint8_t>(size >> 8);
			bytes[16] = 32;
			bytes[17] = 0x28; // Eight alpha bits, top-left origin.

			const bool circle = primitiveName == "Circle";
			for (uint16_t y = 0; y < size; ++y)
			{
				for (uint16_t x = 0; x < size; ++x)
				{
					uint8_t alpha = 255;
					if (circle)
					{
						const float localX = (static_cast<float>(x) + 0.5f) *
							(2.0f / static_cast<float>(size)) - 1.0f;
						const float localY = (static_cast<float>(y) + 0.5f) *
							(2.0f / static_cast<float>(size)) - 1.0f;
						const float coverage = std::clamp((1.0f - std::sqrt(
							localX * localX + localY * localY)) *
							(static_cast<float>(size) * 0.5f) + 0.5f, 0.0f, 1.0f);
						alpha = static_cast<uint8_t>(coverage * 255.0f + 0.5f);
					}

					const size_t pixel = headerSize +
						(static_cast<size_t>(y) * size + x) * 4;
					bytes[pixel + 0] = 255; // B
					bytes[pixel + 1] = 255; // G
					bytes[pixel + 2] = 255; // R
					bytes[pixel + 3] = alpha;
				}
			}
			return bytes;
		}

		template<typename ImportFunction, typename SettingsFunction>
		AssetHandle EnsurePrimitiveSpriteAssetImpl(AssetRegistry& registry,
			std::string_view primitiveName, ImportFunction&& importAsset,
			SettingsFunction&& setImportSettings)
		{
			if (!registry.IsInitialized() || !IsSupportedPrimitive(primitiveName))
				return AssetHandle(0);

			if (const AssetHandle existing = FindPrimitiveSpriteAsset(registry, primitiveName);
				static_cast<uint64_t>(existing) != 0)
				return existing;

			const std::filesystem::path relativePath = std::filesystem::path("Sprites") /
				"TomCat" / (std::string(primitiveName) + ".tga");
			const std::filesystem::path sourcePath =
				(registry.GetAssetDirectory() / relativePath).lexically_normal();

			std::error_code error;
			const std::filesystem::file_status sourceStatus =
				std::filesystem::symlink_status(sourcePath, error);
			const bool sourceMissing = error == std::errc::no_such_file_or_directory ||
				(!error && !std::filesystem::exists(sourceStatus));
			if (error && !sourceMissing)
			{
				TC_Core_Error("Could not inspect primitive Sprite asset '{0}': {1}",
					PathToUTF8(sourcePath), error.message());
				return AssetHandle(0);
			}
			error.clear();
			const bool sourceExists = !sourceMissing &&
				std::filesystem::is_regular_file(sourceStatus);
			if (!sourceMissing && !sourceExists)
			{
				TC_Core_Error("Primitive Sprite path is not a regular file: {0}",
					PathToUTF8(sourcePath));
				return AssetHandle(0);
			}

			if (!sourceExists)
			{
				std::filesystem::create_directories(sourcePath.parent_path(), error);
				if (error)
				{
					TC_Core_Error("Could not create primitive Sprite directory '{0}': {1}",
						PathToUTF8(sourcePath.parent_path()), error.message());
					return AssetHandle(0);
				}

				const std::vector<uint8_t> encoded = EncodePrimitiveTga(primitiveName);
				const std::string_view contents(
					reinterpret_cast<const char*>(encoded.data()), encoded.size());
				std::string writeError;
				if (!FileSystem::WriteFileAtomically(sourcePath, contents, writeError))
				{
					TC_Core_Error("Could not create primitive Sprite asset '{0}': {1}",
						PathToUTF8(sourcePath), writeError);
					return AssetHandle(0);
				}
			}

			const AssetHandle handle = importAsset(sourcePath);
			if (static_cast<uint64_t>(handle) == 0)
			{
				TC_Core_Error("Could not import primitive Sprite asset '{0}'",
					PathToUTF8(sourcePath));
				return AssetHandle(0);
			}

			const AssetMetadata* metadata = registry.GetMetadata(handle);
			if (!metadata || !IsSpriteAsset(*metadata) || metadata->IsMissing)
				return AssetHandle(0);
			AssetImportSettings settings = metadata->ImportSettings;
			settings.insert_or_assign(kUsageKey, kUsageSprite);
			settings.insert_or_assign(kPrimitiveKey, std::string(primitiveName));
			settings.insert_or_assign("PixelsPerUnit", "100");
			if (settings != metadata->ImportSettings && !setImportSettings(handle, settings))
			{
				TC_Core_Error("Could not save primitive Sprite import settings for asset {0}",
					static_cast<uint64_t>(handle));
				return AssetHandle(0);
			}
			return handle;
		}

	}

	std::span<const BuiltInSpriteAsset> GetBuiltInSpriteAssets()
	{
		return kBuiltInSpriteAssets;
	}

	const BuiltInSpriteAsset* FindBuiltInSpriteAsset(AssetHandle handle)
	{
		const auto found = std::find_if(kBuiltInSpriteAssets.begin(),
			kBuiltInSpriteAssets.end(), [handle](const BuiltInSpriteAsset& asset)
			{
				return asset.Handle == handle;
			});
		return found == kBuiltInSpriteAssets.end() ? nullptr : &*found;
	}

	const BuiltInSpriteAsset* FindBuiltInSpriteAsset(std::string_view name)
	{
		const auto found = std::find_if(kBuiltInSpriteAssets.begin(),
			kBuiltInSpriteAssets.end(), [name](const BuiltInSpriteAsset& asset)
			{
				return asset.Name == name;
			});
		return found == kBuiltInSpriteAssets.end() ? nullptr : &*found;
	}

	std::filesystem::path GetBuiltInSpriteAssetPath(AssetHandle handle)
	{
		const BuiltInSpriteAsset* asset = FindBuiltInSpriteAsset(handle);
		return asset ? (std::filesystem::path("Packages") /
			UTF8ToPath(asset->PackageRelativePath)).lexically_normal()
			: std::filesystem::path{};
	}

	bool IsSpriteAsset(const AssetMetadata& metadata)
	{
		return metadata.Type == AssetType::Texture2D;
	}

	AssetHandle FindPrimitiveSpriteAsset(const AssetRegistry& registry,
		std::string_view primitiveName)
	{
		if (!IsSupportedPrimitive(primitiveName))
			return AssetHandle(0);
		const AssetMetadata* bestMatch = nullptr;
		for (const auto& [handle, metadata] : registry.GetAssets())
		{
			if (!IsSpriteAsset(metadata) || metadata.IsMissing)
				continue;
			const AssetMetadata* current = registry.GetMetadata(metadata.FilePath);
			if (!current || current->Handle != handle || current->IsMissing)
				continue;
			const auto usage = metadata.ImportSettings.find(kUsageKey);
			const auto primitive = metadata.ImportSettings.find(kPrimitiveKey);
			if (usage != metadata.ImportSettings.end() && usage->second == kUsageSprite &&
				primitive != metadata.ImportSettings.end() && primitive->second == primitiveName)
			{
				if (!bestMatch || PathToUTF8(metadata.FilePath) < PathToUTF8(bestMatch->FilePath))
					bestMatch = &metadata;
			}
		}
		return bestMatch ? bestMatch->Handle : AssetHandle(0);
	}

	AssetHandle EnsurePrimitiveSpriteAsset(AssetManager& manager,
		std::string_view primitiveName)
	{
		return EnsurePrimitiveSpriteAssetImpl(manager.GetRegistry(), primitiveName,
			[&manager](const std::filesystem::path& path) { return manager.ImportAsset(path); },
			[&manager](AssetHandle handle, const AssetImportSettings& settings)
			{
				return manager.SetImportSettings(handle, settings);
			});
	}

	AssetHandle EnsurePrimitiveSpriteAsset(AssetRegistry& registry,
		std::string_view primitiveName)
	{
		return EnsurePrimitiveSpriteAssetImpl(registry, primitiveName,
			[&registry](const std::filesystem::path& path) { return registry.ImportAsset(path); },
			[&registry](AssetHandle handle, const AssetImportSettings& settings)
			{
				return registry.SetImportSettings(handle, settings);
			});
	}

}
