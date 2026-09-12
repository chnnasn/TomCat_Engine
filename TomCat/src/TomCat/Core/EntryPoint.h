#pragma once
#include "TomCat/Core/Base.h"
#include "TomCat/Core/Application.h"

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
	#include <cwchar>
	#include <stdexcept>
	#include <string>
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

		const wchar_t* fileName = separator + 1;
		if (_wcsicmp(fileName, L"TomCat.exe") != 0 &&
			_wcsicmp(fileName, L"Manager.exe") != 0 &&
			_wcsicmp(fileName, L"TomCatHub.exe") != 0 &&
			_wcsicmp(fileName, L"TomCatPlayer.exe") != 0)
			return;

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

	}

extern TomCat::Application* TomCat::CreateApplication(ApplicationCommandLineArgs args);

int wmain(int argc, wchar_t** argv) {

	TomCat::SetPackagedWorkingDirectory();
	TomCat::Log::Init();
	std::vector<std::string> utf8Arguments;
	std::vector<char*> argumentPointers;
	utf8Arguments.reserve(static_cast<size_t>(argc));
	argumentPointers.reserve(static_cast<size_t>(argc) + 1);
	for (int index = 0; index < argc; ++index)
		utf8Arguments.push_back(TomCat::WideArgumentToUtf8(argv[index]));
	for (std::string& argument : utf8Arguments)
		argumentPointers.push_back(argument.data());
	argumentPointers.push_back(nullptr);

	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Startup.json");
	auto app = TomCat::CreateApplication({ argc, argumentPointers.data() });
	TC_PROFILE_END_SESSION();			 
										 
	TC_PROFILE_BEGIN_SESSION("Runtime", "TomCatProfile-Runtime.json");
	app->Run();							
	TC_PROFILE_END_SESSION();			
	const int exitCode = app->GetExitCode();
										
	TC_PROFILE_BEGIN_SESSION("Startup", "TomCatProfile-Shutdown.json");
	delete app;							
	TC_PROFILE_END_SESSION();
	return exitCode;
}

#endif

