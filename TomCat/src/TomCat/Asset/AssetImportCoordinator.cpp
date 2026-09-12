#include "tcpch.h"
#include "AssetImportCoordinator.h"

#include "AssetRegistry.h"
#include "ContentHash.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_set>

namespace TomCat {

	namespace {

		std::string FileKey(const std::filesystem::path& relativePath)
		{
			std::string key = PathToUTF8(relativePath.lexically_normal());
			std::replace(key.begin(), key.end(), '\\', '/');
#if defined(TC_PLATFORM_WINDOWS)
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
#endif
			return key;
		}

		bool IsMetadataTemporaryFile(const std::filesystem::path& path)
		{
			std::string name = PathToUTF8(path.filename());
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
			return name.find(".tcmeta.tmp-") != std::string::npos;
		}

		std::filesystem::path SourcePathForChange(
			const std::filesystem::path& relativePath)
		{
			if (!AssetRegistry::IsMetaFile(relativePath))
				return relativePath.lexically_normal();
			std::filesystem::path source = relativePath;
			source.replace_extension();
			return source.lexically_normal();
		}

		bool SameImportIdentity(const std::optional<AssetMetadata>& left,
			const std::optional<AssetMetadata>& right)
		{
			if (left.has_value() != right.has_value())
				return false;
			if (!left)
				return true;
			return left->Handle == right->Handle && left->Type == right->Type &&
				left->FilePath.lexically_normal() == right->FilePath.lexically_normal() &&
				left->ImportSettings == right->ImportSettings &&
				left->IsMissing == right->IsMissing;
		}

		AssetLoadResult CoordinatorFailure(AssetLoadStatus status,
			std::string message)
		{
			AssetLoadResult result;
			result.Status = status;
			result.Error = std::move(message);
			return result;
		}

	}

	AssetImportCoordinator::~AssetImportCoordinator()
	{
		Shutdown();
	}

	bool AssetImportCoordinator::Initialize(AssetRegistry& registry,
		AssetDatabase& database, const std::filesystem::path& assetRoot,
		AssetImportCoordinatorOptions options)
	{
		Shutdown();
		if (!registry.IsInitialized() || !database.IsInitialized() || assetRoot.empty())
			return false;
		m_Registry = &registry;
		m_Database = &database;
		m_AssetRoot = assetRoot.lexically_normal();
		m_Options = std::move(options);
		m_Options.PollInterval = (std::max)(m_Options.PollInterval,
			std::chrono::milliseconds(10));
		m_Options.Debounce = (std::max)(m_Options.Debounce,
			std::chrono::milliseconds(0));
		m_Options.ContentVerificationInterval = (std::max)(
			m_Options.ContentVerificationInterval, m_Options.PollInterval);
		m_MainThread = std::this_thread::get_id();

		FileSnapshot initial;
		if (!BuildSnapshot(initial, nullptr, true))
		{
			m_Registry = nullptr;
			m_Database = nullptr;
			m_AssetRoot.clear();
			return false;
		}
		{
			std::scoped_lock lock(m_WatcherMutex);
			m_Snapshot = std::move(initial);
		}
		m_Initialized.store(true, std::memory_order_release);
		return true;
	}

	bool AssetImportCoordinator::Start()
	{
		if (!IsInitialized())
			return false;
		if (IsRunning())
			return true;
		{
			std::scoped_lock lock(m_WatcherMutex);
			m_StopRequested = false;
		}
		try
		{
			m_Watcher = std::thread(&AssetImportCoordinator::WatcherMain, this);
			m_Running.store(true, std::memory_order_release);
			return true;
		}
		catch (const std::exception& exception)
		{
			TC_Core_Error("Could not start asset file monitor: {0}", exception.what());
			return false;
		}
	}

	void AssetImportCoordinator::Stop()
	{
		{
			std::scoped_lock lock(m_WatcherMutex);
			m_StopRequested = true;
		}
		m_WatcherWake.notify_all();
		if (m_Watcher.joinable())
			m_Watcher.join();
		m_Running.store(false, std::memory_order_release);
		CancelAndWaitJobs();
		{
			std::scoped_lock lock(m_WatcherMutex);
			m_DebouncedChanges.clear();
			m_ReadyChanges.clear();
		}
	}

	void AssetImportCoordinator::Shutdown()
	{
		Stop();
		m_Initialized.store(false, std::memory_order_release);
		{
			std::scoped_lock lock(m_WatcherMutex);
			m_Snapshot.clear();
			m_ScanRequest = 0;
			m_StopRequested = false;
		}
		m_Revision = 0;
		m_MainThread = {};
		m_AssetRoot.clear();
		m_Database = nullptr;
		m_Registry = nullptr;
	}

	void AssetImportCoordinator::RequestScan()
	{
		{
			std::scoped_lock lock(m_WatcherMutex);
			// Recheck while synchronized with Shutdown's queue reset. An observer
			// that raced a shutdown must not leave a scan request for a later
			// reinitialization of this coordinator instance.
			if (!m_Initialized.load(std::memory_order_acquire))
				return;
			++m_ScanRequest;
		}
		m_WatcherWake.notify_all();
	}

	bool AssetImportCoordinator::RequestReimport(AssetHandle handle)
	{
		if (!IsInitialized() || std::this_thread::get_id() != m_MainThread ||
			static_cast<uint64_t>(handle) == 0)
			return false;
		const std::optional<AssetMetadata> metadata =
			m_Database->GetMetadataSnapshot(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		std::scoped_lock lock(m_WatcherMutex);
		m_ReadyChanges.push_back({ AssetFileChangeKind::Modified,
			metadata->FilePath });
		return true;
	}

	bool AssetImportCoordinator::BuildSnapshot(FileSnapshot& snapshot,
		const FileSnapshot* baseline, bool verifyAllContent) const
	{
		snapshot.clear();
		std::error_code error;
		if (!std::filesystem::is_directory(m_AssetRoot, error) || error)
			return false;

		std::filesystem::recursive_directory_iterator iterator(m_AssetRoot,
			std::filesystem::directory_options::none, error);
		const std::filesystem::recursive_directory_iterator end;
		if (error)
			return false;
		while (iterator != end)
		{
			const std::filesystem::path path = iterator->path();
			const std::filesystem::file_status status = iterator->symlink_status(error);
			if (error)
				return false;
			if (std::filesystem::is_regular_file(status) &&
				!std::filesystem::is_symlink(status) &&
				!IsMetadataTemporaryFile(path))
			{
				std::filesystem::path relative = path.lexically_relative(m_AssetRoot);
				if (relative.empty() || relative == "." ||
					(relative.begin() != relative.end() && *relative.begin() == ".."))
					return false;
				FileState state;
				state.RelativePath = relative.lexically_normal();
				state.FileSize = std::filesystem::file_size(path, error);
				if (error)
					return false;
				state.WriteTime = std::filesystem::last_write_time(path, error);
				if (error)
					return false;
				const std::string key = FileKey(state.RelativePath);
				const auto previous = baseline ? baseline->find(key) :
					FileSnapshot::const_iterator{};
				if (!verifyAllContent && baseline && previous != baseline->end() &&
					previous->second.FileSize == state.FileSize &&
					previous->second.WriteTime == state.WriteTime)
					state.ContentHash = previous->second.ContentHash;
				else
				{
					std::string readError;
					if (!ComputeFileContentSHA256(path, state.ContentHash,
						readError, [this]()
						{
							return m_StopRequested.load(std::memory_order_acquire);
						}))
						return false;
				}
				snapshot.insert_or_assign(key, std::move(state));
			}
			iterator.increment(error);
			if (error)
				return false;
		}
		return true;
	}

	void AssetImportCoordinator::WatcherMain()
	{
		uint64_t observedRequest = 0;
		Clock::time_point nextContentVerification =
			Clock::now() + m_Options.ContentVerificationInterval;
		for (;;)
		{
			bool explicitScan = false;
			FileSnapshot baseline;
			{
				std::unique_lock lock(m_WatcherMutex);
				m_WatcherWake.wait_for(lock, m_Options.PollInterval, [&]()
				{
					return m_StopRequested || m_ScanRequest != observedRequest;
				});
				if (m_StopRequested)
					break;
				explicitScan = m_ScanRequest != observedRequest;
				observedRequest = m_ScanRequest;
				baseline = m_Snapshot;
			}

			FileSnapshot snapshot;
			const Clock::time_point now = Clock::now();
			const bool verifyAllContent =
				explicitScan || now >= nextContentVerification;
			if (verifyAllContent)
				nextContentVerification =
					now + m_Options.ContentVerificationInterval;
			if (BuildSnapshot(snapshot, &baseline, verifyAllContent))
				ObserveSnapshot(std::move(snapshot), now);
			else
			{
				std::scoped_lock lock(m_WatcherMutex);
				PublishDebounced(now);
			}
		}
	}

	void AssetImportCoordinator::ObserveSnapshot(FileSnapshot snapshot,
		Clock::time_point now)
	{
		std::scoped_lock lock(m_WatcherMutex);
		for (const auto& [key, state] : snapshot)
		{
			const auto previous = m_Snapshot.find(key);
			if (previous == m_Snapshot.end())
				QueueChange(key, state, AssetFileChangeKind::Added, now);
			else if (previous->second.ContentHash != state.ContentHash)
				QueueChange(key, state, AssetFileChangeKind::Modified, now);
		}
		for (const auto& [key, state] : m_Snapshot)
		{
			if (snapshot.find(key) == snapshot.end())
				QueueChange(key, state, AssetFileChangeKind::Removed, now);
		}
		m_Snapshot = std::move(snapshot);
		PublishDebounced(now);
	}

	void AssetImportCoordinator::QueueChange(const std::string& key,
		const FileState& state, AssetFileChangeKind kind, Clock::time_point now)
	{
		const auto found = m_DebouncedChanges.find(key);
		if (found == m_DebouncedChanges.end())
		{
			m_DebouncedChanges.emplace(key,
				PendingChange{ kind, state.RelativePath, now });
			return;
		}

		PendingChange& pending = found->second;
		if (pending.Kind == AssetFileChangeKind::Added &&
			kind == AssetFileChangeKind::Removed)
		{
			m_DebouncedChanges.erase(found);
			return;
		}
		if (pending.Kind == AssetFileChangeKind::Removed &&
			kind == AssetFileChangeKind::Added)
			pending.Kind = AssetFileChangeKind::Modified;
		else if (pending.Kind != AssetFileChangeKind::Added)
			pending.Kind = kind;
		pending.RelativePath = state.RelativePath;
		pending.LastObserved = now;
	}

	void AssetImportCoordinator::PublishDebounced(Clock::time_point now)
	{
		std::vector<std::string> publish;
		for (const auto& [key, pending] : m_DebouncedChanges)
		{
			if (now - pending.LastObserved >= m_Options.Debounce)
				publish.push_back(key);
		}
		std::sort(publish.begin(), publish.end());
		for (const std::string& key : publish)
		{
			auto found = m_DebouncedChanges.find(key);
			if (found == m_DebouncedChanges.end())
				continue;
			m_ReadyChanges.push_back({ found->second.Kind,
				found->second.RelativePath });
			m_DebouncedChanges.erase(found);
		}
	}

	std::vector<AssetImportCoordinator::ReadyChange>
		AssetImportCoordinator::DrainReadyChanges()
	{
		std::vector<ReadyChange> changes;
		std::scoped_lock lock(m_WatcherMutex);
		changes.assign(m_ReadyChanges.begin(), m_ReadyChanges.end());
		m_ReadyChanges.clear();
		return changes;
	}

	size_t AssetImportCoordinator::PumpMainThread(const Callback& callback)
	{
		if (!IsInitialized() || std::this_thread::get_id() != m_MainThread)
			return 0;
		std::vector<ReadyChange> changes = DrainReadyChanges();
		size_t published = 0;
		if (!changes.empty())
			published = ProcessChanges(std::move(changes), callback);
		return published + PublishCompletedJobs(callback);
	}

	size_t AssetImportCoordinator::ProcessChanges(std::vector<ReadyChange> changes,
		const Callback& callback)
	{
		const uint64_t revision = ++m_Revision;

		struct LogicalChange
		{
			std::filesystem::path SourcePath;
			bool SourceChanged = false;
			AssetFileChangeKind SourceKind = AssetFileChangeKind::Modified;
			bool MetadataChanged = false;
			AssetFileChangeKind MetadataKind = AssetFileChangeKind::Modified;
			std::optional<AssetMetadata> Before;
			std::optional<AssetMetadata> After;
		};
		std::unordered_map<std::string, LogicalChange> logicalChanges;
		for (const ReadyChange& change : changes)
		{
			const std::filesystem::path source = SourcePathForChange(change.RelativePath);
			LogicalChange& logical = logicalChanges[FileKey(source)];
			logical.SourcePath = source;
			if (AssetRegistry::IsMetaFile(change.RelativePath))
			{
				logical.MetadataChanged = true;
				logical.MetadataKind = change.Kind;
			}
			else
			{
				logical.SourceChanged = true;
				logical.SourceKind = change.Kind;
			}
		}

		for (auto& [key, logical] : logicalChanges)
		{
			(void)key;
			logical.Before = m_Database->GetMetadataSnapshot(logical.SourcePath);
		}
		const bool registryComplete = m_Database->RefreshRegistry();
		if (!registryComplete)
			TC_Core_Warn("Asset monitor applied a partial registry refresh; damaged sidecars remain quarantined");
		for (auto& [key, logical] : logicalChanges)
		{
			(void)key;
			logical.After = m_Database->GetMetadataSnapshot(logical.SourcePath);
		}

		struct Candidate
		{
			AssetHandle Handle = AssetHandle(0);
			std::filesystem::path RelativePath;
			AssetFileChangeKind Change = AssetFileChangeKind::Modified;
			bool IsDependency = false;
		};
		std::unordered_map<AssetHandle, Candidate> candidates;
		std::vector<AssetImportEvent> immediate;
		std::unordered_set<AssetHandle> dependencyRoots;

		for (const auto& [key, logical] : logicalChanges)
		{
			(void)key;
			if (!logical.SourceChanged && logical.MetadataChanged &&
				SameImportIdentity(logical.Before, logical.After))
				continue;

			if (logical.Before && !logical.Before->IsMissing)
				dependencyRoots.emplace(logical.Before->Handle);
			if (logical.After && !logical.After->IsMissing)
				dependencyRoots.emplace(logical.After->Handle);

			if (logical.Before && (!logical.After || logical.After->IsMissing ||
				logical.After->Handle != logical.Before->Handle))
			{
				const std::optional<AssetMetadata> relocated =
					m_Database->GetMetadataSnapshot(logical.Before->Handle);
				if (!relocated || relocated->IsMissing)
				{
					AssetImportEvent event;
					event.Revision = revision;
					event.Change = AssetFileChangeKind::Removed;
					event.FilePath = logical.SourcePath;
					event.Handle = logical.Before->Handle;
					event.Result = CoordinatorFailure(AssetLoadStatus::NotFound,
						"asset source was removed");
					immediate.push_back(std::move(event));
				}
			}

			if (logical.After && !logical.After->IsMissing)
			{
				AssetFileChangeKind kind = AssetFileChangeKind::Modified;
				if (!logical.Before || logical.Before->IsMissing ||
					logical.Before->Handle != logical.After->Handle)
					kind = AssetFileChangeKind::Added;
				else if (logical.SourceChanged)
					kind = logical.SourceKind == AssetFileChangeKind::Removed
						? AssetFileChangeKind::Modified : logical.SourceKind;
				candidates.insert_or_assign(logical.After->Handle,
					Candidate{ logical.After->Handle, logical.After->FilePath, kind, false });
			}
			else if (!logical.Before)
			{
				AssetImportEvent event;
				event.Revision = revision;
				event.Change = logical.SourceChanged ? logical.SourceKind :
					logical.MetadataKind;
				event.FilePath = logical.SourcePath;
				event.Result = CoordinatorFailure(AssetLoadStatus::NotFound,
					"changed file has no valid asset metadata");
				immediate.push_back(std::move(event));
			}
		}

		for (AssetHandle root : dependencyRoots)
		{
			for (AssetHandle dependent : m_Database->GetDependents(root, true))
			{
				if (candidates.find(dependent) != candidates.end())
					continue;
				const std::optional<AssetMetadata> metadata =
					m_Database->GetMetadataSnapshot(dependent);
				if (!metadata || metadata->IsMissing)
					continue;
				candidates.emplace(dependent, Candidate{ dependent,
					metadata->FilePath, AssetFileChangeKind::Modified, true });
			}
		}

		std::vector<Candidate> ordered;
		ordered.reserve(candidates.size());
		for (auto& [handle, candidate] : candidates)
		{
			(void)handle;
			ordered.push_back(std::move(candidate));
		}
		std::sort(ordered.begin(), ordered.end(), [](const Candidate& left,
			const Candidate& right)
		{
			if (left.IsDependency != right.IsDependency)
				return !left.IsDependency;
			return static_cast<uint64_t>(left.Handle) <
				static_cast<uint64_t>(right.Handle);
		});

		std::unordered_set<AssetHandle> affectedHandles;
		affectedHandles.reserve(ordered.size() + immediate.size());
		for (const Candidate& candidate : ordered)
			affectedHandles.emplace(candidate.Handle);
		for (const AssetImportEvent& event : immediate)
		{
			if (static_cast<uint64_t>(event.Handle) != 0)
				affectedHandles.emplace(event.Handle);
		}
		// A newer revision supersedes only work for the same logical asset. Jobs
		// for unrelated files must be allowed to finish, otherwise two overlapping
		// file-change batches can silently lose the first import.
		SupersedeAffectedJobs(affectedHandles);

		for (const Candidate& candidate : ordered)
		{
			const auto running = std::find_if(m_Jobs.begin(), m_Jobs.end(),
				[&candidate](const ImportJob& job)
				{
					return job.Handle == candidate.Handle;
				});
			if (running != m_Jobs.end())
			{
				// Keep at most one worker plus one newest request per asset. Repeated
				// saves replace this slot instead of spawning an unbounded set of
				// std::async imports while a slow importer is still unwinding.
				running->Replacement = PendingImport{ revision, candidate.Change,
					candidate.RelativePath, candidate.Handle,
					candidate.IsDependency };
				continue;
			}
			AssetLoadOptions options;
			options.Platform = m_Options.Platform;
			options.Backend = m_Options.Backend;
			options.Cancellation = std::make_shared<AssetLoadCancellation>();
			options.DeferMetadataCommit = true;
			ImportJob job;
			job.Revision = revision;
			job.Change = candidate.Change;
			job.RelativePath = candidate.RelativePath;
			job.Handle = candidate.Handle;
			job.IsDependency = candidate.IsDependency;
			job.Cancellation = options.Cancellation;
			try
			{
				job.Future = m_Database->LoadArtifactAsync(candidate.Handle,
					std::move(options));
			}
			catch (const std::exception& exception)
			{
				AssetImportEvent event;
				event.Revision = revision;
				event.Change = candidate.Change;
				event.FilePath = candidate.RelativePath;
				event.Handle = candidate.Handle;
				event.IsDependency = candidate.IsDependency;
				event.Result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
					std::string("could not schedule background import: ")
					+ exception.what());
				immediate.push_back(std::move(event));
				continue;
			}
			catch (...)
			{
				AssetImportEvent event;
				event.Revision = revision;
				event.Change = candidate.Change;
				event.FilePath = candidate.RelativePath;
				event.Handle = candidate.Handle;
				event.IsDependency = candidate.IsDependency;
				event.Result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
					"could not schedule background import");
				immediate.push_back(std::move(event));
				continue;
			}
			m_Jobs.push_back(std::move(job));
			m_JobCount.fetch_add(1, std::memory_order_release);
		}

		std::sort(immediate.begin(), immediate.end(), [](const AssetImportEvent& left,
			const AssetImportEvent& right)
		{
			return FileKey(left.FilePath) < FileKey(right.FilePath);
		});
		size_t published = 0;
		for (const AssetImportEvent& event : immediate)
		{
			if (callback)
				callback(event);
			++published;
		}
		return published;
	}

	size_t AssetImportCoordinator::PublishCompletedJobs(const Callback& callback)
	{
		size_t published = 0;
		for (auto iterator = m_Jobs.begin(); iterator != m_Jobs.end();)
		{
			if (iterator->Future.wait_for(std::chrono::milliseconds(0)) !=
				std::future_status::ready)
			{
				++iterator;
				continue;
			}
			AssetLoadResult result;
			try
			{
				result = iterator->Future.get();
			}
			catch (const std::exception& exception)
			{
				result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
					std::string("background import threw: ") + exception.what());
			}
			catch (...)
			{
				result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
					"background import threw an unknown exception");
			}
			if (iterator->Replacement)
			{
				PendingImport replacement = std::move(*iterator->Replacement);
				iterator->Revision = replacement.Revision;
				iterator->Change = replacement.Change;
				iterator->RelativePath = std::move(replacement.RelativePath);
				iterator->Handle = replacement.Handle;
				iterator->IsDependency = replacement.IsDependency;
				iterator->Superseded = false;
				iterator->Replacement.reset();
				AssetLoadOptions options;
				options.Platform = m_Options.Platform;
				options.Backend = m_Options.Backend;
				options.Cancellation = std::make_shared<AssetLoadCancellation>();
				options.DeferMetadataCommit = true;
				iterator->Cancellation = options.Cancellation;
				try
				{
					iterator->Future = m_Database->LoadArtifactAsync(iterator->Handle,
						std::move(options));
					++iterator;
					continue;
				}
				catch (const std::exception& exception)
				{
					result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
						std::string("could not schedule replacement import: ")
						+ exception.what());
				}
				catch (...)
				{
					result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
						"could not schedule replacement import");
				}
			}
			if (iterator->Superseded)
			{
				iterator = m_Jobs.erase(iterator);
				m_JobCount.fetch_sub(1, std::memory_order_release);
				continue;
			}
			// ProcessChanges already rebuilt the dependency graph for this revision.
			// Avoid reparsing every Scene/Prefab once per completion in the same batch.
			if (result.Succeeded() && !m_Database->FinalizeImportedArtifact(result, false))
				result = CoordinatorFailure(AssetLoadStatus::ImportFailed,
					"import succeeded but metadata publication failed");

			AssetImportEvent event;
			event.Revision = iterator->Revision;
			event.Change = iterator->Change;
			event.FilePath = iterator->RelativePath;
			event.Handle = iterator->Handle;
			event.IsDependency = iterator->IsDependency;
			event.Result = std::move(result);
			if (!event.Result.Succeeded() &&
				event.Result.Status != AssetLoadStatus::Cancelled)
			{
				TC_Core_Warn("Background import failed for '{0}': {1}",
					PathToUTF8(event.FilePath), event.Result.Error);
			}
			++published;
			iterator = m_Jobs.erase(iterator);
			m_JobCount.fetch_sub(1, std::memory_order_release);
			// Erase the consumed future before invoking user code. If the callback
			// throws, the coordinator remains in a valid state and will not call
			// Future::get twice on the next pump.
			if (callback)
				callback(event);
		}
		return published;
	}

	void AssetImportCoordinator::SupersedeAffectedJobs(
		const std::unordered_set<AssetHandle>& handles)
	{
		if (handles.empty())
			return;
		for (ImportJob& job : m_Jobs)
		{
			if (!handles.contains(job.Handle))
				continue;
			job.Superseded = true;
			if (job.Cancellation)
				job.Cancellation->Cancel();
		}
	}

	void AssetImportCoordinator::CancelAndWaitJobs()
	{
		for (ImportJob& job : m_Jobs)
		{
			if (job.Cancellation)
				job.Cancellation->Cancel();
		}
		for (ImportJob& job : m_Jobs)
		{
			if (!job.Future.valid())
				continue;
			try
			{
				(void)job.Future.get();
			}
			catch (...)
			{
			}
		}
		m_Jobs.clear();
		m_JobCount.store(0, std::memory_order_release);
	}

	void AssetImportCoordinator::CancelPendingImports()
	{
		if (std::this_thread::get_id() == m_MainThread)
			CancelAndWaitJobs();
	}

	size_t AssetImportCoordinator::GetPendingImportCount() const
	{
		std::scoped_lock lock(m_WatcherMutex);
		return m_DebouncedChanges.size() + m_ReadyChanges.size()
			+ m_JobCount.load(std::memory_order_acquire);
	}

}
