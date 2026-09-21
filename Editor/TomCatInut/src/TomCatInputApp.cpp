#include <TomCat.h>
#include <TomCat/Core/EditorRuntimeBundle.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

namespace TomCat {
	namespace {

		bool IsCliProxyRequested(int argc, wchar_t** argv)
		{
			return argc > 1 && argv && argv[1]
				&& std::wstring_view(argv[1]) == L"--cli";
		}

		std::filesystem::path ExecutableDirectory()
		{
#ifdef TC_PLATFORM_WINDOWS
			std::vector<wchar_t> buffer(MAX_PATH);
			for (;;)
			{
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
					static_cast<DWORD>(buffer.size()));
				if (length == 0)
					return {};
				if (length < buffer.size() - 1)
					return std::filesystem::path(
						std::wstring(buffer.data(), length)).parent_path();
				if (buffer.size() >= 32768)
					return {};
				buffer.resize(buffer.size() * 2);
			}
#else
			return {};
#endif
		}

		std::wstring QuoteWindowsArgument(std::wstring_view value)
		{
			std::wstring result = L"\"";
			size_t backslashes = 0;
			for (const wchar_t character : value)
			{
				if (character == L'\\')
				{
					++backslashes;
					continue;
				}
				if (character == L'\"')
				{
					result.append(backslashes * 2 + 1, L'\\');
					result.push_back(character);
					backslashes = 0;
					continue;
				}
				result.append(backslashes, L'\\');
				backslashes = 0;
				result.push_back(character);
			}
			result.append(backslashes * 2, L'\\');
			result.push_back(L'\"');
			return result;
		}

		int RunPackagedCli(const EditorRuntimeBundleResult& runtime,
			const std::filesystem::path& workingDirectory, int argc, wchar_t** argv)
		{
#ifdef TC_PLATFORM_WINDOWS
			if (runtime.CliExecutable.empty())
				throw std::runtime_error("The packaged Editor runtime has no TomCatCLI executable");

			std::wstring command = QuoteWindowsArgument(runtime.CliExecutable.wstring());
			for (int index = 2; index < argc; ++index)
			{
				command.push_back(L' ');
				command += QuoteWindowsArgument(argv[index] ? argv[index] : L"");
			}
			std::vector<wchar_t> mutableCommand(command.begin(), command.end());
			mutableCommand.push_back(L'\0');

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			const HANDLE standardInput = GetStdHandle(STD_INPUT_HANDLE);
			const HANDLE standardOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			const HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
			const bool inheritStandardHandles =
				standardInput && standardInput != INVALID_HANDLE_VALUE
				&& standardOutput && standardOutput != INVALID_HANDLE_VALUE
				&& standardError && standardError != INVALID_HANDLE_VALUE;
			if (inheritStandardHandles)
			{
				startup.dwFlags = STARTF_USESTDHANDLES;
				startup.hStdInput = standardInput;
				startup.hStdOutput = standardOutput;
				startup.hStdError = standardError;
			}

			PROCESS_INFORMATION process{};
			if (!CreateProcessW(runtime.CliExecutable.c_str(), mutableCommand.data(),
				nullptr, nullptr, inheritStandardHandles ? TRUE : FALSE, 0, nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
				&startup, &process))
			{
				throw std::runtime_error("Could not start the packaged TomCatCLI (Win32 error "
					+ std::to_string(GetLastError()) + ")");
			}

			CloseHandle(process.hThread);
			const DWORD waitResult = WaitForSingleObject(process.hProcess, INFINITE);
			DWORD exitCode = 1;
			const bool readExitCode = GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
			CloseHandle(process.hProcess);
			if (waitResult != WAIT_OBJECT_0 || !readExitCode)
				throw std::runtime_error("Waiting for the packaged TomCatCLI failed (Win32 error "
					+ std::to_string(GetLastError()) + ")");
			return static_cast<int>(exitCode);
#else
			(void)runtime;
			(void)workingDirectory;
			(void)argc;
			(void)argv;
			throw std::runtime_error("The packaged TomCatCLI proxy is available only on Windows");
#endif
		}

	}

	std::optional<int> BootstrapTomCatEditor(int argc, wchar_t** argv,
		const std::filesystem::path& launchWorkingDirectory)
	{
		const std::filesystem::path executableDirectory = ExecutableDirectory();
		if (executableDirectory.empty())
			throw std::runtime_error("Could not resolve the Editor executable directory");

		const bool cliRequested = IsCliProxyRequested(argc, argv);
		const std::filesystem::path payloadRoot =
			executableDirectory / ".tomcat-runtime";
		std::error_code pathError;
		pathError.clear();
		const bool payloadExists = std::filesystem::exists(payloadRoot, pathError);
		if (pathError)
			throw std::runtime_error("Could not inspect the packaged Editor runtime: "
				+ pathError.message());
		if (!payloadExists)
		{
			if (cliRequested)
				throw std::runtime_error(
					"--cli requires a packaged Editor runtime; no .tomcat-runtime payload was found");
			return std::nullopt;
		}
		pathError.clear();
		if (!std::filesystem::is_directory(payloadRoot, pathError) || pathError)
			throw std::runtime_error("The packaged Editor runtime payload is not a directory");

		EditorRuntimeBundleResult runtime;
		std::string configureError;
		if (!ConfigurePackagedEditorRuntime(payloadRoot, runtime, configureError))
		{
			throw std::runtime_error(configureError.empty()
				? "The packaged Editor runtime could not be configured"
				: configureError);
		}
		if (!cliRequested)
			return std::nullopt;
		return RunPackagedCli(runtime,
			launchWorkingDirectory.empty() ? runtime.Root : launchWorkingDirectory,
			argc, argv);
	}

}

#define TC_APPLICATION_PRODUCT TomCat::ApplicationProduct::Editor
#define TC_APPLICATION_BOOTSTRAP(argc, argv, launchWorkingDirectory) \
	TomCat::BootstrapTomCatEditor(argc, argv, launchWorkingDirectory)
#include <TomCat/Core/EntryPoint.h>
#include <TomCat/Utils/PathUtils.h>

#include "EditorLayer.h"

#include <utility>

namespace TomCat {

	

	class TomCatInput : public Application
	{
	public:
		TomCatInput(ApplicationCommandLineArgs args)
			: Application([] {
                    WindowProps props("TomCatEditor",1440,900,"Packages/Resources/Icons/Logo.ico");
                    props.FitToWorkArea=true;
                    props.EditorStyling=true;
                    return props;
                }(),true)
		{
			std::filesystem::path startupProjectPath;
			if (args.Count > 1)
				startupProjectPath = UTF8ToPath(args[1]);
			
			// EditorLayer acquires the LocalAppData project lock, previews the exact
			// migration, and passes that approved plan to the low-level load API.
			PushLayer(new EditorLayer(std::move(startupProjectPath)));
		}

	};

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new TomCatInput(args);
	}

}
