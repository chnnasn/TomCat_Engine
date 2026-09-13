#include "tcpch.h"
#include "FileSystemUtils.h"
#include "PathUtils.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <limits>
#include <mutex>
#include <vector>

namespace TomCat::FileSystem {

	namespace {

		std::atomic<uint64_t> s_TemporaryFileCounter{ 0 };
		std::mutex s_DirectoryMutationHookMutex;
		DirectoryMutationTestHook s_DirectoryMutationTestHook;

		void InvokeDirectoryMutationTestHook(
			const std::filesystem::path& pinnedDirectory)
		{
			DirectoryMutationTestHook hook;
			{
				const std::scoped_lock lock(s_DirectoryMutationHookMutex);
				hook = s_DirectoryMutationTestHook;
			}
			if (hook)
				hook(pinnedDirectory);
		}

		std::wstring ExtendedLengthPath(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			std::wstring value = (error ? path : absolute).lexically_normal().wstring();
			if (value.rfind(L"\\\\?\\", 0) == 0)
				return value;
			if (value.rfind(L"\\\\", 0) == 0)
				return L"\\\\?\\UNC\\" + value.substr(2);
			return L"\\\\?\\" + value;
		}

#ifdef TC_PLATFORM_WINDOWS
		struct DirectoryIdentityEntry
		{
			std::filesystem::path Path;
			HANDLE Handle = INVALID_HANDLE_VALUE;
			DWORD VolumeSerialNumber = 0;
			DWORD FileIndexHigh = 0;
			DWORD FileIndexLow = 0;
			std::wstring FinalPath;
		};

		std::string WindowsErrorMessage(DWORD error)
		{
			return std::error_code(static_cast<int>(error),
				std::system_category()).message();
		}

		std::wstring NormalizeFinalPath(std::wstring path)
		{
			std::replace(path.begin(), path.end(), L'/', L'\\');
			std::transform(path.begin(), path.end(), path.begin(),
				[](wchar_t character)
				{
					return static_cast<wchar_t>(std::towlower(character));
				});
			while (path.size() > 4 && path.back() == L'\\')
				path.pop_back();
			return path;
		}

		bool ReadDirectoryIdentity(HANDLE handle, DWORD& volumeSerialNumber,
			DWORD& fileIndexHigh, DWORD& fileIndexLow, std::wstring& finalPath,
			std::string& errorMessage)
		{
			FILE_ATTRIBUTE_TAG_INFO attributes{};
			if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
				&attributes, sizeof(attributes)))
			{
				errorMessage = "could not inspect directory attributes: " +
					WindowsErrorMessage(GetLastError());
				return false;
			}
			if ((attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				errorMessage = "path component is not a directory";
				return false;
			}
			if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
			{
				errorMessage = "directory chain contains a reparse point";
				return false;
			}

			BY_HANDLE_FILE_INFORMATION information{};
			if (!GetFileInformationByHandle(handle, &information))
			{
				errorMessage = "could not read directory file identity: " +
					WindowsErrorMessage(GetLastError());
				return false;
			}
			volumeSerialNumber = information.dwVolumeSerialNumber;
			fileIndexHigh = information.nFileIndexHigh;
			fileIndexLow = information.nFileIndexLow;

			const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0,
				FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (required == 0)
			{
				errorMessage = "could not resolve the pinned directory final path: " +
					WindowsErrorMessage(GetLastError());
				return false;
			}
			std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1, L'\0');
			const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(),
				static_cast<DWORD>(buffer.size()),
				FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
			if (written == 0 || written >= buffer.size())
			{
				errorMessage = "could not resolve the pinned directory final path: " +
					WindowsErrorMessage(GetLastError());
				return false;
			}
			finalPath = NormalizeFinalPath(std::wstring(buffer.data(), written));
			return true;
		}

		bool OpenPinnedDirectory(const std::filesystem::path& path,
			DirectoryIdentityEntry& entry, std::string& errorMessage)
		{
			entry = {};
			entry.Path = path;
			entry.Handle = CreateFileW(ExtendedLengthPath(path).c_str(),
				FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (entry.Handle == INVALID_HANDLE_VALUE)
			{
				errorMessage = "could not pin directory '" + PathToUTF8(path) +
					"': " + WindowsErrorMessage(GetLastError());
				return false;
			}
			if (!ReadDirectoryIdentity(entry.Handle, entry.VolumeSerialNumber,
				entry.FileIndexHigh, entry.FileIndexLow, entry.FinalPath, errorMessage))
			{
				errorMessage = "could not pin directory '" + PathToUTF8(path) +
					"': " + errorMessage;
				CloseHandle(entry.Handle);
				entry.Handle = INVALID_HANDLE_VALUE;
				return false;
			}
			return true;
		}

		bool SameDirectoryIdentity(const DirectoryIdentityEntry& left,
			const DirectoryIdentityEntry& right)
		{
			return left.VolumeSerialNumber == right.VolumeSerialNumber &&
				left.FileIndexHigh == right.FileIndexHigh &&
				left.FileIndexLow == right.FileIndexLow &&
				left.FinalPath == right.FinalPath;
		}

		bool InspectPath(const std::filesystem::path& path, bool& exists, DWORD& inspectError)
		{
			const DWORD attributes = GetFileAttributesW(ExtendedLengthPath(path).c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES)
			{
				exists = true;
				inspectError = ERROR_SUCCESS;
				return true;
			}

			inspectError = GetLastError();
			if (inspectError == ERROR_FILE_NOT_FOUND || inspectError == ERROR_PATH_NOT_FOUND)
			{
				exists = false;
				return true;
			}
			return false;
		}

		bool RenameTemporaryHandleAtomically(
			const std::filesystem::path& temporary,
			const std::filesystem::path& destination,
			std::string& errorMessage)
		{
			// Open the source directory entry itself. Renaming this handle avoids a
			// second path lookup of temporary after it has been fully written.
			const HANDLE source = CreateFileW(ExtendedLengthPath(temporary).c_str(),
				DELETE | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (source == INVALID_HANDLE_VALUE)
			{
				errorMessage = "could not open the completed temporary file for rename: " +
					WindowsErrorMessage(GetLastError());
				return false;
			}

			FILE_ATTRIBUTE_TAG_INFO sourceAttributes{};
			if (!GetFileInformationByHandleEx(source, FileAttributeTagInfo,
				&sourceAttributes, sizeof(sourceAttributes)))
			{
				errorMessage = "could not inspect temporary rename source: " +
					WindowsErrorMessage(GetLastError());
				CloseHandle(source);
				return false;
			}
			if ((sourceAttributes.FileAttributes &
				(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
			{
				errorMessage = "temporary rename source is not a regular, non-reparse file";
				CloseHandle(source);
				return false;
			}

			// Reject an already-visible destination reparse point. If another
			// process creates one after this check, FILE_RENAME_INFO still replaces
			// that directory entry; it never opens or follows its reparse target.
			const HANDLE existing = CreateFileW(ExtendedLengthPath(destination).c_str(),
				FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (existing != INVALID_HANDLE_VALUE)
			{
				FILE_ATTRIBUTE_TAG_INFO destinationAttributes{};
				const bool inspected = GetFileInformationByHandleEx(existing,
					FileAttributeTagInfo, &destinationAttributes,
					sizeof(destinationAttributes)) != FALSE;
				const DWORD inspectError = inspected ? ERROR_SUCCESS : GetLastError();
				CloseHandle(existing);
				if (!inspected)
				{
					errorMessage = "could not inspect atomic-write destination: " +
						WindowsErrorMessage(inspectError);
					CloseHandle(source);
					return false;
				}
				if ((destinationAttributes.FileAttributes &
					FILE_ATTRIBUTE_REPARSE_POINT) != 0)
				{
					errorMessage = "refusing to atomically replace a reparse point: " +
						PathToUTF8(destination);
					CloseHandle(source);
					return false;
				}
			}
			else
			{
				const DWORD openError = GetLastError();
				if (openError != ERROR_FILE_NOT_FOUND && openError != ERROR_PATH_NOT_FOUND)
				{
					errorMessage = "could not inspect atomic-write destination: " +
						WindowsErrorMessage(openError);
					CloseHandle(source);
					return false;
				}
			}

			const std::filesystem::path destinationParent = destination.parent_path();
			if (destinationParent.empty() || destination.filename().empty())
			{
				errorMessage = "atomic-write destination has no parent or filename";
				CloseHandle(source);
				return false;
			}
			const HANDLE parent = CreateFileW(
				ExtendedLengthPath(destinationParent).c_str(),
				FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if (parent == INVALID_HANDLE_VALUE)
			{
				errorMessage = "could not open atomic-write parent directory: " +
					WindowsErrorMessage(GetLastError());
				CloseHandle(source);
				return false;
			}
			std::error_code absoluteError;
			const std::filesystem::path absoluteDestination =
				std::filesystem::absolute(destination, absoluteError).lexically_normal();
			if (absoluteError)
			{
				errorMessage = "could not resolve atomic-write destination: " +
					absoluteError.message();
				CloseHandle(parent);
				CloseHandle(source);
				return false;
			}
			const std::wstring destinationName = absoluteDestination.wstring();
			const size_t nameBytes = destinationName.size() * sizeof(wchar_t);
			// FILE_RENAME_INFO declares FileName[1]. The extra zero-initialized wide
			// character keeps the variable-size structure complete while
			// FileNameLength continues to exclude the terminator.
			const size_t informationBytes = sizeof(FILE_RENAME_INFO) + nameBytes;
			std::vector<uint64_t> storage(
				(informationBytes + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
			auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
			rename->ReplaceIfExists = TRUE;
			rename->RootDirectory = nullptr;
			rename->FileNameLength = static_cast<DWORD>(nameBytes);
			std::memcpy(rename->FileName, destinationName.data(), nameBytes);
			if (!SetFileInformationByHandle(source, FileRenameInfo, rename,
				static_cast<DWORD>(informationBytes)))
			{
				errorMessage = WindowsErrorMessage(GetLastError()) +
					" (recovery data kept at '" + PathToUTF8(temporary) + "')";
				CloseHandle(parent);
				CloseHandle(source);
				return false;
			}
			// The namespace mutation is complete once SetFileInformationByHandle
			// succeeds. A close error must not make callers treat an installed file
			// as an uncommitted write.
			CloseHandle(parent);
			CloseHandle(source);
			return true;
		}
#endif

	}

	struct PinnedDirectoryChain::State
	{
#ifdef TC_PLATFORM_WINDOWS
		std::vector<DirectoryIdentityEntry> Entries;

		~State()
		{
			for (DirectoryIdentityEntry& entry : Entries)
			{
				if (entry.Handle != INVALID_HANDLE_VALUE)
					CloseHandle(entry.Handle);
			}
		}
#else
		std::filesystem::path Directory;
		std::filesystem::path CanonicalDirectory;
#endif
	};

	PinnedDirectoryChain::PinnedDirectoryChain() = default;
	PinnedDirectoryChain::~PinnedDirectoryChain() = default;
	PinnedDirectoryChain::PinnedDirectoryChain(
		PinnedDirectoryChain&& other) noexcept = default;
	PinnedDirectoryChain& PinnedDirectoryChain::operator=(
		PinnedDirectoryChain&& other) noexcept = default;

	bool PinnedDirectoryChain::Acquire(const std::filesystem::path& directory,
		std::string& errorMessage)
	{
		Reset();
		errorMessage.clear();
		if (directory.empty())
		{
			errorMessage = "directory path is empty";
			return false;
		}

		std::error_code pathError;
		const std::filesystem::path absolute =
			std::filesystem::absolute(directory, pathError).lexically_normal();
		if (pathError || absolute.empty())
		{
			errorMessage = "could not make directory path absolute: " +
				(pathError ? pathError.message() : PathToUTF8(directory));
			return false;
		}

		auto state = std::make_unique<State>();
#ifdef TC_PLATFORM_WINDOWS
		std::filesystem::path current = absolute.root_path();
		if (current.empty())
		{
			errorMessage = "directory path has no filesystem root";
			return false;
		}
		auto appendPinned = [&](const std::filesystem::path& componentPath)
		{
			DirectoryIdentityEntry entry;
			if (!OpenPinnedDirectory(componentPath, entry, errorMessage))
				return false;
			if (!state->Entries.empty())
			{
				const std::wstring& parentFinal = state->Entries.back().FinalPath;
				const bool withinParent = entry.FinalPath == parentFinal ||
					(entry.FinalPath.size() > parentFinal.size() &&
						entry.FinalPath.compare(0, parentFinal.size(), parentFinal) == 0 &&
						entry.FinalPath[parentFinal.size()] == L'\\');
				if (!withinParent)
				{
					errorMessage = "pinned directory final path escaped its parent: " +
						PathToUTF8(componentPath);
					CloseHandle(entry.Handle);
					return false;
				}
			}
			state->Entries.push_back(std::move(entry));
			return true;
		};

		if (!appendPinned(current))
			return false;
		for (const auto& component : absolute.relative_path())
		{
			current /= component;
			if (!appendPinned(current))
				return false;
		}
#else
		if (!std::filesystem::is_directory(absolute, pathError) || pathError)
		{
			errorMessage = "directory is unavailable: " +
				(pathError ? pathError.message() : PathToUTF8(absolute));
			return false;
		}
		state->Directory = absolute;
		state->CanonicalDirectory = std::filesystem::canonical(absolute, pathError);
		if (pathError)
		{
			errorMessage = "could not canonicalize directory: " + pathError.message();
			return false;
		}

#endif
		m_State = std::move(state);
		if (Verify(errorMessage))
			return true;
		Reset();
		return false;
	}

	bool PinnedDirectoryChain::Verify(std::string& errorMessage) const
	{
		errorMessage.clear();
		if (!m_State)
		{
			errorMessage = "directory chain is not pinned";
			return false;
		}
#ifdef TC_PLATFORM_WINDOWS
		for (size_t index = 0; index < m_State->Entries.size(); ++index)
		{
			const DirectoryIdentityEntry& expected = m_State->Entries[index];
			DirectoryIdentityEntry held;
			held.Path = expected.Path;
			held.Handle = expected.Handle;
			if (!ReadDirectoryIdentity(held.Handle, held.VolumeSerialNumber,
				held.FileIndexHigh, held.FileIndexLow, held.FinalPath, errorMessage))
			{
				errorMessage = "pinned directory changed '" +
					PathToUTF8(expected.Path) + "': " + errorMessage;
				return false;
			}
			if (!SameDirectoryIdentity(expected, held))
			{
				errorMessage = "pinned directory identity changed: " +
					PathToUTF8(expected.Path);
				return false;
			}

			DirectoryIdentityEntry observed;
			if (!OpenPinnedDirectory(expected.Path, observed, errorMessage))
				return false;
			const bool matches = SameDirectoryIdentity(expected, observed);
			CloseHandle(observed.Handle);
			if (!matches)
			{
				errorMessage = "directory path no longer resolves to its pinned identity: " +
					PathToUTF8(expected.Path);
				return false;
			}
		}
#else
		std::error_code error;
		const std::filesystem::path canonical =
			std::filesystem::canonical(m_State->Directory, error);
		if (error || canonical != m_State->CanonicalDirectory)
		{
			errorMessage = "directory identity changed while it was pinned";
			return false;
		}
#endif
		return true;
	}

	void PinnedDirectoryChain::Reset()
	{
		m_State.reset();
	}

	bool PinnedDirectoryChain::IsAcquired() const
	{
		return m_State != nullptr;
	}

	void SetDirectoryMutationTestHookForTesting(DirectoryMutationTestHook hook)
	{
		const std::scoped_lock lock(s_DirectoryMutationHookMutex);
		s_DirectoryMutationTestHook = std::move(hook);
	}

	bool CreateDirectoryAndPin(const std::filesystem::path& directory,
		PinnedDirectoryChain& guard, bool& created, std::string& errorMessage)
	{
		guard.Reset();
		created = false;
		errorMessage.clear();
		if (directory.empty())
		{
			errorMessage = "directory path is empty";
			return false;
		}

		std::error_code pathError;
		std::filesystem::path parent = directory.parent_path();
		if (parent.empty())
			parent = std::filesystem::current_path(pathError);
		if (pathError)
		{
			errorMessage = "could not resolve the directory parent: " +
				pathError.message();
			return false;
		}
		PinnedDirectoryChain parentGuard;
		if (!parentGuard.Acquire(parent, errorMessage))
			return false;
		InvokeDirectoryMutationTestHook(parent);
		if (!parentGuard.Verify(errorMessage))
			return false;

#ifdef TC_PLATFORM_WINDOWS
		if (CreateDirectoryW(ExtendedLengthPath(directory).c_str(), nullptr))
			created = true;
		else
		{
			const DWORD createError = GetLastError();
			if (createError != ERROR_ALREADY_EXISTS)
			{
				errorMessage = "could not create directory '" +
					PathToUTF8(directory) + "': " +
					WindowsErrorMessage(createError);
				return false;
			}
		}
#else
		created = std::filesystem::create_directory(directory, pathError);
		if (pathError)
		{
			errorMessage = "could not create directory '" +
				PathToUTF8(directory) + "': " + pathError.message();
			return false;
		}
#endif
		if (!guard.Acquire(directory, errorMessage))
		{
			if (created)
			{
#ifdef TC_PLATFORM_WINDOWS
				RemoveDirectoryW(ExtendedLengthPath(directory).c_str());
#else
				std::filesystem::remove(directory, pathError);
#endif
			}
			return false;
		}
		return true;
	}

	bool RemovePathSafely(const std::filesystem::path& path, bool& removed,
		std::string& errorMessage)
	{
		removed = false;
		errorMessage.clear();
		if (path.empty())
		{
			errorMessage = "path is empty";
			return false;
		}
		std::error_code pathError;
		std::filesystem::path parent = path.parent_path();
		if (parent.empty())
			parent = std::filesystem::current_path(pathError);
		if (pathError)
		{
			errorMessage = "could not resolve the path parent: " + pathError.message();
			return false;
		}
		PinnedDirectoryChain parentGuard;
		if (!parentGuard.Acquire(parent, errorMessage))
		{
			std::error_code existsError;
			if (!std::filesystem::exists(parent, existsError) && !existsError)
			{
				errorMessage.clear();
				return true;
			}
			return false;
		}
		InvokeDirectoryMutationTestHook(parent);
		if (!parentGuard.Verify(errorMessage))
			return false;

#ifdef TC_PLATFORM_WINDOWS
		const HANDLE target = CreateFileW(ExtendedLengthPath(path).c_str(),
			DELETE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if (target == INVALID_HANDLE_VALUE)
		{
			const DWORD openError = GetLastError();
			if (openError == ERROR_FILE_NOT_FOUND || openError == ERROR_PATH_NOT_FOUND)
				return true;
			errorMessage = "could not open path for safe deletion '" +
				PathToUTF8(path) + "': " + WindowsErrorMessage(openError);
			return false;
		}
		FILE_ATTRIBUTE_TAG_INFO attributes{};
		if (!GetFileInformationByHandleEx(target, FileAttributeTagInfo,
			&attributes, sizeof(attributes)))
		{
			errorMessage = "could not inspect deletion target '" +
				PathToUTF8(path) + "': " + WindowsErrorMessage(GetLastError());
			CloseHandle(target);
			return false;
		}
		if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			errorMessage = "refusing to delete a reparse point: " + PathToUTF8(path);
			CloseHandle(target);
			return false;
		}
		if (!parentGuard.Verify(errorMessage))
		{
			CloseHandle(target);
			return false;
		}
		FILE_DISPOSITION_INFO disposition{};
		disposition.DeleteFile = TRUE;
		if (!SetFileInformationByHandle(target, FileDispositionInfo,
			&disposition, sizeof(disposition)))
		{
			errorMessage = "could not safely delete path '" + PathToUTF8(path) +
				"': " + WindowsErrorMessage(GetLastError());
			CloseHandle(target);
			return false;
		}
		CloseHandle(target);
		removed = true;
#else
		const std::filesystem::file_status status =
			std::filesystem::symlink_status(path, pathError);
		if (pathError == std::errc::no_such_file_or_directory ||
			(!pathError && !std::filesystem::exists(status)))
			return true;
		if (pathError)
		{
			errorMessage = "could not inspect deletion target: " + pathError.message();
			return false;
		}
		if (std::filesystem::is_symlink(status))
		{
			errorMessage = "refusing to delete a symlink: " + PathToUTF8(path);
			return false;
		}
		removed = std::filesystem::remove(path, pathError);
		if (pathError)
		{
			errorMessage = "could not safely delete path: " + pathError.message();
			return false;
		}
#endif
		return true;
	}

	std::filesystem::path MakeTemporarySiblingPath(const std::filesystem::path& destination)
	{
		if (destination.empty())
			return {};

#ifdef TC_PLATFORM_WINDOWS
		const uint64_t processID = static_cast<uint64_t>(GetCurrentProcessId());
#else
		const uint64_t processID = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());
#endif
		for (uint32_t attempt = 0; attempt < 10000; ++attempt)
		{
			std::filesystem::path candidate = destination;
			candidate += ".tmp-" + std::to_string(processID) + "-" +
				std::to_string(s_TemporaryFileCounter.fetch_add(1, std::memory_order_relaxed));
#ifdef TC_PLATFORM_WINDOWS
			bool exists = false;
			DWORD inspectError = ERROR_SUCCESS;
			if (InspectPath(candidate, exists, inspectError) && !exists)
				return candidate;
#else
			std::error_code error;
			if (!std::filesystem::exists(candidate, error) && !error)
				return candidate;
#endif
		}
		return {};
	}

	bool InstallTemporaryFileAtomically(const std::filesystem::path& temporary,
		const std::filesystem::path& destination, std::string& errorMessage)
	{
		errorMessage.clear();
		if (temporary.empty() || destination.empty() ||
			temporary.parent_path() != destination.parent_path())
		{
			errorMessage = "temporary file must be a sibling of the destination";
			return false;
		}
		std::filesystem::path parent = destination.parent_path();
		std::error_code parentError;
		if (parent.empty())
			parent = std::filesystem::current_path(parentError);
		if (parentError)
		{
			errorMessage = "destination directory is unavailable: " +
				parentError.message();
			return false;
		}
		PinnedDirectoryChain parentGuard;
		if (!parentGuard.Acquire(parent, errorMessage))
			return false;
		InvokeDirectoryMutationTestHook(parent);
		if (!parentGuard.Verify(errorMessage))
			return false;

#ifdef TC_PLATFORM_WINDOWS
		if (!parentGuard.Verify(errorMessage))
			return false;
		return RenameTemporaryHandleAtomically(
			temporary, destination, errorMessage);
#else
		std::error_code error;
		if (!parentGuard.Verify(errorMessage))
			return false;
		std::filesystem::rename(temporary, destination, error);
		if (!error)
			return true;
		errorMessage = error.message();
		return false;
#endif
	}

	bool WriteFileAtomically(const std::filesystem::path& destination,
		std::string_view contents, std::string& errorMessage)
	{
		errorMessage.clear();
		if (destination.empty())
		{
			errorMessage = "destination path is empty";
			return false;
		}

		std::filesystem::path parent = destination.parent_path();
		std::error_code error;
		if (parent.empty())
			parent = std::filesystem::current_path(error);
		if (error)
		{
			errorMessage = "destination directory is unavailable";
			return false;
		}
		PinnedDirectoryChain parentGuard;
		if (!parentGuard.Acquire(parent, errorMessage))
			return false;
		InvokeDirectoryMutationTestHook(parent);
		if (!parentGuard.Verify(errorMessage))
			return false;

		const std::filesystem::path temporary = MakeTemporarySiblingPath(destination);
		if (temporary.empty())
		{
			errorMessage = "could not allocate a temporary sibling path";
			return false;
		}

#ifdef TC_PLATFORM_WINDOWS
		const std::wstring temporaryPath = ExtendedLengthPath(temporary);
		HANDLE output = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
		if (output == INVALID_HANDLE_VALUE)
		{
			errorMessage = "could not open the temporary file: " +
				std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
			return false;
		}

		const char* cursor = contents.data();
		size_t remaining = contents.size();
		while (remaining > 0)
		{
			const DWORD chunkSize = static_cast<DWORD>((std::min)(remaining,
				static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
			DWORD written = 0;
			const BOOL writeSucceeded = WriteFile(output, cursor, chunkSize, &written, nullptr);
			if (!writeSucceeded || written == 0)
			{
				const DWORD writeError = writeSucceeded ? ERROR_WRITE_FAULT : GetLastError();
				errorMessage = "failed while writing the temporary file: " +
					std::error_code(static_cast<int>(writeError), std::system_category()).message();
				break;
			}
			cursor += written;
			remaining -= written;
		}
		if (errorMessage.empty() && !FlushFileBuffers(output))
			errorMessage = "failed while flushing the temporary file: " +
				std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
		if (!CloseHandle(output) && errorMessage.empty())
			errorMessage = "failed while closing the temporary file: " +
				std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
#else
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
			{
				errorMessage = "could not open the temporary file";
				return false;
			}
			output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			output.flush();
			if (!output.good())
				errorMessage = "failed while writing the temporary file";
			output.close();
			if (errorMessage.empty() && output.fail())
				errorMessage = "failed while closing the temporary file";
		}
#endif

		if (!errorMessage.empty())
		{
#ifdef TC_PLATFORM_WINDOWS
			DeleteFileW(ExtendedLengthPath(temporary).c_str());
#else
			std::filesystem::remove(temporary, error);
#endif
			return false;
		}
		if (!parentGuard.Verify(errorMessage))
			return false;
		if (InstallTemporaryFileAtomically(temporary, destination, errorMessage))
			return true;
		return false;
	}

}
