#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace TomCat {

	enum class ProjectLockAcquireResult
	{
		Acquired,
		LiveOwner,
		Unavailable
	};

	struct ProjectLockRecord
	{
		std::filesystem::path CanonicalProjectPath;
		uint64_t ProcessId = 0;
		uint64_t ProcessStart = 0;
		std::string Token;
	};

	class EditorProjectLock final
	{
	public:
		EditorProjectLock() = default;
		~EditorProjectLock();
		EditorProjectLock(const EditorProjectLock&) = delete;
		EditorProjectLock& operator=(const EditorProjectLock&) = delete;
		EditorProjectLock(EditorProjectLock&& other) noexcept;
		EditorProjectLock& operator=(EditorProjectLock&& other) noexcept;

		ProjectLockAcquireResult Acquire(
			const std::filesystem::path& projectPath, std::string& error,
			std::optional<std::filesystem::path> productRootOverride = std::nullopt);
		void Release();
		bool IsHeld() const { return m_NativeHandle != nullptr; }
		bool OwnsProject(const std::filesystem::path& projectPath) const;
		const ProjectLockRecord& GetRecord() const { return m_Record; }
		const std::filesystem::path& GetLockPath() const { return m_LockPath; }

		static std::filesystem::path CanonicalizePath(
			const std::filesystem::path& path);
		static std::string MakeStableKey(const std::filesystem::path& path);
		static std::filesystem::path ResolveLockPath(
			const std::filesystem::path& productRoot,
			const std::filesystem::path& projectPath);
		static std::string SerializeRecord(const ProjectLockRecord& record);
		static bool ReadRecord(const std::filesystem::path& path,
			ProjectLockRecord& record, std::string& error);
		static bool IsRecordOwnerAlive(const ProjectLockRecord& record);

	private:
		void* m_NativeHandle = nullptr;
		std::filesystem::path m_LockPath;
		ProjectLockRecord m_Record;
	};

	class EditorRecoveryService final
	{
	public:
		struct RecoveryCandidate
		{
			std::filesystem::path RecoveryPath;
			std::filesystem::path SourceScenePath;
			uint64_t StateId = 0;
			uint64_t SelectedEntity = 0;
			std::shared_ptr<const std::string> Archive;
		};

		explicit EditorRecoveryService(
			std::optional<std::filesystem::path> productRootOverride = std::nullopt);
		~EditorRecoveryService();
		EditorRecoveryService(const EditorRecoveryService&) = delete;
		EditorRecoveryService& operator=(const EditorRecoveryService&) = delete;

		bool Configure(const std::filesystem::path& projectPath,
			std::string& error);
		bool ScheduleAutosave(const std::filesystem::path& sourceScenePath,
			uint64_t stateId, uint64_t selectedEntity,
			std::shared_ptr<const std::string> archive, std::string& error);
		bool Flush(std::string& error);
		std::optional<RecoveryCandidate> FindRecovery(
			const std::filesystem::path& sourceScenePath, std::string& error);
		bool RemoveRecovery(const std::filesystem::path& sourceScenePath,
			std::string& error);

		const std::filesystem::path& GetAutosaveDirectory() const
		{
			return m_AutosaveDirectory;
		}
		std::filesystem::path GetRecoveryPath(
			const std::filesystem::path& sourceScenePath) const;
		static std::string MakeProjectKey(
			const std::filesystem::path& projectPath);

	private:
		struct WriteJob
		{
			std::filesystem::path Destination;
			std::shared_ptr<const std::string> Document;
		};

		void WorkerMain();

		std::optional<std::filesystem::path> m_ProductRootOverride;
		std::filesystem::path m_AutosaveDirectory;
		std::mutex m_Mutex;
		std::condition_variable m_WorkAvailable;
		std::condition_variable m_FlushComplete;
		std::deque<WriteJob> m_Jobs;
		std::thread m_Worker;
		std::string m_LastWriteError;
		bool m_Writing = false;
		bool m_Stop = false;
	};

}
