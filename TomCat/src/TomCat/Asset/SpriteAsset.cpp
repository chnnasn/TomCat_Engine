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
#include <string>
#include <system_error>
#include <vector>

namespace TomCat {

	namespace {

		constexpr std::array<uint8_t, 8> kCookedSpriteMagic = {
			'T', 'C', 'S', 'P', 'R', '0', '0', '2' };

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
