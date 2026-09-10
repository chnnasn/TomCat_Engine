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
#include <vector>

#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/PlatformUtils.h"

namespace TomCat {

	extern const std::filesystem::path g_AssetPath = "Assets";

	namespace {

		const std::unordered_set<std::string> s_ImageExtensions = {
			".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr", ".pic"
		};
		constexpr const char* kAssetDirectoryPayloadID = "TOMCAT_ASSET_DIRECTORY";

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
		m_DirectoryIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/FileIcon.png");
		SetProject(ProjectManager::Get().GetActiveProject());
	}

	std::filesystem::path ContentBrowserPanel::GetAssetRoot() const
	{
		return m_Project ? CanonicalPath(m_Project->GetAssetPath()) : CanonicalPath(g_AssetPath);
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
		m_CurrentDirectory.clear();
		m_SelectedPath.clear();
		m_UserSelectedDirectory = false;
		m_ExpandedNodes.clear();
		m_PendingOpenDirectories.clear();
		m_ContextPath.clear();
		m_PendingCreateFolderParent.clear();
		m_RenamePath.clear();
		m_DeletePath.clear();
		LoadLayoutSetting();
		RestoreProjectState();
	}

	void ContentBrowserPanel::RestoreProjectState()
	{
		const std::filesystem::path root = GetAssetRoot();
		m_CurrentDirectory = root;
		if (!m_Project)
			return;

		EditorProjectState state;
		const EditorProjectStateLoadResult loadResult = m_Project->LoadEditorState(state);
		if (loadResult == EditorProjectStateLoadResult::Failed)
			m_ProjectStateWritable = false;

		auto resolveStoredPath = [&](const std::string& stored) {
			if (stored.empty())
				return root;
			const std::filesystem::path value = UTF8ToPath(stored);
			const std::filesystem::path candidate = value.is_absolute() ? value : root / value;
			return IsWithinRoot(root, candidate) ? CanonicalPath(candidate) : root;
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
			if (IsWithinRoot(root, node) && std::filesystem::is_directory(node, error))
				m_ExpandedNodes.insert(PathToUTF8(node));
		}
	}

	bool ContentBrowserPanel::Serialize()
	{
		// Do not let automatic shutdown persistence overwrite an existing settings
		// file that failed validation or could not be read during this session.
		if (!m_Project || !m_ProjectStateWritable)
			return false;
		const std::filesystem::path root = GetAssetRoot();
		auto storeRelative = [&](const std::filesystem::path& value) {
			if (!IsWithinRoot(root, value))
				return std::string(".");
			std::error_code error;
			std::filesystem::path relative = std::filesystem::relative(CanonicalPath(value), root, error);
			return error || relative.empty() ? std::string(".") : PathToUTF8(relative);
		};

		EditorProjectState state;
		state.ContentBrowserCurrentDirectory = storeRelative(m_CurrentDirectory);
		std::vector<std::string> storedNodes;
		storedNodes.reserve(m_ExpandedNodes.size());
		for (const std::string& node : m_ExpandedNodes)
		{
			const std::filesystem::path nodePath = UTF8ToPath(node);
			if (IsWithinRoot(root, nodePath))
				storedNodes.push_back(storeRelative(nodePath));
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
		const std::filesystem::path root = GetAssetRoot();
		const std::filesystem::path managedPath = CanonicalPath(path);
		std::error_code error;
		if (!IsWithinRoot(root, managedPath) || !std::filesystem::exists(managedPath, error))
		{
			TC_Warn("Refusing to open an asset outside the project root: {0}", PathToUTF8(path));
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

		const AssetHandle handle = AssetManager::Get().ImportAsset(managedPath);
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (metadata && metadata->Type == AssetType::Scene && m_SceneOpenCallback)
			m_SceneOpenCallback(handle);
		else
			TC_Warn("Opening this file type is not supported yet: {0}", PathToUTF8(managedPath.filename()));
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
		m_ContextPath = RemapPath(m_ContextPath, oldPath, newPath);
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
		if (m_ContextPath == managedPath)
			m_ContextPath.clear();
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
			parent = root;
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

	void ContentBrowserPanel::DrawContextMenuBody()
	{
		if (m_ContextPath.empty())
			return;
		const std::filesystem::path target = m_ContextPath;
		const bool isDirectory = m_ContextIsDirectory;
		const bool isRoot = m_ContextIsRoot;
		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Folder"))
				m_PendingCreateFolderParent = isDirectory ? target : target.parent_path();
			ImGui::EndMenu();
		}
		if (ImGui::MenuItem("Open"))
			OpenAsset(target, isDirectory);
		if (ImGui::MenuItem("Delete", nullptr, false, !isRoot))
			RequestDeleteAsset(target, isDirectory);
		if (ImGui::MenuItem("Rename", nullptr, false, !isRoot))
			BeginRename(target);
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
			m_ContextPath = IsWithinRoot(assetRoot, m_CurrentDirectory) ? m_CurrentDirectory : assetRoot;
			m_ContextIsDirectory = true;
			m_ContextIsRoot = CanonicalPath(m_ContextPath) == CanonicalPath(assetRoot);
			DrawContextMenuBody();
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
					"Referenced by %zu project or scene field(s). Forced deletion keeps those references missing:",
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

	Ref<Texture2D> ContentBrowserPanel::GetAssetIcon(const std::filesystem::path& path, bool isDirectory)
	{
		if (isDirectory)
			return m_DirectoryIcon;
		AssetManager& assetManager = AssetManager::Get();
		const AssetMetadata* metadata = assetManager.GetRegistry().GetMetadata(path);
		AssetHandle handle = metadata && !metadata->IsMissing
			? metadata->Handle : assetManager.ImportAsset(path);
		metadata = assetManager.GetRegistry().GetMetadata(handle);
		if (!metadata || metadata->Type != AssetType::Texture2D)
			return m_FileIcon;
		Ref<Texture2D> texture = assetManager.LoadTexture(handle);
		return texture ? texture : m_FileIcon;
	}

	void ContentBrowserPanel::SubmitDragPayload(const std::filesystem::path& path,
		const std::filesystem::path& assetRoot, const Ref<Texture2D>& icon)
	{
		if (!IsWithinRoot(assetRoot, path, false))
			return;
		const AssetHandle handle = AssetManager::Get().ImportAsset(path);
		const uint64_t rawHandle = static_cast<uint64_t>(handle);
		if (rawHandle == 0 || !ImGui::BeginDragDropSource())
			return;
		ImGui::SetDragDropPayload(AssetDragDropPayloadID, &rawHandle, sizeof(rawHandle));
		ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(icon->GetRendererID())),
			ImVec2(64.0f, 64.0f), ImVec2(0, 1), ImVec2(1, 0));
		ImGui::TextUnformatted(PathToUTF8(path.filename()).c_str());
		ImGui::EndDragDropSource();
	}

	void ContentBrowserPanel::SubmitDirectoryDragPayload(const std::filesystem::path& path,
		const std::filesystem::path& assetRoot)
	{
		if (!IsWithinRoot(assetRoot, path, false) || !ImGui::BeginDragDropSource())
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
		if (!ImGui::BeginDragDropTarget())
			return;

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

	void ContentBrowserPanel::DrawFileTreeNode(const std::filesystem::path& path)
	{
		const std::filesystem::path root = GetAssetRoot();
		if (!IsManagedEntry(root, path))
			return;
		ImGui::PushID(PathToUTF8(LexicalPath(path)).c_str());
		const bool selected = m_UserSelectedDirectory && LexicalPath(m_SelectedPath) == LexicalPath(path);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | (selected ? ImGuiTreeNodeFlags_Selected : 0);
		ImGui::TreeNodeEx("##File", flags, "%s", PathToUTF8(path.filename()).c_str());
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
			m_ContextPath = path;
			m_ContextIsDirectory = false;
			m_ContextIsRoot = false;
			DrawContextMenuBody();
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawDirectoryTree(const std::filesystem::path& directoryPath, bool isRoot, bool includeFiles)
	{
		const std::filesystem::path root = GetAssetRoot();
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
		const std::string directoryLabel = isRoot ? "Assets" : PathToUTF8(directoryPath.filename());
		const bool open = ImGui::TreeNodeEx("##Directory", flags, "%s", directoryLabel.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			m_SelectedPath = directoryPath;
			m_UserSelectedDirectory = true;
			if (m_LayoutMode == TwoColumn)
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
			m_ContextPath = directoryPath;
			m_ContextIsDirectory = true;
			m_ContextIsRoot = isRoot;
			DrawContextMenuBody();
			ImGui::EndPopup();
		}
		if (open && hasVisibleChildren)
		{
			for (const auto& entry : entries)
			{
				if (!IsManagedEntry(root, entry.path()))
					continue;
				if (IsRecursiveDirectory(entry))
					DrawDirectoryTree(entry.path(), false, includeFiles);
				else if (includeFiles)
					DrawFileTreeNode(entry.path());
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawBreadcrumbs(const std::filesystem::path& assetRoot)
	{
		if (ImGui::Button("Assets"))
			OpenAsset(assetRoot, true);
		AcceptAssetMoveTarget(assetRoot);
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(m_CurrentDirectory, assetRoot, error);
		if (error || relative == ".")
			return;
		std::filesystem::path accumulated = assetRoot;
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
		const std::filesystem::path& assetRoot)
	{
		const std::filesystem::path path = entry.path();
		if (!IsManagedEntry(assetRoot, path))
			return;
		std::error_code error;
		const bool isDirectory = entry.is_directory(error);
		Ref<Texture2D> icon = GetAssetIcon(path, isDirectory);
		const bool selected = m_UserSelectedDirectory && LexicalPath(m_SelectedPath) == LexicalPath(path);
		ImGui::PushID(PathToUTF8(LexicalPath(path)).c_str());
		if (selected)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
		ImGui::ImageButton(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(icon->GetRendererID())),
			ImVec2(m_ThumbnailSize, m_ThumbnailSize),
			ImVec2(0, 1), ImVec2(1, 0), 0);
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
			SubmitDirectoryDragPayload(path, assetRoot);
			AcceptAssetMoveTarget(path);
		}
		else
			SubmitDragPayload(path, assetRoot, icon);
		if (ImGui::BeginPopupContextItem("Context"))
		{
			m_ContextPath = path;
			m_ContextIsDirectory = isDirectory;
			m_ContextIsRoot = false;
			DrawContextMenuBody();
			ImGui::EndPopup();
		}
		ImGui::TextWrapped("%s", PathToUTF8(path.filename()).c_str());
		if (!isDirectory && s_ImageExtensions.find(ToLower(PathToUTF8(path.extension()))) != s_ImageExtensions.end() && ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(icon->GetRendererID())),
				ImVec2(200.0f, 200.0f), ImVec2(0, 1), ImVec2(1, 0));
			ImGui::EndTooltip();
		}
		ImGui::NextColumn();
		ImGui::PopID();
	}

	void ContentBrowserPanel::DrawAssetGrid(const std::filesystem::path& assetRoot)
	{
		std::error_code directoryError;
		if (!IsWithinRoot(assetRoot, m_CurrentDirectory) ||
			!std::filesystem::is_directory(m_CurrentDirectory, directoryError) || directoryError)
			m_CurrentDirectory = assetRoot;
		DrawBreadcrumbs(assetRoot);
		ImGui::Separator();
		const float cellSize = m_ThumbnailSize + 16.0f;
		const int columns = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellSize));
		ImGui::Columns(columns, nullptr, false);
		for (const auto& entry : ReadDirectory(m_CurrentDirectory))
			DrawAssetItem(entry, assetRoot);
		ImGui::Columns(1);
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl)
		{
			m_ThumbnailSize = std::clamp(m_ThumbnailSize - ImGui::GetIO().MouseWheel * 8.0f, 64.0f, 512.0f);
		}
		DrawEmptyContextMenu(assetRoot);
	}

	void ContentBrowserPanel::OnImGuiRender(bool* open)
	{
		if (open && !*open)
			return;
		const bool visible = ImGui::Begin("Project", open, ImGuiWindowFlags_MenuBar);
		if (!visible)
		{
			ImGui::End();
			return;
		}

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Layout"))
			{
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
			ImGui::EndMenuBar();
		}

		FlushPendingCreateFolder();
		const std::filesystem::path assetRoot = GetAssetRoot();
		std::error_code error;
		if (!m_Project || !std::filesystem::is_directory(assetRoot, error))
		{
			ImGui::TextDisabled("No accessible project asset directory");
			DrawRenamePopup();
			DrawDeleteConfirmation();
			ImGui::End();
			return;
		}

		if (m_LayoutMode == OneColumn)
		{
			DrawDirectoryTree(assetRoot, true, true);
			DrawEmptyContextMenu(assetRoot);
		}
		else
		{
			const float splitterWidth = 8.0f;
			ImGui::BeginChild("DirectoryTree", ImVec2(m_LeftPanelWidth, 0.0f), false);
			DrawDirectoryTree(assetRoot, true, false);
			DrawEmptyContextMenu(assetRoot);
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
			DrawAssetGrid(assetRoot);
			ImGui::EndChild();
		}

		DrawNodeContextMenu();
		DrawRenamePopup();
		DrawDeleteConfirmation();
		ImGui::End();
	}

}
