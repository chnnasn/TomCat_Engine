#pragma once

#include "Asset.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace TomCat {

	// Project-local asset identity and path index. Sidecar .tcmeta files are the
	// authoritative source; the Library registry is only a rebuildable cache.
	class AssetRegistry
	{
	public:
		using AssetMap = std::unordered_map<AssetHandle, AssetMetadata>;

		[[nodiscard]] bool Initialize(const std::filesystem::path& assetDirectory,
			const std::filesystem::path& libraryDirectory = {});
		void Shutdown();
		[[nodiscard]] bool Refresh();

		[[nodiscard]] AssetHandle ImportAsset(const std::filesystem::path& path);
		[[nodiscard]] bool IsManagedPath(const std::filesystem::path& path,
			bool allowMissingLeaf = false) const;

		const AssetMetadata* GetMetadata(AssetHandle handle) const;
		const AssetMetadata* GetMetadata(const std::filesystem::path& path) const;
		[[nodiscard]] std::filesystem::path GetFileSystemPath(AssetHandle handle) const;
		[[nodiscard]] std::vector<AssetHandle> GetHandlesUnderPath(
			const std::filesystem::path& path) const;

		[[nodiscard]] bool MoveAsset(const std::filesystem::path& source,
			const std::filesystem::path& destination,
			AssetHandle expectedHandle = AssetHandle(0));
		[[nodiscard]] bool DeleteAsset(const std::filesystem::path& path, bool force,
			std::vector<AssetReference>& references,
			AssetHandle expectedHandle = AssetHandle(0));
		[[nodiscard]] std::vector<AssetReference> FindReferences(AssetHandle handle) const;

		[[nodiscard]] bool SetImportSettings(AssetHandle handle,
			const AssetImportSettings& settings);

		static bool IsMetaFile(const std::filesystem::path& path);
		static std::filesystem::path GetMetadataPath(const std::filesystem::path& sourcePath);

		const AssetMap& GetAssets() const { return m_Assets; }
		const std::filesystem::path& GetAssetDirectory() const { return m_AssetDirectory; }
		const std::filesystem::path& GetLibraryDirectory() const { return m_LibraryDirectory; }
		bool IsInitialized() const { return m_Initialized; }

	private:
		enum class MetadataTransactionType : uint8_t
		{
			None = 0,
			Move,
			Delete
		};

		struct MetadataTransaction
		{
			MetadataTransactionType Type = MetadataTransactionType::None;
			std::filesystem::path Source;
			std::filesystem::path Destination;
			std::filesystem::path Staging;
			uint64_t OriginalSize = 0;
			uint64_t OriginalHash = 0;
			bool HasFingerprint = false;
		};

		bool LoadCache();
		bool SaveCache() const;
		void RebuildPathIndex();

		bool NormalizeManagedPath(const std::filesystem::path& path,
			std::filesystem::path& absolutePath, std::filesystem::path& relativePath,
			bool allowMissingLeaf, bool allowRoot) const;
		bool ValidateManagedTree(const std::filesystem::path& root) const;
		bool WriteMetadata(const std::filesystem::path& metadataPath,
			const AssetMetadata& metadata, bool replaceExisting = true,
			const MetadataTransaction* transaction = nullptr) const;
		bool ReadMetadata(const std::filesystem::path& metadataPath,
			AssetMetadata& metadata, MetadataTransaction* transaction = nullptr) const;
		bool RecoverInterruptedTransaction(const std::filesystem::path& metadataPath,
			const AssetMetadata& metadata, const MetadataTransaction& transaction) const;
		AssetHandle GenerateUniqueHandle(
			const std::unordered_map<AssetHandle, std::filesystem::path>& claimed) const;
		std::vector<AssetReference> FindReferencesInternal(
			const std::vector<AssetHandle>& handles,
			const std::vector<std::filesystem::path>& ignoredSceneRoots,
			bool& complete) const;

	private:
		std::filesystem::path m_AssetDirectory;
		std::filesystem::path m_LibraryDirectory;
		std::filesystem::path m_CanonicalAssetDirectory;
		AssetMap m_Assets;
		std::unordered_map<std::string, AssetHandle> m_PathIndex;
		bool m_Initialized = false;
	};

}
