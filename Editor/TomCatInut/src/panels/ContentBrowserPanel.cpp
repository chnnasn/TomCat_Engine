#include "tcpch.h"

#include "ContentBrowserPanel.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#endif

#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/ImGui/ImGuiCallback.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "../EditorDragDrop.h"

namespace TomCat {

	extern const std::filesystem::path g_AssetPath = "Assets";

	namespace {

		const std::unordered_set<std::string> s_ImageExtensions = {
			".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr", ".pic"
		};
		constexpr const char* kAssetDirectoryPayloadID = "TOMCAT_ASSET_DIRECTORY";
		constexpr const char* kStoredAssetsRoot = "@assets";
		constexpr const char* kStoredPackagesRoot = "@packages";

		ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
		{
			return texture
				? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
				: nullptr;
		}

		void DrawTreeIcon(const Ref<Texture2D>& texture, const ImVec2& itemMin,
			const ImVec2& itemMax, ImU32 tint = IM_COL32_WHITE)
		{
			if (!texture)
				return;
			const float iconSize = std::min(18.0f, std::max(1.0f, itemMax.y - itemMin.y - 2.0f));
			const float x = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
			const float y = itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f;
			ImGui::GetWindowDrawList()->AddImage(ToImGuiTextureID(texture), ImVec2(x, y),
				ImVec2(x + iconSize, y + iconSize), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
		}

		bool IsReadOnlyPath(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::perms permissions = std::filesystem::status(path, error).permissions();
			if (error || permissions == std::filesystem::perms::unknown)
				return false;
			constexpr std::filesystem::perms writePermissions =
				std::filesystem::perms::owner_write |
				std::filesystem::perms::group_write |
				std::filesystem::perms::others_write;
			return (permissions & writePermissions) == std::filesystem::perms::none;
		}

		std::string ToLower(std::string value)
		{
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}

		std::string TrimCopy(const std::string& value)
		{
			auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
			auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
			return first < last ? std::string(first, last) : std::string{};
		}

		std::string::size_type FindIniSectionHeader(const std::string& ini,
			std::string_view sectionName)
		{
			std::string::size_type position = 0;
			while ((position = ini.find(sectionName, position)) != std::string::npos)
			{
				const bool lineStart = position == 0 || ini[position - 1] == '\n';
				const size_t end = position + sectionName.size();
				const bool lineEnd = end == ini.size() || ini[end] == '\n' || ini[end] == '\r';
				if (lineStart && lineEnd)
					return position;
				position = end;
			}
			return std::string::npos;
		}

		bool IsValidEntryName(const std::string& name)
		{
			if (name.empty() || name == "." || name == "..")
				return false;
			if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
				return false;
			return name.find_first_of("<>:\"|?*") == std::string::npos;
		}

		std::filesystem::path CanonicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			std::filesystem::path result = std::filesystem::weakly_canonical(path, error);
			if (!error)
				return result.lexically_normal();
			error.clear();
			result = std::filesystem::absolute(path, error);
			return (error ? path : result).lexically_normal();
		}

		std::filesystem::path LexicalPath(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		bool IsWithinRoot(const std::filesystem::path& root, const std::filesystem::path& candidate, bool allowRoot = true)
		{
			const std::filesystem::path normalizedRoot = CanonicalPath(root);
			const std::filesystem::path normalizedCandidate = CanonicalPath(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			if (normalizedRoot == normalizedCandidate)
				return allowRoot;
			const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		bool IsWithinLexicalRoot(const std::filesystem::path& root,
			const std::filesystem::path& candidate, bool allowRoot = true)
		{
			const std::filesystem::path normalizedRoot = LexicalPath(root);
			const std::filesystem::path normalizedCandidate = LexicalPath(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			if (normalizedRoot == normalizedCandidate)
				return allowRoot;
			const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		// Resolve ancestors for containment checks, but deliberately keep the
		// final component lexical. This allows a symlink inside Assets to be
		// renamed or removed without ever applying the operation to its target.
		bool GetManagedMutationPath(const std::filesystem::path& root,
			const std::filesystem::path& candidate, std::filesystem::path& nativePath,
			bool mustExist = true)
		{
			nativePath = LexicalPath(candidate);
			const std::filesystem::path nativeRoot = LexicalPath(root);
			if (nativePath.empty() || nativeRoot.empty() || nativePath == nativeRoot ||
				!IsWithinRoot(root, nativePath.parent_path()))
				return false;

			std::error_code error;
			const std::filesystem::file_status status = std::filesystem::symlink_status(nativePath, error);
			if (error)
				return !mustExist && error == std::errc::no_such_file_or_directory;
			if (!std::filesystem::exists(status))
				return !mustExist;
			if (std::filesystem::is_symlink(status))
				return true;

			// Junctions/reparse directories that resolve outside the asset root are
			// rejected even if symlink_status does not classify them as symlinks.
			return IsWithinRoot(root, nativePath, false);
		}

		bool IsManagedEntry(const std::filesystem::path& root, const std::filesystem::path& candidate)
		{
			std::filesystem::path nativePath;
			return GetManagedMutationPath(root, candidate, nativePath);
		}

		std::filesystem::path RemapPath(const std::filesystem::path& value,
			const std::filesystem::path& oldRoot, const std::filesystem::path& newRoot)
		{
			const std::filesystem::path normalizedValue = LexicalPath(value);
			const std::filesystem::path normalizedOldRoot = LexicalPath(oldRoot);
			if (normalizedValue == normalizedOldRoot)
				return newRoot;
			if (!IsWithinLexicalRoot(normalizedOldRoot, normalizedValue, false))
				return value;
			return newRoot / normalizedValue.lexically_relative(normalizedOldRoot);
		}

		std::filesystem::path MakeUniqueFolderPath(const std::filesystem::path& parent)
		{
			for (uint32_t index = 0; index < 10000; ++index)
			{
				std::string name = "New Folder";
				if (index > 0)
					name += " (" + std::to_string(index) + ")";
				const std::filesystem::path candidate = parent / name;
				std::error_code error;
				if (!std::filesystem::exists(candidate, error))
					return candidate;
			}
			return {};
		}

		std::pair<std::filesystem::path, std::string> MakeUniqueCSharpScriptPath(
			const std::filesystem::path& parent)
		{
			for (uint32_t index = 0; index < 10000; ++index)
			{
				const std::string className = index == 0
					? "PlayerController"
					: "PlayerController" + std::to_string(index);
				const std::filesystem::path candidate = parent / UTF8ToPath(className + ".cs");
				std::error_code fileError;
				const bool fileExists = std::filesystem::exists(candidate, fileError);
				fileError.clear();
				const bool metadataExists = std::filesystem::exists(
					AssetRegistry::GetMetadataPath(candidate), fileError);
				if (!fileExists && !metadataExists)
					return { candidate, className };
			}
			return {};
		}

		bool OpenInAssociatedApplication(const std::filesystem::path& path)
		{
#ifdef TC_PLATFORM_WINDOWS
			const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(),
				nullptr, path.parent_path().c_str(), SW_SHOWNORMAL);
			return reinterpret_cast<INT_PTR>(result) > 32;
#else
			(void)path;
			return false;
#endif
		}

		bool ResolveExternalScriptEditor(const std::filesystem::path& requested,
			std::filesystem::path& resolved, std::string& errorMessage)
		{
			resolved.clear();
			if (requested.empty())
			{
				errorMessage = "No external C# editor is configured";
				return false;
			}
			resolved = CanonicalPath(requested);
			if (!resolved.is_absolute() || ToLower(PathToUTF8(resolved.extension())) != ".exe")
			{
				errorMessage = "The external C# editor must be an absolute .exe path";
				return false;
			}
			std::error_code error;
			if (!std::filesystem::is_regular_file(resolved, error) || error)
			{
				errorMessage = "The configured external C# editor does not exist or is not a regular file: "
					+ PathToUTF8(resolved);
				return false;
			}
			return true;
		}

		bool OpenInExternalScriptEditor(const std::filesystem::path& editor,
			const std::filesystem::path& script, std::string& errorMessage)
		{
#ifdef TC_PLATFORM_WINDOWS
			std::error_code error;
			if (!std::filesystem::is_regular_file(script, error) || error)
			{
				errorMessage = "The C# source no longer exists: " + PathToUTF8(script);
				return false;
			}
			const std::wstring scriptArgument = script.native();
			if (scriptArgument.find(L'\"') != std::wstring::npos)
			{
				errorMessage = "The C# source path cannot be represented as a safe command-line argument";
				return false;
			}
			const std::wstring parameters = L"\"" + scriptArgument + L"\"";
			const HINSTANCE result = ShellExecuteW(nullptr, L"open", editor.c_str(),
				parameters.c_str(), script.parent_path().c_str(), SW_SHOWNORMAL);
			const INT_PTR code = reinterpret_cast<INT_PTR>(result);
			if (code <= 32)
			{
				errorMessage = "Windows could not launch the configured C# editor (ShellExecute code "
					+ std::to_string(code) + ")";
				return false;
			}
			return true;
#else
			(void)editor;
			(void)script;
			errorMessage = "External C# editor launching is currently implemented for Windows only";
			return false;
#endif
		}

		std::vector<std::filesystem::directory_entry> ReadDirectory(const std::filesystem::path& directory)
		{
			std::vector<std::filesystem::directory_entry> entries;
			std::error_code error;
			for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
			{
				if (!AssetRegistry::IsMetaFile(it->path()))
					entries.emplace_back(*it);
			}
			if (error)
				TC_Core_Error("Failed to read directory {0}: {1}", PathToUTF8(directory), error.message());
			std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
				std::error_code lhsError, rhsError;
				const bool lhsDirectory = lhs.is_directory(lhsError);
				const bool rhsDirectory = rhs.is_directory(rhsError);
				if (lhsDirectory != rhsDirectory)
					return lhsDirectory;
				return ToLower(PathToUTF8(lhs.path().filename())) < ToLower(PathToUTF8(rhs.path().filename()));
			});
			return entries;
		}

		bool IsRecursiveDirectory(const std::filesystem::directory_entry& entry)
		{
			std::error_code error;
			const bool directory = entry.is_directory(error);
			error.clear();
			return directory && !entry.is_symlink(error);
		}

		std::filesystem::path GetDefaultEditorLayoutPath()
		{
			std::error_code error;
			const std::filesystem::path currentDirectory = std::filesystem::current_path(error);
			return error ? std::filesystem::path("imgui.ini") : currentDirectory / "imgui.ini";
		}

		std::filesystem::path GetEditorLayoutPath(const Ref<Project>& project)
		{
			if (project)
			{
				if (project->GetProjectPath().empty())
				{
					TC_Core_Error("Cannot resolve a Content Browser layout for a project with an empty path");
					return {};
				}
				return project->GetProjectPath().parent_path() / "UserSettings" / "imgui.ini";
			}

			const std::optional<std::filesystem::path> settingsRoot = GetTomCatSettingsRoot();
			return settingsRoot ? *settingsRoot / "editor-layout.ini" : std::filesystem::path{};
		}

		bool EnsureSettingsDirectory(const std::filesystem::path& settingsPath)
		{
			if (settingsPath.empty())
				return false;
			const std::filesystem::path directory = settingsPath.parent_path();
			if (directory.empty())
				return true;

			std::error_code error;
			std::filesystem::create_directories(directory, error);
			if (!error)
				return true;

			TC_Core_Error("Failed to create Content Browser settings directory '{0}': {1}",
				PathToUTF8(directory), error.message());
			return false;
		}

	}

	ContentBrowserPanel::ContentBrowserPanel()
		: m_LayoutMode(TwoColumn)
	{
		RegisterWindowMoreOptionsCallback("Project", [this](ImVec2 popupAnchor)
		{
			m_LayoutOptionsX = popupAnchor.x;
			m_LayoutOptionsY = popupAnchor.y;
			m_OpenLayoutOptions = true;
		});
		SetProject(ProjectManager::Get().GetActiveProject());
	}

	ContentBrowserPanel::~ContentBrowserPanel()
	{
		RemoveWindowMoreOptionsCallback("Project");
	}

	std::filesystem::path ContentBrowserPanel::GetAssetRoot() const
	{
		return m_Project ? CanonicalPath(m_Project->GetAssetPath()) : CanonicalPath(g_AssetPath);
	}

	std::filesystem::path ContentBrowserPanel::GetPackagesRoot() const
	{
		// EntryPoint places packaged builds in the executable directory before the
		// editor starts, so this resolves the same external Packages tree in both
		// development and distribution builds.
		return CanonicalPath("Packages");
	}

	std::filesystem::path ContentBrowserPanel::GetRootForPath(const std::filesystem::path& path) const
	{
		const std::filesystem::path packagesRoot = GetPackagesRoot();
		if (IsWithinRoot(packagesRoot, path))
			return packagesRoot;

		const std::filesystem::path assetRoot = GetAssetRoot();
		return IsWithinRoot(assetRoot, path) ? assetRoot : std::filesystem::path{};
	}

	bool ContentBrowserPanel::IsWritablePath(const std::filesystem::path& path) const
	{
		const std::filesystem::path root = GetRootForPath(path);
		return !root.empty() && LexicalPath(root) == LexicalPath(GetAssetRoot());
	}

	void ContentBrowserPanel::SetActiveScenePath(const std::filesystem::path& path)
	{
		m_ActiveScenePath = path.empty() ? std::filesystem::path{} : LexicalPath(path);
	}

	std::filesystem::path ContentBrowserPanel::GetWritableCreationDirectory() const
	{
		if (!m_Project || !IsWritablePath(m_CurrentDirectory))
			return {};
		std::error_code error;
		const std::filesystem::path directory = CanonicalPath(m_CurrentDirectory);
		return std::filesystem::is_directory(directory, error) && !error
			? directory : std::filesystem::path{};
	}

	void ContentBrowserPanel::RevealAsset(const std::filesystem::path& path)
	{
		const std::filesystem::path asset = CanonicalPath(path);
		std::error_code error;
		if (!m_Project || !IsWritablePath(asset)
			|| !std::filesystem::is_regular_file(asset, error) || error)
			return;
		m_CurrentDirectory = asset.parent_path();
		m_SelectedPath = asset;
		m_UserSelectedDirectory = true;
		m_ExpandedNodes.insert(PathToUTF8(m_CurrentDirectory));
		m_PendingOpenDirectories.insert(PathToUTF8(m_CurrentDirectory));
	}

	void ContentBrowserPanel::SetProject(Ref<Project> project)
	{
		m_Project = std::move(project);
		if (m_Project)
		{
			if (!AssetManager::Get().SetProject(m_Project))
				TC_Core_Warn("Asset registry scan completed with errors for '{0}'",
					PathToUTF8(m_Project->GetProjectPath()));
		}
		else
			AssetManager::Get().Shutdown();
		m_ProjectStateWritable = true;
		m_ExternalScriptEditor.clear();
		m_CurrentDirectory.clear();
		m_SelectedPath.clear();
		m_UserSelectedDirectory = false;
		m_ExpandedNodes.clear();
		m_PendingOpenDirectories.clear();
		m_ActiveScenePath.clear();
		m_PendingCreateFolderParent.clear();
		m_PendingCreateScriptParent.clear();
		m_RenamePath.clear();
		m_DeletePath.clear();
		LoadLayoutSetting();
		RestoreProjectState();
	}

	void ContentBrowserPanel::RestoreProjectState()
	{
		const std::filesystem::path assetRoot = GetAssetRoot();
		const std::filesystem::path packagesRoot = GetPackagesRoot();
		m_CurrentDirectory = assetRoot;
		if (!m_Project)
			return;

		EditorProjectState state;
		const EditorProjectStateLoadResult loadResult = m_Project->LoadEditorState(state);
		if (loadResult == EditorProjectStateLoadResult::Failed)
			m_ProjectStateWritable = false;
		else if (loadResult == EditorProjectStateLoadResult::Loaded)
			m_ExternalScriptEditor = state.ExternalScriptEditor;

		auto resolveStoredPath = [&](const std::string& stored) {
			if (stored.empty())
				return assetRoot;
			const std::filesystem::path value = UTF8ToPath(stored);
			if (value.is_absolute())
				return GetRootForPath(value).empty() ? assetRoot : CanonicalPath(value);

			std::string normalized = stored;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			std::filesystem::path candidate;
			if (normalized == kStoredAssetsRoot)
				candidate = assetRoot;
			else if (normalized.rfind(std::string(kStoredAssetsRoot) + "/", 0) == 0)
				candidate = assetRoot / UTF8ToPath(normalized.substr(std::strlen(kStoredAssetsRoot) + 1));
			else if (normalized == kStoredPackagesRoot)
				candidate = packagesRoot;
			else if (normalized.rfind(std::string(kStoredPackagesRoot) + "/", 0) == 0)
				candidate = packagesRoot / UTF8ToPath(normalized.substr(std::strlen(kStoredPackagesRoot) + 1));
			else
				candidate = assetRoot / value; // Backward-compatible legacy Assets-relative state.

			return GetRootForPath(candidate).empty() ? assetRoot : CanonicalPath(candidate);
		};

		const std::filesystem::path restoredDirectory = resolveStoredPath(
			state.ContentBrowserCurrentDirectory);
		std::error_code error;
		if (std::filesystem::is_directory(restoredDirectory, error))
			m_CurrentDirectory = restoredDirectory;

		for (const std::string& stored : state.ContentBrowserExpandedNodes)
		{
			const std::filesystem::path node = resolveStoredPath(stored);
			error.clear();
			if (!GetRootForPath(node).empty() && std::filesystem::is_directory(node, error))
				m_ExpandedNodes.insert(PathToUTF8(node));
		}
	}

	bool ContentBrowserPanel::Serialize()
	{
		// Do not let automatic shutdown persistence overwrite an existing settings
		// file that failed validation or could not be read during this session.
		if (!m_Project || !m_ProjectStateWritable)
			return false;
		auto storePath = [&](const std::filesystem::path& value) {
			const std::filesystem::path root = GetRootForPath(value);
			if (root.empty())
				return std::string(kStoredAssetsRoot);
			const bool packages = LexicalPath(root) == LexicalPath(GetPackagesRoot());
			const std::string prefix = packages ? kStoredPackagesRoot : kStoredAssetsRoot;
			std::error_code error;
			std::filesystem::path relative = std::filesystem::relative(CanonicalPath(value), root, error);
			if (error || relative.empty() || relative == ".")
				return prefix;
			std::string relativeText = PathToUTF8(relative);
			std::replace(relativeText.begin(), relativeText.end(), '\\', '/');
			return prefix + "/" + relativeText;
		};

		EditorProjectState state;
		state.ContentBrowserCurrentDirectory = storePath(m_CurrentDirectory);
		state.ExternalScriptEditor = m_ExternalScriptEditor;
		std::vector<std::string> storedNodes;
		storedNodes.reserve(m_ExpandedNodes.size());
		for (const std::string& node : m_ExpandedNodes)
		{
			const std::filesystem::path nodePath = UTF8ToPath(node);
			if (!GetRootForPath(nodePath).empty())
				storedNodes.push_back(storePath(nodePath));
		}
		std::sort(storedNodes.begin(), storedNodes.end());
		state.ContentBrowserExpandedNodes = std::move(storedNodes);
		if (!m_Project->SaveEditorState(state))
		{
			m_ProjectStateWritable = false;
			TC_Core_Error("Failed to persist Content Browser state for {0}",
				PathToUTF8(m_Project->GetProjectPath()));
			return false;
		}
		return true;
	}

	void ContentBrowserPanel::LoadLayoutSetting()
	{
		// Always reset before default -> selected layout loading so switching
		// between global/project layouts cannot retain the previous override.
		m_LayoutMode = TwoColumn;

		auto loadFrom = [this](const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			std::string line;
			bool inSection = false;
			while (std::getline(input, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[ContentBrowser]")
				{
					inSection = true;
					continue;
				}
				if (!inSection)
					continue;
				if (line.rfind("Layout=", 0) == 0)
				{
					m_LayoutMode = line.substr(7) == "OneColumn" ? OneColumn : TwoColumn;
					break;
				}
				if (!line.empty() && line.front() == '[')
					break;
			}
		};

		// Custom sections need the same default-then-selected-layout precedence as
		// ImGui's managed Window/Table/Docking sections.
		loadFrom(GetDefaultEditorLayoutPath());
		loadFrom(GetEditorLayoutPath(m_Project));
	}

	void ContentBrowserPanel::SaveLayoutSetting()
	{
		const std::filesystem::path iniPath = GetEditorLayoutPath(m_Project);
		if (!EnsureSettingsDirectory(iniPath))
			return;
		std::string ini;
		{
			std::error_code existsError;
			const bool exists = std::filesystem::exists(iniPath, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect Content Browser settings '{0}': {1}",
					PathToUTF8(iniPath), existsError.message());
				return;
			}
			std::ifstream input(iniPath, std::ios::binary);
			if (exists && !input)
			{
				TC_Core_Error("Could not read Content Browser settings '{0}'", PathToUTF8(iniPath));
				return;
			}
			if (input)
			{
				std::ostringstream contents;
				contents << input.rdbuf();
				if (input.bad())
				{
					TC_Core_Error("Failed while reading Content Browser settings '{0}'", PathToUTF8(iniPath));
					return;
				}
				ini = contents.str();
			}
		}
		const std::string sectionName = "[ContentBrowser]";
		const size_t section = FindIniSectionHeader(ini, sectionName);
		if (section != std::string::npos)
		{
			const size_t next = ini.find("\n[", section + sectionName.size());
			ini.erase(section, next == std::string::npos ? std::string::npos : next - section);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += "[ContentBrowser]\nLayout=";
		ini += m_LayoutMode == OneColumn ? "OneColumn\n" : "TwoColumn\n";
		std::string writeError;
		if (!FileSystem::WriteFileAtomically(iniPath, ini, writeError))
			TC_Core_Error("Failed to save Content Browser layout '{0}': {1}", PathToUTF8(iniPath), writeError);
	}

	void ContentBrowserPanel::OpenAsset(const std::filesystem::path& path, bool isDirectory)
	{
		const std::filesystem::path managedPath = CanonicalPath(path);
		const std::filesystem::path root = GetRootForPath(managedPath);
		std::error_code error;
		if (root.empty() || !std::filesystem::exists(managedPath, error))
		{
			TC_Warn("Refusing to open a file outside the Project Browser roots: {0}", PathToUTF8(path));
			return;
		}
		if (isDirectory)
		{
			if (!std::filesystem::is_directory(managedPath, error))
				return;
			m_CurrentDirectory = managedPath;
			m_SelectedPath = managedPath;
			m_UserSelectedDirectory = true;
			m_ExpandedNodes.insert(PathToUTF8(managedPath));
			return;
		}
		if (!IsWritablePath(managedPath))
		{
			TC_Warn("Package files are read-only editor resources and cannot be opened as project assets: {0}",
				PathToUTF8(managedPath));
			return;
		}

		const AssetHandle handle = AssetManager::Get().ImportAsset(managedPath);
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (metadata && metadata->Type == AssetType::Scene && m_SceneOpenCallback)
			m_SceneOpenCallback(handle);
		else if (metadata && metadata->Type == AssetType::CSharpScript)
			OpenCSharpScript(managedPath);
		else
			TC_Warn("Opening this file type is not supported yet: {0}", PathToUTF8(managedPath.filename()));
	}

	bool ContentBrowserPanel::OpenCSharpScript(const std::filesystem::path& path)
	{
		const std::filesystem::path scriptPath = CanonicalPath(path);
		if (!m_ExternalScriptEditor.empty())
		{
			std::filesystem::path editor;
			std::string errorMessage;
			if (!ResolveExternalScriptEditor(m_ExternalScriptEditor, editor, errorMessage)
				|| !OpenInExternalScriptEditor(editor, scriptPath, errorMessage))
			{
				TC_Core_Error("Could not open C# script '{0}' with the configured editor: {1}. "
					"Use Open With... to select another editor.", PathToUTF8(scriptPath),
					errorMessage);
				return false;
			}
			return true;
		}

		if (!OpenInAssociatedApplication(scriptPath))
		{
			TC_Core_Error("Could not open C# script with its associated application: {0}. "
				"Use Open With... to configure an editor.", PathToUTF8(scriptPath));
			return false;
		}
		return true;
	}

	void ContentBrowserPanel::ChooseExternalScriptEditor(
		const std::filesystem::path& scriptPath)
	{
		if (!m_Project || !m_ProjectStateWritable)
		{
			TC_Core_Error("Cannot configure an external C# editor because project user settings are unavailable");
			return;
		}

		const std::filesystem::path selected = FileDialogs::OpenFile(
			"Windows Executable (*.exe)\0*.exe\0");
		if (selected.empty())
			return;

		std::filesystem::path editor;
		std::string errorMessage;
		if (!ResolveExternalScriptEditor(selected, editor, errorMessage))
		{
			TC_Core_Error("Cannot use the selected C# editor: {0}", errorMessage);
			return;
		}

		const std::filesystem::path previous = m_ExternalScriptEditor;
		m_ExternalScriptEditor = editor;
		if (!Serialize())
		{
			m_ExternalScriptEditor = previous;
			TC_Core_Error("The selected C# editor could not be persisted to project user settings");
			return;
		}
		TC_Core_Info("External C# editor set to: {0}", PathToUTF8(editor));
		OpenCSharpScript(scriptPath);
	}

	void ContentBrowserPanel::BeginRename(const std::filesystem::path& path)
	{
		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path nativePath;
		if (!GetManagedMutationPath(root, path, nativePath))
			return;
		std::error_code typeError;
		if (!std::filesystem::is_directory(nativePath, typeError))
		{
			m_RenameHandle = AssetManager::Get().ImportAsset(nativePath);
			if (static_cast<uint64_t>(m_RenameHandle) == 0)
			{
				TC_Warn("Cannot rename an unregistered or conflicted asset: {0}",
					PathToUTF8(nativePath));
				return;
			}
		}
		else
			m_RenameHandle = AssetHandle(0);
		m_RenamePath = nativePath;
		std::fill(std::begin(m_RenameBuffer), std::end(m_RenameBuffer), '\0');
		const std::string name = PathToUTF8(m_RenamePath.filename());
		std::copy_n(name.begin(), std::min(name.size(), sizeof(m_RenameBuffer) - 1), m_RenameBuffer);
		m_RenameFocus = true;
		m_OpenRenamePopup = true;
	}

	void ContentBrowserPanel::CancelRename()
	{
		m_RenamePath.clear();
		m_RenameHandle = AssetHandle(0);
		m_RenameFocus = false;
	}

	std::filesystem::path ContentBrowserPanel::CommitRename()
	{
		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path oldPath;
		const std::string newName = TrimCopy(m_RenameBuffer);
		if (!GetManagedMutationPath(root, m_RenamePath, oldPath))
		{
			CancelRename();
			return {};
		}
		if (!IsValidEntryName(newName))
		{
			TC_Warn("'{0}' is not a valid asset name", newName);
			return {};
		}
		if (newName == PathToUTF8(oldPath.filename()))
		{
			CancelRename();
			return oldPath;
		}

		const std::filesystem::path requestedNewPath = oldPath.parent_path() / UTF8ToPath(newName);
		std::filesystem::path newPath;
		std::error_code error;
		const std::filesystem::file_status destinationStatus = std::filesystem::symlink_status(requestedNewPath, error);
		const bool destinationExists = !error && std::filesystem::exists(destinationStatus);
		bool destinationConflicts = destinationExists;
		if (destinationExists)
		{
			error.clear();
			destinationConflicts = !std::filesystem::equivalent(oldPath, requestedNewPath, error) || error;
		}
		error.clear();
		if (!GetManagedMutationPath(root, requestedNewPath, newPath, false) || destinationConflicts)
		{
			TC_Warn("Cannot rename asset to {0}", PathToUTF8(requestedNewPath));
			return {};
		}

		const AssetHandle movedHandle = m_RenameHandle;
		const bool moved = static_cast<uint64_t>(movedHandle) != 0
			? AssetManager::Get().MoveAsset(movedHandle, newPath)
			: AssetManager::Get().MoveAsset(oldPath, newPath);
		if (!moved)
		{
			TC_Core_Error("Failed to rename asset '{0}' to '{1}'",
				PathToUTF8(oldPath), PathToUTF8(newPath));
			return {};
		}

		if (!ApplyMovedPath(oldPath, newPath, movedHandle))
			return {};
		m_RenamePath.clear();
		m_RenameHandle = AssetHandle(0);
		m_RenameFocus = false;
		return newPath;
	}

	bool ContentBrowserPanel::ApplyMovedPath(const std::filesystem::path& oldPath,
		const std::filesystem::path& newPath, AssetHandle movedHandle)
	{
		if (m_AssetRenamedCallback && !m_AssetRenamedCallback(oldPath, newPath))
		{
			const bool rolledBack = static_cast<uint64_t>(movedHandle) != 0
				? AssetManager::Get().MoveAsset(movedHandle, oldPath)
				: AssetManager::Get().MoveAsset(newPath, oldPath);
			if (!rolledBack)
				TC_Core_Error("Asset move callback failed and the move could not be rolled back: '{0}'",
					PathToUTF8(newPath));
			return false;
		}
		m_CurrentDirectory = RemapPath(m_CurrentDirectory, oldPath, newPath);
		m_SelectedPath = RemapPath(m_SelectedPath, oldPath, newPath);
		m_DeletePath = RemapPath(m_DeletePath, oldPath, newPath);
		std::unordered_set<std::string> remappedNodes;
		for (const std::string& node : m_ExpandedNodes)
			remappedNodes.insert(PathToUTF8(RemapPath(UTF8ToPath(node), oldPath, newPath)));
		m_ExpandedNodes.swap(remappedNodes);
		return true;
	}

	void ContentBrowserPanel::RequestDeleteAsset(const std::filesystem::path& path, bool isDirectory)
	{
		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path nativePath;
		if (!GetManagedMutationPath(root, path, nativePath))
			return;
		m_DeletePath = nativePath;
		m_DeleteIsDirectory = isDirectory;
		m_DeleteHandle = AssetHandle(0);
		if (!isDirectory)
		{
			m_DeleteHandle = AssetManager::Get().ImportAsset(nativePath);
			if (static_cast<uint64_t>(m_DeleteHandle) == 0)
			{
				TC_Warn("Cannot delete an unregistered or conflicted asset through the Content Browser: {0}",
					PathToUTF8(nativePath));
				m_DeletePath.clear();
				return;
			}
		}
		m_DeleteReferences.clear();
		const std::vector<AssetHandle> handles =
			AssetManager::Get().GetRegistry().GetHandlesUnderPath(nativePath);
		std::unordered_set<std::string> seenReferences;
		for (AssetHandle handle : handles)
		{
			for (const AssetReference& reference : AssetManager::Get().FindReferences(handle))
			{
				const std::string key = std::to_string(static_cast<uint64_t>(reference.ReferencedAsset)) + "|" +
					PathToUTF8(reference.FilePath) + "|" + reference.PropertyPath;
				if (seenReferences.emplace(key).second)
					m_DeleteReferences.push_back(reference);
			}
		}
		m_OpenDeletePopup = true;
	}

	void ContentBrowserPanel::DeleteAsset(const std::filesystem::path& path, bool force)
	{
		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path managedPath;
		if (!GetManagedMutationPath(root, path, managedPath))
		{
			TC_Warn("Refusing to delete an asset outside the project root: {0}", PathToUTF8(path));
			return;
		}

		std::vector<AssetReference> references;
		const bool deleted = !m_DeleteIsDirectory &&
			static_cast<uint64_t>(m_DeleteHandle) != 0
			? AssetManager::Get().DeleteAsset(m_DeleteHandle, force, &references)
			: AssetManager::Get().DeleteAsset(managedPath, force, &references);
		if (!deleted)
		{
			TC_Core_Error("Failed to delete asset '{0}'", PathToUTF8(managedPath));
			return;
		}
		if (m_AssetDeletedCallback)
			m_AssetDeletedCallback(managedPath);

		for (auto it = m_ExpandedNodes.begin(); it != m_ExpandedNodes.end();)
		{
			const std::filesystem::path expandedPath = UTF8ToPath(*it);
			if (LexicalPath(expandedPath) == managedPath || IsWithinLexicalRoot(managedPath, expandedPath, false))
				it = m_ExpandedNodes.erase(it);
			else
				++it;
		}
		if (LexicalPath(m_CurrentDirectory) == managedPath || IsWithinLexicalRoot(managedPath, m_CurrentDirectory, false))
			m_CurrentDirectory = IsWithinRoot(root, managedPath.parent_path()) ? managedPath.parent_path() : root;
		if (LexicalPath(m_SelectedPath) == managedPath || IsWithinLexicalRoot(managedPath, m_SelectedPath, false))
		{
			m_SelectedPath.clear();
			m_UserSelectedDirectory = false;
		}
		m_DeleteHandle = AssetHandle(0);
		m_DeleteReferences.clear();
	}

	void ContentBrowserPanel::FlushPendingCreateFolder()
	{
		if (m_PendingCreateFolderParent.empty())
			return;
		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path parent = CanonicalPath(m_PendingCreateFolderParent);
		m_PendingCreateFolderParent.clear();
		std::error_code error;
		if (!IsWithinRoot(root, parent) || !std::filesystem::is_directory(parent, error))
		{
			TC_Core_Warn("Refusing to create a folder outside Assets: {0}", PathToUTF8(parent));
			return;
		}
		const std::filesystem::path newFolder = MakeUniqueFolderPath(parent);
		if (newFolder.empty() || !std::filesystem::create_directory(newFolder, error) || error)
		{
			TC_Core_Error("Failed to create folder in {0}: {1}", PathToUTF8(parent), error.message());
			return;
		}
		m_CurrentDirectory = parent;
		m_SelectedPath = newFolder;
		m_UserSelectedDirectory = true;
		m_ExpandedNodes.insert(PathToUTF8(parent));
		m_PendingOpenDirectories.insert(PathToUTF8(parent));
		BeginRename(newFolder);
	}

	void ContentBrowserPanel::FlushPendingCreateScript()
	{
		if (m_PendingCreateScriptParent.empty())
			return;

		const std::filesystem::path root = GetAssetRoot();
		const std::filesystem::path parent = CanonicalPath(m_PendingCreateScriptParent);
		m_PendingCreateScriptParent.clear();
		std::error_code error;
		if (!IsWithinRoot(root, parent) || !std::filesystem::is_directory(parent, error))
		{
			TC_Core_Warn("Refusing to create a C# script outside Assets: {0}", PathToUTF8(parent));
			return;
		}

		const auto [scriptPath, className] = MakeUniqueCSharpScriptPath(parent);
		if (scriptPath.empty())
		{
			TC_Core_Error("Could not find a unique C# script name in {0}", PathToUTF8(parent));
			return;
		}

		std::string source;
		source.reserve(256);
		source += "using TomCat;\n\n";
		source += "public sealed class " + className + " : TomCatBehaviour\n";
		source += "{\n";
		source += "    protected override void OnCreate()\n";
		source += "    {\n";
		source += "    }\n\n";
		source += "    protected override void OnUpdate(float deltaTime)\n";
		source += "    {\n";
		source += "    }\n";
		source += "}\n";

		std::string writeError;
		if (!FileSystem::WriteFileAtomically(scriptPath, source, writeError))
		{
			TC_Core_Error("Failed to create C# script '{0}': {1}",
				PathToUTF8(scriptPath), writeError);
			return;
		}

		const AssetHandle handle = AssetManager::Get().ImportAsset(scriptPath);
		if (static_cast<uint64_t>(handle) == 0)
		{
			TC_Core_Error("C# script was created but could not be imported: {0}",
				PathToUTF8(scriptPath));
			return;
		}

		m_CurrentDirectory = parent;
		m_SelectedPath = scriptPath;
		m_UserSelectedDirectory = true;
		m_ExpandedNodes.insert(PathToUTF8(parent));
		m_PendingOpenDirectories.insert(PathToUTF8(parent));
	}

	void ContentBrowserPanel::DrawContextMenuBody(const std::filesystem::path& target,
		bool isDirectory, bool isRoot)
	{
		if (target.empty())
			return;
		const bool writable = IsWritablePath(target);
		if (!writable)
		{
			if (ImGui::MenuItem("Open", nullptr, false, isDirectory))
				OpenAsset(target, true);
			ImGui::Separator();
			ImGui::MenuItem("Read Only", nullptr, false, false);
			return;
		}
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Folder"))
				m_PendingCreateFolderParent = isDirectory ? target : target.parent_path();
			if (ImGui::MenuItem("C# Script"))
				m_PendingCreateScriptParent = isDirectory ? target : target.parent_path();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Open"))
			OpenAsset(target, isDirectory);
		const bool csharpScript = !isDirectory
			&& ToLower(PathToUTF8(target.extension())) == ".cs";
		if (csharpScript && ImGui::MenuItem("Open With...", nullptr, false,
			m_Project && m_ProjectStateWritable))
			ChooseExternalScriptEditor(target);
		if (ImGui::MenuItem("Delete", nullptr, false, !isRoot))
			RequestDeleteAsset(target, isDirectory);
		if (ImGui::MenuItem("Rename", nullptr, false, !isRoot))
			BeginRename(target);
	}

	void ContentBrowserPanel::DrawLayoutMenu()
	{
		if (!ImGui::BeginMenu("Layout"))
			return;
		if (ImGui::MenuItem("One Column", nullptr, m_LayoutMode == OneColumn))
		{
			m_LayoutMode = OneColumn;
			SaveLayoutSetting();
		}
		if (ImGui::MenuItem("Two Column", nullptr, m_LayoutMode == TwoColumn))
		{
			m_LayoutMode = TwoColumn;
			SaveLayoutSetting();
		}
		ImGui::EndMenu();
	}

	void ContentBrowserPanel::DrawAssetsMenu()
	{
		if (!m_Project)
		{
			ImGui::MenuItem("No project loaded", nullptr, false, false);
			return;
		}

		auto resolveExistingTarget = [this](const std::filesystem::path& candidate,
			bool requireDirectory, std::filesystem::path& target, bool& isDirectory)
		{
			if (candidate.empty() || GetRootForPath(candidate).empty())
				return false;

			std::error_code error;
			const std::filesystem::file_status status =
				std::filesystem::symlink_status(candidate, error);
			if (error || !std::filesystem::exists(status))
				return false;

			error.clear();
			isDirectory = std::filesystem::is_directory(candidate, error);
			if (error || (requireDirectory && !isDirectory))
				return false;

			target = LexicalPath(candidate);
			return true;
		};

		std::filesystem::path target;
		bool isDirectory = false;
		const bool hadExplicitSelection = m_UserSelectedDirectory;
		const bool hasSelectedTarget = hadExplicitSelection &&
			resolveExistingTarget(m_SelectedPath, false, target, isDirectory);
		if (hadExplicitSelection && !hasSelectedTarget)
		{
			// Never retarget a destructive top-menu command from a vanished asset to
			// its containing folder. Clear the stale selection and make the user
			// reopen the menu before exposing commands for a different target.
			m_SelectedPath.clear();
			m_UserSelectedDirectory = false;
			ImGui::MenuItem("Selected asset is no longer available", nullptr, false, false);
			ImGui::Separator();
			DrawLayoutMenu();
			// A menu popup can remain open across frames. Close it now so the next
			// frame cannot reinterpret this vanished selection as the current folder.
			ImGui::CloseCurrentPopup();
			return;
		}
		if (!hasSelectedTarget &&
			!resolveExistingTarget(m_CurrentDirectory, true, target, isDirectory))
		{
			const std::filesystem::path assetRoot = GetAssetRoot();
			if (!resolveExistingTarget(assetRoot, true, target, isDirectory))
			{
				ImGui::MenuItem("No accessible Assets directory", nullptr, false, false);
				return;
			}
		}

		const std::filesystem::path targetRoot = GetRootForPath(target);
		const bool isRoot = !targetRoot.empty() &&
			CanonicalPath(target) == CanonicalPath(targetRoot);
		DrawContextMenuBody(target, isDirectory, isRoot);
		ImGui::Separator();
		DrawLayoutMenu();
	}

	void ContentBrowserPanel::DrawNodeContextMenu()
	{
		// Item context menus are rendered next to their owning item so ImGui IDs do
		// not depend on a later tree/grid stack. Kept as the common call site hook.
	}

	void ContentBrowserPanel::DrawEmptyContextMenu(const std::filesystem::path& assetRoot)
	{
		if (ImGui::BeginPopupContextWindow("ProjectEmptyContext",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			const std::filesystem::path target = IsWithinRoot(assetRoot, m_CurrentDirectory)
				? m_CurrentDirectory : assetRoot;
			DrawContextMenuBody(target, true,
				CanonicalPath(target) == CanonicalPath(assetRoot));
			ImGui::Separator();
			DrawLayoutMenu();
			ImGui::EndPopup();
		}
	}

	void ContentBrowserPanel::DrawRenamePopup()
	{
		if (m_OpenRenamePopup)
		{
			ImGui::OpenPopup("Rename Asset");
			m_OpenRenamePopup = false;
		}
		if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (m_RenameFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}
			const bool enter = ImGui::InputText("Name", m_RenameBuffer, sizeof(m_RenameBuffer),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			if (enter || ImGui::Button("Rename"))
			{
				if (!CommitRename().empty())
					ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				CancelRename();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void ContentBrowserPanel::DrawDeleteConfirmation()
	{
		if (m_OpenDeletePopup)
		{
			ImGui::OpenPopup("Delete Asset?");
			m_OpenDeletePopup = false;
		}
		if (ImGui::BeginPopupModal("Delete Asset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextWrapped("This cannot be undone. Delete '%s'?", PathToUTF8(m_DeletePath.filename()).c_str());
			if (!m_DeleteReferences.empty())
			{
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.15f, 1.0f),
					"Referenced by %zu Build Settings, Scene, Prefab, or C# field(s). Forced deletion keeps those references missing:",
					m_DeleteReferences.size());
				const size_t shown = std::min<size_t>(m_DeleteReferences.size(), 6);
				for (size_t index = 0; index < shown; ++index)
				{
					const auto& reference = m_DeleteReferences[index];
					ImGui::BulletText("%s (%s)", PathToUTF8(reference.FilePath).c_str(),
						reference.PropertyPath.c_str());
				}
				if (shown < m_DeleteReferences.size())
					ImGui::TextDisabled("... and %zu more", m_DeleteReferences.size() - shown);
			}
			const char* deleteLabel = m_DeleteReferences.empty() ? "Delete" : "Delete Anyway";
			if (ImGui::Button(deleteLabel))
			{
				DeleteAsset(m_DeletePath, !m_DeleteReferences.empty());
				m_DeletePath.clear();
				m_DeleteHandle = AssetHandle(0);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_DeletePath.clear();
				m_DeleteHandle = AssetHandle(0);
				m_DeleteReferences.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	Ref<Texture2D> ContentBrowserPanel::GetAssetIcon(const std::filesystem::path& path,
		bool isDirectory, bool isOpen)
	{
		if (!m_Icons)
			return {};
		auto icon = [&](EditorIcon id) { return m_Icons->Get(id); };

		std::error_code statusError;
		const std::filesystem::file_status linkStatus = std::filesystem::symlink_status(path, statusError);
		if (!statusError && std::filesystem::is_symlink(linkStatus))
			return icon(EditorIcon::Link);

		if (isDirectory)
		{
			if (LexicalPath(path) == LexicalPath(GetAssetRoot()))
				return icon(EditorIcon::AssetsRoot);
			return icon(isOpen ? EditorIcon::FolderOpen : EditorIcon::FolderClosed);
		}

		AssetManager& assetManager = AssetManager::Get();
		const bool projectAsset = IsWithinRoot(GetAssetRoot(), path);
		const AssetMetadata* metadata = nullptr;
		AssetHandle handle = AssetHandle(0);
		if (projectAsset)
		{
			metadata = assetManager.GetRegistry().GetMetadata(path);
			handle = metadata && !metadata->IsMissing
				? metadata->Handle : assetManager.ImportAsset(path);
			metadata = assetManager.GetRegistry().GetMetadata(handle);
		}
		if (metadata && metadata->IsMissing)
			return icon(EditorIcon::Missing);
		if (projectAsset && IsReadOnlyPath(path))
			return icon(EditorIcon::ReadOnly);

		const std::string extension = ToLower(PathToUTF8(path.extension()));
		if (extension == ".tcproj")
			return icon(EditorIcon::Project);
		if (extension == ".zip" || extension == ".pak" || extension == ".tcpkg" ||
			extension == ".unitypackage")
			return icon(EditorIcon::Package);

		const AssetType type = metadata ? metadata->Type : AssetTypeFromPath(path);
		switch (type)
		{
			case AssetType::Scene:
				return icon(!m_ActiveScenePath.empty() &&
					LexicalPath(path) == m_ActiveScenePath ? EditorIcon::SceneOpen : EditorIcon::SceneClosed);
			case AssetType::Texture2D:
			{
				Ref<Texture2D> texture = static_cast<uint64_t>(handle) != 0
					? assetManager.LoadTexture(handle) : Ref<Texture2D>{};
				return texture ? texture : icon(EditorIcon::Texture);
			}
			case AssetType::Material: return icon(EditorIcon::Material);
			case AssetType::Shader: return icon(EditorIcon::Shader);
			case AssetType::CSharpScript: return icon(EditorIcon::Script);
			case AssetType::Mesh: return icon(EditorIcon::Mesh);
			case AssetType::Audio: return icon(EditorIcon::Audio);
			case AssetType::Font: return icon(EditorIcon::Font);
			case AssetType::None:
			case AssetType::Other:
			default: return icon(EditorIcon::GenericFile);
		}
	}

	void ContentBrowserPanel::SubmitDragPayload(const std::filesystem::path& path,
		const std::filesystem::path& assetRoot, const Ref<Texture2D>& icon)
	{
		if (!IsWritablePath(path) || LexicalPath(assetRoot) != LexicalPath(GetAssetRoot()) ||
			!IsWithinRoot(assetRoot, path, false))
			return;
		const AssetHandle handle = AssetManager::Get().ImportAsset(path);
		const uint64_t rawHandle = static_cast<uint64_t>(handle);
		if (rawHandle == 0 || !ImGui::BeginDragDropSource())
			return;
		ImGui::SetDragDropPayload(AssetDragDropPayloadID, &rawHandle, sizeof(rawHandle));
		if (icon)
			ImGui::Image(ToImGuiTextureID(icon), ImVec2(64.0f, 64.0f),
				ImVec2(0, 1), ImVec2(1, 0));
		ImGui::TextUnformatted(PathToUTF8(path.filename()).c_str());
		ImGui::EndDragDropSource();
	}

	void ContentBrowserPanel::SubmitDirectoryDragPayload(const std::filesystem::path& path,
		const std::filesystem::path& assetRoot)
	{
		if (!IsWritablePath(path) || LexicalPath(assetRoot) != LexicalPath(GetAssetRoot()) ||
			!IsWithinRoot(assetRoot, path, false) || !ImGui::BeginDragDropSource())
			return;
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(path, assetRoot, error);
		if (!error && !relative.empty())
		{
			const std::wstring payloadPath = relative.wstring();
			ImGui::SetDragDropPayload(kAssetDirectoryPayloadID, payloadPath.c_str(),
				(payloadPath.size() + 1) * sizeof(wchar_t));
			ImGui::TextUnformatted(PathToUTF8(path.filename()).c_str());
		}
		ImGui::EndDragDropSource();
	}

	void ContentBrowserPanel::AcceptAssetMoveTarget(const std::filesystem::path& destinationDirectory)
	{
		if (!IsWritablePath(destinationDirectory))
			return;
		if (!ImGui::BeginDragDropTarget())
			return;
		if (m_EntityPrefabCreateCallback)
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				SceneEntityDragDropPayloadID))
			{
				if (payload->IsDelivery() && payload->Data
					&& payload->DataSize == sizeof(uint64_t))
				{
					const uint64_t rawEntity =
						*static_cast<const uint64_t*>(payload->Data);
					if (rawEntity != 0)
						m_EntityPrefabCreateCallback(UUID(rawEntity),
							CanonicalPath(destinationDirectory));
				}
				ImGui::EndDragDropTarget();
				return;
			}
		}

		const std::filesystem::path root = GetAssetRoot();
		std::filesystem::path source;
		AssetHandle sourceHandle = AssetHandle(0);
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID))
		{
			if (payload->DataSize == sizeof(uint64_t))
			{
				sourceHandle = AssetHandle(*static_cast<const uint64_t*>(payload->Data));
				source = AssetManager::Get().ResolvePath(sourceHandle);
			}
		}
		else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDirectoryPayloadID))
		{
			const bool validSize = payload->Data != nullptr &&
				payload->DataSize >= static_cast<int>(sizeof(wchar_t)) &&
				payload->DataSize % static_cast<int>(sizeof(wchar_t)) == 0;
			if (validSize)
			{
				const auto* characters = static_cast<const wchar_t*>(payload->Data);
				const size_t characterCount = static_cast<size_t>(payload->DataSize) / sizeof(wchar_t);
				if (characters[characterCount - 1] == L'\0')
				{
					const std::wstring relativeText(characters, characterCount - 1);
					const std::filesystem::path relativePath(relativeText);
					if (!relativeText.empty() && relativeText.find(L'\0') == std::wstring::npos &&
						!relativePath.is_absolute() && !relativePath.has_root_name() &&
						!relativePath.has_root_directory())
						source = root / relativePath;
				}
			}
		}

		if (!source.empty())
		{
			std::filesystem::path managedSource;
			const std::filesystem::path managedDestinationDirectory = CanonicalPath(destinationDirectory);
			std::error_code error;
			const bool validDestination = IsWithinRoot(root, managedDestinationDirectory) &&
				std::filesystem::is_directory(managedDestinationDirectory, error) && !error;
			if (!GetManagedMutationPath(root, source, managedSource) || !validDestination)
			{
				TC_Core_Error("Rejected invalid Content Browser move from '{0}' to '{1}'",
					PathToUTF8(source), PathToUTF8(destinationDirectory));
			}
			else if (LexicalPath(managedSource.parent_path()) == LexicalPath(managedDestinationDirectory))
			{
				// Dropping an entry into its current parent is a deliberate no-op, not a reorder.
			}
			else
			{
				error.clear();
				const std::filesystem::file_status sourceStatus =
					std::filesystem::symlink_status(managedSource, error);
				const bool sourceIsDirectory = !error && std::filesystem::is_directory(sourceStatus);
				if (sourceIsDirectory &&
					IsWithinLexicalRoot(managedSource, managedDestinationDirectory))
				{
					TC_Core_Warn("Cannot move directory '{0}' into itself or one of its descendants",
						PathToUTF8(managedSource));
					ImGui::EndDragDropTarget();
					return;
				}

				const std::filesystem::path destination =
					managedDestinationDirectory / managedSource.filename();
				error.clear();
				const std::filesystem::file_status destinationStatus =
					std::filesystem::symlink_status(destination, error);
				if (!error && std::filesystem::exists(destinationStatus))
				{
					TC_Core_Warn("Cannot move '{0}': destination already contains '{1}'",
						PathToUTF8(managedSource), PathToUTF8(destination.filename()));
					ImGui::EndDragDropTarget();
					return;
				}
				if (error && error != std::errc::no_such_file_or_directory)
				{
					TC_Core_Error("Cannot inspect asset move destination '{0}': {1}",
						PathToUTF8(destination), error.message());
					ImGui::EndDragDropTarget();
					return;
				}

				const bool moved = static_cast<uint64_t>(sourceHandle) != 0
					? AssetManager::Get().MoveAsset(sourceHandle, destination)
					: AssetManager::Get().MoveAsset(managedSource, destination);
				if (moved)
				{
					if (!ApplyMovedPath(managedSource, destination, sourceHandle))
						TC_Core_Error("Asset move callback rejected '{0}' -> '{1}'; rollback was requested",
							PathToUTF8(managedSource), PathToUTF8(destination));
				}
				else
					TC_Core_Error("Failed to move asset '{0}' to '{1}'",
						PathToUTF8(managedSource), PathToUTF8(destination));
			}
		}
		ImGui::EndDragDropTarget();
	}

	void ContentBrowserPanel::DrawFileTreeNode(const std::filesystem::path& path,
		const std::filesystem::path& root)
	{
		if (!IsManagedEntry(root, path))
			return;
		ImGui::PushID(PathToUTF8(LexicalPath(path)).c_str());
		const bool selected = m_UserSelectedDirectory && LexicalPath(m_SelectedPath) == LexicalPath(path);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | (selected ? ImGuiTreeNodeFlags_Selected : 0);
		ImGui::TreeNodeEx("##File", flags, "     %s", PathToUTF8(path.filename()).c_str());
		DrawTreeIcon(GetAssetIcon(path, false), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		if (ImGui::IsItemClicked())
		{
			m_SelectedPath = path;
			m_UserSelectedDirectory = true;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenAsset(path, false);
		Ref<Texture2D> icon = GetAssetIcon(path, false);
		SubmitDragPayload(path, root, icon);
		if (ImGui::BeginPopupContextItem("Context"))
		{
			DrawContextMenuBody(path, false, false);
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directoryPath,
		const std::filesystem::path& root, const char* rootLabel, bool isRoot, bool includeFiles)
	{
		std::error_code directoryError;
		if (!IsWithinRoot(root, directoryPath) ||
			!std::filesystem::is_directory(directoryPath, directoryError) || directoryError)
			return;
		const std::string key = PathToUTF8(CanonicalPath(directoryPath));
		const auto entries = ReadDirectory(directoryPath);
		const bool hasVisibleChildren = std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
			return IsManagedEntry(root, entry.path()) && (includeFiles || IsRecursiveDirectory(entry));
		});
		const bool selected = m_UserSelectedDirectory && CanonicalPath(m_SelectedPath) == CanonicalPath(directoryPath);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_OpenOnDoubleClick | (selected ? ImGuiTreeNodeFlags_Selected : 0);
		if (!hasVisibleChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (isRoot || m_ExpandedNodes.find(key) != m_ExpandedNodes.end())
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		if (m_PendingOpenDirectories.erase(key) > 0)
			ImGui::SetNextItemOpen(true);

		ImGui::PushID(key.c_str());
		const std::string directoryLabel = isRoot ? rootLabel : PathToUTF8(directoryPath.filename());
		const bool open = ImGui::TreeNodeEx("##Directory", flags, "     %s", directoryLabel.c_str());
		DrawTreeIcon(GetAssetIcon(directoryPath, true, open),
			ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		if (isRoot && !IsWritablePath(directoryPath) && ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Read-only editor packages");
			ImGui::EndTooltip();
		}
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			m_SelectedPath = directoryPath;
			m_UserSelectedDirectory = true;
			m_CurrentDirectory = directoryPath;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenAsset(directoryPath, true);
		if (ImGui::IsItemToggledOpen())
		{
			if (open) m_ExpandedNodes.insert(key);
			else m_ExpandedNodes.erase(key);
		}
		if (!isRoot)
			SubmitDirectoryDragPayload(directoryPath, root);
		AcceptAssetMoveTarget(directoryPath);
		if (ImGui::BeginPopupContextItem("Context"))
		{
			DrawContextMenuBody(directoryPath, true, isRoot);
			ImGui::EndPopup();
		}
		if (open && hasVisibleChildren)
		{
			for (const auto& entry : entries)
			{
				if (!IsManagedEntry(root, entry.path()))
					continue;
				if (IsRecursiveDirectory(entry))
					DrawDirectoryTree(entry.path(), root, rootLabel, false, includeFiles);
				else if (includeFiles)
					DrawFileTreeNode(entry.path(), root);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawBreadcrumbs(const std::filesystem::path& root, const char* rootLabel)
	{
		if (ImGui::Button(rootLabel))
			OpenAsset(root, true);
		AcceptAssetMoveTarget(root);
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, root, error);
		if (error || relative == ".")
			return;
		std::filesystem::path accumulated = root;
		for (const auto& part : relative)
		{
			accumulated /= part;
			ImGui::SameLine();
			ImGui::TextDisabled(">");
			ImGui::SameLine();
			ImGui::PushID(PathToUTF8(accumulated).c_str());
			const std::string label = PathToUTF8(part);
			if (ImGui::Button(label.c_str()))
				OpenAsset(accumulated, true);
			AcceptAssetMoveTarget(accumulated);
			ImGui::PopID();
		}
	}

	void ContentBrowserPanel::DrawAssetItem(const std::filesystem::directory_entry& entry,
		const std::filesystem::path& root)
	{
		const std::filesystem::path path = entry.path();
		if (!IsManagedEntry(root, path))
			return;
		std::error_code error;
		const bool isDirectory = entry.is_directory(error);
		Ref<Texture2D> icon = GetAssetIcon(path, isDirectory);
		if (!icon && m_Icons)
			icon = m_Icons->Get(EditorIcon::GenericFile);
		const bool selected = m_UserSelectedDirectory && LexicalPath(m_SelectedPath) == LexicalPath(path);
		ImGui::PushID(PathToUTF8(LexicalPath(path)).c_str());
		if (selected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
		if (icon)
		{
			ImGui::ImageButton(ToImGuiTextureID(icon), ImVec2(m_ThumbnailSize, m_ThumbnailSize),
				ImVec2(0, 1), ImVec2(1, 0), 0);
		}
		else
			ImGui::Button("##MissingAssetIcon", ImVec2(m_ThumbnailSize, m_ThumbnailSize));
		if (selected)
			ImGui::PopStyleColor();
		if (ImGui::IsItemClicked())
		{
			m_SelectedPath = path;
			m_UserSelectedDirectory = true;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenAsset(path, isDirectory);
		if (isDirectory)
		{
			SubmitDirectoryDragPayload(path, root);
			AcceptAssetMoveTarget(path);
		}
		else
			SubmitDragPayload(path, root, icon);
		if (ImGui::BeginPopupContextItem("Context"))
		{
			DrawContextMenuBody(path, isDirectory, false);
			ImGui::EndPopup();
		}
		ImGui::TextWrapped("%s", PathToUTF8(path.filename()).c_str());
		if (!isDirectory && IsWritablePath(path) &&
			s_ImageExtensions.find(ToLower(PathToUTF8(path.extension()))) != s_ImageExtensions.end() &&
			ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			if (icon)
				ImGui::Image(ToImGuiTextureID(icon), ImVec2(200.0f, 200.0f),
					ImVec2(0, 1), ImVec2(1, 0));
			ImGui::EndTooltip();
		}
		ImGui::NextColumn();
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawAssetGrid(const std::filesystem::path& root, const char* rootLabel)
	{
		std::error_code directoryError;
		if (!IsWithinRoot(root, m_CurrentDirectory) ||
			!std::filesystem::is_directory(m_CurrentDirectory, directoryError) || directoryError)
			m_CurrentDirectory = root;
		DrawBreadcrumbs(root, rootLabel);
		ImGui::Separator();
		const float cellSize = m_ThumbnailSize + 16.0f;
		const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));
		ImGui::Columns(columns, nullptr, false);
		for (const auto& entry : ReadDirectory(m_CurrentDirectory))
			DrawAssetItem(entry, root);
		ImGui::Columns(1);
		if (ImGui::GetDragDropPayload())
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			if (available.x > 1.0f && available.y > 1.0f)
			{
				ImGui::InvisibleButton("##CurrentDirectoryDropTarget", available);
				AcceptAssetMoveTarget(m_CurrentDirectory);
			}
		}
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl)
		{
			m_ThumbnailSize = std::clamp(m_ThumbnailSize - ImGui::GetIO().MouseWheel * 8.0f, 64.0f, 512.0f);
		}
		DrawEmptyContextMenu(root);
	}

	void ContentBrowserPanel::OnImGuiRender(bool* open)
	{
		m_Focused = false;
		// Assets-menu commands must keep advancing even when the docked Project
		// panel is hidden. Modal popups are also drawn after the panel window so
		// they always live in the same parent ImGui scope.
		FlushPendingCreateFolder();
		FlushPendingCreateScript();
		if (open && !*open)
		{
			DrawRenamePopup();
			DrawDeleteConfirmation();
			return;
		}
		const bool visible = ImGui::Begin("Project", open);
		m_Docked = ImGui::IsWindowDocked();
		m_Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			DrawRenamePopup();
			DrawDeleteConfirmation();
			return;
		}

		auto drawLayoutOptions = [this]()
		{
			ImGui::TextDisabled("Layout");
			ImGui::Separator();
			if (ImGui::MenuItem("One Column", nullptr, m_LayoutMode == OneColumn))
			{
				m_LayoutMode = OneColumn;
				SaveLayoutSetting();
			}
			if (ImGui::MenuItem("Two Column", nullptr, m_LayoutMode == TwoColumn))
			{
				m_LayoutMode = TwoColumn;
				SaveLayoutSetting();
			}
		};

		const bool positionLayoutOptions = m_OpenLayoutOptions;
		if (m_OpenLayoutOptions)
		{
			ImGui::OpenPopup("ProjectOptions");
			m_OpenLayoutOptions = false;
		}
		if (positionLayoutOptions)
			ImGui::SetNextWindowPos(ImVec2(m_LayoutOptionsX, m_LayoutOptionsY), ImGuiCond_Always);
		if (ImGui::BeginPopup("ProjectOptions"))
		{
			drawLayoutOptions();
			ImGui::EndPopup();
		}

		const std::filesystem::path assetRoot = GetAssetRoot();
		const std::filesystem::path packagesRoot = GetPackagesRoot();
		std::error_code error;
		if (!m_Project || !std::filesystem::is_directory(assetRoot, error))
		{
			ImGui::TextDisabled("No accessible project asset directory");
			ImGui::End();
			DrawRenamePopup();
			DrawDeleteConfirmation();
			return;
		}
		error.clear();
		const bool packagesAvailable = std::filesystem::is_directory(packagesRoot, error) && !error;
		const std::filesystem::path currentRoot = GetRootForPath(m_CurrentDirectory);
		const bool browsingPackages = packagesAvailable &&
			LexicalPath(currentRoot) == LexicalPath(packagesRoot);
		const std::filesystem::path activeRoot = browsingPackages ? packagesRoot : assetRoot;
		const char* activeRootLabel = browsingPackages ? "Packages" : "Assets";

		if (m_LayoutMode == OneColumn)
		{
			DrawDirectoryTree(assetRoot, assetRoot, "Assets", true, true);
			if (packagesAvailable)
				DrawDirectoryTree(packagesRoot, packagesRoot, "Packages", true, true);
			DrawEmptyContextMenu(activeRoot);
		}
		else
		{
			const float splitterWidth = 8.0f;
			ImGui::BeginChild("DirectoryTree", ImVec2(m_LeftPanelWidth, 0.0f), false);
			DrawDirectoryTree(assetRoot, assetRoot, "Assets", true, false);
			if (packagesAvailable)
				DrawDirectoryTree(packagesRoot, packagesRoot, "Packages", true, false);
			DrawEmptyContextMenu(activeRoot);
			ImGui::EndChild();
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::InvisibleButton("ProjectSplitter", ImVec2(splitterWidth, std::max(1.0f, ImGui::GetContentRegionAvail().y)));
			if (ImGui::IsItemActive())
				m_LeftPanelWidth = std::clamp(m_LeftPanelWidth + ImGui::GetIO().MouseDelta.x,
					120.0f, std::max(120.0f, ImGui::GetWindowWidth() - 240.0f));
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::BeginChild("AssetGrid", ImVec2(0.0f, 0.0f), false);
			DrawAssetGrid(activeRoot, activeRootLabel);
			ImGui::EndChild();
		}

		DrawNodeContextMenu();
		ImGui::End();
		DrawRenamePopup();
		DrawDeleteConfirmation();
	}

}
