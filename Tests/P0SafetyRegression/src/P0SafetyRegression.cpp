#include <TomCat/Core/ApplicationPaths.h>
#include <TomCat/Core/CrashReporter.h>
#include <TomCat/Core/Log.h>
#include <TomCat/Core/Version.h>
#include <TomCat/Asset/ContentHash.h>
#include <TomCat/Project/ProjectManager.h>
#include <TomCat/Runtime/RuntimeCompatibility.h>
#include <TomCat/Scene/SceneSerializer.h>
#include <TomCat/Scene/Serialization/PrefabArchiveCodec.h>
#include <TomCat/Utils/FileSystemUtils.h>
#include <TomCat/Utils/PathUtils.h>

#include <algorithm>
#include <chrono>
#include <cstring>
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
	#include <winioctl.h>
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

#ifdef TC_PLATFORM_WINDOWS
	bool CreateDirectoryJunction(const std::filesystem::path& junction,
		const std::filesystem::path& target)
	{
		std::error_code error;
		const std::filesystem::path absoluteTarget =
			std::filesystem::absolute(target, error).lexically_normal();
		if (error || !std::filesystem::is_directory(absoluteTarget, error) || error)
		{
			SetLastError(error ? static_cast<DWORD>(error.value()) : ERROR_PATH_NOT_FOUND);
			return false;
		}
		if (!CreateDirectoryW(junction.c_str(), nullptr))
			return false;

		const std::wstring substitute = L"\\??\\" + absoluteTarget.wstring();
		const std::wstring printName = absoluteTarget.wstring();
		const size_t substituteBytes = substitute.size() * sizeof(wchar_t);
		const size_t printBytes = printName.size() * sizeof(wchar_t);
		const size_t pathBytes = substituteBytes + sizeof(wchar_t) +
			printBytes + sizeof(wchar_t);
		constexpr size_t mountPointHeaderBytes = 16;
		const size_t inputBytes = mountPointHeaderBytes + pathBytes;
		if (inputBytes - 8 > 0xffff || substituteBytes > 0xffff ||
			printBytes > 0xffff)
		{
			RemoveDirectoryW(junction.c_str());
			SetLastError(ERROR_BUFFER_OVERFLOW);
			return false;
		}

		std::vector<uint8_t> data(inputBytes, 0);
		auto writeWord = [&](size_t offset, WORD value)
		{
			data[offset] = static_cast<uint8_t>(value);
			data[offset + 1] = static_cast<uint8_t>(value >> 8);
		};
		auto writeDword = [&](size_t offset, DWORD value)
		{
			for (size_t byte = 0; byte < sizeof(value); ++byte)
				data[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
		};
		writeDword(0, IO_REPARSE_TAG_MOUNT_POINT);
		writeWord(4, static_cast<WORD>(inputBytes - 8));
		writeWord(10, static_cast<WORD>(substituteBytes));
		writeWord(12,
			static_cast<WORD>(substituteBytes + sizeof(wchar_t)));
		writeWord(14, static_cast<WORD>(printBytes));
		std::memcpy(data.data() + mountPointHeaderBytes,
			substitute.data(), substituteBytes);
		std::memcpy(data.data() + mountPointHeaderBytes + substituteBytes
			+ sizeof(wchar_t), printName.data(), printBytes);

		const HANDLE directory = CreateFileW(junction.c_str(), GENERIC_WRITE, 0,
			nullptr, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (directory == INVALID_HANDLE_VALUE)
		{
			const DWORD failure = GetLastError();
			RemoveDirectoryW(junction.c_str());
			SetLastError(failure);
			return false;
		}
		DWORD returned = 0;
		const BOOL succeeded = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT,
			data.data(), static_cast<DWORD>(inputBytes), nullptr, 0, &returned,
			nullptr);
		const DWORD failure = succeeded ? ERROR_SUCCESS : GetLastError();
		CloseHandle(directory);
		if (!succeeded)
		{
			RemoveDirectoryW(junction.c_str());
			SetLastError(failure);
		}
		return succeeded != FALSE;
	}

	bool SameWindowsPath(const std::filesystem::path& left,
		const std::filesystem::path& right)
	{
		std::error_code leftError;
		std::error_code rightError;
		const std::wstring leftAbsolute =
			std::filesystem::absolute(left, leftError).lexically_normal().wstring();
		const std::wstring rightAbsolute =
			std::filesystem::absolute(right, rightError).lexically_normal().wstring();
		return !leftError && !rightError &&
			CompareStringOrdinal(leftAbsolute.c_str(), -1,
				rightAbsolute.c_str(), -1, TRUE) == CSTR_EQUAL;
	}

	class ScopedDirectoryMutationTestHook
	{
	public:
		explicit ScopedDirectoryMutationTestHook(
			TomCat::FileSystem::DirectoryMutationTestHook hook)
		{
			TomCat::FileSystem::SetDirectoryMutationTestHookForTesting(
				std::move(hook));
		}

		~ScopedDirectoryMutationTestHook()
		{
			TomCat::FileSystem::SetDirectoryMutationTestHookForTesting({});
		}

		ScopedDirectoryMutationTestHook(
			const ScopedDirectoryMutationTestHook&) = delete;
		ScopedDirectoryMutationTestHook& operator=(
			const ScopedDirectoryMutationTestHook&) = delete;
	};
#endif

	void WriteText(const std::filesystem::path& path, const std::string& contents)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		Require(static_cast<bool>(output), "could not create test fixture " + path.string());
		output << contents;
		Require(static_cast<bool>(output), "could not write test fixture " + path.string());
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary);
		Require(static_cast<bool>(input), "could not open test fixture " + path.string());
		std::ostringstream contents;
		contents << input.rdbuf();
		Require(!input.bad(), "could not read test fixture " + path.string());
		return contents.str();
	}

	std::string LegacyProjectDocument(std::string_view name = "LegacyGame")
	{
		return "SchemaVersion: 3\n"
			"Project:\n"
			"  Name: " + std::string(name) + "\n"
			"  Version: 1.0.0\n"
			"  Description: Transactional migration fixture\n"
			"  EditorVersion: 9.9.9\n"
			"  Template: 2D\n"
			"  AssetDirectory: Assets\n"
			"  StartScene: sample.tomcat\n"
			"  StartSceneHandle: 123\n";
	}

	std::string HashText(std::string_view contents)
	{
		return TomCat::ComputeContentSHA256(std::span<const uint8_t>(
			reinterpret_cast<const uint8_t*>(contents.data()), contents.size()));
	}

	std::string PreparedMigrationJournal(std::string_view transactionID,
		std::string_view originalProject, bool settingsDirectoryExisted,
		bool backupRootExisted)
	{
		return
			"{\n"
			"  \"schemaVersion\": 2,\n"
			"  \"transactionId\": \"" + std::string(transactionID) + "\",\n"
			"  \"state\": \"prepared\",\n"
			"  \"sourceSchemaVersion\": 3,\n"
			"  \"targetSchemaVersion\": 4,\n"
			"  \"settingsDirectoryExisted\": " +
				std::string(settingsDirectoryExisted ? "true" : "false") + ",\n"
			"  \"backupRootExisted\": " +
				std::string(backupRootExisted ? "true" : "false") + ",\n"
			"  \"entries\": [\n"
			"    { \"target\": \"Project.tcproj\", \"existed\": true, "
			"\"originalSize\": " + std::to_string(originalProject.size()) +
			", \"originalSha256\": \"" + HashText(originalProject) +
			"\", \"backup\": \"original-0.bin\" },\n"
			"    { \"target\": \"ProjectSettings/BuildSettings.json\", "
			"\"existed\": false, \"originalSize\": 0, "
			"\"originalSha256\": \"\", \"backup\": \"original-1.bin\" },\n"
			"    { \"target\": \"ProjectSettings/PlayerSettings.json\", "
			"\"existed\": false, \"originalSize\": 0, "
			"\"originalSha256\": \"\", \"backup\": \"original-2.bin\" }\n"
			"  ]\n"
			"}\n";
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
		WriteText(projectPath, LegacyProjectDocument());

		const TreeState before = SnapshotTree(projectDirectory);
		for (int iteration = 0; iteration < 100; ++iteration)
		{
			auto project = TomCat::ProjectManager::Get().InspectProject(projectPath);
			Require(project && project->GetName() == "LegacyGame",
				"InspectProject failed to parse the legacy fixture");
			Require(project->GetBuildSettings().EntrySceneHandle == TomCat::AssetHandle(123),
				"InspectProject did not construct the legacy in-memory BuildSettings view");
		}
		TomCat::ProjectMigrationPreview preview;
		std::string previewError;
		Require(TomCat::ProjectManager::Get().PreviewProjectMigration(
			projectPath, preview, previewError),
			"migration preview failed: " + previewError);
		Require(preview.SourceSchemaVersion == 3 &&
			preview.TargetSchemaVersion == TomCat::Project::CurrentSchemaVersion,
			"migration preview reported the wrong schema transition");
		Require(preview.Changes.size() == 4,
			"migration preview did not enumerate Project, Build, Player, and .gitignore");
		{
			ScopedCurrentDirectory projectWorkingDirectory(projectDirectory);
			TomCat::ProjectMigrationPreview relativePreview;
			Require(TomCat::Project::PreviewMigration(
				"Project.tcproj", relativePreview, previewError) &&
				relativePreview.Changes.size() == preview.Changes.size(),
				"migration preview failed for a CWD-relative project path");
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

	void TestTransactionalProjectMigration()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory = temporary.Path / "LegacyGame";
		const std::filesystem::path projectPath = projectDirectory / "Project.tcproj";
		const std::string originalProject = LegacyProjectDocument();
		const std::string originalIgnore = "custom-rule\n";
		WriteText(projectPath, originalProject);
		WriteText(projectDirectory / ".gitignore", originalIgnore);

		TomCat::ProjectMigrationPreview preview;
		std::string previewError;
		Require(TomCat::Project::PreviewMigration(projectPath, preview, previewError),
			"could not preview transactional migration: " + previewError);
		Require(preview.RequiresMigration() && preview.Changes.size() == 4,
			"transactional migration preview omitted an affected file");
		Require(preview.ProjectPath ==
			std::filesystem::weakly_canonical(projectPath),
			"migration preview was not bound to its absolute project path");

		const std::filesystem::path replayDirectory = temporary.Path / "ReplayGame";
		const std::filesystem::path replayProjectPath =
			replayDirectory / "Project.tcproj";
		WriteText(replayProjectPath, originalProject);
		WriteText(replayDirectory / ".gitignore", originalIgnore);
		const TreeState beforeCrossProjectReplay = SnapshotTree(replayDirectory);
		Require(TomCat::Project::LoadWithMigration(
			replayProjectPath, preview) == nullptr,
			"migration approval for one project was replayed against another project");
		Require(SnapshotTree(replayDirectory) == beforeCrossProjectReplay,
			"cross-project migration plan rejection changed the target project tree");

		const TreeState beforeUnapprovedLoad = SnapshotTree(projectDirectory);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"default Project::Load implicitly migrated a legacy project");
		Require(SnapshotTree(projectDirectory) == beforeUnapprovedLoad,
			"default Project::Load changed the project tree while rejecting migration");

		auto project = TomCat::Project::LoadWithMigration(projectPath, preview);
		Require(project != nullptr, "transactional legacy project migration failed");
		Require(ReadText(projectPath).find("SchemaVersion: 4") != std::string::npos,
			"transaction did not upgrade Project.tcproj");
		Require(std::filesystem::is_regular_file(project->GetBuildSettingsPath()) &&
			std::filesystem::is_regular_file(project->GetPlayerSettingsPath()),
			"transaction did not publish both settings documents");
		const std::string migratedIgnore = ReadText(projectDirectory / ".gitignore");
		Require(migratedIgnore.find("custom-rule") != std::string::npos &&
			migratedIgnore.find("/Library/") != std::string::npos &&
			migratedIgnore.find("/Cache/") != std::string::npos &&
			migratedIgnore.find("/UserSettings/") != std::string::npos,
			"transaction did not preserve and extend .gitignore");

		const std::filesystem::path activeJournal =
			projectDirectory / "ProjectSettings" / ".migration-journal.json";
		Require(!std::filesystem::exists(activeJournal),
			"committed migration left an active recovery journal");
		const std::filesystem::path backupRoot =
			projectDirectory / "ProjectSettings" / "MigrationBackups";
		std::vector<std::filesystem::path> backups;
		for (const auto& entry : std::filesystem::directory_iterator(backupRoot))
		{
			if (entry.is_directory())
				backups.push_back(entry.path());
		}
		Require(backups.size() == 1,
			"committed migration did not retain exactly one full backup");
		const std::string archiveJournal = ReadText(backups.front() / "journal.json");
		Require(archiveJournal.find("\"schemaVersion\": 2") != std::string::npos &&
			archiveJournal.find("\"state\": \"committed\"") != std::string::npos &&
			archiveJournal.find("\"originalSha256\": \"" +
				HashText(originalProject) + "\"") != std::string::npos &&
			archiveJournal.find("\"originalSha256\": \"" +
				HashText(originalIgnore) + "\"") != std::string::npos,
			"retained migration journal omitted state or original SHA-256 data");

		bool projectWasBackedUp = false;
		bool ignoreWasBackedUp = false;
		for (const auto& entry : std::filesystem::directory_iterator(backups.front()))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".bin")
				continue;
			const std::string bytes = ReadText(entry.path());
			projectWasBackedUp |= bytes == originalProject;
			ignoreWasBackedUp |= bytes == originalIgnore;
		}
		Require(projectWasBackedUp && ignoreWasBackedUp,
			"full migration backup did not preserve original bytes");
	}

	void TestMigrationPreviewDetectsSameSizeReplacement()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory =
			temporary.Path / "PreviewHash";
		const std::filesystem::path projectPath =
			projectDirectory / "Project.tcproj";
		const std::string firstDocument = LegacyProjectDocument("HashAlpha");
		const std::string secondDocument = LegacyProjectDocument("HashBravo");
		Require(firstDocument.size() == secondDocument.size(),
			"same-size preview fixture has different lengths");
		WriteText(projectPath, firstDocument);

		TomCat::ProjectMigrationPreview firstPreview;
		std::string previewError;
		Require(TomCat::Project::PreviewMigration(
			projectPath, firstPreview, previewError),
			"first hash preview failed: " + previewError);
		WriteText(projectPath, secondDocument);
		TomCat::ProjectMigrationPreview secondPreview;
		Require(TomCat::Project::PreviewMigration(
			projectPath, secondPreview, previewError),
			"second hash preview failed: " + previewError);

		auto projectChange = [&](const TomCat::ProjectMigrationPreview& preview)
			-> const TomCat::ProjectMigrationChange&
		{
			const auto found = std::find_if(preview.Changes.begin(),
				preview.Changes.end(), [&](const TomCat::ProjectMigrationChange& change)
				{
					return change.RelativePath == projectPath.filename();
				});
			Require(found != preview.Changes.end(),
				"migration preview omitted the project file");
			return *found;
		};
		const TomCat::ProjectMigrationChange& first = projectChange(firstPreview);
		const TomCat::ProjectMigrationChange& second = projectChange(secondPreview);
		Require(first.OriginalSize == second.OriginalSize &&
			first.OriginalSHA256.size() == 64 &&
			second.OriginalSHA256.size() == 64 &&
			first.OriginalSHA256 != second.OriginalSHA256,
			"migration preview did not detect a same-size file replacement");

		const TreeState beforeStalePlan = SnapshotTree(projectDirectory);
		Require(TomCat::Project::LoadWithMigration(
			projectPath, firstPreview) == nullptr,
			"migration accepted an approved plan after a same-size source replacement");
		Require(SnapshotTree(projectDirectory) == beforeStalePlan,
			"stale migration plan rejection changed the project tree");
	}

#ifdef TC_PLATFORM_WINDOWS
	void TestMigrationFailureRollsBackEveryFile()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory = temporary.Path / "LockedGame";
		const std::filesystem::path projectPath = projectDirectory / "Project.tcproj";
		const std::string originalProject = LegacyProjectDocument("LockedGame");
		WriteText(projectPath, originalProject);
		TomCat::ProjectMigrationPreview preview;
		std::string previewError;
		Require(TomCat::Project::PreviewMigration(
			projectPath, preview, previewError),
			"could not preview locked migration fixture: " + previewError);

		const HANDLE projectLock = CreateFileW(projectPath.c_str(), GENERIC_READ,
			FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		Require(projectLock != INVALID_HANDLE_VALUE,
			"could not lock the project fixture against replacement");
		auto project = TomCat::Project::LoadWithMigration(projectPath, preview);
		CloseHandle(projectLock);

		Require(project == nullptr,
			"migration unexpectedly succeeded while Project.tcproj was replacement-locked");
		Require(ReadText(projectPath) == originalProject,
			"failed migration did not restore the original Project.tcproj bytes");
		Require(!std::filesystem::exists(
			projectDirectory / "ProjectSettings" / "BuildSettings.json") &&
			!std::filesystem::exists(
				projectDirectory / "ProjectSettings" / "PlayerSettings.json"),
			"failed migration left one side of the cross-file settings update");
		Require(!std::filesystem::exists(
			projectDirectory / "ProjectSettings" / ".migration-journal.json") &&
			!std::filesystem::exists(
				projectDirectory / "ProjectSettings" / "MigrationBackups"),
			"successful rollback left an active journal or transaction backup");
		Require(!std::filesystem::exists(projectDirectory / ".gitignore"),
			"failed migration leaked a later .gitignore update");
	}
#endif

	void TestInterruptedMigrationRecovery()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory = temporary.Path / "CrashedGame";
		const std::filesystem::path projectPath = projectDirectory / "Project.tcproj";
		const std::string originalProject = LegacyProjectDocument("CrashedGame");
		WriteText(projectPath, originalProject);

		const std::filesystem::path settingsDirectory =
			projectDirectory / "ProjectSettings";
		const std::filesystem::path backupDirectory =
			settingsDirectory / "MigrationBackups" / "recovery-test";
		WriteText(backupDirectory / "original-0.bin", originalProject);
		WriteText(projectPath,
			"SchemaVersion: 4\nProject:\n  Name: CrashedGame\n"
			"  Version: 1.0.0\n  Description: partial\n"
			"  EditorVersion: 9.9.9\n  Template: 2D\n"
			"  AssetDirectory: Assets\n");
		WriteText(settingsDirectory / "BuildSettings.json", "partial build");
		WriteText(settingsDirectory / "PlayerSettings.json", "partial player");

		const std::string journal = PreparedMigrationJournal(
			"recovery-test", originalProject, false, false);
		WriteText(backupDirectory / "journal.json", journal);
		WriteText(settingsDirectory / ".migration-journal.json", journal);

		const TreeState beforeUnapprovedRecovery = SnapshotTree(projectDirectory);
		Require(TomCat::Project::Load(projectPath) == nullptr,
			"default Project::Load implicitly recovered an interrupted migration");
		Require(SnapshotTree(projectDirectory) == beforeUnapprovedRecovery,
			"default Project::Load changed the interrupted migration tree");

		TomCat::ProjectMigrationRecoveryPreview recoveryPreview;
		std::string recoveryError;
		Require(TomCat::Project::PreviewInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"interrupted migration preview failed: " + recoveryError);
		Require(recoveryPreview.HasPendingRecovery() &&
			recoveryPreview.TransactionID == "recovery-test" &&
			recoveryPreview.JournalState == "prepared" &&
			recoveryPreview.WillModifyProjectFiles() &&
			recoveryPreview.Changes.size() == 3,
			"interrupted migration preview omitted recovery actions");
		Require(recoveryPreview.Changes[0].Action ==
				TomCat::ProjectMigrationRecoveryAction::RestoreOriginal &&
			recoveryPreview.Changes[1].Action ==
				TomCat::ProjectMigrationRecoveryAction::RemoveCreatedFile,
			"interrupted migration preview reported incorrect file actions");
		Require(SnapshotTree(projectDirectory) == beforeUnapprovedRecovery,
			"interrupted migration preview modified the project tree");

		WriteText(settingsDirectory / ".migration-journal.json", journal + "\n");
		const TreeState beforeStaleJournalApproval =
			SnapshotTree(projectDirectory);
		Require(!TomCat::Project::RecoverInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"recovery accepted a journal changed after approval");
		Require(SnapshotTree(projectDirectory) == beforeStaleJournalApproval,
			"stale journal approval changed the interrupted migration tree");
		WriteText(settingsDirectory / ".migration-journal.json", journal);

		const std::string manualRepair =
			"SchemaVersion: 4\nProject:\n  Name: UserRepair\n";
		WriteText(projectPath, manualRepair);
		const TreeState beforeStaleApproval = SnapshotTree(projectDirectory);
		Require(!TomCat::Project::RecoverInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"recovery accepted an approval made before a manual repair");
		Require(recoveryError.find("changed after approval") != std::string::npos,
			"stale recovery approval did not explain that a new review is required");
		Require(SnapshotTree(projectDirectory) == beforeStaleApproval,
			"stale recovery approval changed a manually repaired project");

		TomCat::ProjectMigrationRecoveryPreview refreshedPreview;
		Require(TomCat::Project::PreviewInterruptedMigration(
			projectPath, refreshedPreview, recoveryError),
			"refreshed interrupted migration preview failed: " + recoveryError);
		const std::filesystem::path exportDirectory =
			temporary.Path / "ExportedRecovery";
		const TreeState beforeExport = SnapshotTree(projectDirectory);
		Require(TomCat::Project::ExportInterruptedMigrationBackup(
			projectPath, refreshedPreview, exportDirectory, recoveryError),
			"interrupted migration backup export failed: " + recoveryError);
		Require(SnapshotTree(projectDirectory) == beforeExport,
			"migration recovery backup export modified the project tree");
		Require(ReadText(exportDirectory / "Original" / "Project.tcproj")
				== originalProject &&
			ReadText(exportDirectory / "Current" / "Project.tcproj")
				== manualRepair &&
			ReadText(exportDirectory / "migration-journal.json") == journal,
			"migration recovery export did not preserve original/current/journal bytes");

		Require(TomCat::Project::RecoverInterruptedMigration(
			projectPath, refreshedPreview, recoveryError),
			"interrupted migration recovery failed: " + recoveryError);
		Require(ReadText(projectPath) == originalProject,
			"journal recovery did not restore original project bytes");
		Require(!std::filesystem::exists(settingsDirectory),
			"journal recovery did not remove transaction-created files and directories");
	}

	void TestAbandonRecoveryKeepsCurrentFilesAndArchive()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory =
			temporary.Path / "KeepRepair";
		const std::filesystem::path projectPath =
			projectDirectory / "Project.tcproj";
		const std::filesystem::path settingsDirectory =
			projectDirectory / "ProjectSettings";
		const std::filesystem::path backupDirectory =
			settingsDirectory / "MigrationBackups" / "keep-repair";
		const std::string originalProject =
			LegacyProjectDocument("KeepRepair");
		const std::string repairedProject =
			"SchemaVersion: 4\nProject:\n  Name: RepairedByUser\n";
		WriteText(projectPath, repairedProject);
		WriteText(settingsDirectory / "BuildSettings.json",
			"user repaired build settings");
		WriteText(settingsDirectory / "PlayerSettings.json",
			"user repaired player settings");
		WriteText(backupDirectory / "original-0.bin", originalProject);
		const std::string journal = PreparedMigrationJournal(
			"keep-repair", originalProject, false, false);
		WriteText(backupDirectory / "journal.json", journal);
		WriteText(settingsDirectory / ".migration-journal.json", journal);

		TomCat::ProjectMigrationRecoveryPreview preview;
		std::string error;
		Require(TomCat::Project::PreviewInterruptedMigration(
			projectPath, preview, error),
			"keep-current recovery preview failed: " + error);
		WriteText(backupDirectory / "journal.json", "damaged archive journal");
		const TreeState beforeUnsafeAbandon = SnapshotTree(projectDirectory);
		Require(!TomCat::Project::AbandonInterruptedMigrationRecovery(
			projectPath, preview, error),
			"keep-current recovery discarded its only valid journal manifest");
		Require(SnapshotTree(projectDirectory) == beforeUnsafeAbandon &&
			std::filesystem::is_regular_file(
				settingsDirectory / ".migration-journal.json"),
			"failed keep-current preflight changed the project or active journal");
		WriteText(backupDirectory / "journal.json", journal);
		Require(TomCat::Project::AbandonInterruptedMigrationRecovery(
			projectPath, preview, error),
			"keep-current recovery action failed: " + error);
		Require(ReadText(projectPath) == repairedProject &&
			ReadText(settingsDirectory / "BuildSettings.json") ==
				"user repaired build settings" &&
			ReadText(settingsDirectory / "PlayerSettings.json") ==
				"user repaired player settings",
			"abandoning recovery changed user-repaired project files");
		Require(!std::filesystem::exists(
				settingsDirectory / ".migration-journal.json") &&
			ReadText(backupDirectory / "original-0.bin") == originalProject &&
			ReadText(backupDirectory / "journal.json") == journal,
			"abandoning recovery did not preserve its backup archive");

		TomCat::ProjectMigrationRecoveryPreview after;
		Require(TomCat::Project::PreviewInterruptedMigration(
			projectPath, after, error) && !after.HasPendingRecovery(),
			"abandoned recovery remained active: " + error);
	}

	void TestSameSizeBackupTamperIsRejected()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory =
			temporary.Path / "TamperedBackup";
		const std::filesystem::path projectPath =
			projectDirectory / "Project.tcproj";
		const std::string originalProject =
			LegacyProjectDocument("TamperedBackup");
		const std::string partialProject =
			"SchemaVersion: 4\nProject:\n  Name: TamperedBackup\n";
		const std::filesystem::path settingsDirectory =
			projectDirectory / "ProjectSettings";
		const std::filesystem::path backupDirectory =
			settingsDirectory / "MigrationBackups" / "tamper-test";
		std::string tamperedBackup = originalProject;
		Require(!tamperedBackup.empty(), "tamper fixture was unexpectedly empty");
		tamperedBackup[0] = tamperedBackup[0] == 'X' ? 'Y' : 'X';
		Require(tamperedBackup.size() == originalProject.size(),
			"tamper fixture changed backup size");

		WriteText(projectPath, partialProject);
		WriteText(backupDirectory / "original-0.bin", tamperedBackup);
		const std::string journal = PreparedMigrationJournal(
			"tamper-test", originalProject, true, true);
		WriteText(backupDirectory / "journal.json", journal);
		WriteText(settingsDirectory / ".migration-journal.json", journal);

		TomCat::ProjectMigrationRecoveryPreview recoveryPreview;
		std::string recoveryError;
		Require(!TomCat::Project::PreviewInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"recovery preview accepted a same-size modified backup");
		Require(recoveryError.find("SHA-256") != std::string::npos,
			"same-size backup tamper did not report an integrity failure");
		Require(ReadText(projectPath) == partialProject,
			"backup preflight failure modified a migration target");
		Require(std::filesystem::is_regular_file(
			settingsDirectory / ".migration-journal.json") &&
			ReadText(backupDirectory / "original-0.bin") == tamperedBackup,
			"failed integrity recovery discarded its journal evidence");
	}

#ifdef TC_PLATFORM_WINDOWS
	void TestMigrationRejectsReparseAncestorReplacement()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory =
			temporary.Path / "ReparseMigration";
		const std::filesystem::path projectPath =
			projectDirectory / "Project.tcproj";
		const std::string originalProject =
			LegacyProjectDocument("ReparseMigration");
		WriteText(projectPath, originalProject);

		TomCat::ProjectMigrationPreview preview;
		std::string previewError;
		Require(TomCat::Project::PreviewMigration(
			projectPath, preview, previewError),
			"could not create reparse replacement preview: " + previewError);
		const std::filesystem::path outsideSettings =
			temporary.Path / "OutsideSettings";
		std::filesystem::create_directories(outsideSettings);
		const bool junctionCreated = CreateDirectoryJunction(
			projectDirectory / "ProjectSettings", outsideSettings);
		const DWORD junctionError = junctionCreated ? ERROR_SUCCESS : GetLastError();
		Require(junctionCreated,
			"could not create ProjectSettings junction fixture (Win32 error " +
				std::to_string(junctionError) + ")");

		Require(TomCat::Project::LoadWithMigration(projectPath, preview) == nullptr,
			"migration followed ProjectSettings after it became a junction");
		Require(ReadText(projectPath) == originalProject,
			"reparse ancestor rejection modified the project file");
		Require(std::filesystem::is_empty(outsideSettings),
			"migration wrote through a ProjectSettings junction");
	}

	void TestRecoveryRejectsReparseBackupAncestor()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory =
			temporary.Path / "ReparseRecovery";
		const std::filesystem::path projectPath =
			projectDirectory / "Project.tcproj";
		const std::string originalProject =
			LegacyProjectDocument("ReparseRecovery");
		const std::string partialProject =
			"SchemaVersion: 4\nProject:\n  Name: ReparseRecovery\n";
		const std::filesystem::path settingsDirectory =
			projectDirectory / "ProjectSettings";
		const std::filesystem::path outsideBackups =
			temporary.Path / "OutsideBackups";
		const std::filesystem::path outsideTransaction =
			outsideBackups / "reparse-recovery";
		const std::string journal = PreparedMigrationJournal(
			"reparse-recovery", originalProject, true, true);

		WriteText(projectPath, partialProject);
		WriteText(outsideTransaction / "original-0.bin", originalProject);
		WriteText(outsideTransaction / "journal.json", journal);
		WriteText(settingsDirectory / ".migration-journal.json", journal);
		Require(CreateDirectoryJunction(
			settingsDirectory / "MigrationBackups", outsideBackups),
			"could not create MigrationBackups junction fixture");

		TomCat::ProjectMigrationRecoveryPreview recoveryPreview;
		std::string recoveryError;
		Require(!TomCat::Project::PreviewInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"recovery preview followed a reparse-point backup ancestor");
		Require(recoveryError.find("reparse point") != std::string::npos,
			"reparse backup rejection did not report the unsafe ancestor");
		Require(ReadText(projectPath) == partialProject &&
			ReadText(outsideTransaction / "original-0.bin") == originalProject,
			"reparse backup rejection modified a target or outside backup");
	}

	void TestMigrationPinsMutationParentsAgainstReplacement()
	{
		auto runCase = [](std::string_view caseName, bool replaceBackupRoot)
		{
			TemporaryDirectory temporary;
			const std::filesystem::path projectDirectory =
				temporary.Path / std::string(caseName);
			const std::filesystem::path projectPath =
				projectDirectory / "Project.tcproj";
			WriteText(projectPath, LegacyProjectDocument(caseName));
			TomCat::ProjectMigrationPreview preview;
			std::string previewError;
			Require(TomCat::Project::PreviewMigration(
				projectPath, preview, previewError),
				"could not preview pinned-parent migration: " + previewError);
			const std::filesystem::path outside = temporary.Path / "Outside";
			std::filesystem::create_directories(outside);
			const std::filesystem::path protectedDirectory = replaceBackupRoot ?
				projectDirectory / "ProjectSettings" / "MigrationBackups" :
				projectDirectory / "ProjectSettings";
			const std::filesystem::path parked = replaceBackupRoot ?
				projectDirectory / "MigrationBackups-parked" :
				projectDirectory / "ProjectSettings-parked";

			bool attempted = false;
			bool replacementSucceeded = false;
			bool junctionCreated = false;
			DWORD replacementError = ERROR_SUCCESS;
			TomCat::Ref<TomCat::Project> migrated;
			{
				ScopedDirectoryMutationTestHook hook(
					[&](const std::filesystem::path& pinnedDirectory)
					{
						if (attempted ||
							!SameWindowsPath(pinnedDirectory, protectedDirectory))
							return;
						attempted = true;
						if (MoveFileExW(protectedDirectory.c_str(), parked.c_str(),
							MOVEFILE_WRITE_THROUGH))
						{
							replacementSucceeded = true;
							junctionCreated = CreateDirectoryJunction(
								protectedDirectory, outside);
						}
						else
							replacementError = GetLastError();
					});
				migrated = TomCat::Project::LoadWithMigration(projectPath, preview);
			}

			Require(attempted,
				"migration did not reach the protected directory mutation hook");
			Require(!replacementSucceeded,
				"a pinned migration parent was renamed and replaced during mutation" +
				std::string(junctionCreated ? " (junction installed)" : ""));
			Require(replacementError == ERROR_SHARING_VIOLATION ||
				replacementError == ERROR_ACCESS_DENIED ||
				replacementError == ERROR_LOCK_VIOLATION,
				"pinned parent replacement was rejected for an unexpected reason: " +
					std::to_string(replacementError));
			std::cout << "[migration-race] " << caseName
				<< " parent rename blocked with Win32 error "
				<< replacementError << '\n';
			Require(migrated != nullptr,
				"migration failed after the pinned-parent replacement was blocked");
			Require(std::filesystem::is_empty(outside),
				"migration wrote through the attempted parent replacement");
		};

		runCase("PinnedProjectSettings", false);
		runCase("PinnedMigrationBackups", true);
	}

	void TestAtomicWriteRejectsDestinationReparseRace()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path parent = temporary.Path / "AtomicParent";
		const std::filesystem::path outside = temporary.Path / "AtomicOutside";
		std::filesystem::create_directories(parent);
		std::filesystem::create_directories(outside);
		const std::filesystem::path destination = parent / "target.txt";

		bool injected = false;
		std::string writeError;
		{
			ScopedDirectoryMutationTestHook hook(
				[&](const std::filesystem::path& pinnedDirectory)
				{
					if (injected || !SameWindowsPath(pinnedDirectory, parent))
						return;
					injected = true;
					Require(CreateDirectoryJunction(destination, outside),
						"could not inject destination reparse point");
				});
			Require(!TomCat::FileSystem::WriteFileAtomically(
				destination, "must not reach outside", writeError),
				"atomic write replaced a destination reparse point");
		}
		Require(injected,
			"atomic-write destination reparse race hook did not run");
		Require(writeError.find("reparse point") != std::string::npos,
			"atomic-write destination reparse rejection was not reported");
		Require(std::filesystem::is_empty(outside),
			"atomic write followed the injected destination reparse point");
	}
#endif

	void TestMigrationJournalCannotEscapeProject()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path projectDirectory = temporary.Path / "JournalGuard";
		const std::filesystem::path projectPath = projectDirectory / "Project.tcproj";
		const std::filesystem::path outside = temporary.Path / "outside.txt";
		WriteText(projectPath, LegacyProjectDocument("JournalGuard"));
		WriteText(outside, "must survive");
		WriteText(projectDirectory / "ProjectSettings" / ".migration-journal.json",
			"{\n"
			"  \"schemaVersion\": 2,\n"
			"  \"transactionId\": \"escape-test\",\n"
			"  \"state\": \"prepared\",\n"
			"  \"sourceSchemaVersion\": 3,\n"
			"  \"targetSchemaVersion\": 4,\n"
			"  \"settingsDirectoryExisted\": true,\n"
			"  \"backupRootExisted\": true,\n"
			"  \"entries\": [\n"
			"    { \"target\": \"../outside.txt\", \"existed\": false, "
			"\"originalSize\": 0, \"originalSha256\": \"\", "
			"\"backup\": \"original-0.bin\" }\n"
			"  ]\n"
			"}\n");

		TomCat::ProjectMigrationRecoveryPreview recoveryPreview;
		std::string recoveryError;
		Require(!TomCat::Project::PreviewInterruptedMigration(
			projectPath, recoveryPreview, recoveryError),
			"migration recovery preview accepted a journal path escape");
		Require(ReadText(outside) == "must survive",
			"malicious migration journal modified a path outside the project");
	}

	void TestEditorVersionResolutionHasNoFallback()
	{
		TemporaryDirectory temporary;
		const std::filesystem::path renamedEditor =
			temporary.Path / "Editors" / "Custom Build" / "Studio Editor.exe";
		const std::vector<TomCat::EditorInstallation> installations = {
			{ "2026.2", "TomCat-2026.2", renamedEditor }
		};
		Require(!TomCat::ProjectManager::ResolveEditorExecutable(installations, "2026.1"),
			"missing requested Editor unexpectedly resolved through an adjacent fallback");
		const auto resolved = TomCat::ProjectManager::ResolveEditorExecutable(
			installations, "2026.2");
		Require(resolved && *resolved == renamedEditor,
			"installed Editor did not resolve to its discovered executable path");
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

	void TestGameDataPaths()
	{
		const std::filesystem::path localRoot = "C:/Users/Test/AppData/Local";
		const auto paths = TomCat::ApplicationPaths::ResolveGameDataPaths(
			localRoot, "TomCat Studio", "Moon Rabbit", "State/Saves",
			"State/Logs", "Diagnostics/Crashes");
		Require(paths.has_value(), "valid per-game data paths were rejected");
		const std::filesystem::path expectedRoot =
			(localRoot / "TomCat" / "Games" / "TomCat Studio" / "Moon Rabbit")
				.lexically_normal();
		Require(paths->Root == expectedRoot
			&& paths->Saves == expectedRoot / "State/Saves"
			&& paths->Logs == expectedRoot / "State/Logs"
			&& paths->Crashes == expectedRoot / "Diagnostics/Crashes",
			"per-game data paths did not preserve the configured directories");
		Require(!TomCat::ApplicationPaths::ResolveGameDataPaths(
			localRoot, "../Studio", "Game", "Saves", "Logs", "Crashes"),
			"company path traversal was accepted");
		Require(!TomCat::ApplicationPaths::ResolveGameDataPaths(
			localRoot, "Studio", "CON", "Saves", "Logs", "Crashes"),
			"a reserved Windows product directory was accepted");
		Require(!TomCat::ApplicationPaths::ResolveGameDataPaths(
			localRoot, "Studio", "Game", "../Saves", "Logs", "Crashes"),
			"save directory traversal was accepted");

		TomCat::ApplicationPaths::SetRuntimeGameDataPaths(*paths);
		Require(TomCat::ApplicationPaths::GetRuntimeGameDataPaths() == paths
			&& TomCat::ApplicationPaths::GetRuntimeSaveDirectory() == paths->Saves
			&& TomCat::ApplicationPaths::GetRuntimeLogDirectory() == paths->Logs
			&& TomCat::ApplicationPaths::GetRuntimeCrashDirectory() == paths->Crashes,
			"Player runtime paths were not published to core consumers");
		TomCat::ApplicationPaths::ClearRuntimeGameDataPaths();
		Require(!TomCat::ApplicationPaths::GetRuntimeGameDataPaths(),
			"Player runtime paths survived explicit shutdown");
	}

	void TestCrashReport(const std::filesystem::path& root)
	{
		const std::filesystem::path crashDirectory = root / "Game" / "Crashes";
		Require(TomCat::CrashReporter::Configure(crashDirectory,
			"Regression Studio", "Crash Probe", "7.4.2"),
			"valid crash directory could not be configured");
		const std::filesystem::path report =
			TomCat::CrashReporter::WriteReport("caught regression exception");
		Require(std::filesystem::is_regular_file(report),
			"caught exception did not create a crash report");
#ifdef TC_PLATFORM_WINDOWS
		std::filesystem::path dump = report;
		dump.replace_extension(".dmp");
		std::error_code dumpError;
		Require(std::filesystem::is_regular_file(dump, dumpError) && !dumpError
			&& std::filesystem::file_size(dump, dumpError) > 0 && !dumpError,
			"caught exception did not create a usable Windows minidump");
#endif
		std::ifstream input(report, std::ios::binary);
		std::ostringstream contents;
		contents << input.rdbuf();
		const std::string text = contents.str();
		Require(text.find("Company: Regression Studio") != std::string::npos
			&& text.find("Product: Crash Probe") != std::string::npos
			&& text.find("Version: 7.4.2") != std::string::npos
			&& text.find("Reason: caught regression exception") != std::string::npos,
			"crash report omitted product identity or exception reason");
	}

	void TestRotatingLog(const std::filesystem::path& root)
	{
		const std::filesystem::path logFile = root / "Game" / "Logs" / "Player.log";
		Require(TomCat::Log::InitFile(logFile, 512, 2),
			"explicit per-game rotating log could not be initialized");
		const std::string payload(240, 'R');
		for (int index = 0; index < 4; ++index)
			TC_Core_Info("rotation probe {0}: {1}", index, payload);
		TomCat::Log::Shutdown();
		Require(std::filesystem::is_regular_file(logFile)
			&& std::filesystem::is_regular_file(
				logFile.parent_path() / "Player.1.log"),
			"bounded file logger did not rotate the per-game log");
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
		TestGameDataPaths();
		TestUnifiedVersionSource();
		TestEditorVersionResolutionHasNoFallback();
		TestInspectProjectNeverWrites();
		TestTransactionalProjectMigration();
		TestMigrationPreviewDetectsSameSizeReplacement();
#ifdef TC_PLATFORM_WINDOWS
		TestMigrationFailureRollsBackEveryFile();
#endif
		TestInterruptedMigrationRecovery();
		TestAbandonRecoveryKeepsCurrentFilesAndArchive();
		TestSameSizeBackupTamperIsRejected();
#ifdef TC_PLATFORM_WINDOWS
		TestMigrationRejectsReparseAncestorReplacement();
		TestRecoveryRejectsReparseBackupAncestor();
		TestMigrationPinsMutationParentsAgainstReplacement();
		TestAtomicWriteRejectsDestinationReparseRace();
#endif
		TestMigrationJournalCannotEscapeProject();
		TestCrashReport(logging.Path);
		TestRotatingLog(logging.Path);
		std::cout << "PASS P0 safety: migration SHA-256 backup/recovery, "
			"explicit plan authorization, journal/rollback/reparse and "
			"parent-replacement containment, "
			"read-only inspection, exact Editor version, per-game data/log/crash paths, "
			"and console fallback\n";
		TomCat::Log::Shutdown();
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL P0 safety regression: " << exception.what() << '\n';
		return 1;
	}
}
