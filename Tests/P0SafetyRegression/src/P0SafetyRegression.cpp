#include <TomCat/Core/ApplicationPaths.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Core/Version.h>
#include <TomCat/Project/ProjectManager.h>
#include <TomCat/Runtime/RuntimeCompatibility.h>
#include <TomCat/Scene/SceneSerializer.h>
#include <TomCat/Scene/Serialization/PrefabArchiveCodec.h>
#include <TomCat/Utils/PathUtils.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

namespace {

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
			Path = std::filesystem::temp_directory_path() /
				("TomCat-P0Safety-" + std::to_string(nonce));
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Path;
	};

	class ScopedCurrentDirectory
	{
	public:
		explicit ScopedCurrentDirectory(const std::filesystem::path& path)
			: m_Previous(std::filesystem::current_path())
		{
			std::filesystem::current_path(path);
		}

		~ScopedCurrentDirectory()
		{
			std::error_code error;
			std::filesystem::current_path(m_Previous, error);
		}

	private:
		std::filesystem::path m_Previous;
	};

	void WriteText(const std::filesystem::path& path, const std::string& contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create test fixture " + path.string());
		output << contents;
		Require(static_cast<bool>(output), "could not write test fixture " + path.string());
	}

	struct EntryState
	{
		std::filesystem::file_type Type{};
		std::filesystem::file_time_type LastWriteTime{};
		uintmax_t Size = 0;
		std::string Contents;

		bool operator==(const EntryState&) const = default;
	};

	using TreeState = std::map<std::string, EntryState>;

	TreeState SnapshotTree(const std::filesystem::path& root)
	{
		TreeState snapshot;
		auto capture = [&](const std::filesystem::path& path)
		{
			std::error_code error;
			const auto status = std::filesystem::symlink_status(path, error);
			Require(!error, "could not stat fixture path");
			EntryState state;
			state.Type = status.type();
			state.LastWriteTime = std::filesystem::last_write_time(path, error);
			Require(!error, "could not read fixture mtime");
			if (std::filesystem::is_regular_file(status))
			{
				state.Size = std::filesystem::file_size(path, error);
				Require(!error, "could not read fixture file size");
				std::ifstream input(path, std::ios::binary);
				std::ostringstream contents;
				contents << input.rdbuf();
				Require(!input.bad(), "could not read fixture contents");
				state.Contents = contents.str();
			}
			const auto relative = std::filesystem::relative(path, root, error);
			Require(!error, "could not make fixture path relative");
			snapshot.emplace(relative.generic_string(), std::move(state));
		};

		capture(root);
		for (std::filesystem::recursive_directory_iterator iterator(root), end;
			iterator != end; ++iterator)
			capture(iterator->path());
		return snapshot;
	}

	void TestInspectProjectNeverWrites()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory = temporary.Path / "LegacyGame";
		const std::filesystem::path projectPath = projectDirectory / "Project.tcproj";
		WriteText(projectPath,
			"SchemaVersion: 3\n"
			"Project:\n"
			"  Name: LegacyGame\n"
			"  Version: 1.0.0\n"
			"  Description: Read-only inspection fixture\n"
			"  EditorVersion: 9.9.9\n"
			"  Template: 2D\n"
			"  AssetDirectory: Assets\n"
			"  StartScene: sample.tomcat\n"
			"  StartSceneHandle: 123\n");

		const TreeState before = SnapshotTree(projectDirectory);
		for (int iteration = 0; iteration < 100; ++iteration)
		{
			auto project = TomCat::ProjectManager::Get().InspectProject(projectPath);
			Require(project && project->GetName() == "LegacyGame",
				"InspectProject failed to parse the legacy fixture");
			Require(project->GetBuildSettings().EntrySceneHandle == TomCat::AssetHandle(123),
				"InspectProject did not construct the legacy in-memory BuildSettings view");
		}
		const TreeState after = SnapshotTree(projectDirectory);
		Require(before == after,
			"100 InspectProject calls changed project contents, entries, size, or mtime");
		Require(!std::filesystem::exists(projectDirectory / ".gitignore"),
			"InspectProject created .gitignore");
		Require(!std::filesystem::exists(
			projectDirectory / "ProjectSettings" / "BuildSettings.json"),
			"InspectProject migrated BuildSettings.json");
	}

	void TestEditorVersionResolutionHasNoFallback()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path editorRoot = temporary.Path / "Editors";
		WriteText(temporary.Path / "TomCat.exe", "adjacent wrong-version editor");
		Require(!TomCat::ProjectManager::ResolveEditorExecutable(editorRoot, "2026.1"),
			"missing requested Editor unexpectedly resolved through an adjacent fallback");
		Require(!TomCat::ProjectManager::ResolveEditorExecutable(
			editorRoot, "../TomCat.exe"),
			"Editor version path traversal escaped the configured install root");

		const std::filesystem::path exactEditor =
			editorRoot / "2026.1" / "TomCat.exe";
		WriteText(exactEditor, "requested editor");
		const auto resolved = TomCat::ProjectManager::ResolveEditorExecutable(
			editorRoot, "2026.1");
		Require(resolved && *resolved == exactEditor.lexically_normal(),
			"installed requested Editor version did not resolve exactly");
	}

	void TestApplicationPaths()
	{
		const std::filesystem::path localRoot = "C:/Users/Test/AppData/Local";
		const auto editor = TomCat::ApplicationPaths::ResolveProductDataRoot(
			localRoot, TomCat::ApplicationProduct::Editor);
		const auto hub = TomCat::ApplicationPaths::ResolveProductDataRoot(
			localRoot, TomCat::ApplicationProduct::Hub);
		const auto player = TomCat::ApplicationPaths::ResolveProductDataRoot(
			localRoot, TomCat::ApplicationProduct::Player);
		Require(editor && *editor == (localRoot / "TomCat" / "Editor").lexically_normal(),
			"Editor data path escaped its LocalAppData product root");
		Require(hub && *hub == (localRoot / "TomCat" / "Hub").lexically_normal(),
			"Hub data path escaped its LocalAppData product root");
		Require(player && *player == (localRoot / "TomCat" / "Player").lexically_normal(),
			"Player data path escaped its LocalAppData product root");
		Require(!TomCat::ApplicationPaths::ResolveProductDataRoot(
			localRoot, TomCat::ApplicationProduct::Unknown),
			"unknown products must not receive an implicit writable path");

		Require(TomCat::ApplicationPaths::IdentifyExecutable("TomCat.exe")
			== TomCat::ApplicationProduct::Editor, "packaged Editor identity mismatch");
		Require(TomCat::ApplicationPaths::IdentifyExecutable("TomCatInut.exe")
			== TomCat::ApplicationProduct::Editor, "development Editor identity mismatch");
		Require(TomCat::ApplicationPaths::IdentifyExecutable("TomCatHub.exe")
			== TomCat::ApplicationProduct::Hub, "packaged Hub identity mismatch");
		Require(TomCat::ApplicationPaths::IdentifyExecutable("Manager.exe")
			== TomCat::ApplicationProduct::Hub, "development Hub identity mismatch");
		Require(TomCat::ApplicationPaths::IdentifyExecutable("TomCatPlayer.exe")
			== TomCat::ApplicationProduct::Player, "Player identity mismatch");
	}

	void TestUnifiedVersionSource()
	{
		static_assert(TomCat::Project::CurrentSchemaVersion
			== TomCat::Version::ProjectFormatCurrent);
		static_assert(TomCat::SceneSerializer::CurrentSchemaVersion
			== TomCat::Version::SceneFormatCurrent);
		static_assert(TomCat::PrefabArchiveCodec::CurrentSchemaVersion
			== TomCat::Version::PrefabFormatCurrent);
		static_assert(TomCat::RuntimeCompatibility::TcpakVersion
			== TomCat::Version::TcpakFormatCurrent);
		static_assert(TomCat::RuntimeCompatibility::PlayerTemplateSchemaVersion
			== TomCat::Version::PlayerTemplateFormatCurrent);
		static_assert(TomCat::RuntimeCompatibility::PlayerAbiVersion
			== TomCat::Version::PlayerAbiCurrent);
		Require(!TomCat::Version::ProductVersion.empty()
			&& !TomCat::Version::EngineBuildID.empty(),
			"product release version source is empty");
	}

#ifdef TC_PLATFORM_WINDOWS
	std::filesystem::path CurrentExecutablePath()
	{
		std::vector<wchar_t> buffer(MAX_PATH);
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
				static_cast<DWORD>(buffer.size()));
			Require(length != 0, "could not resolve regression executable");
			if (length < buffer.size() - 1)
				return std::filesystem::path(std::wstring(buffer.data(), length));
			Require(buffer.size() < 32768, "regression executable path is too long");
			buffer.resize(buffer.size() * 2);
		}
	}

	void RunChild(const std::filesystem::path& executable,
		const std::filesystem::path& blockedLocalRoot)
	{
		std::wstring command = L"\"" + executable.wstring()
			+ L"\" --log-fallback \"" + blockedLocalRoot.wstring() + L"\"";
		std::vector<wchar_t> mutableCommand(command.begin(), command.end());
		mutableCommand.push_back(L'\0');
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(),
			nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
			&startup, &process);
		Require(created != FALSE, "could not launch log fallback child");
		CloseHandle(process.hThread);
		WaitForSingleObject(process.hProcess, 10000);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		Require(exitCode == 0,
			"file logging failure did not preserve console-only startup");
	}
#endif

}

int main(int argc, char** argv)
{
	try
	{
		if (argc == 3 && std::string_view(argv[1]) == "--log-fallback")
		{
			const bool fileSink = TomCat::Log::Init(TomCat::ApplicationProduct::Player,
				std::filesystem::path(argv[2]));
			Require(!fileSink, "blocked LocalAppData unexpectedly accepted a file sink");
			Require(static_cast<bool>(TomCat::Log::GetCoreLogger()),
				"console logger was not initialized after file sink failure");
			TC_Core_Info("console fallback remains available");
			TomCat::Log::Shutdown();
			return 0;
		}
		Require(argc == 1, "unexpected P0SafetyRegression arguments");

		TemporaryDirectory logging;
		const std::filesystem::path installDirectory = logging.Path / "ReadOnlyInstall";
		const std::filesystem::path localRoot = logging.Path / "LocalAppData";
		std::filesystem::create_directories(installDirectory);
		const std::filesystem::path installLog = installDirectory / "TomCat.log";
		bool fileSink = false;
		{
			// Packaged EntryPoint uses the executable directory as CWD for virtual
			// resources. The old logger therefore wrote into this simulated install.
			ScopedCurrentDirectory installWorkingDirectory(installDirectory);
			fileSink = TomCat::Log::Init(
				TomCat::ApplicationProduct::Player, localRoot);
			TC_Core_Info("P0 path regression");
		}
		Require(fileSink, "writable LocalAppData did not enable file logging");
		const auto expectedLog = TomCat::ApplicationPaths::ResolveProductDataRoot(
			localRoot, TomCat::ApplicationProduct::Player);
		Require(expectedLog
			&& std::filesystem::is_regular_file(*expectedLog / "TomCat.log"),
			"Player log was not written under LocalAppData/TomCat/Player");
		Require(!std::filesystem::exists(installLog),
			"logging wrote TomCat.log into the installation directory");

#ifdef TC_PLATFORM_WINDOWS
		const std::filesystem::path blockedRoot = logging.Path / "BlockedLocalAppData";
		WriteText(blockedRoot / "TomCat", "blocks directory creation");
		RunChild(CurrentExecutablePath(), blockedRoot);
#endif

		TestApplicationPaths();
		TestUnifiedVersionSource();
		TestEditorVersionResolutionHasNoFallback();
		TestInspectProjectNeverWrites();
		std::cout << "PASS P0 safety: read-only inspection, exact Editor version, "
			"LocalAppData paths, and console fallback\n";
		TomCat::Log::Shutdown();
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL P0 safety regression: " << exception.what() << '\n';
		return 1;
	}
}
