#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Core/Application.h"
#include "TomCat/Core/CrashReporter.h"
#include "TomCat/Core/Version.h"

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <cstdio>
	#include <cwchar>
	#include <iterator>
	#include <stdexcept>
	#include <string>
	#include <string_view>
	#include <vector>

	namespace TomCat {

	// EVB resolves virtual resource paths relative to the process working
	// directory.  A shortcut or launcher may provide a different working
	// directory, so packaged builds must anchor it to their own location before
	// loading the first icon, shader, or font.  Development binaries keep their
	// existing working-directory behavior.
	inline void SetPackagedWorkingDirectory()
	{
		std::vector<wchar_t> modulePath(MAX_PATH);
		DWORD length = 0;
		for (;;)
		{
			length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (length == 0)
				return;
			if (length < modulePath.size() - 1)
				break;

			if (modulePath.size() >= 32768)
				return;
			modulePath.resize(modulePath.size() * 2);
		}

		wchar_t* separator = nullptr;
		for (wchar_t* cursor = modulePath.data() + length; cursor != modulePath.data(); --cursor)
		{
			if (*cursor == L'\\' || *cursor == L'/')
			{
				separator = cursor;
				break;
			}
		}
		if (!separator)
			return;

		// Product identity is compiled into current executables, so a packaged
		// product remains relocatable when a launcher or user renames the file.
		// Keep filename detection only for older consumers without an identity.
#ifndef TC_APPLICATION_PRODUCT
		const wchar_t* fileName = separator + 1;
		if (_wcsicmp(fileName, L"TomCat.exe") != 0 &&
			_wcsicmp(fileName, L"Manager.exe") != 0 &&
			_wcsicmp(fileName, L"TomCatHub.exe") != 0 &&
			_wcsicmp(fileName, L"TomCatPlayer.exe") != 0)
			return;
#endif

		// Preserve the root slash for an executable placed directly on a drive
		// root (`C:\\TomCat.exe`); `SetCurrentDirectoryW(L"C:")` is drive-relative.
		if (separator == modulePath.data() + 2 && modulePath[1] == L':')
			separator[1] = L'\0';
		else
			*separator = L'\0';
		SetCurrentDirectoryW(modulePath.data());
	}

	inline std::string WideArgumentToUtf8(const wchar_t* argument)
	{
		if (!argument)
			return {};
		const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
			nullptr, 0, nullptr, nullptr);
		if (required <= 0)
			throw std::runtime_error("Failed to convert a command-line argument to UTF-8");
		std::string result(static_cast<size_t>(required), '\0');
		if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1,
			result.data(), required, nullptr, nullptr) <= 0)
			throw std::runtime_error("Failed to convert a command-line argument to UTF-8");
		result.pop_back();
		return result;
	}

	inline std::filesystem::path GetProfileOutputPath(
		ApplicationProduct product, const std::filesystem::path& fileName)
	{
		std::filesystem::path directory;
		if (const auto gamePaths = ApplicationPaths::GetRuntimeGameDataPaths())
			directory = gamePaths->Logs / "Profiles";
		else if (const auto productRoot =
			ApplicationPaths::GetProductDataRoot(product))
			directory = *productRoot / "Profiles";
		if (directory.empty())
			return fileName;
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		return error ? fileName : directory / fileName;
	}

	inline bool IsProfilingRequested(int argc, wchar_t** argv) noexcept
	{
		for (int index = 1; index < argc; ++index)
		{
			if (argv[index] && _wcsicmp(argv[index], L"--profile") == 0)
				return true;
		}

		wchar_t value[16]{};
		const DWORD length = GetEnvironmentVariableW(
			L"TOMCAT_PROFILE", value, static_cast<DWORD>(std::size(value)));
		if (length == 0 || length >= std::size(value))
			return false;
		return _wcsicmp(value, L"1") == 0 ||
			_wcsicmp(value, L"true") == 0 ||
			_wcsicmp(value, L"on") == 0 ||
			_wcsicmp(value, L"yes") == 0;
	}

	inline bool WriteProductInfoIfRequested(ApplicationProduct product,
		int argc, wchar_t** argv) noexcept
	{
		bool requested = false;
		for (int index = 1; index < argc; ++index)
		{
			if (argv[index] && _wcsicmp(argv[index], L"--product-info-json") == 0)
			{
				requested = true;
				break;
			}
		}
		if (!requested)
			return false;

		const std::string_view productName = ApplicationPaths::GetProductDirectoryName(product);
		std::fprintf(stdout,
			"{\"product\":\"%.*s\",\"version\":\"%.*s\",\"buildId\":\"%.*s\","
			"\"projectFormatOldest\":%u,\"projectFormatCurrent\":%u}\n",
			static_cast<int>(productName.size()), productName.data(),
			static_cast<int>(Version::ProductVersion.size()), Version::ProductVersion.data(),
			static_cast<int>(Version::EngineBuildID.size()), Version::EngineBuildID.data(),
			Version::ProjectFormatOldest, Version::ProjectFormatCurrent);
		std::fflush(stdout);
		return true;
	}

	}

extern TomCat::Application* TomCat::CreateApplication(ApplicationCommandLineArgs args);

int wmain(int argc, wchar_t** argv) {
	TomCat::ApplicationProduct applicationProduct = TomCat::ApplicationProduct::Unknown;
	bool profilingEnabled = false;
	try
	{
		TomCat::SetPackagedWorkingDirectory();
#ifdef TC_APPLICATION_PRODUCT
	// Product identity is compiled into each executable. This remains correct
	// when PlayerBuilder safely renames TomCatPlayer.exe to the game product name.
		applicationProduct = TC_APPLICATION_PRODUCT;
#else
		applicationProduct = TomCat::ApplicationPaths::IdentifyCurrentExecutable();
#endif
		if (TomCat::WriteProductInfoIfRequested(applicationProduct, argc, argv))
			return 0;
		TomCat::Log::Init(applicationProduct);
		TomCat::CrashReporter::Install(applicationProduct);
		profilingEnabled = TomCat::IsProfilingRequested(argc, argv);
		std::vector<std::string> utf8Arguments;
		std::vector<char*> argumentPointers;
		utf8Arguments.reserve(static_cast<size_t>(argc));
		argumentPointers.reserve(static_cast<size_t>(argc) + 1);
		for (int index = 0; index < argc; ++index)
		{
			// Engine-owned switches must not reach the product's strict parser or be
			// mistaken for an Editor project path.
			if (index != 0 && argv[index]
				&& _wcsicmp(argv[index], L"--profile") == 0)
				continue;
			utf8Arguments.push_back(TomCat::WideArgumentToUtf8(argv[index]));
		}
		for (std::string& argument : utf8Arguments)
			argumentPointers.push_back(argument.data());
		argumentPointers.push_back(nullptr);

		if (profilingEnabled)
			TC_PROFILE_BEGIN_SESSION("Startup", TomCat::GetProfileOutputPath(
				applicationProduct, "TomCatProfile-Startup.json"));
		TomCat::Scope<TomCat::Application> app(
			TomCat::CreateApplication({ static_cast<int>(utf8Arguments.size()),
				argumentPointers.data() }));
		if (!app)
			throw std::runtime_error("CreateApplication returned null");
		if (profilingEnabled)
			TC_PROFILE_END_SESSION();

		if (profilingEnabled)
			TC_PROFILE_BEGIN_SESSION("Runtime", TomCat::GetProfileOutputPath(
				applicationProduct, "TomCatProfile-Runtime.json"));
		app->Run();
		if (profilingEnabled)
			TC_PROFILE_END_SESSION();
		const int exitCode = app->GetExitCode();

		if (profilingEnabled)
			TC_PROFILE_BEGIN_SESSION("Shutdown", TomCat::GetProfileOutputPath(
				applicationProduct, "TomCatProfile-Shutdown.json"));
		app.reset();
		if (profilingEnabled)
			TC_PROFILE_END_SESSION();
		TomCat::ApplicationPaths::ClearRuntimeGameDataPaths();
		TomCat::Log::Shutdown();
		TomCat::CrashReporter::Uninstall();
		return exitCode;
	}
	catch (const std::exception& exception)
	{
		if (profilingEnabled)
			TC_PROFILE_END_SESSION();
		if (TomCat::Log::GetCoreLogger())
		{
			try
			{
				TomCat::Log::GetCoreLogger()->critical(
					"Unhandled application exception: {0}", exception.what());
				TomCat::Log::Flush();
			}
			catch (...) {}
		}
		(void)TomCat::CrashReporter::WriteReport(exception.what());
		TomCat::ApplicationPaths::ClearRuntimeGameDataPaths();
		TomCat::Log::Shutdown();
		TomCat::CrashReporter::Uninstall();
		return 70;
	}
	catch (...)
	{
		if (profilingEnabled)
			TC_PROFILE_END_SESSION();
		if (TomCat::Log::GetCoreLogger())
		{
			try
			{
				TomCat::Log::GetCoreLogger()->critical(
					"Unhandled non-standard application exception");
				TomCat::Log::Flush();
			}
			catch (...) {}
		}
		(void)TomCat::CrashReporter::WriteReport(
			"unhandled non-standard application exception");
		TomCat::ApplicationPaths::ClearRuntimeGameDataPaths();
		TomCat::Log::Shutdown();
		TomCat::CrashReporter::Uninstall();
		return 70;
	}
}

#endif

