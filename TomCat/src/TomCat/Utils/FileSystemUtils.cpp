#include "tcpch.h"
#include "FileSystemUtils.h"
#include "PathUtils.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>

namespace TomCat::FileSystem {

	namespace {

		std::atomic<uint64_t> s_TemporaryFileCounter{ 0 };

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

		bool ReplaceExistingFile(const std::filesystem::path& temporary,
			const std::filesystem::path& destination, bool& retryAllowed,
			std::string& errorMessage)
		{
			retryAllowed = false;
			const std::filesystem::path backup = MakeTemporarySiblingPath(destination);
			if (backup.empty())
			{
				errorMessage = "could not allocate a recovery backup path";
				return false;
			}

			const std::wstring temporaryPath = ExtendedLengthPath(temporary);
			const std::wstring destinationPath = ExtendedLengthPath(destination);
			const std::wstring backupPath = ExtendedLengthPath(backup);
			if (ReplaceFileW(destinationPath.c_str(), temporaryPath.c_str(), backupPath.c_str(),
				0, nullptr, nullptr))
			{
				// Replacement already succeeded. A locked backup may remain as a harmless
				// recovery copy and must not make the completed save look like a failure.
				DeleteFileW(backupPath.c_str());
				return true;
			}

			const DWORD replaceError = GetLastError();
			errorMessage = std::error_code(static_cast<int>(replaceError),
				std::system_category()).message();

			// ReplaceFile can move the original to the backup and still fail before
			// installing the replacement. Inspect every failure instead of relying on
			// a specific error code, then put the original name back without replacing
			// a file that another writer may have installed in the meantime.
			bool destinationExists = false;
			bool backupExists = false;
			DWORD destinationInspectError = ERROR_SUCCESS;
			DWORD backupInspectError = ERROR_SUCCESS;
			const bool destinationInspected = InspectPath(destination, destinationExists,
				destinationInspectError);
			const bool backupInspected = InspectPath(backup, backupExists, backupInspectError);

			if (destinationInspected && backupInspected && !destinationExists && backupExists)
			{
				if (MoveFileExW(backupPath.c_str(), destinationPath.c_str(), MOVEFILE_WRITE_THROUGH))
					errorMessage += " (the original destination was restored; recovery data kept at '" +
						PathToUTF8(temporary) + "')";
				else
				{
					const DWORD restoreError = GetLastError();
					errorMessage += " (failed to restore the original destination: " +
						std::error_code(static_cast<int>(restoreError), std::system_category()).message() +
						"; recovery data kept at '" + PathToUTF8(temporary) + "' and '" +
						PathToUTF8(backup) + "')";
				}
			}
			else if (destinationInspected && backupInspected && !destinationExists && !backupExists &&
				(replaceError == ERROR_FILE_NOT_FOUND || replaceError == ERROR_PATH_NOT_FOUND))
			{
				// The destination disappeared before ReplaceFile touched either file.
				retryAllowed = true;
				errorMessage += " (the destination changed during replacement; retrying)";
			}
			else
			{
				errorMessage += " (recovery data kept at '" + PathToUTF8(temporary) + "'";
				if (backupInspected && backupExists)
					errorMessage += " and '" + PathToUTF8(backup) + "'";
				if (!destinationInspected)
					errorMessage += "; destination inspection failed: " +
						std::error_code(static_cast<int>(destinationInspectError), std::system_category()).message();
				if (!backupInspected)
					errorMessage += "; backup inspection failed: " +
						std::error_code(static_cast<int>(backupInspectError), std::system_category()).message();
				errorMessage += ")";
			}
			return false;
		}
#endif

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

#ifdef TC_PLATFORM_WINDOWS
		const std::wstring temporaryPath = ExtendedLengthPath(temporary);
		const std::wstring destinationPath = ExtendedLengthPath(destination);
		for (uint32_t attempt = 0; attempt < 3; ++attempt)
		{
			bool destinationExists = false;
			DWORD inspectError = ERROR_SUCCESS;
			if (!InspectPath(destination, destinationExists, inspectError))
			{
				errorMessage = "could not inspect the destination: " +
					std::error_code(static_cast<int>(inspectError), std::system_category()).message();
				return false;
			}

			if (destinationExists)
			{
				bool retryAllowed = false;
				if (ReplaceExistingFile(temporary, destination, retryAllowed, errorMessage))
					return true;
				if (retryAllowed)
					continue;
				return false;
			}

			if (MoveFileExW(temporaryPath.c_str(), destinationPath.c_str(), MOVEFILE_WRITE_THROUGH))
				return true;
			const DWORD moveError = GetLastError();
			if (moveError == ERROR_ALREADY_EXISTS || moveError == ERROR_FILE_EXISTS)
				continue;
			errorMessage = std::error_code(static_cast<int>(moveError), std::system_category()).message() +
				" (recovery data kept at '" + PathToUTF8(temporary) + "')";
			return false;
		}

		errorMessage = "destination changed repeatedly during installation (recovery data kept at '" +
			PathToUTF8(temporary) + "')";
		return false;
#else
		std::error_code error;
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

#ifdef TC_PLATFORM_WINDOWS
		const DWORD parentAttributes = GetFileAttributesW(ExtendedLengthPath(parent).c_str());
		if (parentAttributes == INVALID_FILE_ATTRIBUTES ||
			(parentAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			errorMessage = "destination directory is unavailable";
			return false;
		}
#else
		if (!std::filesystem::is_directory(parent, error) || error)
		{
			errorMessage = "destination directory is unavailable";
			return false;
		}
#endif

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
		if (InstallTemporaryFileAtomically(temporary, destination, errorMessage))
			return true;
		return false;
	}

}
