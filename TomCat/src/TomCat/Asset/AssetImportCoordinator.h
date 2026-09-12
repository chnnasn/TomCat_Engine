#pragma once

#include "AssetDatabase.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace TomCat {

	class AssetRegistry;

	enum class AssetFileChangeKind : uint8_t
	{
		Added = 0,
		Modified,
		Removed
	};

	struct AssetImportCoordinatorOptions
	{
		std::chrono::milliseconds PollInterval{ 250 };
		std::chrono::milliseconds Debounce{ 120 };
		// Normal polls hash only timestamp/size candidates. This bounded full pass
		// also catches tools that preserve both fields while replacing content.
		std::chrono::milliseconds ContentVerificationInterval{ 30000 };
		std::string Platform = "editor";
		std::string Backend = "opengl";
	};

	// Delivered only by PumpMainThread. Heavy file hashing and importing remain off
	// the main thread, while registry/GPU-facing publication stays deterministic.
	struct AssetImportEvent
	{
		uint64_t Revision = 0;
		AssetFileChangeKind Change = AssetFileChangeKind::Modified;
		std::filesystem::path FilePath;
		AssetHandle Handle = AssetHandle(0);
		bool IsDependency = false;
		AssetLoadResult Result;
	};

	class AssetImportCoordinator final
	{
	public:
		using Callback = std::function<void(const AssetImportEvent&)>;

		AssetImportCoordinator() = default;
		~AssetImportCoordinator();
		AssetImportCoordinator(const AssetImportCoordinator&) = delete;
		AssetImportCoordinator& operator=(const AssetImportCoordinator&) = delete;

		[[nodiscard]] bool Initialize(AssetRegistry& registry,
			AssetDatabase& database, const std::filesystem::path& assetRoot,
			AssetImportCoordinatorOptions options = {});
		[[nodiscard]] bool Start();
		// Stop/Shutdown and PumpMainThread belong to the Initialize owner thread.
		// RequestScan and GetPendingImportCount are safe for observer threads.
		void Stop();
		void Shutdown();

		// Forces an immediate content-hash pass. It is useful for deterministic
		// tests and for platforms whose timestamp resolution is coarse.
		void RequestScan();
		[[nodiscard]] bool RequestReimport(AssetHandle handle);
		[[nodiscard]] size_t PumpMainThread(const Callback& callback = {});
		void CancelPendingImports();

		[[nodiscard]] bool IsInitialized() const noexcept
		{
			return m_Initialized.load(std::memory_order_acquire);
		}
		[[nodiscard]] bool IsRunning() const noexcept
		{
			return m_Running.load(std::memory_order_acquire);
		}
		[[nodiscard]] size_t GetPendingImportCount() const;

	private:
		using Clock = std::chrono::steady_clock;

		struct FileState
		{
			std::filesystem::path RelativePath;
			std::string ContentHash;
			uintmax_t FileSize = 0;
			std::filesystem::file_time_type WriteTime{};
		};

		struct PendingChange
		{
			AssetFileChangeKind Kind = AssetFileChangeKind::Modified;
			std::filesystem::path RelativePath;
			Clock::time_point LastObserved{};
		};

		struct ReadyChange
		{
			AssetFileChangeKind Kind = AssetFileChangeKind::Modified;
			std::filesystem::path RelativePath;
		};

		struct PendingImport
		{
			uint64_t Revision = 0;
			AssetFileChangeKind Change = AssetFileChangeKind::Modified;
			std::filesystem::path RelativePath;
			AssetHandle Handle = AssetHandle(0);
			bool IsDependency = false;
		};

		struct ImportJob
		{
			uint64_t Revision = 0;
			AssetFileChangeKind Change = AssetFileChangeKind::Modified;
			std::filesystem::path RelativePath;
			AssetHandle Handle = AssetHandle(0);
			bool IsDependency = false;
			bool Superseded = false;
			std::optional<PendingImport> Replacement;
			std::shared_ptr<AssetLoadCancellation> Cancellation;
			std::future<AssetLoadResult> Future;
		};

		using FileSnapshot = std::unordered_map<std::string, FileState>;

		[[nodiscard]] bool BuildSnapshot(FileSnapshot& snapshot,
			const FileSnapshot* baseline = nullptr,
			bool verifyAllContent = true) const;
		void WatcherMain();
		void ObserveSnapshot(FileSnapshot snapshot, Clock::time_point now);
		void QueueChange(const std::string& key, const FileState& state,
			AssetFileChangeKind kind, Clock::time_point now);
		void PublishDebounced(Clock::time_point now);
		std::vector<ReadyChange> DrainReadyChanges();
		size_t ProcessChanges(std::vector<ReadyChange> changes,
			const Callback& callback);
		size_t PublishCompletedJobs(const Callback& callback);
		void SupersedeAffectedJobs(const std::unordered_set<AssetHandle>& handles);
		void CancelAndWaitJobs();

	private:
		AssetRegistry* m_Registry = nullptr;
		AssetDatabase* m_Database = nullptr;
		std::filesystem::path m_AssetRoot;
		AssetImportCoordinatorOptions m_Options;
		std::thread::id m_MainThread;

		mutable std::mutex m_WatcherMutex;
		std::condition_variable m_WatcherWake;
		FileSnapshot m_Snapshot;
		std::unordered_map<std::string, PendingChange> m_DebouncedChanges;
		std::deque<ReadyChange> m_ReadyChanges;
		uint64_t m_ScanRequest = 0;
		std::atomic_bool m_StopRequested = false;
		std::atomic_bool m_Initialized = false;
		std::thread m_Watcher;
		std::atomic_bool m_Running = false;

		std::vector<ImportJob> m_Jobs;
		std::atomic_size_t m_JobCount = 0;
		uint64_t m_Revision = 0;
	};

}
