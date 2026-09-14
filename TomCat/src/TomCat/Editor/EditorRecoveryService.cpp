#include "tcpch.h"
#include "EditorRecoveryService.h"

#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string_view>
#include <system_error>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

namespace TomCat {

	namespace {

		constexpr std::string_view RecoveryMagic = "TomCatEditorRecovery";
		constexpr std::string_view LockMagic = "TomCatEditorProjectLock";

		bool ReadFile(const std::filesystem::path& path, std::string& contents)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::ostringstream stream;
			stream << input.rdbuf();
			if (input.bad())
				return false;
			contents = stream.str();
			return true;
		}

		bool ParseUnsigned(std::string_view text, uint64_t& value)
		{
			if (text.empty())
				return false;
			const char* begin = text.data();
			const char* end = begin + text.size();
			const auto result = std::from_chars(begin, end, value);
			return result.ec == std::errc{} && result.ptr == end;
		}

		std::filesystem::path ProductRoot(
			const std::optional<std::filesystem::path>& overrideRoot)
		{
			if (overrideRoot)
				return overrideRoot->lexically_normal();
			const auto root =
				ApplicationPaths::GetProductDataRoot(ApplicationProduct::Editor);
			return root ? *root : std::filesystem::path{};
		}

		std::string NormalizeKeyInput(const std::filesystem::path& path)
		{
			std::string value = PathToUTF8(
				EditorProjectLock::CanonicalizePath(path));
#ifdef TC_PLATFORM_WINDOWS
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
#endif
			return value;
		}

		std::string HashKey(std::string_view value)
		{
			uint64_t hash = 14695981039346656037ull;
			for (unsigned char byte : value)
			{
				hash ^= byte;
				hash *= 1099511628211ull;
			}
			std::ostringstream output;
			output << std::hex << std::setfill('0') << std::setw(16) << hash;
			return output.str();
		}

		std::string MakeToken(uint64_t processId, uint64_t processStart)
		{
			std::random_device random;
			const uint64_t entropy =
				(static_cast<uint64_t>(random()) << 32) ^ random();
			const uint64_t clock = static_cast<uint64_t>(
				std::chrono::high_resolution_clock::now()
					.time_since_epoch().count());
			std::ostringstream token;
			token << std::hex << processId << '-' << processStart << '-'
				<< entropy << '-' << clock;
			return token.str();
		}

		bool CurrentProcessIdentity(uint64_t& processId, uint64_t& processStart)
		{
#ifdef TC_PLATFORM_WINDOWS
			processId = static_cast<uint64_t>(GetCurrentProcessId());
			FILETIME creation{}, exit{}, kernel{}, user{};
			if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
				return false;
			processStart = (static_cast<uint64_t>(creation.dwHighDateTime) << 32)
				| creation.dwLowDateTime;
			return processStart != 0;
#else
			processId = 1;
			processStart = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			return true;
#endif
		}

		std::string BuildRecoveryDocument(
			const std::filesystem::path& sourceScenePath, uint64_t stateId,
			uint64_t selectedEntity, std::string_view archive)
		{
			const std::string source = sourceScenePath.empty() ? std::string{}
				: PathToUTF8(EditorProjectLock::CanonicalizePath(sourceScenePath));
			std::ostringstream header;
			header << RecoveryMagic << "\n"
				<< "Schema=1\n"
				<< "StateId=" << stateId << "\n"
				<< "Selection=" << selectedEntity << "\n"
				<< "SourceBytes=" << source.size() << "\n"
				<< "ArchiveBytes=" << archive.size() << "\n\n";
			std::string document = header.str();
			document.reserve(document.size() + source.size() + archive.size());
			document += source;
			document.append(archive.data(), archive.size());
			return document;
		}

		bool ParseRecoveryDocument(std::string_view document,
			EditorRecoveryService::RecoveryCandidate& candidate,
			std::string& error)
		{
			const size_t headerEnd = document.find("\n\n");
			if (headerEnd == std::string_view::npos)
			{
				error = "recovery header is incomplete";
				return false;
			}
			const std::string_view header = document.substr(0, headerEnd);
			size_t cursor = 0;
			auto nextLine = [&]() -> std::string_view
			{
				if (cursor > header.size())
					return {};
				const size_t end = header.find('\n', cursor);
				const std::string_view line = header.substr(cursor,
					end == std::string_view::npos ? std::string_view::npos
						: end - cursor);
				cursor = end == std::string_view::npos ? header.size() + 1 : end + 1;
				return line;
			};
			if (nextLine() != RecoveryMagic || nextLine() != "Schema=1")
			{
				error = "unsupported recovery document";
				return false;
			}
			auto readField = [&](std::string_view name, uint64_t& value)
			{
				const std::string_view line = nextLine();
				return line.starts_with(name)
					&& ParseUnsigned(line.substr(name.size()), value);
			};
			uint64_t sourceBytes = 0;
			uint64_t archiveBytes = 0;
			if (!readField("StateId=", candidate.StateId)
				|| !readField("Selection=", candidate.SelectedEntity)
				|| !readField("SourceBytes=", sourceBytes)
				|| !readField("ArchiveBytes=", archiveBytes))
			{
				error = "invalid recovery metadata";
				return false;
			}
			const size_t payload = headerEnd + 2;
			if (sourceBytes > document.size() - payload
				|| archiveBytes > document.size() - payload - sourceBytes
				|| payload + sourceBytes + archiveBytes != document.size())
			{
				error = "invalid recovery payload length";
				return false;
			}
			candidate.SourceScenePath = sourceBytes == 0
				? std::filesystem::path{}
				: UTF8ToPath(std::string(document.substr(payload,
					static_cast<size_t>(sourceBytes))));
			candidate.Archive = std::make_shared<const std::string>(
				document.substr(payload + static_cast<size_t>(sourceBytes),
					static_cast<size_t>(archiveBytes)));
			return true;
		}

	}

	EditorProjectLock::~EditorProjectLock()
	{
		Release();
	}

	EditorProjectLock::EditorProjectLock(EditorProjectLock&& other) noexcept
	{
		*this = std::move(other);
	}

	EditorProjectLock& EditorProjectLock::operator=(
		EditorProjectLock&& other) noexcept
	{
		if (this == &other)
			return *this;
		Release();
		m_NativeHandle = other.m_NativeHandle;
		m_LockPath = std::move(other.m_LockPath);
		m_Record = std::move(other.m_Record);
		other.m_NativeHandle = nullptr;
		other.m_LockPath.clear();
		other.m_Record = {};
		return *this;
	}

	ProjectLockAcquireResult EditorProjectLock::Acquire(
		const std::filesystem::path& projectPath, std::string& error,
		std::optional<std::filesystem::path> productRootOverride)
	{
		error.clear();
		Release();
		const std::filesystem::path canonical = CanonicalizePath(projectPath);
		const std::filesystem::path root = ProductRoot(productRootOverride);
		if (canonical.empty() || root.empty())
		{
			error = "project or LocalAppData Editor root is unavailable";
			return ProjectLockAcquireResult::Unavailable;
		}
		const std::filesystem::path lockDirectory = root / "ProjectLocks";
		std::error_code directoryError;
		std::filesystem::create_directories(lockDirectory, directoryError);
		if (directoryError)
		{
			error = "could not create the Editor project-lock directory: "
				+ directoryError.message();
			return ProjectLockAcquireResult::Unavailable;
		}
		const std::filesystem::path lockPath =
			ResolveLockPath(root, canonical);

		uint64_t processId = 0;
		uint64_t processStart = 0;
		if (!CurrentProcessIdentity(processId, processStart))
		{
			error = "could not query the current process start time";
			return ProjectLockAcquireResult::Unavailable;
		}
		ProjectLockRecord ownRecord{
			canonical, processId, processStart,
			MakeToken(processId, processStart)
		};

#ifdef TC_PLATFORM_WINDOWS
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			HANDLE handle = CreateFileW(lockPath.c_str(),
				GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
				CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
				nullptr);
			if (handle != INVALID_HANDLE_VALUE)
			{
				const std::string document = SerializeRecord(ownRecord);
				DWORD written = 0;
				const bool writeSucceeded = document.size() <= MAXDWORD
					&& WriteFile(handle, document.data(),
						static_cast<DWORD>(document.size()), &written, nullptr)
					&& written == document.size() && FlushFileBuffers(handle);
				if (!writeSucceeded)
				{
					const DWORD writeError = GetLastError();
					CloseHandle(handle);
					error = "could not write the Editor project lock: "
						+ std::error_code(static_cast<int>(writeError),
							std::system_category()).message();
					return ProjectLockAcquireResult::Unavailable;
				}
				m_NativeHandle = handle;
				m_LockPath = lockPath;
				m_Record = std::move(ownRecord);
				return ProjectLockAcquireResult::Acquired;
			}

			const DWORD createError = GetLastError();
			if (createError != ERROR_FILE_EXISTS
				&& createError != ERROR_ALREADY_EXISTS)
			{
				error = "could not create the Editor project lock: "
					+ std::error_code(static_cast<int>(createError),
						std::system_category()).message();
				return ProjectLockAcquireResult::Unavailable;
			}

			ProjectLockRecord existing;
			std::string readError;
			if (!ReadRecord(lockPath, existing, readError))
			{
				// A live owner denies write sharing. If an exclusive probe succeeds,
				// this is instead an abandoned partial/corrupt record left by a crash.
				HANDLE probe = CreateFileW(lockPath.c_str(),
					GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL, nullptr);
				if (probe == INVALID_HANDLE_VALUE)
				{
					const DWORD probeError = GetLastError();
					error = probeError == ERROR_SHARING_VIOLATION
						? "project is locked by another Editor instance"
						: "project lock is unreadable and cannot be replaced: "
							+ std::error_code(static_cast<int>(probeError),
								std::system_category()).message();
					return probeError == ERROR_SHARING_VIOLATION
						? ProjectLockAcquireResult::LiveOwner
						: ProjectLockAcquireResult::Unavailable;
				}
				CloseHandle(probe);
				std::error_code removeError;
				if (!std::filesystem::remove(lockPath, removeError) || removeError)
				{
					error = "abandoned project lock could not be removed";
					return ProjectLockAcquireResult::Unavailable;
				}
				continue;
			}
			if (NormalizeKeyInput(existing.CanonicalProjectPath)
				!= NormalizeKeyInput(canonical))
			{
				error = "project lock key collision";
				return ProjectLockAcquireResult::Unavailable;
			}
			if (IsRecordOwnerAlive(existing))
			{
				error = "project is already open by process "
					+ std::to_string(existing.ProcessId);
				return ProjectLockAcquireResult::LiveOwner;
			}
			std::error_code removeError;
			if (!std::filesystem::remove(lockPath, removeError) || removeError)
			{
				error = "stale project lock could not be removed";
				return ProjectLockAcquireResult::Unavailable;
			}
		}
		error = "project lock changed while it was being acquired";
		return ProjectLockAcquireResult::Unavailable;
#else
		if (std::filesystem::exists(lockPath))
		{
			error = "project is already locked";
			return ProjectLockAcquireResult::LiveOwner;
		}
		std::ofstream output(lockPath, std::ios::binary | std::ios::trunc);
		const std::string document = SerializeRecord(ownRecord);
		output.write(document.data(), static_cast<std::streamsize>(document.size()));
		output.close();
		if (!output)
		{
			error = "could not create the Editor project lock";
			return ProjectLockAcquireResult::Unavailable;
		}
		m_NativeHandle = this;
		m_LockPath = lockPath;
		m_Record = std::move(ownRecord);
		return ProjectLockAcquireResult::Acquired;
#endif
	}

	void EditorProjectLock::Release()
	{
		if (!m_NativeHandle)
			return;
#ifdef TC_PLATFORM_WINDOWS
		CloseHandle(static_cast<HANDLE>(m_NativeHandle));
		// Keep the record readable while the lock is live. A normal shutdown
		// removes it explicitly; after a crash it intentionally remains so the
		// PID/process-start stale-owner check can validate and replace it.
		std::error_code error;
		std::filesystem::remove(m_LockPath, error);
#else
		std::error_code error;
		std::filesystem::remove(m_LockPath, error);
#endif
		m_NativeHandle = nullptr;
		m_LockPath.clear();
		m_Record = {};
	}

	bool EditorProjectLock::OwnsProject(
		const std::filesystem::path& projectPath) const
	{
		return IsHeld()
			&& NormalizeKeyInput(m_Record.CanonicalProjectPath)
				== NormalizeKeyInput(projectPath);
	}

	std::filesystem::path EditorProjectLock::CanonicalizePath(
		const std::filesystem::path& path)
	{
		if (path.empty())
			return {};
		std::error_code error;
		std::filesystem::path canonical =
			std::filesystem::weakly_canonical(path, error);
		if (error)
		{
			error.clear();
			canonical = std::filesystem::absolute(path, error);
		}
		return error ? path.lexically_normal() : canonical.lexically_normal();
	}

	std::string EditorProjectLock::MakeStableKey(
		const std::filesystem::path& path)
	{
		return HashKey(NormalizeKeyInput(path));
	}

	std::filesystem::path EditorProjectLock::ResolveLockPath(
		const std::filesystem::path& productRoot,
		const std::filesystem::path& projectPath)
	{
		return productRoot / "ProjectLocks"
			/ (MakeStableKey(projectPath) + ".lock");
	}

	std::string EditorProjectLock::SerializeRecord(
		const ProjectLockRecord& record)
	{
		const std::string path = PathToUTF8(
			CanonicalizePath(record.CanonicalProjectPath));
		std::ostringstream header;
		header << LockMagic << "\n"
			<< "Schema=1\n"
			<< "Pid=" << record.ProcessId << "\n"
			<< "ProcessStart=" << record.ProcessStart << "\n"
			<< "TokenBytes=" << record.Token.size() << "\n"
			<< "PathBytes=" << path.size() << "\n\n";
		std::string result = header.str();
		result += record.Token;
		result += path;
		return result;
	}

	bool EditorProjectLock::ReadRecord(const std::filesystem::path& path,
		ProjectLockRecord& record, std::string& error)
	{
		error.clear();
		std::string document;
		if (!ReadFile(path, document))
		{
			error = "could not read project lock";
			return false;
		}
		const size_t headerEnd = document.find("\n\n");
		if (headerEnd == std::string::npos)
		{
			error = "project lock header is incomplete";
			return false;
		}
		std::istringstream lines(document.substr(0, headerEnd));
		std::string line;
		auto next = [&]()
		{
			return static_cast<bool>(std::getline(lines, line));
		};
		if (!next() || line != LockMagic || !next() || line != "Schema=1")
		{
			error = "unsupported project lock";
			return false;
		}
		auto read = [&](std::string_view prefix, uint64_t& value)
		{
			return next() && std::string_view(line).starts_with(prefix)
				&& ParseUnsigned(std::string_view(line).substr(prefix.size()), value);
		};
		uint64_t tokenBytes = 0;
		uint64_t pathBytes = 0;
		if (!read("Pid=", record.ProcessId)
			|| !read("ProcessStart=", record.ProcessStart)
			|| !read("TokenBytes=", tokenBytes)
			|| !read("PathBytes=", pathBytes))
		{
			error = "invalid project lock metadata";
			return false;
		}
		const size_t payload = headerEnd + 2;
		if (tokenBytes > document.size() - payload
			|| pathBytes > document.size() - payload - tokenBytes
			|| payload + tokenBytes + pathBytes != document.size())
		{
			error = "invalid project lock payload";
			return false;
		}
		record.Token = document.substr(payload, static_cast<size_t>(tokenBytes));
		record.CanonicalProjectPath = UTF8ToPath(document.substr(
			payload + static_cast<size_t>(tokenBytes),
			static_cast<size_t>(pathBytes)));
		return record.ProcessId != 0 && record.ProcessStart != 0
			&& !record.Token.empty() && !record.CanonicalProjectPath.empty();
	}

	bool EditorProjectLock::IsRecordOwnerAlive(
		const ProjectLockRecord& record)
	{
#ifdef TC_PLATFORM_WINDOWS
		if (record.ProcessId == 0 || record.ProcessId > MAXDWORD)
			return false;
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
			FALSE, static_cast<DWORD>(record.ProcessId));
		if (!process)
			return GetLastError() == ERROR_ACCESS_DENIED;
		FILETIME creation{}, exit{}, kernel{}, user{};
		const BOOL queried =
			GetProcessTimes(process, &creation, &exit, &kernel, &user);
		CloseHandle(process);
		if (!queried)
			return true;
		const uint64_t start =
			(static_cast<uint64_t>(creation.dwHighDateTime) << 32)
			| creation.dwLowDateTime;
		return start == record.ProcessStart;
#else
		return false;
#endif
	}

	EditorRecoveryService::EditorRecoveryService(
		std::optional<std::filesystem::path> productRootOverride)
		: m_ProductRootOverride(std::move(productRootOverride)),
		  m_Worker(&EditorRecoveryService::WorkerMain, this)
	{
	}

	EditorRecoveryService::~EditorRecoveryService()
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Stop = true;
		}
		m_WorkAvailable.notify_one();
		if (m_Worker.joinable())
			m_Worker.join();
	}

	bool EditorRecoveryService::Configure(
		const std::filesystem::path& projectPath, std::string& error)
	{
		const bool flushed = Flush(error);
		// Never retain a previous project's destination after a failed
		// reconfiguration. Jobs already queued carry their immutable destination.
		m_AutosaveDirectory.clear();
		if (!flushed)
			return false;
		const std::filesystem::path root = ProductRoot(m_ProductRootOverride);
		if (root.empty())
		{
			error = "LocalAppData Editor root is unavailable";
			return false;
		}
		const std::filesystem::path directory =
			root / "Autosaves" / MakeProjectKey(projectPath);
		std::error_code directoryError;
		std::filesystem::create_directories(directory, directoryError);
		if (directoryError)
		{
			error = "could not create the Editor autosave directory: "
				+ directoryError.message();
			return false;
		}
		m_AutosaveDirectory = directory.lexically_normal();
		error.clear();
		return true;
	}

	bool EditorRecoveryService::ScheduleAutosave(
		const std::filesystem::path& sourceScenePath, uint64_t stateId,
		uint64_t selectedEntity, std::shared_ptr<const std::string> archive,
		std::string& error)
	{
		error.clear();
		if (m_AutosaveDirectory.empty() || !archive)
		{
			error = "Editor recovery is not configured";
			return false;
		}
		WriteJob job;
		job.Destination = GetRecoveryPath(sourceScenePath);
		job.Document = std::make_shared<const std::string>(
			BuildRecoveryDocument(sourceScenePath, stateId,
				selectedEntity, *archive));
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Stop)
			{
				error = "Editor recovery worker is stopping";
				return false;
			}
			m_Jobs.push_back(std::move(job));
		}
		m_WorkAvailable.notify_one();
		return true;
	}

	bool EditorRecoveryService::RestoreHistoryAndScheduleAutosave(
		SceneHistory& history, HistoryDirection direction,
		const SceneHistory::RestoreCallback& restore,
		const std::filesystem::path& sourceScenePath, std::string& error)
	{
		error.clear();
		const bool restored = direction == HistoryDirection::Undo
			? history.Undo(restore)
			: history.Redo(restore);
		if (!restored)
			return false;
		if (m_AutosaveDirectory.empty())
			return true;

		const SceneHistory::Snapshot* snapshot =
			history.GetCurrentSnapshot();
		if (!snapshot || !snapshot->Archive)
		{
			error =
				"restored Scene history has no recovery snapshot";
			return true;
		}
		(void)ScheduleAutosave(sourceScenePath, snapshot->Id,
			snapshot->SelectedEntity, snapshot->Archive, error);
		return true;
	}

	bool EditorRecoveryService::Flush(std::string& error)
	{
		std::unique_lock lock(m_Mutex);
		m_FlushComplete.wait(lock,
			[this]() { return m_Jobs.empty() && !m_Writing; });
		error = m_LastWriteError;
		m_LastWriteError.clear();
		return error.empty();
	}

	std::optional<EditorRecoveryService::RecoveryCandidate>
	EditorRecoveryService::FindRecovery(
		const std::filesystem::path& sourceScenePath, std::string& error)
	{
		if (!Flush(error))
			return std::nullopt;
		const std::filesystem::path recoveryPath =
			GetRecoveryPath(sourceScenePath);
		if (recoveryPath.empty())
			return std::nullopt;
		std::error_code inspectError;
		if (!std::filesystem::is_regular_file(recoveryPath, inspectError)
			|| inspectError)
			return std::nullopt;

		std::string document;
		if (!ReadFile(recoveryPath, document))
		{
			error = "could not read the Editor recovery file";
			return std::nullopt;
		}
		RecoveryCandidate candidate;
		candidate.RecoveryPath = recoveryPath;
		if (!ParseRecoveryDocument(document, candidate, error))
			return std::nullopt;

		const std::filesystem::path expected = sourceScenePath.empty()
			? std::filesystem::path{} : EditorProjectLock::CanonicalizePath(
				sourceScenePath);
		if ((expected.empty() != candidate.SourceScenePath.empty())
			|| (!expected.empty()
				&& NormalizeKeyInput(expected)
					!= NormalizeKeyInput(candidate.SourceScenePath)))
		{
			error = "recovery source does not match the active scene";
			return std::nullopt;
		}

		if (!expected.empty())
		{
			std::error_code sourceError;
			const bool sourceExists =
				std::filesystem::is_regular_file(expected, sourceError);
			if (sourceError)
			{
				error = "could not inspect the source scene";
				return std::nullopt;
			}
			if (sourceExists)
			{
				const auto recoveryTime =
					std::filesystem::last_write_time(recoveryPath, inspectError);
				if (inspectError)
					return std::nullopt;
				const auto sourceTime =
					std::filesystem::last_write_time(expected, sourceError);
				if (sourceError || recoveryTime <= sourceTime)
					return std::nullopt;
				std::string sourceContents;
				if (!ReadFile(expected, sourceContents))
				{
					error = "could not compare the source scene to recovery";
					return std::nullopt;
				}
				if (candidate.Archive && sourceContents == *candidate.Archive)
					return std::nullopt;
			}
		}
		if (!candidate.Archive || candidate.Archive->empty())
			return std::nullopt;
		error.clear();
		return candidate;
	}

	bool EditorRecoveryService::RemoveRecovery(
		const std::filesystem::path& sourceScenePath, std::string& error)
	{
		if (!Flush(error))
			return false;
		const std::filesystem::path path = GetRecoveryPath(sourceScenePath);
		if (path.empty())
			return true;
		std::error_code removeError;
		const bool exists = std::filesystem::exists(path, removeError);
		if (removeError)
		{
			error = removeError.message();
			return false;
		}
		if (!exists)
			return true;
		if (!std::filesystem::remove(path, removeError) || removeError)
		{
			error = removeError ? removeError.message()
				: "recovery file could not be removed";
			return false;
		}
		return true;
	}

	std::filesystem::path EditorRecoveryService::GetRecoveryPath(
		const std::filesystem::path& sourceScenePath) const
	{
		if (m_AutosaveDirectory.empty())
			return {};
		const std::string key = sourceScenePath.empty() ? "untitled"
			: EditorProjectLock::MakeStableKey(sourceScenePath);
		return m_AutosaveDirectory / (key + ".recovery");
	}

	std::string EditorRecoveryService::MakeProjectKey(
		const std::filesystem::path& projectPath)
	{
		return projectPath.empty() ? "no-project"
			: EditorProjectLock::MakeStableKey(projectPath);
	}

	void EditorRecoveryService::WorkerMain()
	{
		for (;;)
		{
			WriteJob job;
			{
				std::unique_lock lock(m_Mutex);
				m_WorkAvailable.wait(lock,
					[this]() { return m_Stop || !m_Jobs.empty(); });
				if (m_Stop && m_Jobs.empty())
					return;
				job = std::move(m_Jobs.front());
				m_Jobs.pop_front();
				m_Writing = true;
			}

			std::string writeError;
			if (!FileSystem::WriteFileAtomically(
				job.Destination, *job.Document, writeError))
			{
				std::scoped_lock lock(m_Mutex);
				m_LastWriteError = "autosave failed: " + writeError;
			}

			{
				std::scoped_lock lock(m_Mutex);
				m_Writing = false;
				if (m_Jobs.empty())
					m_FlushComplete.notify_all();
			}
		}
	}

}
