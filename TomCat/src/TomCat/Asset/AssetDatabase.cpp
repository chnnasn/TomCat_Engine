#include "tcpch.h"
#include "AssetDatabase.h"

#include "ArtifactKey.h"
#include "AssetRegistry.h"
#include "ContentHash.h"
#include "MaterialArtifact.h"
#include "Advanced2DAuthoringAssets.h"
#include "SpriteAsset.h"
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
		constexpr uint32_t kMaximumLoadSnapshotAttempts = 4;

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

		bool SameSubAssets(const std::vector<AssetSubAsset>& left,
			const std::vector<AssetSubAsset>& right)
		{
			if (left.size() != right.size())
				return false;
			for (size_t index = 0; index < left.size(); ++index)
			{
				const AssetSubAsset& a = left[index];
				const AssetSubAsset& b = right[index];
				if (a.Handle != b.Handle || a.PersistentID != b.PersistentID
					|| a.Name != b.Name || a.Type != b.Type || !(a.Sprite == b.Sprite))
					return false;
			}
			return true;
		}

		bool SameMetadataExceptSubAssets(const AssetMetadata& left,
			const AssetMetadata& right)
		{
			return left.Handle == right.Handle && left.Type == right.Type
				&& left.FilePath == right.FilePath
				&& left.ImportSettings == right.ImportSettings
				&& left.IsMissing == right.IsMissing;
		}

		bool SameMetadata(const AssetMetadata& left, const AssetMetadata& right)
		{
			return SameMetadataExceptSubAssets(left, right)
				&& SameSubAssets(left.SubAssets, right.SubAssets);
		}

		bool SameDependencySnapshot(const AssetDependencySnapshot& left,
			const AssetDependencySnapshot& right, bool compareRevision = true)
		{
			return (!compareRevision || left.Revision == right.Revision)
				&& left.Dependencies == right.Dependencies
				&& left.ArtifactDependencies == right.ArtifactDependencies
				&& left.SourceSHA256 == right.SourceSHA256;
		}

		bool HasDiscoverableDependencies(AssetType type)
		{
			return type == AssetType::Scene || type == AssetType::Prefab
				|| type == AssetType::Material
				|| type == AssetType::AnimationClip
				|| type == AssetType::AnimatorController
				|| type == AssetType::TilePalette;
		}

		bool IsLiveDependencyGraphOwner(const AssetRegistry* registry,
			AssetHandle handle)
		{
			if (!registry)
				return false;
			if (const AssetMetadata* metadata = registry->GetMetadata(handle))
				return !metadata->IsMissing;
			const AssetSubAsset* child = nullptr;
			const AssetMetadata* owner = registry->GetSubAssetOwner(handle, &child);
			return owner && child && !owner->IsMissing;
		}

		AssetHandle ResolveDependencyArtifactHandle(const AssetRegistry* registry,
			AssetHandle dependency, bool* resolved = nullptr)
		{
			if (resolved)
				*resolved = false;
			if (!registry)
				return dependency;
			if (registry->GetMetadata(dependency))
			{
				if (resolved)
					*resolved = true;
				return dependency;
			}
			const AssetSubAsset* child = nullptr;
			if (const AssetMetadata* owner = registry->GetSubAssetOwner(dependency,
				&child); owner && child)
			{
				if (resolved)
					*resolved = true;
				return owner->Handle;
			}
			return dependency;
		}

		std::vector<AssetHandle> ResolveArtifactDependencies(
			const AssetRegistry* registry,
			const std::vector<AssetHandle>& dependencies,
			bool* fullyResolved = nullptr)
		{
			if (fullyResolved)
				*fullyResolved = true;
			std::vector<AssetHandle> resolved;
			resolved.reserve(dependencies.size());
			for (AssetHandle dependency : dependencies)
			{
				bool dependencyResolved = false;
				const AssetHandle artifact = ResolveDependencyArtifactHandle(registry,
					dependency, &dependencyResolved);
				// A rebuild with partial-resolution tracking merges prior aliases below.
				// Do not add the unresolved logical child itself as an artifact key.
				if (dependencyResolved || !fullyResolved)
					resolved.push_back(artifact);
				if (!dependencyResolved && fullyResolved)
					*fullyResolved = false;
			}
			std::sort(resolved.begin(), resolved.end(), [](AssetHandle left,
				AssetHandle right)
			{
				return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
			});
			resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
			return resolved;
		}

		std::unordered_map<AssetHandle, std::vector<AssetHandle>> BuildDependents(
			const std::unordered_map<AssetHandle, std::vector<AssetHandle>>& dependencies,
			const std::unordered_map<AssetHandle, std::vector<AssetHandle>>&
				artifactDependencies)
		{
			std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependents;
			const auto append = [&dependents](AssetHandle owner,
				const std::vector<AssetHandle>& values)
			{
				for (AssetHandle dependency : values)
					dependents[dependency].push_back(owner);
			};
			for (const auto& [owner, values] : dependencies)
				append(owner, values);
			for (const auto& [owner, values] : artifactDependencies)
				append(owner, values);
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
			return dependents;
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
				TC_Core_Warn("Some source asset dependencies could not be discovered during initialization");
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
			const bool changed = !m_Dependencies.empty()
				|| !m_ArtifactDependencies.empty() || !m_Dependents.empty()
				|| !m_DependencySourceSHA256.empty();
			m_Dependencies.clear();
			m_ArtifactDependencies.clear();
			m_Dependents.clear();
			m_DependencySourceSHA256.clear();
			if (changed && ++m_DependencyGraphRevision == 0)
				++m_DependencyGraphRevision;
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

		// Keep metadata resolution, the read/modify/write graph transaction, and
		// cache publication ordered against a concurrent source-driven rebuild.
		std::scoped_lock metadataLock(m_MetadataCommitMutex);
		std::scoped_lock mutationLock(m_DependencyMutationMutex);
		std::unique_lock lock(m_GraphMutex);
		const std::vector<AssetHandle> artifactDependencies =
			ResolveArtifactDependencies(m_Registry, dependencies);
		for (AssetHandle dependency : artifactDependencies)
		{
			if (dependency == asset)
				return false;
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
				const auto next = m_ArtifactDependencies.find(current);
				if (next != m_ArtifactDependencies.end())
					pending.insert(pending.end(), next->second.begin(), next->second.end());
			}
		}

		const auto old = m_Dependencies.find(asset);
		const bool dependenciesChanged = old == m_Dependencies.end()
			|| old->second != dependencies;
		const auto oldArtifacts = m_ArtifactDependencies.find(asset);
		const bool artifactDependenciesChanged =
			oldArtifacts == m_ArtifactDependencies.end()
			|| oldArtifacts->second != artifactDependencies;
		const bool sourceSnapshotRemoved =
			m_DependencySourceSHA256.contains(asset);
		m_Dependencies.insert_or_assign(asset, dependencies);
		m_ArtifactDependencies.insert_or_assign(asset, artifactDependencies);
		// SetDependencies is an explicit graph override, so it no longer represents
		// a dependency list parsed from one identified source snapshot.
		m_DependencySourceSHA256.erase(asset);
		auto dependents = BuildDependents(m_Dependencies, m_ArtifactDependencies);
		const bool dependentsChanged = dependents != m_Dependents;
		m_Dependents = std::move(dependents);
		if ((dependenciesChanged || artifactDependenciesChanged
			|| dependentsChanged || sourceSnapshotRemoved)
			&& ++m_DependencyGraphRevision == 0)
			++m_DependencyGraphRevision;
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

	AssetDependencySnapshot AssetDatabase::GetDependencySnapshot(
		AssetHandle asset) const
	{
		std::shared_lock lock(m_GraphMutex);
		AssetDependencySnapshot snapshot;
		if (const auto found = m_Dependencies.find(asset);
			found != m_Dependencies.end())
			snapshot.Dependencies = found->second;
		if (const auto found = m_ArtifactDependencies.find(asset);
			found != m_ArtifactDependencies.end())
			snapshot.ArtifactDependencies = found->second;
		if (const auto found = m_DependencySourceSHA256.find(asset);
			found != m_DependencySourceSHA256.end())
			snapshot.SourceSHA256 = found->second;
		snapshot.Revision = m_DependencyGraphRevision;
		return snapshot;
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
		for (uint32_t attemptIndex = 0;
			attemptIndex < kMaximumLoadSnapshotAttempts; ++attemptIndex)
		{
			LoadAttempt attempt;
			std::vector<AssetHandle> stack;
			AssetLoadResult result = LoadArtifactInternal(handle, options, stack, attempt);
			if (result.Status != AssetLoadStatus::StaleSnapshot)
				return result;
			if (IsCancelled(options))
				return Failure(AssetLoadStatus::Cancelled, "asset load was cancelled");
			// A synchronous load owns its metadata publication and can rebuild a
			// discoverable closure before retrying. Deferred workers remain read-only;
			// their owner thread refreshes the registry before it reschedules them.
			if (!options.DeferMetadataCommit)
				(void)RefreshRegistry();
		}
		return Failure(options.DeferMetadataCommit
			? AssetLoadStatus::StaleSnapshot : AssetLoadStatus::ImportFailed,
			"asset inputs changed repeatedly while loading");
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

	std::vector<AssetMetadata> AssetDatabase::GetAllMetadataSnapshots()
	{
		std::scoped_lock lock(m_MetadataCommitMutex);
		std::vector<AssetMetadata> snapshots;
		if (!m_Registry)
			return snapshots;
		snapshots.reserve(m_Registry->GetAssets().size());
		for (const auto& [handle, metadata] : m_Registry->GetAssets())
		{
			(void)handle;
			snapshots.push_back(metadata);
		}
		return snapshots;
	}

	bool AssetDatabase::GetSubAssetSnapshot(AssetHandle handle,
		AssetMetadata& owner, AssetSubAsset& subAsset)
	{
		std::scoped_lock lock(m_MetadataCommitMutex);
		if (!m_Registry)
			return false;
		const AssetSubAsset* foundSubAsset = nullptr;
		const AssetMetadata* foundOwner =
			m_Registry->GetSubAssetOwner(handle, &foundSubAsset);
		if (!foundOwner || !foundSubAsset)
			return false;
		owner = *foundOwner;
		subAsset = *foundSubAsset;
		return true;
	}



	bool AssetDatabase::FinalizeImportedArtifact(AssetLoadResult& result,
		bool refreshDependencies)
	{
		if (!result.Succeeded() || result.Artifact.LoadSnapshots.empty())
			return false;
		const AssetHandle handle = result.Artifact.Handle;
		const AssetLoadInputSnapshot& rootSnapshot =
			result.Artifact.LoadSnapshots.front();
		if (rootSnapshot.Metadata.Handle != handle
			|| rootSnapshot.SourceSHA256 != result.Artifact.SourceSHA256)
			return false;

		AssetLoadOptions options;
		options.Platform = result.Artifact.LoadPlatform;
		options.Backend = result.Artifact.LoadBackend;
		LoadAttempt attempt;
		attempt.GraphRevision = rootSnapshot.Dependencies.Revision;
		attempt.Snapshots = result.Artifact.LoadSnapshots;

		if (!AcquireHandleFlight(handle, options))
			return false;
		AssetLoadStatus status = AssetLoadStatus::Cancelled;
		{
			ScopeExit handleFlight([this, handle]() { ReleaseHandleFlight(handle); });
			status = FinalizeSubAssetsForLoad(result, refreshDependencies,
				attempt, 0, options);
		}
		if (status == AssetLoadStatus::Success)
		{
			result.Artifact.LoadSnapshots = std::move(attempt.Snapshots);
			return true;
		}
		if (status == AssetLoadStatus::StaleSnapshot)
		{
			// The coordinator treats this as a retryable completion and keeps the
			// same logical job alive with a fresh deferred load.
			result.Status = status;
			result.Error = "asset inputs changed before deferred publication";
			return true;
		}
		return false;
	}

	bool AssetDatabase::ReadLoadSnapshotLocked(AssetHandle handle,
		AssetLoadInputSnapshot& snapshot) const
	{
		if (!m_Registry)
			return false;
		const AssetMetadata* metadata = m_Registry->GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		snapshot = {};
		snapshot.Metadata = *metadata;
		snapshot.SourcePath = m_Registry->GetFileSystemPath(handle);
		if (const auto found = m_Dependencies.find(handle);
			found != m_Dependencies.end())
			snapshot.Dependencies.Dependencies = found->second;
		if (const auto found = m_ArtifactDependencies.find(handle);
			found != m_ArtifactDependencies.end())
			snapshot.Dependencies.ArtifactDependencies = found->second;
		if (const auto found = m_DependencySourceSHA256.find(handle);
			found != m_DependencySourceSHA256.end())
			snapshot.Dependencies.SourceSHA256 = found->second;
		snapshot.Dependencies.Revision = m_DependencyGraphRevision;
		return true;
	}

	bool AssetDatabase::CaptureLoadSnapshot(AssetHandle handle,
		LoadAttempt& attempt, size_t& snapshotIndex, AssetLoadResult& failure)
	{
		std::scoped_lock metadataLock(m_MetadataCommitMutex);
		std::shared_lock graphLock(m_GraphMutex);
		if (!m_Registry)
		{
			failure = Failure(AssetLoadStatus::NotInitialized,
				"asset database is not initialized");
			return false;
		}
		if (attempt.GraphRevision != 0
			&& attempt.GraphRevision != m_DependencyGraphRevision)
		{
			failure = Failure(AssetLoadStatus::StaleSnapshot,
				"asset dependency graph changed during load");
			return false;
		}
		AssetLoadInputSnapshot snapshot;
		if (!ReadLoadSnapshotLocked(handle, snapshot))
		{
			failure = Failure(AssetLoadStatus::NotFound, "asset handle is missing");
			return false;
		}
		if (attempt.GraphRevision == 0)
			attempt.GraphRevision = snapshot.Dependencies.Revision;
		snapshotIndex = attempt.Snapshots.size();
		attempt.Snapshots.push_back(std::move(snapshot));
		return true;
	}

	bool AssetDatabase::IsLoadAttemptCurrentLocked(
		const LoadAttempt& attempt) const
	{
		if (!m_Registry || attempt.GraphRevision == 0
			|| attempt.GraphRevision != m_DependencyGraphRevision)
			return false;
		for (const AssetLoadInputSnapshot& expected : attempt.Snapshots)
		{
			AssetLoadInputSnapshot current;
			if (!ReadLoadSnapshotLocked(expected.Metadata.Handle, current)
				|| current.SourcePath != expected.SourcePath
				|| !SameMetadata(current.Metadata, expected.Metadata)
				|| !SameDependencySnapshot(current.Dependencies,
					expected.Dependencies))
				return false;
		}
		return true;
	}

	bool AssetDatabase::AreLoadAttemptSourcesCurrent(
		const LoadAttempt& attempt, const AssetLoadOptions& options) const
	{
		for (const AssetLoadInputSnapshot& expected : attempt.Snapshots)
		{
			if (expected.SourceSHA256.empty())
				continue;
			std::string currentHash;
			std::string hashError;
			if (!ComputeFileContentSHA256(expected.SourcePath, currentHash, hashError,
				[&options]() { return IsCancelled(options); })
				|| currentHash != expected.SourceSHA256)
				return false;
		}
		return true;
	}

	bool AssetDatabase::IsLoadAttemptCurrent(const LoadAttempt& attempt,
		const AssetLoadOptions& options, bool validateSources)
	{
		std::scoped_lock metadataLock(m_MetadataCommitMutex);
		std::shared_lock graphLock(m_GraphMutex);
		return IsLoadAttemptCurrentLocked(attempt)
			&& (!validateSources || AreLoadAttemptSourcesCurrent(attempt, options));
	}

	bool AssetDatabase::PublishIfLoadAttemptCurrent(
		const std::string& artifactKey, std::span<const uint8_t> serialized,
		const LoadAttempt& attempt, const AssetLoadOptions& options)
	{
		// Keep the metadata and graph read gates through Publish. Every runtime
		// graph writer takes metadata -> graph, so no mutation can enter between
		// this validation and the immutable DDC publication.
		std::scoped_lock metadataLock(m_MetadataCommitMutex);
		std::shared_lock graphLock(m_GraphMutex);
		if (!IsLoadAttemptCurrentLocked(attempt)
			|| !AreLoadAttemptSourcesCurrent(attempt, options))
			return false;
		(void)m_Cache.Publish(artifactKey, serialized);
		return true;
	}

	AssetLoadStatus AssetDatabase::FinalizeSubAssetsForLoad(
		AssetLoadResult& result, bool refreshDependencies, LoadAttempt& attempt,
		size_t snapshotIndex, const AssetLoadOptions& options)
	{
		if (snapshotIndex >= attempt.Snapshots.size())
			return AssetLoadStatus::ImportFailed;
		std::scoped_lock metadataLock(m_MetadataCommitMutex);
		{
			std::shared_lock graphLock(m_GraphMutex);
			if (!IsLoadAttemptCurrentLocked(attempt)
				|| !AreLoadAttemptSourcesCurrent(attempt, options))
				return IsCancelled(options) ? AssetLoadStatus::Cancelled
					: AssetLoadStatus::StaleSnapshot;
		}

		const std::vector<AssetSubAsset> previousSubAssets =
			attempt.Snapshots[snapshotIndex].Metadata.SubAssets;
		std::vector<AssetSubAsset> assigned;
		if (!m_Registry || !m_Registry->SynchronizeSubAssets(
			result.Artifact.Handle, result.Artifact.SubAssets, &assigned))
			return AssetLoadStatus::ImportFailed;
		result.Artifact.SubAssets = std::move(assigned);
		const bool subAssetsChanged =
			!SameSubAssets(previousSubAssets, result.Artifact.SubAssets);
		if ((refreshDependencies || subAssetsChanged)
			&& !RebuildDiscoveredDependenciesLocked())
			TC_Core_Warn("Some source asset dependencies could not be refreshed after import publication");

		std::shared_lock graphLock(m_GraphMutex);
		std::vector<AssetLoadInputSnapshot> refreshed;
		refreshed.reserve(attempt.Snapshots.size());
		for (size_t index = 0; index < attempt.Snapshots.size(); ++index)
		{
			const AssetLoadInputSnapshot& expected = attempt.Snapshots[index];
			AssetLoadInputSnapshot current;
			if (!ReadLoadSnapshotLocked(expected.Metadata.Handle, current)
				|| current.SourcePath != expected.SourcePath
				|| !SameDependencySnapshot(current.Dependencies,
					expected.Dependencies, false))
				return AssetLoadStatus::StaleSnapshot;
			if (index == snapshotIndex)
			{
				if (!SameMetadataExceptSubAssets(current.Metadata,
					expected.Metadata)
					|| !SameSubAssets(current.Metadata.SubAssets,
						result.Artifact.SubAssets))
					return AssetLoadStatus::StaleSnapshot;
			}
			else if (!SameMetadata(current.Metadata, expected.Metadata))
				return AssetLoadStatus::StaleSnapshot;
			current.SourceSHA256 = expected.SourceSHA256;
			refreshed.push_back(std::move(current));
		}
		attempt.GraphRevision = m_DependencyGraphRevision;
		attempt.Snapshots = std::move(refreshed);
		if (!AreLoadAttemptSourcesCurrent(attempt, options))
			return IsCancelled(options) ? AssetLoadStatus::Cancelled
				: AssetLoadStatus::StaleSnapshot;
		return AssetLoadStatus::Success;
	}

	AssetLoadResult AssetDatabase::LoadArtifactInternal(AssetHandle handle,
		const AssetLoadOptions& options, std::vector<AssetHandle>& stack,
		LoadAttempt& attempt)
	{
		if (!m_Registry)
			return Failure(AssetLoadStatus::NotInitialized, "asset database is not initialized");
		if (IsCancelled(options))
			return Failure(AssetLoadStatus::Cancelled, "asset load was cancelled");
		if (std::find(stack.begin(), stack.end(), handle) != stack.end())
			return Failure(AssetLoadStatus::DependencyCycle, "asset dependency cycle detected");
		const bool topLevel = stack.empty();

		size_t snapshotIndex = 0;
		AssetLoadResult snapshotFailure;
		if (!CaptureLoadSnapshot(handle, attempt, snapshotIndex, snapshotFailure))
			return snapshotFailure;
		const AssetMetadata metadata = attempt.Snapshots[snapshotIndex].Metadata;
		const std::filesystem::path sourcePath =
			attempt.Snapshots[snapshotIndex].SourcePath;
		const std::vector<AssetHandle> artifactDependencies =
			attempt.Snapshots[snapshotIndex].Dependencies.ArtifactDependencies;
		const std::shared_ptr<const IAssetImporter> importer =
			m_Importers.Find(metadata.Type);
		if (!importer)
			return Failure(AssetLoadStatus::UnsupportedType,
				"no importer is registered for asset type");

		stack.push_back(handle);
		std::vector<std::string> dependencyKeys;
		for (AssetHandle dependency : artifactDependencies)
		{
			AssetLoadResult loaded =
				LoadArtifactInternal(dependency, options, stack, attempt);
			if (!loaded.Succeeded())
			{
				stack.pop_back();
				if (loaded.Status == AssetLoadStatus::Cancelled
					|| loaded.Status == AssetLoadStatus::StaleSnapshot)
					return loaded;
				return Failure(loaded.Status == AssetLoadStatus::DependencyCycle
					? AssetLoadStatus::DependencyCycle : AssetLoadStatus::DependencyFailed,
					"dependency import failed: " + loaded.Error);
			}
			dependencyKeys.push_back(std::move(loaded.Artifact.ArtifactKey));
		}
		stack.pop_back();

		auto staleFailure = [&options]()
		{
			return Failure(IsCancelled(options) ? AssetLoadStatus::Cancelled
				: AssetLoadStatus::StaleSnapshot,
				IsCancelled(options) ? "asset load was cancelled"
					: "asset metadata, source, or dependency graph changed during load");
		};
		if (!IsLoadAttemptCurrent(attempt, options, false))
			return staleFailure();

		// Dependencies are resolved before this gate so two cross-referencing
		// loads cannot deadlock while holding different handle slots.
		if (!AcquireHandleFlight(handle, options))
			return Failure(AssetLoadStatus::Cancelled,
				"asset load was cancelled while waiting for an older import");
		ScopeExit handleFlight([this, handle]() { ReleaseHandleFlight(handle); });
		if (!IsLoadAttemptCurrent(attempt, options, false))
			return staleFailure();

		std::vector<uint8_t> sourceBytes;
		std::string readError;
		if (!ReadFileForImport(sourcePath, sourceBytes,
			readError, [&options]() { return IsCancelled(options); }))
		{
			return Failure(IsCancelled(options) ? AssetLoadStatus::Cancelled :
				AssetLoadStatus::SourceReadFailed, std::move(readError));
		}
		const std::string sourceHash = ComputeContentSHA256(sourceBytes);
		attempt.Snapshots[snapshotIndex].SourceSHA256 = sourceHash;
		const std::string& dependencySourceHash =
			attempt.Snapshots[snapshotIndex].Dependencies.SourceSHA256;
		if (!dependencySourceHash.empty() && dependencySourceHash != sourceHash)
			return staleFailure();
		if (!IsLoadAttemptCurrent(attempt, options, true))
			return staleFailure();

		AssetLoadResult result = GetOrImport(metadata, importer, sourcePath, sourceBytes,
			sourceHash, std::move(dependencyKeys), options, attempt);
		if (!result.Succeeded())
		{
			if (result.Status == AssetLoadStatus::StaleSnapshot
				|| result.Status == AssetLoadStatus::Cancelled)
				return result;
			if (!IsLoadAttemptCurrent(attempt, options, true))
				return staleFailure();
			return result;
		}

		if (!options.DeferMetadataCommit)
		{
			const AssetLoadStatus finalized = FinalizeSubAssetsForLoad(result, true,
				attempt, snapshotIndex, options);
			if (finalized != AssetLoadStatus::Success)
				return finalized == AssetLoadStatus::StaleSnapshot
					? staleFailure()
					: Failure(finalized, finalized == AssetLoadStatus::Cancelled
						? "asset load was cancelled"
						: "import succeeded but sub-asset metadata could not be committed");
		}
		if (!IsLoadAttemptCurrent(attempt, options, true))
			return staleFailure();
		if (topLevel)
		{
			result.Artifact.LoadSnapshots = attempt.Snapshots;
			result.Artifact.LoadPlatform = options.Platform;
			result.Artifact.LoadBackend = options.Backend;
		}
		return result;
	}

	AssetLoadResult AssetDatabase::GetOrImport(const AssetMetadata& metadata,
		const std::shared_ptr<const IAssetImporter>& importer,
		const std::filesystem::path& sourcePath,
		std::span<const uint8_t> sourceBytes, const std::string& sourceHash,
		std::vector<std::string> dependencyKeys, const AssetLoadOptions& options,
		const LoadAttempt& attempt)
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

		auto staleFailure = [&options]()
		{
			return Failure(IsCancelled(options) ? AssetLoadStatus::Cancelled
				: AssetLoadStatus::StaleSnapshot,
				IsCancelled(options) ? "asset load was cancelled"
					: "asset inputs changed during cache access or import");
		};
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
			result.Artifact.SourceSHA256 = sourceHash;
			result.Artifact.DependencyKeys = dependencyKeys;
			result.Artifact.FromCache = true;
			return result;
		};
		for (;;)
		{
			if (std::optional<AssetLoadResult> cached = readCached())
			{
				if (!IsLoadAttemptCurrent(attempt, options, true))
					return staleFailure();
				return std::move(*cached);
			}

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
				if (shared.Status == AssetLoadStatus::StaleSnapshot)
					return shared;
				if (!IsLoadAttemptCurrent(attempt, options, true))
					return staleFailure();
				shared.Artifact.Handle = metadata.Handle;
				return shared;
			}

			AssetLoadResult result;
			try
			{
				if (std::optional<AssetLoadResult> cached = readCached())
				{
					result = IsLoadAttemptCurrent(attempt, options, true)
						? std::move(*cached) : staleFailure();
				}
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
						else if (!PublishIfLoadAttemptCurrent(artifactKey, serialized,
							attempt, options))
							result = staleFailure();
						else
						{
							result.Status = AssetLoadStatus::Success;
							result.Artifact.Handle = metadata.Handle;
							result.Artifact.Type = metadata.Type;
							result.Artifact.ArtifactKey = artifactKey;
							result.Artifact.SourceSHA256 = sourceHash;
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
			if (!root.IsMap() || !root["SchemaVersion"])
				return false;
			const uint32_t schemaVersion = root["SchemaVersion"].as<uint32_t>();
			if (schemaVersion != 1 && schemaVersion != 2)
				return false;
			const YAML::Node assets = root["Assets"];
			if (!assets || !assets.IsSequence())
				return false;

			std::unordered_map<AssetHandle, std::vector<AssetHandle>> dependencies;
			std::unordered_map<AssetHandle, std::vector<AssetHandle>>
				artifactDependencies;
			const auto readHandles = [](const YAML::Node& sequence,
				AssetHandle owner, std::vector<AssetHandle>& values)
			{
				if (!sequence || !sequence.IsSequence())
					return false;
				for (const YAML::Node& dependency : sequence)
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
				return true;
			};
			for (const YAML::Node& entry : assets)
			{
				if (!entry.IsMap() || !entry["Handle"] || !entry["Dependencies"])
					return false;
				const AssetHandle owner(entry["Handle"].as<uint64_t>());
				if (!IsLiveDependencyGraphOwner(m_Registry, owner))
					continue;
				auto& logicalValues = dependencies[owner];
				if (!readHandles(entry["Dependencies"], owner, logicalValues))
					return false;
				auto& artifactValues = artifactDependencies[owner];
				if (schemaVersion == 2)
				{
					if (!readHandles(entry["ArtifactDependencies"], owner,
						artifactValues))
						return false;
				}
				else
				{
					// Version 1 did not preserve a Sprite slice's atlas owner. Resolve
					// what is still present so existing projects upgrade automatically.
					artifactValues = ResolveArtifactDependencies(m_Registry,
						logicalValues);
				}
			}

			auto dependents = BuildDependents(dependencies, artifactDependencies);
			std::unique_lock graphLock(m_GraphMutex);
			const bool changed = dependencies != m_Dependencies
				|| artifactDependencies != m_ArtifactDependencies
				|| dependents != m_Dependents
				|| !m_DependencySourceSHA256.empty();
			m_Dependencies = std::move(dependencies);
			m_ArtifactDependencies = std::move(artifactDependencies);
			m_Dependents = std::move(dependents);
			m_DependencySourceSHA256.clear();
			if (changed && ++m_DependencyGraphRevision == 0)
				++m_DependencyGraphRevision;
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
		std::unordered_map<AssetHandle, std::vector<AssetHandle>>
			artifactDependencies;
		std::unordered_map<AssetHandle, std::string> dependencySourceHashes;
		{
			std::shared_lock graphLock(m_GraphMutex);
			dependencies = m_Dependencies;
			artifactDependencies = m_ArtifactDependencies;
			dependencySourceHashes = m_DependencySourceSHA256;
		}
		for (auto iterator = dependencies.begin(); iterator != dependencies.end();)
		{
			if (!IsLiveDependencyGraphOwner(m_Registry, iterator->first))
			{
				artifactDependencies.erase(iterator->first);
				iterator = dependencies.erase(iterator);
			}
			else
				++iterator;
		}
		for (auto iterator = artifactDependencies.begin();
			iterator != artifactDependencies.end();)
		{
			if (!dependencies.contains(iterator->first))
				iterator = artifactDependencies.erase(iterator);
			else
				++iterator;
		}

		std::vector<AssetMetadata> discoverable;
		std::unordered_set<AssetHandle> discoverableHandles;
		for (const auto& [handle, metadata] : m_Registry->GetAssets())
		{
			(void)handle;
			if (!metadata.IsMissing && HasDiscoverableDependencies(metadata.Type))
			{
				discoverable.push_back(metadata);
				discoverableHandles.emplace(metadata.Handle);
			}
		}
		for (auto iterator = dependencySourceHashes.begin();
			iterator != dependencySourceHashes.end();)
		{
			if (!discoverableHandles.contains(iterator->first))
				iterator = dependencySourceHashes.erase(iterator);
			else
				++iterator;
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
			// A failed visitor must never turn discovered edges into an explicit,
			// source-independent graph. Preserve the last verified hash, or install
			// a non-SHA marker on a cold rebuild so loads stay stale until parsing
			// succeeds.
			dependencySourceHashes.try_emplace(metadata.Handle, "<unverified>");
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
							const AssetHandle dependency = reference.Handle;
							const AssetSubAsset* child = nullptr;
							const AssetMetadata* dependencyMetadata =
								m_Registry->GetMetadata(dependency);
							if (!dependencyMetadata)
								dependencyMetadata = m_Registry->GetSubAssetOwner(
									dependency, &child);
							const AssetType effectiveType = child ? child->Type
								: (dependencyMetadata ? dependencyMetadata->Type
									: AssetType::None);
							if (!dependencyMetadata || dependencyMetadata->IsMissing
								|| effectiveType != reference.ExpectedType)
							{
								visitorError = "material dependency '" + reference.Name
									+ "' is missing or has the wrong asset type";
								visited = false;
								break;
							}
							if (dependency == metadata.Handle
								|| dependencyMetadata->Handle == metadata.Handle)
							{
								visitorError = "material cannot depend on itself";
								visited = false;
								break;
							}
							// Keep the serialized logical handle in the graph so Cook
							// packages an exact Sprite slice. Import dependency keys
							// resolve this handle to its source atlas separately.
							found.push_back(dependency);
						}
					}
				}
				else if (metadata.Type == AssetType::AnimationClip
					|| metadata.Type == AssetType::AnimatorController
					|| metadata.Type == AssetType::TilePalette)
				{
					const auto collectAuthoring = [&](
						const AuthoringAssetReference& reference)
					{
						const AssetHandle dependency = reference.Handle;
						const bool builtInSprite = FindBuiltInSpriteAsset(dependency) != nullptr;
						const AssetSubAsset* child = nullptr;
						const AssetMetadata* dependencyMetadata = builtInSprite
							? nullptr : m_Registry->GetMetadata(dependency);
						if (!builtInSprite && !dependencyMetadata)
							dependencyMetadata = m_Registry->GetSubAssetOwner(dependency,
								&child);
						const AssetType effectiveType = builtInSprite
							? AssetType::Texture2D
							: (child ? child->Type : (dependencyMetadata
								? dependencyMetadata->Type : AssetType::None));
						if (static_cast<uint64_t>(dependency) == 0
							|| (!builtInSprite && (!dependencyMetadata
								|| dependencyMetadata->IsMissing))
							|| effectiveType != reference.ExpectedType)
						{
							visitorError = "authoring asset reference '"
								+ reference.PropertyPath
								+ "' is missing or has the wrong asset type";
							return false;
						}
						if (dependency == metadata.Handle
							|| (dependencyMetadata
								&& dependencyMetadata->Handle == metadata.Handle))
						{
							visitorError = "authoring asset reference '"
								+ reference.PropertyPath + "' depends on itself";
							return false;
						}
						found.push_back(dependency);
						return true;
					};
					if (metadata.Type == AssetType::AnimationClip)
					{
						AnimationClipAsset asset;
						visited = AnimationClipAssetCodec::Decode(bytes, asset,
							visitorError)
							&& AnimationClipAssetCodec::VisitAssetReferences(asset,
								collectAuthoring, visitorError);
					}
					else if (metadata.Type == AssetType::AnimatorController)
					{
						AnimatorControllerAsset asset;
						visited = AnimatorControllerAssetCodec::Decode(bytes, asset,
							visitorError)
							&& AnimatorControllerAssetCodec::VisitAssetReferences(asset,
								collectAuthoring, visitorError);
					}
					else
					{
						TilePaletteAsset asset;
						visited = TilePaletteAssetCodec::Decode(bytes, asset,
							visitorError)
							&& TilePaletteAssetCodec::VisitAssetReferences(asset,
								collectAuthoring, visitorError);
					}
				}
				else
				{
					const YAML::Node root = YAML::Load(std::string(bytes.begin(), bytes.end()));
					const auto collect = [&](const SerializedAssetReference& reference)
					{
						const AssetHandle dependency = reference.Handle;
						if (static_cast<uint64_t>(dependency) == 0)
							return true;
						// Preserve the serialized logical handle. Artifact-key imports and
						// reverse invalidation resolve a Sprite slice to its atlas owner in
						// the separate artifact dependency graph below.
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
				dependencySourceHashes.insert_or_assign(metadata.Handle,
					ComputeContentSHA256(bytes));
				std::sort(found.begin(), found.end(), [](AssetHandle left,
					AssetHandle right)
					{
						return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
					});
				found.erase(std::unique(found.begin(), found.end()), found.end());
				std::vector<AssetHandle> foundArtifacts =
					ResolveArtifactDependencies(m_Registry, found);
				if (found.empty())
				{
					dependencies.erase(metadata.Handle);
					artifactDependencies.erase(metadata.Handle);
				}
				else
				{
					dependencies.insert_or_assign(metadata.Handle, std::move(found));
					artifactDependencies.insert_or_assign(metadata.Handle,
						std::move(foundArtifacts));
				}
			}
			catch (const std::exception& exception)
			{
				TC_Core_Warn("Could not inspect dependencies for '{0}': {1}",
					PathToUTF8(metadata.FilePath), exception.what());
				complete = false;
			}
		}

		// Registry refresh can transfer a stable logical child handle to another
		// artifact owner. Re-resolve every graph entry, including explicit edges on
		// asset types that have no source dependency visitor. When a logical child is
		// temporarily absent, retain its persisted owner so that restoring or changing
		// the old atlas still invalidates the dependent.
		for (const auto& [owner, logicalDependencies] : dependencies)
		{
			bool fullyResolved = false;
			std::vector<AssetHandle> resolved = ResolveArtifactDependencies(
				m_Registry, logicalDependencies, &fullyResolved);
			const auto existing = artifactDependencies.find(owner);
			if (fullyResolved || existing == artifactDependencies.end())
				artifactDependencies.insert_or_assign(owner, std::move(resolved));
			else
			{
				// A missing logical child has no current owner mapping. Keep every
				// persisted alias while also publishing owners resolved in this refresh.
				resolved.insert(resolved.end(), existing->second.begin(),
					existing->second.end());
				std::sort(resolved.begin(), resolved.end(), [](AssetHandle left,
					AssetHandle right)
				{
					return static_cast<uint64_t>(left) < static_cast<uint64_t>(right);
				});
				resolved.erase(std::unique(resolved.begin(), resolved.end()),
					resolved.end());
				existing->second = std::move(resolved);
			}
		}

		auto dependents = BuildDependents(dependencies, artifactDependencies);
		{
			std::unique_lock graphLock(m_GraphMutex);
			const bool changed = dependencies != m_Dependencies
				|| artifactDependencies != m_ArtifactDependencies
				|| dependents != m_Dependents
				|| dependencySourceHashes != m_DependencySourceSHA256;
			m_Dependencies = std::move(dependencies);
			m_ArtifactDependencies = std::move(artifactDependencies);
			m_Dependents = std::move(dependents);
			m_DependencySourceSHA256 = std::move(dependencySourceHashes);
			if (changed && ++m_DependencyGraphRevision == 0)
				++m_DependencyGraphRevision;
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
		struct DependencyCacheEntry
		{
			AssetHandle Owner;
			std::vector<AssetHandle> Dependencies;
			std::vector<AssetHandle> ArtifactDependencies;
		};
		std::vector<DependencyCacheEntry> entries;
		{
			std::shared_lock graphLock(m_GraphMutex);
			entries.reserve(m_Dependencies.size());
			for (const auto& [owner, values] : m_Dependencies)
			{
				DependencyCacheEntry entry{ owner, values, {} };
				if (const auto found = m_ArtifactDependencies.find(owner);
					found != m_ArtifactDependencies.end())
					entry.ArtifactDependencies = found->second;
				entries.push_back(std::move(entry));
			}
		}
		std::sort(entries.begin(), entries.end(), [](const auto& left,
			const auto& right)
		{
			return static_cast<uint64_t>(left.Owner)
				< static_cast<uint64_t>(right.Owner);
		});

		YAML::Emitter output;
		output << YAML::BeginMap
			<< YAML::Key << "SchemaVersion" << YAML::Value << 2
			<< YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
		for (const DependencyCacheEntry& entry : entries)
		{
			output << YAML::BeginMap
				<< YAML::Key << "Handle" << YAML::Value
				<< static_cast<uint64_t>(entry.Owner)
				<< YAML::Key << "Dependencies" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq;
			for (AssetHandle dependency : entry.Dependencies)
				output << static_cast<uint64_t>(dependency);
			output << YAML::EndSeq
				<< YAML::Key << "ArtifactDependencies" << YAML::Value << YAML::Flow
				<< YAML::BeginSeq;
			for (AssetHandle dependency : entry.ArtifactDependencies)
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
