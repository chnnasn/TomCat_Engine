#include "tcpch.h"
#include "SpriteAsset.h"

#include "AssetManager.h"
#include "AssetRegistry.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace TomCat {

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
