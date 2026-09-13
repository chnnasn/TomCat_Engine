#include "tcpch.h"
#include "AssetDatabase.h"

#include "ArtifactKey.h"
#include "AssetRegistry.h"
#include "ContentHash.h"
#include "MaterialArtifact.h"
#include "TextureArtifact.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <deque>
#include <limits>
#include <sstream>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		constexpr std::array<char, 8> kArtifactMagic = {
			'T', 'C', 'I', 'M', 'P', '0', '0', '1' };
		constexpr uint32_t kArtifactEnvelopeVersion = 2;
		constexpr uint32_t kOldestArtifactEnvelopeVersion = 1;
		constexpr uint64_t kMaximumArtifactBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
		constexpr uint32_t kMaximumSubAssets = 1'000'000;
		constexpr uint32_t kMaximumStringBytes = 1024 * 1024;

		void AppendU16(std::vector<uint8_t>& output, uint16_t value)
		{
			output.push_back(static_cast<uint8_t>(value & 0xffU));
			output.push_back(static_cast<uint8_t>((value >> 8) & 0xffU));
		}

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
		}

		void AppendU64(std::vector<uint8_t>& output, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				output.push_back(static_cast<uint8_t>((value >> shift) & 0xffULL));
		}

		void AppendFloat(std::vector<uint8_t>& output, float value)
		{
			AppendU32(output, std::bit_cast<uint32_t>(value));
		}

		void AppendString(std::vector<uint8_t>& output, std::string_view value)
		{
			AppendU32(output, static_cast<uint32_t>(value.size()));
			output.insert(output.end(), value.begin(), value.end());
		}

		bool ReadU16(std::span<const uint8_t> input, size_t& offset, uint16_t& value)
		{
			if (input.size() - offset < 2)
				return false;
			value = static_cast<uint16_t>(input[offset]) |
				(static_cast<uint16_t>(input[offset + 1]) << 8);
			offset += 2;
			return true;
		}

		bool ReadU32(std::span<const uint8_t> input, size_t& offset, uint32_t& value)
		{
			if (input.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t byte = 0; byte < 4; ++byte)
				value |= static_cast<uint32_t>(input[offset++]) << (byte * 8);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> input, size_t& offset, uint64_t& value)
		{
			if (input.size() - offset < 8)
				return false;
			value = 0;
			for (uint32_t byte = 0; byte < 8; ++byte)
				value |= static_cast<uint64_t>(input[offset++]) << (byte * 8);
			return true;
		}

		bool ReadFloat(std::span<const uint8_t> input, size_t& offset, float& value)
		{
			uint32_t bits = 0;
			if (!ReadU32(input, offset, bits))
				return false;
			value = std::bit_cast<float>(bits);
			return std::isfinite(value);
		}

		bool ReadString(std::span<const uint8_t> input, size_t& offset,
			std::string& value)
		{
			uint32_t size = 0;
			if (!ReadU32(input, offset, size) || size > kMaximumStringBytes ||
				input.size() - offset < size)
				return false;
			value.assign(reinterpret_cast<const char*>(input.data() + offset), size);
			offset += size;
			return true;
		}

		std::vector<uint8_t> SerializeArtifact(const AssetImportResult& imported,
			AssetType type)
		{
			if (imported.Format.size() > kMaximumStringBytes ||
				imported.SubAssets.size() > kMaximumSubAssets ||
				imported.ArtifactBytes.size() > kMaximumArtifactBytes)
				return {};
			std::vector<uint8_t> output;
			output.reserve(32 + imported.Format.size() + imported.ArtifactBytes.size() +
				imported.SubAssets.size() * 48);
			output.insert(output.end(), kArtifactMagic.begin(), kArtifactMagic.end());
			AppendU32(output, kArtifactEnvelopeVersion);
			AppendU16(output, static_cast<uint16_t>(type));
			AppendString(output, imported.Format);
			AppendU32(output, static_cast<uint32_t>(imported.SubAssets.size()));
			for (const ImportedSubAsset& child : imported.SubAssets)
			{
				if (child.PersistentID.empty() || child.PersistentID.size() > kMaximumStringBytes ||
					child.Name.size() > kMaximumStringBytes || child.Type == AssetType::None)
					return {};
				AppendString(output, child.PersistentID);
				AppendString(output, child.Name);
				AppendU16(output, static_cast<uint16_t>(child.Type));
				AppendU32(output, child.Sprite.X); AppendU32(output, child.Sprite.Y);
				AppendU32(output, child.Sprite.Width); AppendU32(output, child.Sprite.Height);
				AppendFloat(output, child.Sprite.PivotX);
				AppendFloat(output, child.Sprite.PivotY);
				AppendFloat(output, child.Sprite.PixelsPerUnit);
				AppendFloat(output, child.Sprite.BorderLeft);
				AppendFloat(output, child.Sprite.BorderBottom);
				AppendFloat(output, child.Sprite.BorderRight);
				AppendFloat(output, child.Sprite.BorderTop);
			}
			AppendU64(output, imported.ArtifactBytes.size());
			output.insert(output.end(), imported.ArtifactBytes.begin(),
				imported.ArtifactBytes.end());
			return output;
		}

		bool DeserializeArtifact(std::span<const uint8_t> input, AssetType expectedType,
			ImportedArtifact& artifact)
		{
			if (input.size() < kArtifactMagic.size() ||
				!std::equal(kArtifactMagic.begin(), kArtifactMagic.end(), input.begin()))
				return false;
			size_t offset = kArtifactMagic.size();
			uint32_t version = 0;
			uint16_t rawType = 0;
			if (!ReadU32(input, offset, version)
				|| version < kOldestArtifactEnvelopeVersion
				|| version > kArtifactEnvelopeVersion ||
				!ReadU16(input, offset, rawType) ||
				static_cast<AssetType>(rawType) != expectedType ||
				!ReadString(input, offset, artifact.Format))
				return false;
			uint32_t childCount = 0;
			if (!ReadU32(input, offset, childCount) || childCount > kMaximumSubAssets)
				return false;
			std::unordered_set<std::string> persistentIDs;
			artifact.SubAssets.clear();
			artifact.SubAssets.reserve(childCount);
			for (uint32_t index = 0; index < childCount; ++index)
			{
				AssetSubAsset child;
				uint16_t childType = 0;
				if (!ReadString(input, offset, child.PersistentID) ||
					!ReadString(input, offset, child.Name) ||
					!ReadU16(input, offset, childType))
					return false;
				child.Type = static_cast<AssetType>(childType);
				if (version >= 2
					&& (!ReadU32(input, offset, child.Sprite.X)
						|| !ReadU32(input, offset, child.Sprite.Y)
						|| !ReadU32(input, offset, child.Sprite.Width)
						|| !ReadU32(input, offset, child.Sprite.Height)
						|| !ReadFloat(input, offset, child.Sprite.PivotX)
						|| !ReadFloat(input, offset, child.Sprite.PivotY)
						|| !ReadFloat(input, offset, child.Sprite.PixelsPerUnit)
						|| !ReadFloat(input, offset, child.Sprite.BorderLeft)
						|| !ReadFloat(input, offset, child.Sprite.BorderBottom)
						|| !ReadFloat(input, offset, child.Sprite.BorderRight)
						|| !ReadFloat(input, offset, child.Sprite.BorderTop)))
					return false;
				if (child.PersistentID.empty() || child.Type == AssetType::None ||
					!persistentIDs.emplace(child.PersistentID).second)
					return false;
				artifact.SubAssets.push_back(std::move(child));
			}
			uint64_t byteCount = 0;
			if (!ReadU64(input, offset, byteCount) || byteCount > kMaximumArtifactBytes ||
				input.size() - offset != byteCount)
				return false;
			artifact.Bytes.assign(input.begin() + offset, input.end());
			artifact.Type = expectedType;
			return true;
		}

		AssetLoadResult Failure(AssetLoadStatus status, std::string message)
		{
			AssetLoadResult result;
			result.Status = status;
			result.Error = std::move(message);
			return result;
		}

		bool IsCancelled(const AssetLoadOptions& options)
		{
			return options.Cancellation &&
				options.Cancellation->IsCancellationRequested();
		}

		bool HasDiscoverableDependencies(AssetType type)
		{
			return type == AssetType::Scene || type == AssetType::Prefab
				|| type == AssetType::Material;
		}

		class ScopeExit final
		{
		public:
			explicit ScopeExit(std::function<void()> callback)
				: m_Callback(std::move(callback)) {}
			~ScopeExit() { if (m_Callback) m_Callback(); }
			ScopeExit(const ScopeExit&) = delete;
			ScopeExit& operator=(const ScopeExit&) = delete;
		private:
			std::function<void()> m_Callback;
		};

	}

	bool AssetDatabase::Initialize(AssetRegistry& registry,
		const std::filesystem::path& libraryDirectory)
	{
		if (AssetJobSystem::Get().IsWorkerThread())
		{
			TC_Core_Error("AssetDatabase cannot be initialized from an asset worker thread");
			return false;
		}
		Shutdown();
		if (!registry.IsInitialized() || libraryDirectory.empty() ||
			!m_Cache.Initialize(libraryDirectory / "DerivedData"))
			return false;
		m_Registry = &registry;
		m_Importers.RegisterBuiltInImporters();
		if (!LoadDependencyCache())
			TC_Core_Warn("Could not load the rebuildable asset dependency cache");
		{
			std::scoped_lock lock(m_MetadataCommitMutex);
			if (!RebuildDiscoveredDependenciesLocked())
				TC_Core_Warn("Some Scene/Prefab/Material dependencies could not be discovered during initialization");
		}
		EnableAsyncTasks();
		return true;
	}

	void AssetDatabase::Shutdown()
	{
		if (!StopAndWaitForAsyncTasks())
		{
			TC_Core_Error("AssetDatabase shutdown was rejected on an asset worker thread");
			return;
		}
		{
			std::scoped_lock lock(m_FlightMutex);
			m_Flights.clear();
		}
		{
			std::scoped_lock lock(m_HandleFlightMutex);
			m_ActiveHandleFlights.clear();
		}
		m_HandleFlightWake.notify_all();
		{
			std::scoped_lock mutationLock(m_DependencyMutationMutex);
			std::unique_lock lock(m_GraphMutex);
			m_Dependencies.clear();
			m_Dependents.clear();
		}
		m_Importers.Clear();
		m_Cache.Shutdown();
		m_Registry = nullptr;
	}

	bool AssetDatabase::BeginAsyncTask()
	{
		std::lock_guard lock(m_AsyncTaskMutex);
		if (!m_AcceptingAsyncTasks)
			return false;
		++m_AsyncTasksInFlight;
		return true;
	}

	void AssetDatabase::FinishAsyncTask() noexcept
	{
		bool becameIdle = false;
		{
			std::lock_guard lock(m_AsyncTaskMutex);
			if (m_AsyncTasksInFlight == 0)
				return;
			becameIdle = --m_AsyncTasksInFlight == 0;
		}
		if (becameIdle)
			m_AsyncTasksIdle.notify_all();
	}

	void AssetDatabase::EnableAsyncTasks()
	{
		std::lock_guard lock(m_AsyncTaskMutex);
		m_AcceptingAsyncTasks = true;
	}

	bool AssetDatabase::StopAndWaitForAsyncTasks()
	{
		if (AssetJobSystem::Get().IsWorkerThread())
			return false;
		std::unique_lock lock(m_AsyncTaskMutex);
		m_AcceptingAsyncTasks = false;
		m_AsyncTasksIdle.wait(lock,
			[this]() { return m_AsyncTasksInFlight == 0; });
		return true;
	}

	bool AssetDatabase::SetDependencies(AssetHandle asset,
		std::vector<AssetHandle> dependencies)
	{
		if (static_cast<uint64_t>(asset) == 0)
			return false;
		for (AssetHandle dependency : dependencies)
		{
			if (static_cast<uint64_t>(dependency) == 0 || dependency == asset)
				return false;
		}
		std::sort(dependencies.begin(), dependencies.end(),
			[](AssetHandle left, AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
		dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
			dependencies.end());

		// Keep the read/modify/write graph transaction and its cache publication
		// ordered against a concurrent source-driven rebuild.
		std::scoped_lock mutationLock(m_DependencyMutationMutex);
		std::unique_lock lock(m_GraphMutex);
		for (AssetHandle dependency : dependencies)
		{
			std::vector<AssetHandle> pending{ dependency };
			std::unordered_set<AssetHandle> visited;
			while (!pending.empty())
			{
				const AssetHandle current = pending.back();
				pending.pop_back();
				if (!visited.emplace(current).second)
					continue;
				if (current == asset)
					return false;
				const auto next = m_Dependencies.find(current);
				if (next != m_Dependencies.end())
					pending.insert(pending.end(), next->second.begin(), next->second.end());
			}
		}

		const auto old = m_Dependencies.find(asset);
		if (old != m_Dependencies.end())
		{
			for (AssetHandle dependency : old->second)
			{
				auto reverse = m_Dependents.find(dependency);
				if (reverse == m_Dependents.end())
					continue;
				auto& values = reverse->second;
				values.erase(std::remove(values.begin(), values.end(), asset), values.end());
				if (values.empty())
					m_Dependents.erase(reverse);
			}
		}
		m_Dependencies.insert_or_assign(asset, dependencies);
		for (AssetHandle dependency : dependencies)
		{
			auto& reverse = m_Dependents[dependency];
			reverse.push_back(asset);
			std::sort(reverse.begin(), reverse.end(), [](AssetHandle left, AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
			reverse.erase(std::unique(reverse.begin(), reverse.end()), reverse.end());
		}
		lock.unlock();
		(void)PersistDependencyGraph();
		return true;
	}

	std::vector<AssetHandle> AssetDatabase::GetDependencies(AssetHandle asset) const
	{
		std::shared_lock lock(m_GraphMutex);
		const auto found = m_Dependencies.find(asset);
		return found == m_Dependencies.end() ? std::vector<AssetHandle>{} : found->second;
	}

	std::vector<AssetHandle> AssetDatabase::GetDependents(AssetHandle asset,
		bool transitive) const
	{
		std::shared_lock lock(m_GraphMutex);
		const auto direct = m_Dependents.find(asset);
		if (direct == m_Dependents.end())
			return {};
		if (!transitive)
			return direct->second;
		std::vector<AssetHandle> pending = direct->second;
		std::unordered_set<AssetHandle> visited;
		while (!pending.empty())
		{
			const AssetHandle current = pending.back();
			pending.pop_back();
			if (!visited.emplace(current).second)
				continue;
			const auto next = m_Dependents.find(current);
			if (next != m_Dependents.end())
				pending.insert(pending.end(), next->second.begin(), next->second.end());
		}
		std::vector<AssetHandle> result(visited.begin(), visited.end());
		std::sort(result.begin(), result.end(), [](AssetHandle left, AssetHandle right)
		{
			return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
		});
		return result;
	}

	AssetLoadResult AssetDatabase::LoadArtifact(AssetHandle handle,
		AssetLoadOptions options)
	{
		std::vector<AssetHandle> stack;
		return LoadArtifactInternal(handle, options, stack);
	}

	std::future<AssetLoadResult> AssetDatabase::LoadArtifactAsync(
		AssetHandle handle, AssetLoadOptions options)
	{
		if (!BeginAsyncTask())
		{
			AssetLoadResult result;
			result.Status = AssetLoadStatus::NotInitialized;
			result.Error = "asset database is not accepting asynchronous loads";
			std::promise<AssetLoadResult> promise;
			std::future<AssetLoadResult> future = promise.get_future();
			promise.set_value(std::move(result));
			return future;
		}

		try
		{
			const uint64_t reservation = EstimateJobReservation(handle);
			return AssetJobSystem::Get().Submit(reservation,
				[this, handle, options = std::move(options)]() mutable
				{
					try
					{
						AssetLoadResult result = LoadArtifact(handle, std::move(options));
						FinishAsyncTask();
						return result;
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
			FinishAsyncTask();
			throw;
		}
	}

	uint64_t AssetDatabase::EstimateJobReservation(AssetHandle handle)
	{
		uint64_t reservation = 0;
		std::vector<AssetHandle> pending{ handle };
		std::unordered_set<AssetHandle> visited;
		while (!pending.empty())
		{
			const AssetHandle current = pending.back();
			pending.pop_back();
			if (static_cast<uint64_t>(current) == 0
				|| !visited.emplace(current).second)
				continue;
			reservation = std::max(reservation,
				EstimateSingleJobReservation(current));
			std::vector<AssetHandle> dependencies = GetDependencies(current);
			pending.insert(pending.end(), dependencies.begin(), dependencies.end());
		}
		return reservation;
	}

	uint64_t AssetDatabase::EstimateSingleJobReservation(AssetHandle handle)
	{
		if (!m_Registry || static_cast<uint64_t>(handle) == 0)
			return 0;
		std::filesystem::path source;
		AssetType type = AssetType::None;
		{
			std::scoped_lock lock(m_MetadataCommitMutex);
			const AssetMetadata* metadata = m_Registry->GetMetadata(handle);
			if (!metadata)
				metadata = m_Registry->GetSubAssetOwner(handle);
			if (metadata && !metadata->IsMissing)
			{
				source = m_Registry->GetFileSystemPath(metadata->Handle);
				type = metadata->Type;
			}
		}
		std::error_code error;
		const uintmax_t sourceBytes = source.empty() ? 0
			: std::filesystem::file_size(source, error);
		if (error || sourceBytes == 0)
			return 0;
		auto scaled = [sourceBytes](uint64_t multiplier)
		{
			return sourceBytes > (std::numeric_limits<uint64_t>::max)() / multiplier
				? (std::numeric_limits<uint64_t>::max)()
				: static_cast<uint64_t>(sourceBytes) * multiplier;
		};
		uint64_t reservation = scaled(6);
		switch (type)
		{
			case AssetType::Texture2D:
			{
				uint64_t texturePeak = 0;
				std::string estimateError;
				if (EstimateTextureBuildMemory(source, texturePeak, estimateError))
					reservation = std::max(reservation, texturePeak);
				else
					reservation = std::max(reservation, 16ULL * 1024ULL * 1024ULL);
				break;
			}
			case AssetType::Shader:
				reservation = std::max(scaled(12), 64ULL * 1024ULL * 1024ULL);
				break;
			case AssetType::Mesh:
				// OBJ parsing keeps source text, vertex build records and ordered-map
				// nodes alive together before the packed artifact is emitted.
				reservation = std::max(scaled(32), 64ULL * 1024ULL * 1024ULL);
				break;
			case AssetType::Audio:
				reservation = std::max(scaled(8), 16ULL * 1024ULL * 1024ULL);
				break;
			case AssetType::Material:
				reservation = std::max(scaled(8), 4ULL * 1024ULL * 1024ULL);
				break;
			default:
				break;
		}
		return reservation;
	}

	bool AssetDatabase::RefreshRegistry()
	{
		std::scoped_lock lock(m_MetadataCommitMutex);
		if (!m_Registry)
			return false;
		const bool registryComplete = m_Registry->Refresh();
		const bool dependenciesComplete = RebuildDiscoveredDependenciesLocked();
		return registryComplete && dependenciesComplete;
	}

	std::optional<AssetMetadata> AssetDatabase::GetMetadataSnapshot(
		AssetHandle handle)
	{
		std::scoped_lock lock(m_MetadataCommitMutex);
		if (!m_Registry)
			return std::nullopt;
		const AssetMetadata* metadata = m_Registry->GetMetadata(handle);
		return metadata ? std::optional<AssetMetadata>(*metadata) : std::nullopt;
	}

	std::optional<AssetMetadata> AssetDatabase::GetMetadataSnapshot(
		const std::filesystem::path& path)
	{
		std::scoped_lock lock(m_MetadataCommitMutex);
		if (!m_Registry)
			return std::nullopt;
		const AssetMetadata* metadata = m_Registry->GetMetadata(path);
		return metadata ? std::optional<AssetMetadata>(*metadata) : std::nullopt;
	}

	bool AssetDatabase::FinalizeImportedArtifact(AssetLoadResult& result,
		bool refreshDependencies)
	{
		if (!result.Succeeded())
			return false;
		AssetLoadOptions options;
		if (!AcquireHandleFlight(result.Artifact.Handle, options))
			return false;
		ScopeExit handleFlight([this, handle = result.Artifact.Handle]()
			{ ReleaseHandleFlight(handle); });
		return FinalizeSubAssets(result, refreshDependencies);
	}

	AssetLoadResult AssetDatabase::LoadArtifactInternal(AssetHandle handle,
		const AssetLoadOptions& options, std::vector<AssetHandle>& stack)
	{
		if (!m_Registry)
			return Failure(AssetLoadStatus::NotInitialized, "asset database is not initialized");
		if (IsCancelled(options))
			return Failure(AssetLoadStatus::Cancelled, "asset load was cancelled");
		if (std::find(stack.begin(), stack.end(), handle) != stack.end())
			return Failure(AssetLoadStatus::DependencyCycle, "asset dependency cycle detected");
		AssetMetadata metadata;
		std::filesystem::path sourcePath;
		{
			std::scoped_lock lock(m_MetadataCommitMutex);
			const AssetMetadata* found = m_Registry->GetMetadata(handle);
			if (!found || found->IsMissing)
				return Failure(AssetLoadStatus::NotFound, "asset handle is missing");
			metadata = *found;
			sourcePath = m_Registry->GetFileSystemPath(handle);
		}
		std::shared_ptr<const IAssetImporter> importer =
			m_Importers.Find(metadata.Type);
		if (!importer)
			return Failure(AssetLoadStatus::UnsupportedType,
				"no importer is registered for asset type");

		stack.push_back(handle);
		std::vector<std::string> dependencyKeys;
		for (AssetHandle dependency : GetDependencies(handle))
		{
			AssetLoadResult loaded = LoadArtifactInternal(dependency, options, stack);
			if (!loaded.Succeeded())
			{
				stack.pop_back();
				if (loaded.Status == AssetLoadStatus::Cancelled)
					return loaded;
				return Failure(loaded.Status == AssetLoadStatus::DependencyCycle
					? AssetLoadStatus::DependencyCycle : AssetLoadStatus::DependencyFailed,
					"dependency import failed: " + loaded.Error);
			}
			dependencyKeys.push_back(std::move(loaded.Artifact.ArtifactKey));
		}
		stack.pop_back();

		// Dependencies are resolved before this gate so two cross-referencing
		// loads cannot deadlock while holding different Handle slots. Once the
		// slot is acquired, refresh metadata/path and keep it through source read,
		// hashing, artifact build and synchronous metadata finalization.
		if (!AcquireHandleFlight(handle, options))
			return Failure(AssetLoadStatus::Cancelled,
				"asset load was cancelled while waiting for an older import");
		ScopeExit handleFlight([this, handle]() { ReleaseHandleFlight(handle); });
		{
			std::scoped_lock lock(m_MetadataCommitMutex);
			const AssetMetadata* found = m_Registry->GetMetadata(handle);
			if (!found || found->IsMissing)
				return Failure(AssetLoadStatus::NotFound, "asset handle is missing");
			metadata = *found;
			sourcePath = m_Registry->GetFileSystemPath(handle);
		}
		importer = m_Importers.Find(metadata.Type);
		if (!importer)
			return Failure(AssetLoadStatus::UnsupportedType,
				"no importer is registered for the current asset type");

		std::vector<uint8_t> sourceBytes;
		std::string readError;
		if (!ReadFileForImport(sourcePath, sourceBytes,
			readError, [&options]() { return IsCancelled(options); }))
		{
			return Failure(IsCancelled(options) ? AssetLoadStatus::Cancelled :
				AssetLoadStatus::SourceReadFailed, std::move(readError));
		}
		const std::string sourceHash = ComputeContentSHA256(sourceBytes);
		AssetLoadResult result = GetOrImport(metadata, importer, sourcePath, sourceBytes,
			sourceHash, std::move(dependencyKeys), options);
		if (result.Succeeded() && !options.DeferMetadataCommit &&
			!FinalizeSubAssets(result, true))
			return Failure(AssetLoadStatus::ImportFailed,
				"import succeeded but sub-asset metadata could not be committed");
		return result;
	}

	AssetLoadResult AssetDatabase::GetOrImport(const AssetMetadata& metadata,
		const std::shared_ptr<const IAssetImporter>& importer,
		const std::filesystem::path& sourcePath,
		std::span<const uint8_t> sourceBytes, const std::string& sourceHash,
		std::vector<std::string> dependencyKeys, const AssetLoadOptions& options)
	{
		ArtifactKeyInput keyInput;
		keyInput.ImporterID = std::string(importer->GetID());
		keyInput.ImporterVersion = importer->GetVersion();
		keyInput.Type = metadata.Type;
		keyInput.SourceSHA256 = sourceHash;
		keyInput.Settings = metadata.ImportSettings;
		keyInput.Platform = options.Platform;
		keyInput.Backend = options.Backend;
		keyInput.DependencyKeys = dependencyKeys;
		const std::string artifactKey = BuildArtifactKey(std::move(keyInput));
		if (artifactKey.empty())
			return Failure(AssetLoadStatus::ImportFailed, "artifact key input is invalid");

		auto readCached = [&]() -> std::optional<AssetLoadResult>
		{
			std::vector<uint8_t> cached;
			if (!m_Cache.TryRead(artifactKey, cached))
				return std::nullopt;
			AssetLoadResult result;
			if (!DeserializeArtifact(cached, metadata.Type, result.Artifact))
				return std::nullopt;
			result.Status = AssetLoadStatus::Success;
			result.Artifact.Handle = metadata.Handle;
			result.Artifact.ArtifactKey = artifactKey;
			result.Artifact.DependencyKeys = dependencyKeys;
			result.Artifact.FromCache = true;
			return result;
		};
		for (;;)
		{
			if (std::optional<AssetLoadResult> cached = readCached())
				return std::move(*cached);

			std::shared_future<AssetLoadResult> flight;
			std::shared_ptr<std::promise<AssetLoadResult>> ownerPromise;
			{
				std::scoped_lock lock(m_FlightMutex);
				const auto existing = m_Flights.find(artifactKey);
				if (existing != m_Flights.end())
					flight = existing->second;
				else
				{
					ownerPromise = std::make_shared<std::promise<AssetLoadResult>>();
					flight = ownerPromise->get_future().share();
					m_Flights.emplace(artifactKey, flight);
				}
			}

			if (!ownerPromise)
			{
				while (flight.wait_for(std::chrono::milliseconds(2)) !=
					std::future_status::ready)
				{
					if (IsCancelled(options))
						return Failure(AssetLoadStatus::Cancelled,
							"asset load was cancelled");
				}
				AssetLoadResult shared = flight.get();
				if (shared.Status == AssetLoadStatus::Cancelled && !IsCancelled(options))
					continue;
				shared.Artifact.Handle = metadata.Handle;
				return shared;
			}

			AssetLoadResult result;
			try
			{
				if (std::optional<AssetLoadResult> cached = readCached())
					result = std::move(*cached);
				else if (IsCancelled(options))
					result = Failure(AssetLoadStatus::Cancelled, "asset load was cancelled");
				else
				{
					AssetImportRequest request;
					request.Handle = metadata.Handle;
					request.Type = metadata.Type;
					request.SourcePath = sourcePath;
					request.SourceBytes = sourceBytes;
					request.SourceSHA256 = sourceHash;
					request.Settings = metadata.ImportSettings;
					request.Platform = options.Platform;
					request.Backend = options.Backend;
					request.DependencyKeys = dependencyKeys;
					request.Cancellation = options.Cancellation;
					AssetImportResult imported = importer->Import(request);
					if (!imported.Succeeded())
						result = Failure(IsCancelled(options) || imported.Error == "cancelled"
							? AssetLoadStatus::Cancelled : AssetLoadStatus::ImportFailed,
							std::move(imported.Error));
					else
					{
						std::vector<uint8_t> serialized = SerializeArtifact(imported, metadata.Type);
						if (serialized.empty())
							result = Failure(AssetLoadStatus::ImportFailed,
								"importer returned an invalid or oversized artifact");
						else
						{
							(void)m_Cache.Publish(artifactKey, serialized);
							result.Status = AssetLoadStatus::Success;
							result.Artifact.Handle = metadata.Handle;
							result.Artifact.Type = metadata.Type;
							result.Artifact.ArtifactKey = artifactKey;
							result.Artifact.Format = std::move(imported.Format);
							result.Artifact.Bytes = std::move(imported.ArtifactBytes);
							result.Artifact.DependencyKeys = dependencyKeys;
							for (ImportedSubAsset& child : imported.SubAssets)
							{
								result.Artifact.SubAssets.push_back({ AssetHandle(0),
									std::move(child.PersistentID), std::move(child.Name),
									child.Type, child.Sprite });
							}
						}
					}
				}
			}
			catch (const std::exception& exception)
			{
				result = Failure(AssetLoadStatus::ImportFailed,
					std::string("importer threw an exception: ") + exception.what());
			}
			catch (...)
			{
				result = Failure(AssetLoadStatus::ImportFailed,
					"importer threw an unknown exception");
			}
			// Publish the promise and remove its map entry under one gate. Otherwise a
			// third caller can enter after erase but before set_value and duplicate the
			// same artifact build.
			{
				std::scoped_lock lock(m_FlightMutex);
				ownerPromise->set_value(result);
				m_Flights.erase(artifactKey);
			}
			return result;
		}
	}

	bool AssetDatabase::FinalizeSubAssets(AssetLoadResult& result,
		bool refreshDependencies)
	{
		if (!m_Registry)
			return false;
		std::scoped_lock lock(m_MetadataCommitMutex);
		std::vector<AssetSubAsset> assigned;
		if (!m_Registry->SynchronizeSubAssets(result.Artifact.Handle,
			result.Artifact.SubAssets, &assigned))
			return false;
		result.Artifact.SubAssets = std::move(assigned);
		if (refreshDependencies && !RebuildDiscoveredDependenciesLocked())
			TC_Core_Warn("Some Scene/Prefab/Material dependencies could not be refreshed after import publication");
		return true;
	}

	bool AssetDatabase::AcquireHandleFlight(AssetHandle handle,
		const AssetLoadOptions& options)
	{
		std::unique_lock lock(m_HandleFlightMutex);
		while (m_ActiveHandleFlights.contains(handle))
		{
			if (IsCancelled(options))
				return false;
			m_HandleFlightWake.wait_for(lock, std::chrono::milliseconds(2));
		}
		if (IsCancelled(options))
			return false;
		m_ActiveHandleFlights.emplace(handle);
		return true;
	}

	void AssetDatabase::ReleaseHandleFlight(AssetHandle handle)
	{
		{
			std::scoped_lock lock(m_HandleFlightMutex);
			m_ActiveHandleFlights.erase(handle);
		}
		m_HandleFlightWake.notify_all();
	}

	bool AssetDatabase::LoadDependencyCache()
	{
		if (!m_Registry)
			return false;
		std::scoped_lock cacheLock(m_DependencyCacheMutex);
		const std::filesystem::path path =
			m_Registry->GetLibraryDirectory() / "AssetDependencies.yaml";
		std::error_code statusError;
		if (!std::filesystem::exists(path, statusError))
			return !statusError;
		try
		{
			std::vector<uint8_t> bytes;
			std::string readError;
			if (!ReadFileForImport(path, bytes, readError))
				return false;
			const YAML::Node root = YAML::Load(std::string(bytes.begin(), bytes.end()));
			if (!root.IsMap() || !root["SchemaVersion"]
				|| root["SchemaVersion"].as<uint32_t>() != 1)
				return false;
			const YAML::Node assets = root["Assets"];
			if (!assets || !assets.IsSequence())
				return false;

			std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependencies;
			for (const YAML::Node& entry : assets)
			{
				if (!entry.IsMap() || !entry["Handle"] || !entry["Dependencies"]
					|| !entry["Dependencies"].IsSequence())
					return false;
				const AssetHandle owner(entry["Handle"].as<uint64_t>());
				const AssetMetadata* metadata = m_Registry->GetMetadata(owner);
				if (!metadata || metadata->IsMissing)
					continue;
				auto& values = dependencies[owner];
				for (const YAML::Node& dependency : entry["Dependencies"])
				{
					const AssetHandle handle(dependency.as<uint64_t>());
					if (static_cast<uint64_t>(handle) != 0 && handle != owner)
						values.push_back(handle);
				}
				std::sort(values.begin(), values.end(), [](AssetHandle left,
					AssetHandle right)
					{
						return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
					});
				values.erase(std::unique(values.begin(), values.end()), values.end());
			}

			std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependents;
			for (const auto& [owner, values] : dependencies)
			{
				for (AssetHandle dependency : values)
					dependents[dependency].push_back(owner);
			}
			for (auto& [dependency, values] : dependents)
			{
				(void)dependency;
				std::sort(values.begin(), values.end(), [](AssetHandle left,
					AssetHandle right)
					{
						return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
					});
				values.erase(std::unique(values.begin(), values.end()), values.end());
			}
			std::unique_lock graphLock(m_GraphMutex);
			m_Dependencies = std::move(dependencies);
			m_Dependents = std::move(dependents);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool AssetDatabase::RebuildDiscoveredDependenciesLocked()
	{
		if (!m_Registry)
			return false;
		std::scoped_lock mutationLock(m_DependencyMutationMutex);

		std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependencies;
		{
			std::shared_lock graphLock(m_GraphMutex);
			dependencies = m_Dependencies;
		}
		for (auto iterator = dependencies.begin(); iterator != dependencies.end();)
		{
			const AssetMetadata* metadata = m_Registry->GetMetadata(iterator->first);
			if (!metadata || metadata->IsMissing)
				iterator = dependencies.erase(iterator);
			else
				++iterator;
		}

		std::vector<AssetMetadata> discoverable;
		for (const auto& [handle, metadata] : m_Registry->GetAssets())
		{
			(void)handle;
			if (!metadata.IsMissing && HasDiscoverableDependencies(metadata.Type))
				discoverable.push_back(metadata);
		}
		std::sort(discoverable.begin(), discoverable.end(),
			[](const AssetMetadata& left, const AssetMetadata& right)
			{
				return static_cast<uint64_t>(left.Handle)
					< static_cast<uint64_t>(right.Handle);
			});

		bool complete = true;
		for (const AssetMetadata& metadata : discoverable)
		{
			const std::filesystem::path source =
				m_Registry->GetFileSystemPath(metadata.Handle);
			std::vector<uint8_t> bytes;
			std::string readError;
			if (!ReadFileForImport(source, bytes, readError))
			{
				TC_Core_Warn("Could not inspect dependencies for '{0}': {1}",
					PathToUTF8(metadata.FilePath), readError);
				complete = false;
				continue;
			}

			try
			{
				std::vector<AssetHandle> found;
				std::string visitorError;
				bool visited = false;
				if (metadata.Type == AssetType::Material)
				{
					std::vector<TypedAssetDependency> typedDependencies;
					visited = ParseMaterialSourceDependencies(bytes,
						typedDependencies, visitorError);
					if (visited)
					{
						for (const TypedAssetDependency& reference : typedDependencies)
						{
							AssetHandle dependency = reference.Handle;
							const AssetSubAsset* child = nullptr;
							const AssetMetadata* dependencyMetadata =
								m_Registry->GetMetadata(dependency);
							if (!dependencyMetadata)
							{
								dependencyMetadata = m_Registry->GetSubAssetOwner(
									dependency, &child);
								if (dependencyMetadata && child)
									dependency = dependencyMetadata->Handle;
							}
							if (!dependencyMetadata || dependencyMetadata->IsMissing
								|| dependencyMetadata->Type != reference.ExpectedType)
							{
								visitorError = "material dependency '" + reference.Name
									+ "' is missing or has the wrong asset type";
								visited = false;
								break;
							}
							if (dependency == metadata.Handle)
							{
								visitorError = "material cannot depend on itself";
								visited = false;
								break;
							}
							found.push_back(dependency);
						}
					}
				}
				else
				{
					const YAML::Node root = YAML::Load(std::string(bytes.begin(), bytes.end()));
					const auto collect = [&](const SerializedAssetReference& reference)
					{
						AssetHandle dependency = reference.Handle;
						if (static_cast<uint64_t>(dependency) == 0)
							return true;
						const AssetSubAsset* child = nullptr;
						if (const AssetMetadata* owner =
							m_Registry->GetSubAssetOwner(dependency, &child); owner && child)
							dependency = owner->Handle;
						if (dependency != metadata.Handle)
							found.push_back(dependency);
						return true;
					};
					visited = metadata.Type == AssetType::Scene
						? AssetReferenceVisitor::VisitScene(root, collect, visitorError)
						: AssetReferenceVisitor::VisitPrefab(root, collect, visitorError);
				}
				if (!visited)
				{
					TC_Core_Warn("Could not inspect dependencies for '{0}': {1}",
						PathToUTF8(metadata.FilePath), visitorError);
					complete = false;
					continue;
				}
				std::sort(found.begin(), found.end(), [](AssetHandle left,
					AssetHandle right)
					{
						return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
					});
				found.erase(std::unique(found.begin(), found.end()), found.end());
				if (found.empty())
					dependencies.erase(metadata.Handle);
				else
					dependencies.insert_or_assign(metadata.Handle, std::move(found));
			}
			catch (const std::exception& exception)
			{
				TC_Core_Warn("Could not inspect dependencies for '{0}': {1}",
					PathToUTF8(metadata.FilePath), exception.what());
				complete = false;
			}
		}

		std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependents;
		for (const auto& [owner, values] : dependencies)
		{
			for (AssetHandle dependency : values)
				dependents[dependency].push_back(owner);
		}
		for (auto& [dependency, values] : dependents)
		{
			(void)dependency;
			std::sort(values.begin(), values.end(), [](AssetHandle left,
				AssetHandle right)
				{
					return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
				});
			values.erase(std::unique(values.begin(), values.end()), values.end());
		}
		{
			std::unique_lock graphLock(m_GraphMutex);
			m_Dependencies = std::move(dependencies);
			m_Dependents = std::move(dependents);
		}
		if (!PersistDependencyGraph())
		{
			TC_Core_Warn("Could not persist the rebuildable asset dependency cache");
			complete = false;
		}
		return complete;
	}

	bool AssetDatabase::PersistDependencyGraph() const
	{
		if (!m_Registry || m_Registry->GetLibraryDirectory().empty())
			return false;
		// Serialize cache publication before taking the graph snapshot so an older
		// writer can never overtake and replace a newer graph on disk.
		std::scoped_lock cacheLock(m_DependencyCacheMutex);
		std::vector<std::pair<AssetHandle, std::vector<AssetHandle>>> entries;
		{
			std::shared_lock graphLock(m_GraphMutex);
			entries.reserve(m_Dependencies.size());
			for (const auto& entry : m_Dependencies)
				entries.push_back(entry);
		}
		std::sort(entries.begin(), entries.end(), [](const auto& left,
			const auto& right)
			{
				return static_cast<uint64_t>(left.first)
					< static_cast<uint64_t>(right.first);
			});

		YAML::Emitter output;
		output << YAML::BeginMap
			<< YAML::Key << "SchemaVersion" << YAML::Value << 1
			<< YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
		for (const auto& [owner, values] : entries)
		{
			output << YAML::BeginMap
				<< YAML::Key << "Handle" << YAML::Value
				<< static_cast<uint64_t>(owner)
				<< YAML::Key << "Dependencies" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq;
			for (AssetHandle dependency : values)
				output << static_cast<uint64_t>(dependency);
			output << YAML::EndSeq << YAML::EndMap;
		}
		output << YAML::EndSeq << YAML::EndMap;
		if (!output.good())
			return false;

		std::string writeError;
		return FileSystem::WriteFileAtomically(
			m_Registry->GetLibraryDirectory() / "AssetDependencies.yaml",
			output.c_str(), writeError);
	}

}
