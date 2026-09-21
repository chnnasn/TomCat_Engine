#include "tcpch.h"
#include "CrashReporter.h"

#include "Log.h"
#include "Version.h"
#include "TomCat/Utils/PathUtils.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

#ifdef TC_PLATFORM_WINDOWS
	#include <DbgHelp.h>
	#include <Windows.h>
#endif

namespace TomCat {
	namespace {

		std::mutex s_CrashMutex;
		std::filesystem::path s_CrashDirectory;
		std::string s_CompanyName;
		std::string s_ProductName;
		std::string s_ProductVersion;
		std::terminate_handler s_PreviousTerminate = nullptr;
		std::atomic<bool> s_HandlingCrash = false;
		bool s_Installed = false;

#ifdef TC_PLATFORM_WINDOWS
		LPTOP_LEVEL_EXCEPTION_FILTER s_PreviousExceptionFilter = nullptr;
#endif

		struct CrashFileSet
		{
			std::filesystem::path Report;
			std::filesystem::path Dump;
			std::string Timestamp;
		};

		CrashFileSet MakeCrashFiles(const std::filesystem::path& directory)
		{
			CrashFileSet files;
#ifdef TC_PLATFORM_WINDOWS
			SYSTEMTIME time{};
			GetLocalTime(&time);
			wchar_t stem[128]{};
			swprintf_s(stem, L"Crash-%04u%02u%02u-%02u%02u%02u-%lu-%lu",
				time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
				time.wSecond, GetCurrentProcessId(), GetCurrentThreadId());
			char timestamp[64]{};
			sprintf_s(timestamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u",
				time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
				time.wSecond, time.wMilliseconds);
			files.Timestamp = timestamp;
			files.Report = directory / (std::wstring(stem) + L".txt");
			files.Dump = directory / (std::wstring(stem) + L".dmp");
#else
			files.Timestamp = "unknown";
			files.Report = directory / "Crash.txt";
#endif
			return files;
		}

#ifdef TC_PLATFORM_WINDOWS
		bool WriteMiniDump(const std::filesystem::path& path,
			EXCEPTION_POINTERS* exceptionPointers) noexcept
		{
			HMODULE dbgHelp = LoadLibraryW(L"Dbghelp.dll");
			if (!dbgHelp)
				return false;
			using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
				HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
				PMINIDUMP_EXCEPTION_INFORMATION,
				PMINIDUMP_USER_STREAM_INFORMATION,
				PMINIDUMP_CALLBACK_INFORMATION);
			const auto writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(
				GetProcAddress(dbgHelp, "MiniDumpWriteDump"));
			if (!writeDump)
			{
				FreeLibrary(dbgHelp);
				return false;
			}

			const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
				FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				FreeLibrary(dbgHelp);
				return false;
			}
			MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
			MINIDUMP_EXCEPTION_INFORMATION* info = nullptr;
			if (exceptionPointers)
			{
				exceptionInfo.ThreadId = GetCurrentThreadId();
				exceptionInfo.ExceptionPointers = exceptionPointers;
				exceptionInfo.ClientPointers = FALSE;
				info = &exceptionInfo;
			}
			const BOOL written = writeDump(GetCurrentProcess(),
				GetCurrentProcessId(), file, MiniDumpNormal, info, nullptr, nullptr);
			CloseHandle(file);
			FreeLibrary(dbgHelp);
			if (written == FALSE)
				DeleteFileW(path.c_str());
			return written != FALSE;
		}
#endif

		std::filesystem::path WriteCrashArtifacts(std::string_view reason,
#ifdef TC_PLATFORM_WINDOWS
			EXCEPTION_POINTERS* exceptionPointers
#else
			void*
#endif
		) noexcept
		{
			if (s_HandlingCrash.exchange(true))
				return {};
			struct HandlingReset
			{
				~HandlingReset() { s_HandlingCrash = false; }
			} reset;

			try
			{
				std::unique_lock<std::mutex> lock(s_CrashMutex, std::try_to_lock);
				if (!lock.owns_lock() || s_CrashDirectory.empty())
					return {};
				std::error_code directoryError;
				std::filesystem::create_directories(s_CrashDirectory, directoryError);
				if (directoryError)
					return {};

				const CrashFileSet files = MakeCrashFiles(s_CrashDirectory);
				std::ofstream report(files.Report, std::ios::binary | std::ios::trunc);
				if (!report)
					return {};
				report << "TomCat Player crash report\n"
					<< "Timestamp: " << files.Timestamp << '\n'
					<< "Company: " << s_CompanyName << '\n'
					<< "Product: " << s_ProductName << '\n'
					<< "Version: " << s_ProductVersion << '\n';
#ifdef TC_PLATFORM_WINDOWS
				report << "ProcessId: " << GetCurrentProcessId() << '\n'
					<< "ThreadId: " << GetCurrentThreadId() << '\n';
				if (exceptionPointers && exceptionPointers->ExceptionRecord)
				{
					report << "ExceptionCode: 0x" << std::hex
						<< exceptionPointers->ExceptionRecord->ExceptionCode << std::dec
						<< '\n'
						<< "ExceptionAddress: "
						<< exceptionPointers->ExceptionRecord->ExceptionAddress << '\n';
				}
#endif
				report << "Reason: " << reason << '\n';
				report.flush();
#ifdef TC_PLATFORM_WINDOWS
				// A dump is useful for both native faults and C++ exceptions
				// caught by EntryPoint. Exception metadata is attached when the
				// operating system supplied EXCEPTION_POINTERS.
				WriteMiniDump(files.Dump, exceptionPointers);
#endif
				return files.Report;
			}
			catch (...)
			{
				return {};
			}
		}

		void TerminateHandler() noexcept
		{
			std::string reason = "std::terminate";
			if (const std::exception_ptr exception = std::current_exception())
			{
				try
				{
					std::rethrow_exception(exception);
				}
				catch (const std::exception& value)
				{
					reason += ": ";
					reason += value.what();
				}
				catch (...)
				{
					reason += ": non-standard exception";
				}
			}
			WriteCrashArtifacts(reason, nullptr);
			std::_Exit(70);
		}

#ifdef TC_PLATFORM_WINDOWS
		LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers)
		{
			WriteCrashArtifacts("unhandled native exception", exceptionPointers);
			return EXCEPTION_EXECUTE_HANDLER;
		}
#endif

	}

	bool CrashReporter::Install(ApplicationProduct product)
	{
		std::filesystem::path directory;
		if (const auto root = ApplicationPaths::GetProductDataRoot(product))
			directory = *root / "Crashes";
		const std::string productName(ApplicationPaths::GetProductDirectoryName(product));
		const bool configured = Configure(directory, "TomCat", productName,
			std::string(Version::ProductVersion));
		std::lock_guard<std::mutex> lock(s_CrashMutex);
		if (!s_Installed)
		{
			s_PreviousTerminate = std::set_terminate(TerminateHandler);
#ifdef TC_PLATFORM_WINDOWS
			s_PreviousExceptionFilter =
				SetUnhandledExceptionFilter(UnhandledExceptionFilter);
#endif
			s_Installed = true;
		}
		return configured;
	}

	bool CrashReporter::Configure(const std::filesystem::path& crashDirectory,
		std::string companyName, std::string productName, std::string version)
	{
		std::error_code error;
		if (!crashDirectory.empty())
			std::filesystem::create_directories(crashDirectory, error);
		std::lock_guard<std::mutex> lock(s_CrashMutex);
		if (crashDirectory.empty() || error)
		{
			s_CrashDirectory.clear();
			return false;
		}
		s_CrashDirectory = crashDirectory.lexically_normal();
		s_CompanyName = std::move(companyName);
		s_ProductName = std::move(productName);
		s_ProductVersion = std::move(version);
		return true;
	}

	std::filesystem::path CrashReporter::WriteReport(
		std::string_view reason) noexcept
	{
		return WriteCrashArtifacts(reason, nullptr);
	}

	void CrashReporter::Uninstall() noexcept
	{
		std::lock_guard<std::mutex> lock(s_CrashMutex);
		if (!s_Installed)
			return;
		std::set_terminate(s_PreviousTerminate);
#ifdef TC_PLATFORM_WINDOWS
		SetUnhandledExceptionFilter(s_PreviousExceptionFilter);
		s_PreviousExceptionFilter = nullptr;
#endif
		s_PreviousTerminate = nullptr;
		s_Installed = false;
	}

}
