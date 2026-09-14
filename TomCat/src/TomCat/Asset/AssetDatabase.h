#pragma once

#include "AssetJobSystem.h"

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
		Cancelled,
		// Consumed by bounded LoadArtifact retries. It is only surfaced when a
		// deferred publication exhausts its own fresh-result retries.
		StaleSnapshot
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

	struct AssetDependencySnapshot
	{
		// Logical handles serialized by the source. Cook uses these exact handles.
		std::vector<AssetHandle> Dependencies;
		// Source assets used to build dependency artifact keys. A Sprite logical
		// handle resolves to its atlas owner here.
		std::vector<AssetHandle> ArtifactDependencies;
		// Present for Scene, Prefab, and Material edges discovered from source.
		// The digest, dependency list, and revision are published under one graph
		// lock. Revision changes whenever any dependency graph state changes.
		std::string SourceSHA256;
		uint64_t Revision = 0;
	};

	struct AssetLoadInputSnapshot
	{
		AssetMetadata Metadata;
		std::filesystem::path SourcePath;
		AssetDependencySnapshot Dependencies;
		std::string SourceSHA256;
	};

	struct ImportedArtifact
	{
		AssetHandle Handle = AssetHandle(0);
		AssetType Type = AssetType::None;
		std::string ArtifactKey;
		// Hash of the exact source bytes used to build or locate this artifact.
		// It is runtime metadata and is not serialized into the artifact payload.
		std::string SourceSHA256;
		std::string Format;
		std::vector<uint8_t> Bytes;
		std::vector<AssetSubAsset> SubAssets;
		std::vector<std::string> DependencyKeys;
		bool FromCache = false;
		// Runtime-only identity used to reject stale deferred sub-asset publication.
		// These fields are intentionally excluded from the artifact envelope.
		std::vector<AssetLoadInputSnapshot> LoadSnapshots;
		std::string LoadPlatform;
		std::string LoadBackend;
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
		[[nodiscard]] AssetDependencySnapshot GetDependencySnapshot(
			AssetHandle asset) const;
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
		[[nodiscard]] std::vector<AssetMetadata> GetAllMetadataSnapshots();
		[[nodiscard]] bool GetSubAssetSnapshot(AssetHandle handle,
			AssetMetadata& owner, AssetSubAsset& subAsset);
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
			if (!BeginAsyncTask())
			{
				std::promise<std::optional<T>> promise;
				std::future<std::optional<T>> future = promise.get_future();
				promise.set_value(std::nullopt);
				return future;
			}

			try
			{
				const uint64_t reservation = EstimateJobReservation(handle);
				return AssetJobSystem::Get().Submit(reservation,
					[this, handle, decoder = std::move(decoder),
					options = std::move(options)]() mutable -> std::optional<T>
					{
						try
						{
							AssetLoadResult result = LoadArtifact(handle, std::move(options));
							std::optional<T> decoded = result.Succeeded()
								? decoder(result.Artifact) : std::nullopt;
							FinishAsyncTask();
							return decoded;
						}
						catch (...)
						{
							FinishAsyncTask();
							throw;
						}
					});
			}
			catch (...)
			{
				// Submit can reject work while the global executor is stopping.
				FinishAsyncTask();
				throw;
			}
		}

	private:
		struct LoadAttempt
		{
			uint64_t GraphRevision = 0;
			std::vector<AssetLoadInputSnapshot> Snapshots;
		};

		AssetLoadResult LoadArtifactInternal(AssetHandle handle,
			const AssetLoadOptions& options, std::vector<AssetHandle>& stack,
			LoadAttempt& attempt);
		AssetLoadResult GetOrImport(const AssetMetadata& metadata,
			const std::shared_ptr<const IAssetImporter>& importer,
			const std::filesystem::path& sourcePath,
			std::span<const uint8_t> sourceBytes, const std::string& sourceHash,
			std::vector<std::string> dependencyKeys,
			const AssetLoadOptions& options, const LoadAttempt& attempt);
		bool ReadLoadSnapshotLocked(AssetHandle handle, AssetLoadInputSnapshot& snapshot) const;
		bool CaptureLoadSnapshot(AssetHandle handle, LoadAttempt& attempt,
			size_t& snapshotIndex, AssetLoadResult& failure);
		bool IsLoadAttemptCurrentLocked(const LoadAttempt& attempt) const;
		bool AreLoadAttemptSourcesCurrent(const LoadAttempt& attempt,
			const AssetLoadOptions& options) const;
		bool IsLoadAttemptCurrent(const LoadAttempt& attempt,
			const AssetLoadOptions& options, bool validateSources);
		bool PublishIfLoadAttemptCurrent(const std::string& artifactKey,
			std::span<const uint8_t> serialized, const LoadAttempt& attempt,
			const AssetLoadOptions& options);
		AssetLoadStatus FinalizeSubAssetsForLoad(AssetLoadResult& result,
			bool refreshDependencies, LoadAttempt& attempt, size_t snapshotIndex,
			const AssetLoadOptions& options);
		bool AcquireHandleFlight(AssetHandle handle, const AssetLoadOptions& options);
		void ReleaseHandleFlight(AssetHandle handle);
		bool LoadDependencyCache();
		bool RebuildDiscoveredDependenciesLocked();
		bool PersistDependencyGraph() const;
		uint64_t EstimateJobReservation(AssetHandle handle);
		uint64_t EstimateSingleJobReservation(AssetHandle handle);
		bool BeginAsyncTask();
		void FinishAsyncTask() noexcept;
		void EnableAsyncTasks();
		bool StopAndWaitForAsyncTasks();

	private:
		AssetRegistry* m_Registry = nullptr;
		ImporterRegistry m_Importers;
		DerivedDataCache m_Cache;
		mutable std::shared_mutex m_GraphMutex;
		std::unordered_map<AssetHandle, std::vector<AssetHandle>> m_Dependencies;
		std::unordered_map<AssetHandle, std::vector<AssetHandle>>
			m_ArtifactDependencies;
		std::unordered_map<AssetHandle, std::vector<AssetHandle>> m_Dependents;
		std::unordered_map<AssetHandle, std::string> m_DependencySourceSHA256;
		uint64_t m_DependencyGraphRevision = 1;
		mutable std::mutex m_DependencyMutationMutex;
		mutable std::mutex m_DependencyCacheMutex;
		std::mutex m_FlightMutex;
		std::unordered_map<std::string, std::shared_future<AssetLoadResult>> m_Flights;
		std::mutex m_HandleFlightMutex;
		std::condition_variable m_HandleFlightWake;
		std::unordered_set<AssetHandle> m_ActiveHandleFlights;
		std::mutex m_MetadataCommitMutex;
		std::mutex m_AsyncTaskMutex;
		std::condition_variable m_AsyncTasksIdle;
		size_t m_AsyncTasksInFlight = 0;
		bool m_AcceptingAsyncTasks = false;
	};

}
