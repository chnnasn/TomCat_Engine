#pragma once

#include "DerivedDataCache.h"
#include "ImporterRegistry.h"

#include <cstdint>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TomCat {

	class AssetRegistry;

	enum class AssetLoadStatus : uint8_t
	{
		Success = 0,
		NotInitialized,
		NotFound,
		UnsupportedType,
		SourceReadFailed,
		DependencyFailed,
		DependencyCycle,
		ImportFailed,
		Cancelled
	};

	struct AssetLoadOptions
	{
		std::string Platform = "editor";
		std::string Backend = "opengl";
		std::shared_ptr<AssetLoadCancellation> Cancellation;
		// ImportCoordinator performs this publication from PumpMainThread so the
		// registry and consumers never observe background-thread mutation.
		bool DeferMetadataCommit = false;
	};

	struct ImportedArtifact
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::string ArtifactKey;
		std::string Format;
		std::vector<uint8_t> Bytes;
		std::vector<AssetSubAsset> SubAssets;
		std::vector<std::string> DependencyKeys;
		bool FromCache = false;
	};

	struct AssetLoadResult
	{
		AssetLoadStatus Status = AssetLoadStatus::NotInitialized;
		ImportedArtifact Artifact;
		std::string Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return Status == AssetLoadStatus::Success;
		}
	};

	// Owns importer selection, dependency-key propagation and single-flight
	// publication into the project-local DerivedDataCache.
	class AssetDatabase final
	{
	public:
		[[nodiscard]] bool Initialize(AssetRegistry& registry,
			const std::filesystem::path& libraryDirectory);
		void Shutdown();
		[[nodiscard]] bool IsInitialized() const noexcept { return m_Registry != nullptr; }

		ImporterRegistry& GetImporters() noexcept { return m_Importers; }
		const ImporterRegistry& GetImporters() const noexcept { return m_Importers; }
		DerivedDataCache& GetCache() noexcept { return m_Cache; }
		const DerivedDataCache& GetCache() const noexcept { return m_Cache; }

		[[nodiscard]] bool SetDependencies(AssetHandle asset,
			std::vector<AssetHandle> dependencies);
		[[nodiscard]] std::vector<AssetHandle> GetDependencies(AssetHandle asset) const;
		[[nodiscard]] std::vector<AssetHandle> GetDependents(AssetHandle asset,
			bool transitive = false) const;

		[[nodiscard]] AssetLoadResult LoadArtifact(AssetHandle handle,
			AssetLoadOptions options = {});
		[[nodiscard]] std::future<AssetLoadResult> LoadArtifactAsync(
			AssetHandle handle, AssetLoadOptions options = {});

		// Registry operations used by the file monitor share the same gate as
		// metadata reads and sub-asset publication performed by imports.
		[[nodiscard]] bool RefreshRegistry();
		[[nodiscard]] std::optional<AssetMetadata> GetMetadataSnapshot(
			AssetHandle handle);
		[[nodiscard]] std::optional<AssetMetadata> GetMetadataSnapshot(
			const std::filesystem::path& path);
		[[nodiscard]] bool FinalizeImportedArtifact(AssetLoadResult& result,
			bool refreshDependencies = true);

		template<typename T>
		[[nodiscard]] std::optional<T> Load(AssetHandle handle,
			const std::function<std::optional<T>(const ImportedArtifact&)>& decoder,
			AssetLoadOptions options = {}, std::string* error = nullptr)
		{
			AssetLoadResult result = LoadArtifact(handle, std::move(options));
			if (!result.Succeeded())
			{
				if (error)
					*error = result.Error;
				return std::nullopt;
			}
			std::optional<T> decoded = decoder(result.Artifact);
			if (!decoded && error)
				*error = "artifact decoder rejected the imported data";
			return decoded;
		}

		template<typename T>
		[[nodiscard]] std::future<std::optional<T>> LoadAsync(AssetHandle handle,
			std::function<std::optional<T>(const ImportedArtifact&)> decoder,
			AssetLoadOptions options = {})
		{
			return std::async(std::launch::async,
				[this, handle, decoder = std::move(decoder),
				options = std::move(options)]() mutable -> std::optional<T>
				{
					AssetLoadResult result = LoadArtifact(handle, std::move(options));
					return result.Succeeded() ? decoder(result.Artifact) : std::nullopt;
				});
		}

	private:
		AssetLoadResult LoadArtifactInternal(AssetHandle handle,
			const AssetLoadOptions& options, std::vector<AssetHandle>& stack);
		AssetLoadResult GetOrImport(const AssetMetadata& metadata,
			const std::shared_ptr<const IAssetImporter>& importer,
			const std::filesystem::path& sourcePath,
			std::span<const uint8_t> sourceBytes, const std::string& sourceHash,
			std::vector<std::string> dependencyKeys,
			const AssetLoadOptions& options);
		bool FinalizeSubAssets(AssetLoadResult& result, bool refreshDependencies = true);
		bool AcquireHandleFlight(AssetHandle handle, const AssetLoadOptions& options);
		void ReleaseHandleFlight(AssetHandle handle);
		bool LoadDependencyCache();
		bool RebuildDiscoveredDependenciesLocked();
		bool PersistDependencyGraph() const;

	private:
		AssetRegistry* m_Registry = nullptr;
		ImporterRegistry m_Importers;
		DerivedDataCache m_Cache;
		mutable std::shared_mutex m_GraphMutex;
		std::unordered_map<AssetHandle, std::vector<AssetHandle>> m_Dependencies;
		std::unordered_map<AssetHandle, std::vector<AssetHandle>> m_Dependents;
		mutable std::mutex m_DependencyMutationMutex;
		mutable std::mutex m_DependencyCacheMutex;
		std::mutex m_FlightMutex;
		std::unordered_map<std::string, std::shared_future<AssetLoadResult>> m_Flights;
		std::mutex m_HandleFlightMutex;
		std::condition_variable m_HandleFlightWake;
		std::unordered_set<AssetHandle> m_ActiveHandleFlights;
		std::mutex m_MetadataCommitMutex;
	};

}
