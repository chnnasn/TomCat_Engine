#pragma once

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Paths used by the applications for Dear ImGui settings.  The packaged
// default is deliberately kept under Packages so Enigma Virtual Box can serve
// it as a read-only virtual file.  All writable settings are resolved outside
// that tree.
namespace TomCat::ImGuiSettings
{
	inline std::filesystem::path CurrentPathNoThrow()
	{
		std::error_code error;
		const auto path = std::filesystem::current_path(error);
		return error ? std::filesystem::path{} : path;
	}

	inline std::filesystem::path LocalAppDataPath()
	{
	#ifdef _WIN32
		const wchar_t* wideValue = _wgetenv(L"LOCALAPPDATA");
		if (wideValue && *wideValue)
			return std::filesystem::path(wideValue);
	#else
		const char* value = std::getenv("LOCALAPPDATA");
		if (value && *value)
			return std::filesystem::path(value);
	#endif
		return {};
	}

	inline std::filesystem::path ExecutableDirectoryNoThrow()
	{
	#ifdef _WIN32
		// `_wpgmptr` is maintained by the MSVC CRT and does not require pulling
		// the very large Windows API header into every translation unit that uses
		// this header.  It is available before `main()` and remains stable for the
		// lifetime of the process.
		#if defined(_MSC_VER)
			if (_wpgmptr && *_wpgmptr)
				return std::filesystem::path(_wpgmptr).parent_path();
		#endif
	#endif
		return {};
	}

	inline std::filesystem::path ResolveDefaultIni(const std::filesystem::path& applicationSource)
	{
		const std::filesystem::path relative =
			std::filesystem::path("Packages") / "Defaults" / "imgui.ini";
		std::vector<std::filesystem::path> candidates;
		auto addCandidate = [&candidates](const std::filesystem::path& candidate)
		{
			if (candidate.empty())
				return;
			const auto normalized = candidate.lexically_normal();
			for (const auto& existing : candidates)
				if (existing == normalized)
					return;
			candidates.push_back(normalized);
		};

		auto addSearchRoots = [&](const std::filesystem::path& root)
		{
			if (root.empty())
				return;
			auto base = root;
			for (int depth = 0; depth < 7; ++depth)
			{
				// Prefer the known application source tree over an arbitrary
				// Packages directory in the caller's project. This prevents a
				// project's own `Packages/Defaults/imgui.ini` from shadowing the
				// Editor/Hub package when the executable is launched from there.
				addCandidate(base / applicationSource / relative);
				addCandidate(base / relative);
				const auto parent = base.parent_path();
				if (parent.empty() || parent == base)
					break;
				base = parent;
			}
		};

		// Resolve beside the executable first (important for shortcuts whose
		// working directory is unrelated to the installation), then fall back to
		// the current directory/repository layout used by development builds.
		addSearchRoots(ExecutableDirectoryNoThrow());
		addSearchRoots(CurrentPathNoThrow());

		for (const auto& candidate : candidates)
		{
			std::error_code error;
			if (std::filesystem::is_regular_file(candidate, error) && !error)
				return candidate;
		}

		// Do not turn a missing virtual file into a disk file.  EVB intercepts the
		// relative path even when filesystem metadata APIs cannot see it.
		return relative;
	}

	inline std::filesystem::path GetEditorDefaultIniPath()
	{
		return ResolveDefaultIni(std::filesystem::path("Editor") / "TomCatInut");
	}

	inline std::filesystem::path GetHubDefaultIniPath()
	{
		return ResolveDefaultIni(std::filesystem::path("Builder") / "Manager");
	}

	inline std::filesystem::path GetEditorUserIniPath(const std::filesystem::path& projectFile = {})
	{
		if (!projectFile.empty())
		{
			std::error_code error;
			const auto absoluteProjectFile = std::filesystem::absolute(projectFile, error);
			const auto resolvedProjectFile = error ? projectFile : absoluteProjectFile;
			auto projectDirectory = resolvedProjectFile.parent_path();
			if (projectDirectory.empty())
				projectDirectory = CurrentPathNoThrow();
			return projectDirectory / "UserSettings" / "imgui.ini";
		}

		// A project-less Editor still needs a safe place for temporary layout
		// changes.  It must never fall back to the executable/project directory.
		const auto localAppData = LocalAppDataPath();
		if (!localAppData.empty())
			return localAppData / "TomCat" / "Editor" / "imgui.ini";

		const auto current = CurrentPathNoThrow();
		return (current.empty() ? std::filesystem::path("UserSettings") : current / "UserSettings") / "imgui.ini";
	}

	inline std::filesystem::path GetHubUserIniPath()
	{
		const auto localAppData = LocalAppDataPath();
		if (!localAppData.empty())
			return localAppData / "TomCat" / "Hub" / "imgui.ini";

		const auto current = CurrentPathNoThrow();
		const auto userSettings = current.empty()
			? std::filesystem::path("UserSettings")
			: current / "UserSettings";
		return userSettings / "Hub" / "imgui.ini";
	}

	inline bool EnsureParentDirectory(const std::filesystem::path& filePath)
	{
		const auto parent = filePath.parent_path();
		if (parent.empty())
			return true;

		std::error_code error;
		std::filesystem::create_directories(parent, error);
		return !error;
	}

	inline bool ReadTextFile(const std::filesystem::path& filePath, std::string& contents)
	{
		contents.clear();
		std::ifstream input(filePath, std::ios::binary);
		if (!input)
			return false;

		contents.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		return input.good() || input.eof();
	}

	inline bool WriteTextFile(const std::filesystem::path& filePath, const char* contents, size_t size)
	{
		if (size > 0 && contents == nullptr)
			return false;
		if (!EnsureParentDirectory(filePath))
			return false;

		std::ofstream output(filePath, std::ios::binary | std::ios::trunc);
		if (!output)
			return false;
		if (size > 0)
			output.write(contents, static_cast<std::streamsize>(size));
		output.flush();
		return output.good();
	}

	inline bool WriteTextFileAtomically(const std::filesystem::path& filePath,
		const char* contents, size_t size)
	{
		if (size > 0 && contents == nullptr)
			return false;
		if (!EnsureParentDirectory(filePath))
			return false;

		// Settings are written synchronously, nevertheless use a process/time
		// suffix so two editor instances cannot trample each other's temporary
		// file before the final rename.
		std::ostringstream suffix;
		suffix << ".tmp-" << std::chrono::steady_clock::now().time_since_epoch().count();
		std::filesystem::path temporary = filePath;
		temporary += suffix.str();
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output)
				return false;
			if (size > 0)
				output.write(contents, static_cast<std::streamsize>(size));
			output.flush();
			if (!output.good())
			{
				output.close();
				std::error_code removeError;
				std::filesystem::remove(temporary, removeError);
				return false;
			}
		}

	#ifdef _WIN32
		// MoveFileExW replaces the destination in one operation, unlike a
		// remove-then-rename sequence which can leave a missing settings file.
		if (!MoveFileExW(temporary.c_str(), filePath.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::error_code removeError;
			std::filesystem::remove(temporary, removeError);
			return false;
		}
	#else
		std::error_code renameError;
		std::filesystem::rename(temporary, filePath, renameError);
		if (renameError)
		{
			std::error_code removeError;
			std::filesystem::remove(temporary, removeError);
			return false;
		}
	#endif
		return true;
	}

	inline bool WriteTextFileAtomically(const std::filesystem::path& filePath,
		const std::string& contents)
	{
		return WriteTextFileAtomically(filePath, contents.data(), contents.size());
	}

	inline bool IsIniSectionPrefix(std::string_view line, std::string_view prefix)
	{
		if (!line.starts_with(prefix))
			return false;
		// A prefix such as `[Docking]` also matches `[Docking][Data]`, but
		// must not match malformed names such as `[Docking]Legacy`.
		return line.size() == prefix.size() || line[prefix.size()] == '[';
	}

	inline bool WriteTextFile(const std::filesystem::path& filePath, const std::string& contents)
	{
		return WriteTextFile(filePath, contents.data(), contents.size());
	}

	inline bool HasIniSectionPrefix(std::string_view contents, std::string_view prefix)
	{
		size_t lineStart = 0;
		while (lineStart < contents.size())
		{
			const size_t lineEnd = contents.find_first_of("\r\n", lineStart);
			const size_t length = (lineEnd == std::string_view::npos ? contents.size() : lineEnd) - lineStart;
			if (IsIniSectionPrefix(contents.substr(lineStart, length), prefix))
				return true;
			if (lineEnd == std::string_view::npos)
				break;
			lineStart = lineEnd + 1;
			while (lineStart < contents.size() && (contents[lineStart] == '\r' || contents[lineStart] == '\n'))
				++lineStart;
		}
		return false;
	}

	inline std::string ExtractIniSections(std::string_view contents, std::string_view prefix)
	{
		std::string result;
		size_t lineStart = 0;
		while (lineStart < contents.size())
		{
			const size_t lineEnd = contents.find_first_of("\r\n", lineStart);
			const size_t headerEnd = lineEnd == std::string_view::npos ? contents.size() : lineEnd;
			if (!IsIniSectionPrefix(contents.substr(lineStart, headerEnd - lineStart), prefix))
			{
				if (lineEnd == std::string_view::npos)
					break;
				lineStart = lineEnd + 1;
				continue;
			}

			size_t sectionEnd = contents.size();
			size_t scan = lineEnd == std::string_view::npos ? contents.size() : lineEnd + 1;
			while (scan < contents.size())
			{
				while (scan < contents.size() && (contents[scan] == '\r' || contents[scan] == '\n'))
					++scan;
				if (scan >= contents.size())
					break;
				if (contents[scan] == '[')
				{
					sectionEnd = scan;
					break;
				}
				const size_t nextLine = contents.find_first_of("\r\n", scan);
				if (nextLine == std::string_view::npos)
					break;
				scan = nextLine + 1;
			}

			if (!result.empty() && result.back() != '\n')
				result.push_back('\n');
			result.append(contents.substr(lineStart, sectionEnd - lineStart));
			if (!result.empty() && result.back() != '\n')
				result.push_back('\n');
			lineStart = sectionEnd;
		}
		return result;
	}

	inline std::string ReplaceIniSections(std::string contents, std::string_view prefix,
		std::string_view replacement)
	{
		size_t lineStart = 0;
		while (lineStart < contents.size())
		{
			const size_t lineEnd = contents.find_first_of("\r\n", lineStart);
			const size_t headerEnd = lineEnd == std::string::npos ? contents.size() : lineEnd;
			if (IsIniSectionPrefix(std::string_view(contents).substr(lineStart, headerEnd - lineStart), prefix))
			{
				size_t sectionEnd = contents.size();
				size_t scan = lineEnd == std::string::npos ? contents.size() : lineEnd + 1;
				while (scan < contents.size())
				{
					while (scan < contents.size() && (contents[scan] == '\r' || contents[scan] == '\n'))
						++scan;
					if (scan >= contents.size())
						break;
					if (contents[scan] == '[')
					{
						sectionEnd = scan;
						break;
					}
					const size_t nextLine = contents.find_first_of("\r\n", scan);
					if (nextLine == std::string::npos)
						break;
					scan = nextLine + 1;
				}
				std::string replacementText(replacement);
				if (!replacementText.empty() && replacementText.back() != '\n')
					replacementText.push_back('\n');
				contents.replace(lineStart, sectionEnd - lineStart, replacementText);
				return contents;
			}
			if (lineEnd == std::string::npos)
				break;
			lineStart = lineEnd + 1;
		}

		if (!contents.empty() && contents.back() != '\n')
			contents.push_back('\n');
		contents.append(replacement.data(), replacement.size());
		return contents;
	}

	inline bool HasIniDockingData(std::string_view contents)
	{
		const std::string docking = ExtractIniSections(contents, "[Docking]");
		if (docking.empty() || !HasIniSectionPrefix(docking, "[Docking][Data]"))
			return false;
		// An empty Data block is technically parseable, but treating it as an
		// invalid override keeps a packaged dock from being erased by a truncated
		// user file after a crash.
		return docking.find("DockNode") != std::string::npos ||
			docking.find("DockSpace") != std::string::npos;
	}

	inline bool MigrateLegacyEditorIni(const std::filesystem::path& projectFile,
		const std::filesystem::path& userPath)
	{
		std::string userContents;
		if (ReadTextFile(userPath, userContents) && !userContents.empty())
			return true;

		std::vector<std::filesystem::path> candidates;
		size_t projectCandidateCount = 0;
		if (!projectFile.empty())
		{
			std::error_code error;
			const auto absoluteProjectFile = std::filesystem::absolute(projectFile, error);
			const auto resolvedProjectFile = error ? projectFile : absoluteProjectFile;
			if (!resolvedProjectFile.parent_path().empty())
			{
				candidates.push_back(resolvedProjectFile.parent_path() / "imgui.ini");
				projectCandidateCount = 1;
			}
		}
		const auto current = CurrentPathNoThrow();
		if (!current.empty())
			candidates.push_back(current / "imgui.ini");

		for (size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex)
		{
			const auto& candidate = candidates[candidateIndex];
			std::error_code error;
			if (std::filesystem::equivalent(candidate, userPath, error) && !error)
				continue;

			std::string legacyContents;
			if (!ReadTextFile(candidate, legacyContents) || legacyContents.empty())
				continue;

			// A cwd-level imgui.ini was shared by old Editor and Hub builds. Only
			// treat it as an Editor migration source when it carries one of the
			// Editor-owned sections; this prevents a Hub-only file from becoming a
			// project's layout. The project-local legacy file remains authoritative.
			const bool isProjectLocal = candidateIndex < projectCandidateCount;
			if (!isProjectLocal &&
				legacyContents.find("[SceneToolbars]") == std::string::npos &&
				legacyContents.find("[ContentBrowser]") == std::string::npos)
				continue;

			// Do not copy the same global legacy file into every project opened in
			// this or a later process. A marker is migration metadata only; layout
			// writes still go exclusively to the project's UserSettings file.
			if (!isProjectLocal)
			{
				const auto localAppData = LocalAppDataPath();
				const auto marker = localAppData.empty()
					? std::filesystem::path{}
					: localAppData / "TomCat" / "Editor" / "legacy-imgui.migrated";
				std::error_code markerError;
				if (!marker.empty() && std::filesystem::is_regular_file(marker, markerError) && !markerError)
					continue;
				static bool processGlobalMigrationDone = false;
				if (processGlobalMigrationDone)
					continue;
				processGlobalMigrationDone = true;
				if (!WriteTextFileAtomically(userPath, legacyContents))
					return false;
				if (!marker.empty())
					WriteTextFile(marker, candidate.string());
				return true;
			}
			return WriteTextFileAtomically(userPath, legacyContents);
		}
		return true;
	}

	inline bool HasImGuiLayoutSections(const std::filesystem::path& filePath)
	{
		std::string contents;
		if (!ReadTextFile(filePath, contents))
			return false;

		// Dear ImGui's built-in handlers are the sections that affect dock
		// state. A file containing only application-owned sections (for
		// example [HubConfig]) must not clear the packaged dock defaults.
		return HasIniSectionPrefix(contents, "[Window]") ||
			HasIniSectionPrefix(contents, "[Docking]");
	}
}
