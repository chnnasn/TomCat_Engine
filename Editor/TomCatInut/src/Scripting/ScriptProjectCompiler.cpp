#include "ScriptProjectCompiler.h"
#include "ScriptMetadataCache.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

#ifdef TC_PLATFORM_WINDOWS
#include <Windows.h>
#endif

namespace TomCat {

	namespace {

		constexpr uint64_t kFNVOffset = 14695981039346656037ull;
		constexpr uint64_t kFNVPrime = 1099511628211ull;

		void HashBytes(uint64_t& hash, const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= kFNVPrime;
			}
		}

		void HashText(uint64_t& hash, const std::string& value)
		{
			HashBytes(hash, value.data(), value.size());
			constexpr unsigned char separator = 0xff;
			HashBytes(hash, &separator, sizeof(separator));
		}

		std::string HexHash(uint64_t value)
		{
			std::ostringstream output;
			output << std::hex << std::setfill('0') << std::setw(16) << value;
			return output.str();
		}

		std::string EscapeJson(const std::string& value)
		{
			std::ostringstream output;
			for (const unsigned char character : value)
			{
				switch (character)
				{
					case '"': output << "\\\""; break;
					case '\\': output << "\\\\"; break;
					case '\b': output << "\\b"; break;
					case '\f': output << "\\f"; break;
					case '\n': output << "\\n"; break;
					case '\r': output << "\\r"; break;
					case '\t': output << "\\t"; break;
					default:
						if (character < 0x20)
							output << "\\u" << std::hex << std::setfill('0')
								<< std::setw(4) << static_cast<unsigned int>(character)
								<< std::dec;
						else
							output << static_cast<char>(character);
						break;
				}
			}
			return output.str();
		}

		std::string EscapeXml(const std::string& value)
		{
			std::string output;
			output.reserve(value.size());
			for (const char character : value)
			{
				switch (character)
				{
					case '&': output += "&amp;"; break;
					case '<': output += "&lt;"; break;
					case '>': output += "&gt;"; break;
					case '"': output += "&quot;"; break;
					case '\'': output += "&apos;"; break;
					default: output += character; break;
				}
			}
			return output;
		}

		std::filesystem::path AbsoluteLexical(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool IsSafeRelativePath(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name() ||
				path.has_root_directory())
				return false;
			for (const auto& component : path)
			{
				if (component == "..")
					return false;
			}
			return true;
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
					break;
				if (length < buffer.size() - 1)
					return std::filesystem::path(
						std::wstring(buffer.data(), length)).parent_path();
				if (buffer.size() >= 32768)
					break;
				buffer.resize(buffer.size() * 2);
			}
#endif
			std::error_code error;
			return std::filesystem::current_path(error);
		}

		std::vector<std::filesystem::path> SearchAncestors()
		{
			std::vector<std::filesystem::path> roots;
			auto addChain = [&roots](std::filesystem::path path)
			{
				path = AbsoluteLexical(path);
				for (uint32_t depth = 0; depth < 12 && !path.empty(); ++depth)
				{
					if (std::find(roots.begin(), roots.end(), path) == roots.end())
						roots.push_back(path);
					if (path == path.root_path() || path.parent_path() == path)
						break;
					path = path.parent_path();
				}
			};

			std::error_code error;
			addChain(std::filesystem::current_path(error));
			addChain(ExecutableDirectory());
			return roots;
		}

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error;
			return !path.empty() && std::filesystem::is_regular_file(path, error) && !error;
		}

		bool IsManagedRuntimeDirectory(const std::filesystem::path& path)
		{
			return !path.empty() &&
				IsRegularFile(path / "TomCat.ScriptHost.dll") &&
				IsRegularFile(path / "TomCat.ScriptHost.runtimeconfig.json") &&
				IsRegularFile(path / "TomCat.Managed.dll");
		}

		std::string MakeBuildID()
		{
			static std::atomic<uint32_t> sequence = 0;
			const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			std::ostringstream output;
			output << ticks;
#ifdef TC_PLATFORM_WINDOWS
			output << '-' << GetCurrentProcessId();
#endif
			output << '-' << sequence.fetch_add(1, std::memory_order_relaxed);
			return output.str();
		}

		std::string Trim(std::string value)
		{
			auto whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
			const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
			const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
			return first < last ? std::string(first, last) : std::string{};
		}

		bool ReadTextFile(const std::filesystem::path& path, std::string& contents)
		{
			std::ifstream input(path, std::ios::binary);
			if (!input)
				return false;
			std::ostringstream output;
			output << input.rdbuf();
			if (input.bad())
				return false;
			contents = output.str();
			return true;
		}

		bool ReadJsonStringProperty(const std::string& json, const char* property,
			std::string& value)
		{
			value.clear();
			const std::string key = std::string("\"") + property + "\"";
			size_t position = json.find(key);
			if (position == std::string::npos)
				return false;
			position = json.find(':', position + key.size());
			if (position == std::string::npos)
				return false;
			position = json.find_first_not_of(" \t\r\n", position + 1);
			if (position == std::string::npos || json[position] != '"')
				return false;
			for (++position; position < json.size(); ++position)
			{
				const char character = json[position];
				if (character == '"')
					return true;
				if (character != '\\')
				{
					value.push_back(character);
					continue;
				}
				if (++position >= json.size())
					return false;
				switch (json[position])
				{
					case '"': value.push_back('"'); break;
					case '\\': value.push_back('\\'); break;
					case '/': value.push_back('/'); break;
					case 'b': value.push_back('\b'); break;
					case 'f': value.push_back('\f'); break;
					case 'n': value.push_back('\n'); break;
					case 'r': value.push_back('\r'); break;
					case 't': value.push_back('\t'); break;
					default: return false;
				}
			}
			return false;
		}

#ifdef TC_PLATFORM_WINDOWS
		bool RunDotNetCommand(const std::wstring& arguments,
			const std::filesystem::path& workingDirectory,
			std::string& processOutput, int& exitCode, std::string& launchError)
		{
			processOutput.clear();
			exitCode = -1;
			launchError.clear();

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			HANDLE readPipe = nullptr;
			HANDLE writePipe = nullptr;
			if (!CreatePipe(&readPipe, &writePipe, &security, 0))
			{
				launchError = "CreatePipe failed with Win32 error " +
					std::to_string(GetLastError());
				return false;
			}
			SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startup.hStdOutput = writePipe;
			startup.hStdError = writePipe;
			PROCESS_INFORMATION process{};

			std::wstring command = L"dotnet.exe " + arguments;
			std::vector<wchar_t> mutableCommand(command.begin(), command.end());
			mutableCommand.push_back(L'\0');
			const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
				TRUE, CREATE_NO_WINDOW, nullptr,
				workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup, &process);
			CloseHandle(writePipe);
			writePipe = nullptr;
			if (!created)
			{
				launchError = "Could not start dotnet.exe (Win32 error " +
					std::to_string(GetLastError()) + ")";
				CloseHandle(readPipe);
				return false;
			}

			std::array<char, 4096> buffer{};
			DWORD read = 0;
			while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()),
				&read, nullptr) && read != 0)
				processOutput.append(buffer.data(), read);
			CloseHandle(readPipe);

			WaitForSingleObject(process.hProcess, INFINITE);
			DWORD nativeExitCode = static_cast<DWORD>(-1);
			GetExitCodeProcess(process.hProcess, &nativeExitCode);
			exitCode = static_cast<int>(nativeExitCode);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return true;
		}

		bool RunDotNetBuild(const std::filesystem::path& projectPath,
			std::string& processOutput, int& exitCode, std::string& launchError)
		{
			return RunDotNetCommand(L"build \"" + projectPath.wstring() +
				L"\" --configuration Release --nologo --verbosity minimal",
				projectPath.parent_path(), processOutput, exitCode, launchError);
		}

		bool RunRestrictedDotNetBuild(const std::filesystem::path& projectPath,
			std::string& processOutput, int& exitCode, std::string& launchError)
		{
			// These are command-line global properties, so neither an environment
			// property nor an imported project can turn the extension points back on.
			// The script project is engine-generated and deliberately has no NuGet or
			// third-party managed dependency surface.
			static constexpr std::array<const wchar_t*, 19> lockedProperties = {
				L"ImportDirectoryBuildProps=false",
				L"ImportDirectoryBuildTargets=false",
				L"ImportDirectoryPackagesProps=false",
				L"ImportProjectExtensionProps=false",
				L"ImportProjectExtensionTargets=false",
				L"RestoreEnableGlobalPackageReference=false",
				L"ManagePackageVersionsCentrally=false",
				L"ImportUserLocationsByWildcardBeforeMicrosoftCommonProps=false",
				L"ImportUserLocationsByWildcardAfterMicrosoftCommonProps=false",
				L"ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets=false",
				L"ImportUserLocationsByWildcardAfterMicrosoftCommonTargets=false",
				L"ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets=false",
				L"ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets=false",
				L"ImportByWildcardBeforeMicrosoftCommonProps=false",
				L"ImportByWildcardAfterMicrosoftCommonProps=false",
				L"ImportByWildcardBeforeMicrosoftCommonTargets=false",
				L"ImportByWildcardAfterMicrosoftCommonTargets=false",
				L"ImportByWildcardBeforeMicrosoftCSharpTargets=false",
				L"ImportByWildcardAfterMicrosoftCSharpTargets=false"
			};

			std::wstring arguments = L"build -noAutoResponse \"" + projectPath.wstring() +
				L"\" --configuration Release --nologo --verbosity minimal";
			for (const wchar_t* property : lockedProperties)
				arguments += L" -p:" + std::wstring(property);
			return RunDotNetCommand(arguments, projectPath.parent_path(), processOutput,
				exitCode, launchError);
		}
#else
		bool RunDotNetBuild(const std::filesystem::path&, std::string&, int& exitCode,
			std::string& launchError)
		{
			exitCode = -1;
			launchError = "Script compilation is not implemented on this platform";
			return false;
		}

		bool RunRestrictedDotNetBuild(const std::filesystem::path&, std::string&,
			int& exitCode, std::string& launchError)
		{
			exitCode = -1;
			launchError = "Script compilation is not implemented on this platform";
			return false;
		}
#endif

		bool CheckDotNet10Sdk(std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::string output;
			std::string launchError;
			int exitCode = -1;
			if (!RunDotNetCommand(L"--list-sdks", {}, output, exitCode, launchError))
			{
				errorMessage = ".NET 10 SDK is required to compile TomCat C# scripts, "
					"but dotnet.exe could not be started. " + launchError +
					". Install the .NET 10 SDK and ensure dotnet.exe is on PATH.";
				return false;
			}
			if (exitCode != 0)
			{
				errorMessage = "Could not query installed .NET SDKs with "
					"'dotnet --list-sdks' (exit code " + std::to_string(exitCode) + ").";
				const std::string details = Trim(output);
				if (!details.empty())
					errorMessage += " " + details.substr(0, 4000);
				return false;
			}

			const std::regex sdk10Pattern(R"(^\s*10\.[0-9]+\.[^\s]+\s+\[)");
			std::istringstream lines(output);
			std::string line;
			while (std::getline(lines, line))
			{
				if (std::regex_search(line, sdk10Pattern))
					return true;
			}
			errorMessage = ".NET 10 SDK is required to compile TomCat C# scripts, "
				"but no 10.x SDK was reported by 'dotnet --list-sdks'. "
				"Install the .NET 10 SDK from https://dotnet.microsoft.com/download/dotnet/10.0.";
			return false;
#else
			errorMessage = ".NET 10 SDK detection is only supported on Windows x64.";
			return false;
#endif
		}

		std::vector<ScriptCompilerDiagnostic> ParseDiagnostics(const std::string& output)
		{
			std::vector<ScriptCompilerDiagnostic> diagnostics;
			const std::regex pattern(
				R"(^(.+)\(([0-9]+),([0-9]+)\):[ \t]+(warning|error)[ \t]+([^:]+):[ \t]+(.*)$)",
				std::regex_constants::icase);
			std::istringstream lines(output);
			std::string line;
			while (std::getline(lines, line))
			{
				line = Trim(std::move(line));
				std::smatch match;
				if (!std::regex_match(line, match, pattern))
					continue;
				ScriptCompilerDiagnostic diagnostic;
				diagnostic.File = UTF8ToPath(match[1].str());
				try
				{
					diagnostic.Line = static_cast<uint32_t>(std::stoul(match[2].str()));
					diagnostic.Column = static_cast<uint32_t>(std::stoul(match[3].str()));
				}
				catch (const std::exception&)
				{
					diagnostic.Line = 0;
					diagnostic.Column = 0;
				}
				std::string severity = match[4].str();
				std::transform(severity.begin(), severity.end(), severity.begin(),
					[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
				diagnostic.Level = severity == "error"
					? ScriptCompilerDiagnostic::Severity::Error
					: ScriptCompilerDiagnostic::Severity::Warning;
				diagnostic.Code = Trim(match[5].str());
				diagnostic.Message = Trim(match[6].str());
				const size_t projectSuffix = diagnostic.Message.rfind(" [");
				if (projectSuffix != std::string::npos && !diagnostic.Message.empty() &&
					diagnostic.Message.back() == ']')
					diagnostic.Message.erase(projectSuffix);
				diagnostics.push_back(std::move(diagnostic));
			}
			return diagnostics;
		}

	}

	bool ScriptProjectCompiler::Configure(const Ref<Project>& project,
		std::filesystem::path managedApiReference,
		std::filesystem::path generatorReference)
	{
		Reset();
		m_Project = project;
		if (!m_Project || m_Project->GetProjectDirectory().empty())
			return false;

		m_ScriptProjectDirectory = m_Project->GetLibraryPath() / "ScriptProject";
		m_AssembliesDirectory = m_Project->GetLibraryPath() / "ScriptAssemblies";
		m_ManagedApiReference = AbsoluteLexical(managedApiReference);
		m_GeneratorReference = AbsoluteLexical(generatorReference);
		m_ManagedApiIsProject = m_ManagedApiReference.extension() == ".csproj";
		m_GeneratorIsProject = m_GeneratorReference.extension() == ".csproj";

		std::string sdkError;
		if (!CheckDotNet10Sdk(sdkError))
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0020";
			diagnostic.Message = std::move(sdkError);
			Emit(diagnostic);
			m_State = ScriptBuildState::Failed;
			return false;
		}

		if (!ResolveManagedReferences())
		{
			m_State = ScriptBuildState::Failed;
			return false;
		}
		LoadLastGood();
		return RefreshSourceState();
	}

	void ScriptProjectCompiler::Reset()
	{
		m_AsyncJob.reset();
		++m_ConfigurationGeneration;
		if (m_ConfigurationGeneration == 0)
			++m_ConfigurationGeneration;
		m_Project.reset();
		m_ScriptProjectDirectory.clear();
		m_AssembliesDirectory.clear();
		m_ManagedApiReference.clear();
		m_GeneratorReference.clear();
		m_ManagedSolution.clear();
		m_ManagedRuntimeDirectory.clear();
		m_ManagedApiIsProject = false;
		m_GeneratorIsProject = false;
		m_State = ScriptBuildState::Unconfigured;
		m_CurrentSourceHash.clear();
		m_LastGoodSourceHash.clear();
		m_LastGoodBuildID.clear();
		m_LastGoodAssemblyPath.clear();
	}

	bool ScriptProjectCompiler::ResolveManagedReferences(bool bootstrapIfMissing,
		std::vector<ScriptCompilerDiagnostic>* diagnostics)
	{
		auto record = [this, diagnostics](ScriptCompilerDiagnostic diagnostic)
		{
			if (diagnostics)
				diagnostics->push_back(diagnostic);
			Emit(diagnostic);
		};

		if (!m_ManagedApiReference.empty() && !IsRegularFile(m_ManagedApiReference))
			m_ManagedApiReference.clear();
		if (!m_GeneratorReference.empty() && !IsRegularFile(m_GeneratorReference))
			m_GeneratorReference.clear();
		if (!m_ManagedSolution.empty() && !IsRegularFile(m_ManagedSolution))
			m_ManagedSolution.clear();
		if (!IsManagedRuntimeDirectory(m_ManagedRuntimeDirectory))
			m_ManagedRuntimeDirectory.clear();

		const std::filesystem::path executableDirectory = ExecutableDirectory();
		for (const std::filesystem::path& candidate : {
			executableDirectory / "Managed", executableDirectory })
		{
			if (IsManagedRuntimeDirectory(candidate))
			{
				m_ManagedRuntimeDirectory = AbsoluteLexical(candidate);
				break;
			}
		}
		if (m_ManagedApiReference.empty())
		{
			for (const std::filesystem::path& candidate : {
				executableDirectory / "Managed" / "TomCat.Managed.dll",
				executableDirectory / "TomCat.Managed.dll" })
			{
				if (IsRegularFile(candidate))
				{
					m_ManagedApiReference = AbsoluteLexical(candidate);
					m_ManagedApiIsProject = false;
					break;
				}
			}
		}
		if (m_GeneratorReference.empty())
		{
			for (const std::filesystem::path& candidate : {
				executableDirectory / "Managed" / "TomCat.ScriptGenerator.dll",
				executableDirectory / "TomCat.ScriptGenerator.dll" })
			{
				if (IsRegularFile(candidate))
				{
					m_GeneratorReference = AbsoluteLexical(candidate);
					m_GeneratorIsProject = false;
					break;
				}
			}
		}

		for (const std::filesystem::path& root : SearchAncestors())
		{
			const std::filesystem::path managedRoot = root / "Managed";
			const std::filesystem::path solution = managedRoot / "TomCat.Managed.slnx";
			if (m_ManagedSolution.empty() && IsRegularFile(solution))
			{
				m_ManagedSolution = AbsoluteLexical(solution);
				if (m_ManagedApiReference.empty())
				{
					m_ManagedApiReference = AbsoluteLexical(managedRoot /
						"TomCat.Managed" / "bin" / "Release" / "net10.0" /
						"TomCat.Managed.dll");
					m_ManagedApiIsProject = false;
				}
				if (m_GeneratorReference.empty())
				{
					m_GeneratorReference = AbsoluteLexical(managedRoot /
						"TomCat.ScriptGenerator" / "bin" / "Release" / "net10.0" /
						"TomCat.ScriptGenerator.dll");
					m_GeneratorIsProject = false;
				}
				if (m_ManagedRuntimeDirectory.empty())
					m_ManagedRuntimeDirectory = AbsoluteLexical(managedRoot /
						"TomCat.ScriptHost" / "bin" / "Release" / "net10.0");
			}

			if (m_ManagedApiReference.empty())
			{
				const std::filesystem::path candidate = managedRoot /
					"TomCat.Managed" / "TomCat.Managed.csproj";
				if (IsRegularFile(candidate))
				{
					m_ManagedApiReference = AbsoluteLexical(candidate);
					m_ManagedApiIsProject = true;
				}
			}
			if (m_GeneratorReference.empty())
			{
				const std::filesystem::path candidate = managedRoot /
					"TomCat.ScriptGenerator" / "TomCat.ScriptGenerator.csproj";
				if (IsRegularFile(candidate))
				{
					m_GeneratorReference = AbsoluteLexical(candidate);
					m_GeneratorIsProject = true;
				}
			}
			if (m_ManagedRuntimeDirectory.empty())
			{
				for (const char* configuration : { "Release", "Debug" })
				{
					const std::filesystem::path candidate = managedRoot /
						"TomCat.ScriptHost" / "bin" / configuration / "net10.0";
					if (IsManagedRuntimeDirectory(candidate))
					{
						m_ManagedRuntimeDirectory = AbsoluteLexical(candidate);
						break;
					}
				}
			}
		}

		// Once repository Release outputs exist, always canonicalize all three
		// references to that one solution generation. This also keeps the source
		// hash identical before promotion after a clean-checkout bootstrap.
		if (!m_ManagedSolution.empty())
		{
			const std::filesystem::path managedRoot =
				m_ManagedSolution.parent_path();
			const std::filesystem::path apiOutput = managedRoot /
				"TomCat.Managed" / "bin" / "Release" / "net10.0" /
				"TomCat.Managed.dll";
			const std::filesystem::path generatorOutput = managedRoot /
				"TomCat.ScriptGenerator" / "bin" / "Release" / "net10.0" /
				"TomCat.ScriptGenerator.dll";
			const std::filesystem::path runtimeOutput = managedRoot /
				"TomCat.ScriptHost" / "bin" / "Release" / "net10.0";
			if (IsRegularFile(apiOutput) && IsRegularFile(generatorOutput) &&
				IsManagedRuntimeDirectory(runtimeOutput))
			{
				m_ManagedApiReference = AbsoluteLexical(apiOutput);
				m_GeneratorReference = AbsoluteLexical(generatorOutput);
				m_ManagedRuntimeDirectory = AbsoluteLexical(runtimeOutput);
				m_ManagedApiIsProject = false;
				m_GeneratorIsProject = false;
			}
		}

		auto referencesReady = [this]()
		{
			return IsRegularFile(m_ManagedApiReference) &&
				IsRegularFile(m_GeneratorReference);
		};
		if (referencesReady() && IsManagedRuntimeDirectory(m_ManagedRuntimeDirectory))
			return true;

		// A clean repository intentionally has no bin/obj outputs. Defer the one
		// solution build to a compile worker so project configuration stays cheap.
		if (!bootstrapIfMissing && !m_ManagedSolution.empty() &&
			!m_ManagedApiReference.empty() && !m_GeneratorReference.empty())
			return true;

		if (bootstrapIfMissing && !m_ManagedSolution.empty())
		{
			ScriptCompilerDiagnostic starting;
			starting.Level = ScriptCompilerDiagnostic::Severity::Info;
			starting.Code = "TCSP1001";
			starting.Message = "Managed scripting outputs are missing; bootstrapping " +
				PathToUTF8(m_ManagedSolution) + ".";
			record(std::move(starting));

			std::string output;
			std::string launchError;
			int exitCode = -1;
			if (!RunDotNetBuild(m_ManagedSolution, output, exitCode, launchError))
			{
				ScriptCompilerDiagnostic diagnostic;
				diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
				diagnostic.Code = "TCSP0011";
				diagnostic.Message = "Could not bootstrap the managed scripting toolchain: " +
					launchError + ". Install the .NET 10 SDK and ensure dotnet.exe is on PATH.";
				record(std::move(diagnostic));
				return false;
			}
			for (ScriptCompilerDiagnostic& parsed : ParseDiagnostics(output))
				record(std::move(parsed));
			if (exitCode != 0)
			{
				ScriptCompilerDiagnostic diagnostic;
				diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
				diagnostic.Code = "TCSP0012";
				diagnostic.Message = "Managed scripting bootstrap failed (exit code " +
					std::to_string(exitCode) + ").";
				const std::string details = Trim(output);
				if (!details.empty())
					diagnostic.Message += " " + details.substr(0, 16000);
				record(std::move(diagnostic));
				return false;
			}

			for (const std::filesystem::path& root : SearchAncestors())
			{
				const std::filesystem::path managedRoot = root / "Managed";
				if (AbsoluteLexical(managedRoot / "TomCat.Managed.slnx") !=
					m_ManagedSolution)
					continue;
				m_ManagedApiReference = AbsoluteLexical(managedRoot /
					"TomCat.Managed" / "bin" / "Release" / "net10.0" /
					"TomCat.Managed.dll");
				m_ManagedApiIsProject = false;
				m_GeneratorReference = AbsoluteLexical(managedRoot /
					"TomCat.ScriptGenerator" / "bin" / "Release" / "net10.0" /
					"TomCat.ScriptGenerator.dll");
				m_GeneratorIsProject = false;
				m_ManagedRuntimeDirectory = AbsoluteLexical(managedRoot /
					"TomCat.ScriptHost" / "bin" / "Release" / "net10.0");
				break;
			}
			if (referencesReady() && IsManagedRuntimeDirectory(m_ManagedRuntimeDirectory))
				return true;
		}

		ScriptCompilerDiagnostic diagnostic;
		diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
		diagnostic.Code = "TCSP0001";
		diagnostic.Message = "TomCat.Managed, TomCat.ScriptGenerator, or the "
			"TomCat.ScriptHost runtime outputs could not be located. A repository "
			"checkout must contain Managed/TomCat.Managed.slnx and the .NET 10 SDK; "
			"a packaged Editor must place the managed files beside the executable "
			"or in its Managed directory.";
		record(std::move(diagnostic));
		return false;
	}

	bool ScriptProjectCompiler::EnumerateSources(std::vector<ScriptSource>& sources) const
	{
		sources.clear();
		if (!m_Project)
			return false;

		const AssetRegistry& registry = AssetManager::Get().GetRegistry();
		for (const auto& [handle, metadata] : registry.GetAssets())
		{
			if (metadata.Type != AssetType::CSharpScript || metadata.IsMissing ||
				static_cast<uint64_t>(handle) == 0)
				continue;
			const AssetMetadata* current = registry.GetMetadata(metadata.FilePath);
			if (!current || current->Handle != handle || current->IsMissing ||
				current->Type != AssetType::CSharpScript)
				continue;

			const std::filesystem::path absolute = registry.GetFileSystemPath(handle);
			if (!IsRegularFile(absolute))
				continue;
			ScriptSource source;
			source.Handle = static_cast<uint64_t>(handle);
			source.AbsolutePath = AbsoluteLexical(absolute);
			source.ProjectRelativePath =
				(m_Project->GetConfig().AssetDirectory / metadata.FilePath).lexically_normal();
			sources.push_back(std::move(source));
		}

		std::sort(sources.begin(), sources.end(), [](const ScriptSource& left,
			const ScriptSource& right)
		{
			const std::string leftPath = PathToUTF8(left.ProjectRelativePath);
			const std::string rightPath = PathToUTF8(right.ProjectRelativePath);
			return leftPath == rightPath ? left.Handle < right.Handle : leftPath < rightPath;
		});
		return true;
	}

	std::string ScriptProjectCompiler::ComputeSourceHash(
		const std::vector<ScriptSource>& sources) const
	{
		uint64_t hash = kFNVOffset;
		HashText(hash, "TomCat.ScriptProject.v1");
		for (const ScriptSource& source : sources)
		{
			HashText(hash, PathToUTF8(source.ProjectRelativePath));
			HashBytes(hash, &source.Handle, sizeof(source.Handle));
			std::ifstream input(source.AbsolutePath, std::ios::binary);
			std::array<char, 64 * 1024> buffer{};
			while (input)
			{
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				const std::streamsize count = input.gcount();
				if (count > 0)
					HashBytes(hash, buffer.data(), static_cast<size_t>(count));
			}
		}
		for (const std::filesystem::path& reference : {
			m_ManagedApiReference, m_GeneratorReference })
		{
			HashText(hash, PathToUTF8(reference));
			std::error_code error;
			const uintmax_t size = std::filesystem::file_size(reference, error);
			if (!error)
				HashBytes(hash, &size, sizeof(size));
			error.clear();
			const auto modified = std::filesystem::last_write_time(reference, error);
			if (!error)
			{
				const auto ticks = modified.time_since_epoch().count();
				HashBytes(hash, &ticks, sizeof(ticks));
			}
		}
		return HexHash(hash);
	}

	bool ScriptProjectCompiler::RefreshSourceState()
	{
		if (!m_Project)
		{
			m_State = ScriptBuildState::Unconfigured;
			return false;
		}

		if (!AssetManager::Get().Refresh())
		{
			m_State = ScriptBuildState::Failed;
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0002";
			diagnostic.Message = "Asset refresh failed; scripts were not compiled.";
			Emit(diagnostic);
			return false;
		}

		std::vector<ScriptSource> sources;
		if (!EnumerateSources(sources))
		{
			m_State = ScriptBuildState::Failed;
			return false;
		}
		const std::string previousHash = m_CurrentSourceHash;
		const ScriptBuildState previousState = m_State;
		m_CurrentSourceHash = ComputeSourceHash(sources);
		if (!m_LastGoodSourceHash.empty() &&
			m_CurrentSourceHash == m_LastGoodSourceHash &&
			IsRegularFile(m_LastGoodAssemblyPath))
			m_State = ScriptBuildState::Succeeded;
		else if (previousState == ScriptBuildState::Failed &&
			previousHash == m_CurrentSourceHash)
			m_State = ScriptBuildState::Failed;
		else
			m_State = ScriptBuildState::Dirty;
		return true;
	}

	bool ScriptProjectCompiler::WriteScriptAssetMap(
		const std::vector<ScriptSource>& sources, std::string& errorMessage) const
	{
		std::ostringstream json;
		json << "{\n  \"version\": 1,\n  \"assets\": {";
		for (size_t index = 0; index < sources.size(); ++index)
		{
			json << (index == 0 ? "\n" : ",\n") << "    \""
				<< EscapeJson(PathToUTF8(sources[index].ProjectRelativePath)) << "\": "
				<< sources[index].Handle;
		}
		if (!sources.empty())
			json << '\n';
		json << "  }\n}\n";
		return FileSystem::WriteFileAtomically(
			m_ScriptProjectDirectory / "ScriptAssets.json", json.str(), errorMessage);
	}

	bool ScriptProjectCompiler::WriteGeneratedProject(
		const std::vector<ScriptSource>& sources, const std::string& buildID,
		const std::filesystem::path& buildDirectory, std::string& errorMessage) const
	{
		if (!WriteScriptAssetMap(sources, errorMessage))
			return false;

		const std::filesystem::path objectDirectory =
			m_ScriptProjectDirectory / "obj" / buildID;
		const std::filesystem::path disabledImportPath =
			m_ScriptProjectDirectory / ("TomCat.Imports.Disabled." + buildID);
		std::ostringstream project;
		project << "<Project>\n"
			<< "  <!-- Dependency policy: set these before Sdk.props can discover any "
				"project-local or per-user imports. -->\n"
			<< "  <PropertyGroup>\n"
			<< "    <ImportDirectoryBuildProps>false</ImportDirectoryBuildProps>\n"
			<< "    <ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets>\n"
			<< "    <ImportDirectoryPackagesProps>false</ImportDirectoryPackagesProps>\n"
			<< "    <ImportProjectExtensionProps>false</ImportProjectExtensionProps>\n"
			<< "    <ImportProjectExtensionTargets>false</ImportProjectExtensionTargets>\n"
			<< "    <RestoreEnableGlobalPackageReference>false</RestoreEnableGlobalPackageReference>\n"
			<< "    <ManagePackageVersionsCentrally>false</ManagePackageVersionsCentrally>\n"
			<< "    <ImportUserLocationsByWildcardBeforeMicrosoftCommonProps>false</ImportUserLocationsByWildcardBeforeMicrosoftCommonProps>\n"
			<< "    <ImportUserLocationsByWildcardAfterMicrosoftCommonProps>false</ImportUserLocationsByWildcardAfterMicrosoftCommonProps>\n"
			<< "    <ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets>false</ImportUserLocationsByWildcardBeforeMicrosoftCommonTargets>\n"
			<< "    <ImportUserLocationsByWildcardAfterMicrosoftCommonTargets>false</ImportUserLocationsByWildcardAfterMicrosoftCommonTargets>\n"
			<< "    <ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets>false</ImportUserLocationsByWildcardBeforeMicrosoftCSharpTargets>\n"
			<< "    <ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets>false</ImportUserLocationsByWildcardAfterMicrosoftCSharpTargets>\n"
			<< "    <ImportByWildcardBeforeMicrosoftCommonProps>false</ImportByWildcardBeforeMicrosoftCommonProps>\n"
			<< "    <ImportByWildcardAfterMicrosoftCommonProps>false</ImportByWildcardAfterMicrosoftCommonProps>\n"
			<< "    <ImportByWildcardBeforeMicrosoftCommonTargets>false</ImportByWildcardBeforeMicrosoftCommonTargets>\n"
			<< "    <ImportByWildcardAfterMicrosoftCommonTargets>false</ImportByWildcardAfterMicrosoftCommonTargets>\n"
			<< "    <ImportByWildcardBeforeMicrosoftCSharpTargets>false</ImportByWildcardBeforeMicrosoftCSharpTargets>\n"
			<< "    <ImportByWildcardAfterMicrosoftCSharpTargets>false</ImportByWildcardAfterMicrosoftCSharpTargets>\n"
			<< "    <CustomBeforeDirectoryBuildProps />\n"
			<< "    <CustomAfterDirectoryBuildProps />\n"
			<< "    <CustomBeforeDirectoryBuildTargets />\n"
			<< "    <CustomAfterDirectoryBuildTargets />\n"
			<< "    <CustomBeforeMicrosoftCommonProps>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".props</CustomBeforeMicrosoftCommonProps>\n"
			<< "    <CustomAfterMicrosoftCommonProps>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".props</CustomAfterMicrosoftCommonProps>\n"
			<< "    <CustomBeforeMicrosoftCommonTargets>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".targets</CustomBeforeMicrosoftCommonTargets>\n"
			<< "    <CustomAfterMicrosoftCommonTargets>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".targets</CustomAfterMicrosoftCommonTargets>\n"
			<< "    <CustomBeforeMicrosoftCSharpTargets>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".targets</CustomBeforeMicrosoftCSharpTargets>\n"
			<< "    <CustomAfterMicrosoftCSharpTargets>"
			<< EscapeXml(PathToUTF8(disabledImportPath))
			<< ".targets</CustomAfterMicrosoftCSharpTargets>\n"
			<< "    <BaseIntermediateOutputPath>"
			<< EscapeXml(PathToUTF8(objectDirectory))
			<< "\\</BaseIntermediateOutputPath>\n"
			<< "    <MSBuildProjectExtensionsPath>"
			<< EscapeXml(PathToUTF8(objectDirectory))
			<< "\\</MSBuildProjectExtensionsPath>\n"
			<< "  </PropertyGroup>\n"
			<< "  <Import Project=\"Sdk.props\" Sdk=\"Microsoft.NET.Sdk\" />\n"
			<< "  <PropertyGroup>\n"
			<< "    <TargetFramework>net10.0</TargetFramework>\n"
			<< "    <AssemblyName>Assembly-CSharp</AssemblyName>\n"
			<< "    <RootNamespace>Game</RootNamespace>\n"
			<< "    <OutputType>Library</OutputType>\n"
			<< "    <LangVersion>latest</LangVersion>\n"
			<< "    <Nullable>enable</Nullable>\n"
			<< "    <ImplicitUsings>disable</ImplicitUsings>\n"
			<< "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
			<< "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
			<< "    <Deterministic>true</Deterministic>\n"
			<< "    <DebugType>portable</DebugType>\n"
			<< "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n"
			<< "    <AppendRuntimeIdentifierToOutputPath>false</AppendRuntimeIdentifierToOutputPath>\n"
			<< "    <OutputPath>" << EscapeXml(PathToUTF8(buildDirectory)) << "\\</OutputPath>\n"
			<< "    <IntermediateOutputPath>" << EscapeXml(PathToUTF8(objectDirectory))
			<< "\\</IntermediateOutputPath>\n"
			<< "    <PathMap>" << EscapeXml(PathToUTF8(m_Project->GetProjectDirectory()))
			<< "=.</PathMap>\n"
			<< "  </PropertyGroup>\n"
			<< "  <ItemGroup>\n";

		if (m_ManagedApiIsProject)
		{
			project << "    <ProjectReference Include=\""
				<< EscapeXml(PathToUTF8(m_ManagedApiReference))
				<< "\"><Private>false</Private>"
					"<TomCatTrustedReference>true</TomCatTrustedReference>"
					"</ProjectReference>\n";
		}
		else
		{
			project << "    <Reference Include=\"TomCat.Managed\"><HintPath>"
				<< EscapeXml(PathToUTF8(m_ManagedApiReference))
				<< "</HintPath><Private>false</Private>"
					"<TomCatTrustedReference>true</TomCatTrustedReference>"
					"</Reference>\n";
		}

		if (m_GeneratorIsProject)
		{
			project << "    <ProjectReference Include=\""
				<< EscapeXml(PathToUTF8(m_GeneratorReference))
				<< "\" OutputItemType=\"Analyzer\" ReferenceOutputAssembly=\"false\">"
					"<TomCatTrustedReference>true</TomCatTrustedReference>"
					"</ProjectReference>\n";
		}
		else
		{
			project << "    <Analyzer Include=\""
				<< EscapeXml(PathToUTF8(m_GeneratorReference)) << "\" />\n";
		}
		project << "    <AdditionalFiles Include=\""
			<< EscapeXml(PathToUTF8(m_ScriptProjectDirectory / "ScriptAssets.json"))
			<< "\" />\n";
		for (const ScriptSource& source : sources)
		{
			project << "    <Compile Include=\""
				<< EscapeXml(PathToUTF8(source.AbsolutePath)) << "\" Link=\""
				<< EscapeXml(PathToUTF8(source.ProjectRelativePath)) << "\" />\n";
		}
		project << "  </ItemGroup>\n"
			// Snapshot only items declared before the trusted SDK target import. This
			// avoids treating framework references resolved later by the SDK as local
			// DLL injection while still catching props/central-package additions.
			<< "  <ItemGroup>\n"
			<< "    <_TomCatBlockedPackageItem Include=\"@(PackageReference);@(PackageDownload);@(GlobalPackageReference);@(DotNetCliToolReference)\" />\n"
			<< "    <_TomCatBlockedReference Include=\"@(Reference)\" />\n"
			<< "    <_TomCatBlockedReference Remove=\"@(_TomCatBlockedReference->WithMetadataValue('TomCatTrustedReference', 'true'))\" />\n"
			<< "    <_TomCatBlockedProjectReference Include=\"@(ProjectReference)\" />\n"
			<< "    <_TomCatBlockedProjectReference Remove=\"@(_TomCatBlockedProjectReference->WithMetadataValue('TomCatTrustedReference', 'true'))\" />\n"
			<< "  </ItemGroup>\n"
			<< "  <Target Name=\"TomCatValidateBuildInputs\" "
				"BeforeTargets=\"_GenerateRestoreProjectSpec;CollectPackageReferences;ResolveReferences;CoreCompile\">\n"
			<< "    <Error Code=\"TCSP0021\" "
				"Condition=\"'@(_TomCatBlockedPackageItem)' != ''\" "
				"Text=\"NuGet PackageReference and package download items are disabled for TomCat scripts.\" />\n"
			<< "    <Error Code=\"TCSP0022\" "
				"Condition=\"'@(_TomCatBlockedReference)' != '' Or "
				"'@(_TomCatBlockedProjectReference)' != ''\" "
				"Text=\"Local or third-party managed references are disabled for TomCat scripts.\" />\n"
			<< "  </Target>\n"
			<< "  <Import Project=\"Sdk.targets\" Sdk=\"Microsoft.NET.Sdk\" />\n"
			<< "</Project>\n";

		return FileSystem::WriteFileAtomically(
			m_ScriptProjectDirectory / "Assembly-CSharp.csproj", project.str(), errorMessage);
	}

	bool ScriptProjectCompiler::LoadLastGood()
	{
		m_LastGoodSourceHash.clear();
		m_LastGoodBuildID.clear();
		m_LastGoodAssemblyPath.clear();
		const std::filesystem::path path = m_AssembliesDirectory / "last-good.json";
		if (!IsRegularFile(path))
			return false;

		try
		{
			std::string json;
			if (!ReadTextFile(path, json) ||
				!std::regex_search(json, std::regex(R"("version"\s*:\s*1\s*[,}])")))
				return false;
			std::string sourceHash;
			std::string buildID;
			std::string assemblyText;
			if (!ReadJsonStringProperty(json, "sourceHash", sourceHash) ||
				!ReadJsonStringProperty(json, "buildId", buildID) ||
				!ReadJsonStringProperty(json, "assembly", assemblyText))
				return false;
			const std::filesystem::path relativeAssembly =
				UTF8ToPath(assemblyText);
			if (!IsSafeRelativePath(relativeAssembly))
				return false;
			const std::filesystem::path assembly =
				AbsoluteLexical(m_AssembliesDirectory / relativeAssembly);
			if (!IsRegularFile(assembly))
				return false;
			m_LastGoodSourceHash = std::move(sourceHash);
			m_LastGoodBuildID = std::move(buildID);
			m_LastGoodAssemblyPath = assembly;
			return !m_LastGoodSourceHash.empty() && !m_LastGoodBuildID.empty();
		}
		catch (const std::exception& error)
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Warning;
			diagnostic.Code = "TCSP0003";
			diagnostic.Message = std::string("Ignoring invalid last-good.json: ") + error.what();
			diagnostic.File = path;
			Emit(diagnostic);
			return false;
		}
	}

	bool ScriptProjectCompiler::StoreLastGood(const ScriptBuildResult& result,
		std::string& errorMessage)
	{
		const std::filesystem::path relativeAssembly =
			result.AssemblyPath.lexically_relative(m_AssembliesDirectory);
		if (!IsSafeRelativePath(relativeAssembly))
		{
			errorMessage = "compiled assembly is outside Library/ScriptAssemblies";
			return false;
		}

		std::ostringstream json;
		json << "{\n"
			<< "  \"version\": 1,\n"
			<< "  \"sourceHash\": \"" << EscapeJson(result.SourceHash) << "\",\n"
			<< "  \"buildId\": \"" << EscapeJson(result.BuildID) << "\",\n"
			<< "  \"assembly\": \"" << EscapeJson(PathToUTF8(relativeAssembly)) << "\"";
		if (!result.PdbPath.empty())
		{
			const std::filesystem::path relativePdb =
				result.PdbPath.lexically_relative(m_AssembliesDirectory);
			if (IsSafeRelativePath(relativePdb))
				json << ",\n  \"pdb\": \"" << EscapeJson(PathToUTF8(relativePdb)) << "\"";
		}
		json << "\n}\n";
		if (!FileSystem::WriteFileAtomically(
			m_AssembliesDirectory / "last-good.json", json.str(), errorMessage))
			return false;

		m_LastGoodSourceHash = result.SourceHash;
		m_LastGoodBuildID = result.BuildID;
		m_LastGoodAssemblyPath = result.AssemblyPath;
		return true;
	}

	ScriptBuildResult ScriptProjectCompiler::CompileCandidate(
		std::vector<ScriptSource> sources, std::string sourceHash, std::string buildID)
	{
		ScriptBuildResult result;
		result.SourceHash = std::move(sourceHash);
		result.BuildID = std::move(buildID);
		if (!ResolveManagedReferences(true, &result.Diagnostics))
			return result;

		// Managed references can appear during clean-checkout bootstrap and are
		// part of the reproducibility hash.
		result.SourceHash = ComputeSourceHash(sources);
		const std::filesystem::path buildDirectory =
			m_AssembliesDirectory / "Build" / result.BuildID;
		std::error_code directoryError;
		std::filesystem::create_directories(buildDirectory, directoryError);
		if (!directoryError)
			std::filesystem::create_directories(
				m_ScriptProjectDirectory / "obj" / result.BuildID, directoryError);
		if (directoryError)
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0005";
			diagnostic.Message = "Could not create script build directories: " +
				directoryError.message();
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		std::string generationError;
		if (!WriteGeneratedProject(sources, result.BuildID, buildDirectory, generationError))
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0006";
			diagnostic.Message = "Could not generate Assembly-CSharp.csproj: " +
				generationError;
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		std::string output;
		std::string launchError;
		if (!RunRestrictedDotNetBuild(m_ScriptProjectDirectory / "Assembly-CSharp.csproj",
			output, result.ExitCode, launchError))
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0007";
			diagnostic.Message = launchError;
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		std::vector<ScriptCompilerDiagnostic> compilerDiagnostics =
			ParseDiagnostics(output);
		result.Diagnostics.insert(result.Diagnostics.end(),
			std::make_move_iterator(compilerDiagnostics.begin()),
			std::make_move_iterator(compilerDiagnostics.end()));
		result.AssemblyPath = buildDirectory / "Assembly-CSharp.dll";
		const std::filesystem::path pdb = buildDirectory / "Assembly-CSharp.pdb";
		if (IsRegularFile(pdb))
			result.PdbPath = pdb;

		if (result.ExitCode != 0 || !IsRegularFile(result.AssemblyPath))
		{
			const bool hasCompilerError = std::any_of(result.Diagnostics.begin(),
				result.Diagnostics.end(), [](const ScriptCompilerDiagnostic& diagnostic)
				{
					return diagnostic.Level ==
						ScriptCompilerDiagnostic::Severity::Error;
				});
			if (!hasCompilerError)
			{
				ScriptCompilerDiagnostic diagnostic;
				diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
				diagnostic.Code = "TCSP0008";
				diagnostic.Message = Trim(output);
				if (diagnostic.Message.empty())
					diagnostic.Message = "dotnet build failed without compiler diagnostics.";
				if (diagnostic.Message.size() > 16000)
					diagnostic.Message.resize(16000);
				result.Diagnostics.push_back(std::move(diagnostic));
			}
			return result;
		}

		const std::filesystem::path managedDirectory =
			GetManagedRuntimeDirectory();
		if (managedDirectory.empty())
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0013";
			diagnostic.Message = "The candidate assembly compiled, but TomCat.ScriptHost.dll, "
				"its runtimeconfig, and TomCat.Managed.dll are not available together.";
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		std::string runtimeError;
		auto runtime = Scripting::CreateManagedScriptRuntime(managedDirectory,
			result.AssemblyPath, result.PdbPath, {}, &runtimeError);
		if (!runtime || !runtime->IsReady())
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0014";
			diagnostic.Message = "The candidate assembly failed ManagedApiV1 validation";
			if (!runtimeError.empty())
				diagnostic.Message += ": " + runtimeError;
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		std::string manifestJson;
		if (!runtime->ReadProjectMetadata(manifestJson))
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0015";
			diagnostic.Message = "The candidate assembly could not create a metadata domain "
				"or return its generated script manifest.";
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}
		ScriptMetadataCache validationCache;
		std::string metadataError;
		if (!validationCache.ParseAndReplace(manifestJson, metadataError))
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0016";
			diagnostic.Message = "The candidate assembly returned invalid script metadata: " +
				metadataError;
			result.Diagnostics.push_back(std::move(diagnostic));
			return result;
		}

		result.Succeeded = true;
		return result;
	}

	bool ScriptProjectCompiler::FinalizeCandidate(ScriptBuildResult& result,
		const std::vector<ScriptSource>& sources)
	{
		for (const ScriptCompilerDiagnostic& diagnostic : result.Diagnostics)
			Emit(diagnostic);
		if (!result.Succeeded)
		{
			m_State = ScriptBuildState::Failed;
			return false;
		}

		// Bootstrap may have materialized references after the source snapshot.
		if (!ResolveManagedReferences() || !RefreshSourceState())
		{
			result.Succeeded = false;
			m_State = ScriptBuildState::Failed;
			return false;
		}
		if (m_CurrentSourceHash != result.SourceHash)
		{
			result.Succeeded = false;
			result.SourceChangedDuringBuild = true;
			m_State = ScriptBuildState::Dirty;
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Warning;
			diagnostic.Code = "TCSP0009";
			diagnostic.Message = "C# source changed while it was compiling; the validated "
				"assembly was not promoted to last-good.";
			result.Diagnostics.push_back(diagnostic);
			Emit(diagnostic);
			return false;
		}

		// The worker only writes immutable, build-specific inputs. Publish the
		// current project files on the main thread after the generation check.
		std::string generationError;
		if (!WriteGeneratedProject(sources, result.BuildID,
			result.AssemblyPath.parent_path(), generationError))
		{
			result.Succeeded = false;
			m_State = ScriptBuildState::Failed;
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0006";
			diagnostic.Message = "The validated build could not publish "
				"Assembly-CSharp.csproj: " + generationError;
			result.Diagnostics.push_back(diagnostic);
			Emit(diagnostic);
			return false;
		}

		std::string installError;
		if (!StoreLastGood(result, installError))
		{
			result.Succeeded = false;
			m_State = ScriptBuildState::Failed;
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0010";
			diagnostic.Message = "The validated build could not update last-good.json: " +
				installError;
			result.Diagnostics.push_back(diagnostic);
			Emit(diagnostic);
			return false;
		}

		m_State = ScriptBuildState::Succeeded;
		ScriptCompilerDiagnostic diagnostic;
		diagnostic.Level = ScriptCompilerDiagnostic::Severity::Info;
		diagnostic.Code = "TCSP1000";
		diagnostic.Message = "C# scripts compiled and validated successfully (build " +
			result.BuildID + ").";
		result.Diagnostics.push_back(diagnostic);
		Emit(diagnostic);
		return true;
	}

	ScriptBuildResult ScriptProjectCompiler::CompileNow()
	{
		ScriptBuildResult result;
		if (IsCompileInProgress())
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Warning;
			diagnostic.Code = "TCSP0017";
			diagnostic.Message = "A C# script build is already running.";
			result.Diagnostics.push_back(diagnostic);
			Emit(diagnostic);
			return result;
		}
		if (!m_Project)
		{
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0004";
			diagnostic.Message = "No project is configured for script compilation.";
			result.Diagnostics.push_back(diagnostic);
			Emit(diagnostic);
			return result;
		}
		if (!ResolveManagedReferences() || !RefreshSourceState())
			return result;

		std::vector<ScriptSource> sources;
		if (!EnumerateSources(sources))
			return result;
		const std::string buildID = MakeBuildID();
		m_State = ScriptBuildState::Building;

		ScriptProjectCompiler worker;
		worker.m_Project = m_Project;
		worker.m_ScriptProjectDirectory =
			m_ScriptProjectDirectory / "Build" / buildID;
		worker.m_AssembliesDirectory = m_AssembliesDirectory;
		worker.m_ManagedApiReference = m_ManagedApiReference;
		worker.m_GeneratorReference = m_GeneratorReference;
		worker.m_ManagedSolution = m_ManagedSolution;
		worker.m_ManagedRuntimeDirectory = m_ManagedRuntimeDirectory;
		worker.m_ManagedApiIsProject = m_ManagedApiIsProject;
		worker.m_GeneratorIsProject = m_GeneratorIsProject;
		result = worker.CompileCandidate(sources, m_CurrentSourceHash, buildID);
		FinalizeCandidate(result, sources);
		return result;
	}

	bool ScriptProjectCompiler::StartCompile(bool force)
	{
		if (!m_Project || IsCompileInProgress())
			return false;
		if (!ResolveManagedReferences() || !RefreshSourceState())
			return false;
		if (!force && IsCurrentSourceBuilt())
			return false;

		std::vector<ScriptSource> sources;
		if (!EnumerateSources(sources))
			return false;
		const std::string sourceHash = ComputeSourceHash(sources);
		m_CurrentSourceHash = sourceHash;
		const std::string buildID = MakeBuildID();
		auto job = std::make_shared<AsyncCompileJob>();
		job->ConfigurationGeneration = m_ConfigurationGeneration;
		job->Sources = sources;

		const Ref<Project> project = m_Project;
		const std::filesystem::path scriptProjectDirectory =
			m_ScriptProjectDirectory / "Build" / buildID;
		const std::filesystem::path assembliesDirectory = m_AssembliesDirectory;
		const std::filesystem::path managedApiReference = m_ManagedApiReference;
		const std::filesystem::path generatorReference = m_GeneratorReference;
		const std::filesystem::path managedSolution = m_ManagedSolution;
		const std::filesystem::path managedRuntimeDirectory =
			m_ManagedRuntimeDirectory;
		const bool managedApiIsProject = m_ManagedApiIsProject;
		const bool generatorIsProject = m_GeneratorIsProject;
		std::vector<ScriptSource> workerSources = sources;

		m_AsyncJob = job;
		m_State = ScriptBuildState::Building;
		try
		{
			std::thread([job, project, scriptProjectDirectory, assembliesDirectory,
				managedApiReference, generatorReference, managedSolution,
				managedRuntimeDirectory, managedApiIsProject, generatorIsProject,
				workerSources = std::move(workerSources), sourceHash, buildID]() mutable
			{
				ScriptBuildResult result;
				try
				{
					ScriptProjectCompiler worker;
					worker.m_Project = project;
					worker.m_ScriptProjectDirectory = scriptProjectDirectory;
					worker.m_AssembliesDirectory = assembliesDirectory;
					worker.m_ManagedApiReference = managedApiReference;
					worker.m_GeneratorReference = generatorReference;
					worker.m_ManagedSolution = managedSolution;
					worker.m_ManagedRuntimeDirectory = managedRuntimeDirectory;
					worker.m_ManagedApiIsProject = managedApiIsProject;
					worker.m_GeneratorIsProject = generatorIsProject;
					result = worker.CompileCandidate(std::move(workerSources),
						sourceHash, buildID);
				}
				catch (const std::exception& error)
				{
					ScriptCompilerDiagnostic diagnostic;
					diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
					diagnostic.Code = "TCSP0018";
					diagnostic.Message = std::string("Background C# compilation failed: ") +
						error.what();
					result.Diagnostics.push_back(std::move(diagnostic));
				}
				catch (...)
				{
					ScriptCompilerDiagnostic diagnostic;
					diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
					diagnostic.Code = "TCSP0018";
					diagnostic.Message =
						"Background C# compilation failed with an unknown exception.";
					result.Diagnostics.push_back(std::move(diagnostic));
				}
				{
					std::lock_guard<std::mutex> lock(job->ResultMutex);
					job->Result = std::move(result);
				}
				job->Complete.store(true, std::memory_order_release);
			}).detach();
		}
		catch (const std::exception& error)
		{
			m_AsyncJob.reset();
			m_State = ScriptBuildState::Failed;
			ScriptCompilerDiagnostic diagnostic;
			diagnostic.Level = ScriptCompilerDiagnostic::Severity::Error;
			diagnostic.Code = "TCSP0019";
			diagnostic.Message = std::string("Could not start the C# compile worker: ") +
				error.what();
			Emit(diagnostic);
			return false;
		}

		ScriptCompilerDiagnostic diagnostic;
		diagnostic.Level = ScriptCompilerDiagnostic::Severity::Info;
		diagnostic.Code = "TCSP1002";
		diagnostic.Message = "C# script compilation started in the background.";
		Emit(diagnostic);
		return true;
	}

	bool ScriptProjectCompiler::PollCompile(ScriptBuildResult& result)
	{
		const std::shared_ptr<AsyncCompileJob> job = m_AsyncJob;
		if (!job || !job->Complete.load(std::memory_order_acquire))
			return false;
		{
			std::lock_guard<std::mutex> lock(job->ResultMutex);
			result = std::move(job->Result);
		}
		const std::vector<ScriptSource> sources = std::move(job->Sources);
		m_AsyncJob.reset();
		if (job->ConfigurationGeneration != m_ConfigurationGeneration)
			return false;
		FinalizeCandidate(result, sources);
		return true;
	}

	bool ScriptProjectCompiler::EnsureCurrentBuild()
	{
		ScriptBuildResult completed;
		(void)PollCompile(completed);
		if (IsCompileInProgress() || !RefreshSourceState())
			return false;
		if (IsCurrentSourceBuilt())
			return true;
		return CompileNow().Succeeded;
	}

	bool ScriptProjectCompiler::IsCurrentSourceBuilt() const
	{
		return m_State == ScriptBuildState::Succeeded &&
			!m_CurrentSourceHash.empty() &&
			m_CurrentSourceHash == m_LastGoodSourceHash &&
			IsRegularFile(m_LastGoodAssemblyPath);
	}

	std::filesystem::path ScriptProjectCompiler::GetManagedRuntimeDirectory() const
	{
		std::vector<std::filesystem::path> candidates;
		if (!m_ManagedRuntimeDirectory.empty())
			candidates.push_back(m_ManagedRuntimeDirectory);
		const std::filesystem::path executableDirectory = ExecutableDirectory();
		candidates.push_back(executableDirectory / "Managed");
		candidates.push_back(executableDirectory);
		if (!m_ManagedApiReference.empty())
		{
			if (!m_ManagedApiIsProject)
				candidates.push_back(m_ManagedApiReference.parent_path());
			else
			{
				const std::filesystem::path managedRoot =
					m_ManagedApiReference.parent_path().parent_path();
				candidates.push_back(managedRoot / "TomCat.ScriptHost" /
					"bin" / "Release" / "net10.0");
				candidates.push_back(managedRoot / "TomCat.ScriptHost" /
					"bin" / "Debug" / "net10.0");
			}
		}
		for (const std::filesystem::path& root : SearchAncestors())
		{
			candidates.push_back(root / "Managed");
			candidates.push_back(root / "Managed" / "TomCat.ScriptHost" /
				"bin" / "Release" / "net10.0");
			candidates.push_back(root / "Managed" / "TomCat.ScriptHost" /
				"bin" / "Debug" / "net10.0");
		}

		for (const std::filesystem::path& candidate : candidates)
		{
			if (IsRegularFile(candidate / "TomCat.ScriptHost.dll") &&
				IsRegularFile(candidate / "TomCat.ScriptHost.runtimeconfig.json") &&
				IsRegularFile(candidate / "TomCat.Managed.dll"))
				return AbsoluteLexical(candidate);
		}
		return {};
	}

	void ScriptProjectCompiler::Emit(const ScriptCompilerDiagnostic& diagnostic) const
	{
		if (m_DiagnosticCallback)
			m_DiagnosticCallback(diagnostic);
	}

}
