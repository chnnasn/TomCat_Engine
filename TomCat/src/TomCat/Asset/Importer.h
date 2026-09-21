#pragma once

#include "Asset.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	class AssetLoadCancellation final
	{
	public:
		void Cancel() noexcept { m_Cancelled.store(true, std::memory_order_release); }
		[[nodiscard]] bool IsCancellationRequested() const noexcept
		{
			return m_Cancelled.load(std::memory_order_acquire);
		}

	private:
		std::atomic_bool m_Cancelled = false;
	};

	struct ImportedSubAsset
	{
		// Importer-stable logical identity (for example "sprite:idle/0"). The
		// registry maps this to a persistent AssetHandle in .tcmeta v2.
		std::string PersistentID;
		std::string Name;
		AssetType Type = AssetType::None;
		SpriteSubAssetData Sprite;
	};

	struct AssetImportRequest
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::filesystem::path SourcePath;
		std::span<const uint8_t> SourceBytes;
		std::string SourceSHA256;
		AssetImportSettings Settings;
		std::string Platform;
		std::string Backend;
		std::vector<std::string> DependencyKeys;
		std::shared_ptr<const AssetLoadCancellation> Cancellation;

		[[nodiscard]] bool IsCancellationRequested() const noexcept
		{
			return Cancellation && Cancellation->IsCancellationRequested();
		}
	};

	struct AssetImportResult
	{
		std::vector<uint8_t> ArtifactBytes;
		std::vector<ImportedSubAsset> SubAssets;
		std::string Format;
		std::string Error;

		[[nodiscard]] bool Succeeded() const noexcept { return Error.empty(); }
	};

	class IAssetImporter
	{
	public:
		virtual ~IAssetImporter() = default;
		[[nodiscard]] virtual std::string_view GetID() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetVersion() const noexcept = 0;
		[[nodiscard]] virtual AssetType GetAssetType() const noexcept = 0;
		[[nodiscard]] virtual AssetImportResult Import(
			const AssetImportRequest& request) const = 0;
	};

}
