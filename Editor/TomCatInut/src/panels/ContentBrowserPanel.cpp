#include"tcpch.h"

#include "ContentBrowserPanel.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <cstring>

namespace { int g_ContentBrowserLayout = 0; /* 0=TwoColumn,1=OneColumn */ }
#include <fstream>

#include "TomCat/ImGui/ImGuiCallback.h"
#include "TomCat/Project/ProjectManager.h"

namespace TomCat {

    extern const std::filesystem::path g_AssetPath = "Assets";

	static const std::unordered_set<std::string> s_ImageExtensions = {
		".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp", ".psd", ".hdr", ".pic"
	};

	static const std::unordered_set<std::wstring> s_ImageExtensionsW = {
	L".png", L".jpg", L".jpeg", L".bmp", L".tga", L".gif", L".webp", L".psd", L".hdr", L".pic"
	};

	bool ShowMenu = false;

	ImVec2 MenuPosi;

	static std::string TrimCopy(const std::string& value)
	{
		std::string result = value;
		while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
			result.erase(result.begin());
		while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
			result.pop_back();
		return result;
	}

	static void PushSelectedTreeColors()
	{
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(44.0f / 255.0f, 93.0f / 255.0f, 135.0f / 255.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(58.0f / 255.0f, 112.0f / 255.0f, 157.0f / 255.0f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(36.0f / 255.0f, 79.0f / 255.0f, 115.0f / 255.0f, 1.0f));
	}

	static bool IsValidEntryName(const std::string& name)
	{
		if (name.empty() || name == "." || name == "..")
			return false;
		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
			return false;
		for (char c : name)
		{
			if (c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*')
				return false;
		}
		return true;
	}

	static std::filesystem::path MakeUniqueFolderPath(const std::filesystem::path& parent)
	{
		for (int index = 0; index < 10000; ++index)
		{
			std::string name = "New Folder";
			if (index > 0)
				name += " (" + std::to_string(index) + ")";
			std::filesystem::path candidate = parent / name;
			if (!std::filesystem::exists(candidate))
				return candidate;
		}
		return parent / "New Folder";
	}

	static bool IsPathInside(const std::filesystem::path& parent, const std::filesystem::path& child)
	{
		if (child == parent)
			return false;
		const std::filesystem::path relative = child.lexically_relative(parent);
		if (relative.empty())
			return false;
		for (const auto& part : relative)
		{
			if (part == "..")
				return false;
		}
		return true;
	}

	static std::filesystem::path RemapPath(const std::filesystem::path& oldPath,
		const std::filesystem::path& oldRoot, const std::filesystem::path& newRoot)
	{
		if (oldPath == oldRoot)
			return newRoot;
		if (!IsPathInside(oldRoot, oldPath))
			return oldPath;
		const std::filesystem::path relative = oldPath.lexically_relative(oldRoot);
		return newRoot / relative;
	}

	ContentBrowserPanel::ContentBrowserPanel()
		: m_LayoutMode(TwoColumn)
	{
		// Read the editor-level layout from imgui.ini ([ContentBrowser] section)
		LoadLayoutSetting();

		m_Project = ProjectManager::Get().GetActiveProject();
		if (m_Project)
		{
            m_CurrentDirectory = m_Project->GetAssetPath();
            std::string twoColumnFolder = m_Project->GetConfig().TwoColumnCurrentFolder;
            if (!twoColumnFolder.empty())
            {
                m_TwoColumnCurrentFolder = twoColumnFolder;
                m_SelectedDirectory = twoColumnFolder;
            }
            
            for (const auto& node : m_Project->GetConfig().ExpandedNodes)
            {
                m_ExpandedNodes.insert(node);
            }
		}
        else
        {
            m_CurrentDirectory = g_AssetPath;
        }
		
		m_DirectoryIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/FileIcon.png");

		TomCat::RegisterWindowMoreOptionsCallback("Project", [](ImVec2 pos) {

			MenuPosi = ImVec2{pos.x,pos.y+40};
			
			ShowMenu = true; // every click opens the menu (popup closes on outside click / CloseCurrentPopup)

			});
	}

	void ContentBrowserPanel::SetProject(Ref<Project> project)
	{
		m_Project = project;
		m_ContextPath.clear();
		m_ContextIsDirectory = false;
		m_ContextIsRoot = false;
		m_PendingCreateFolderParent.clear();
		m_RenamePath.clear();
		m_RenameFocus = false;
		m_UserSelectedDirectory = false;
		m_PendingOpenDirectories.clear();
		if (project)
		{
            m_CurrentDirectory = project->GetAssetPath();
            std::string twoColumnFolder = project->GetConfig().TwoColumnCurrentFolder;
            if (!twoColumnFolder.empty())
            {
                m_TwoColumnCurrentFolder = twoColumnFolder;
                m_SelectedDirectory = twoColumnFolder;
            }
            else
            {
                m_TwoColumnCurrentFolder = project->GetAssetPath();
                m_SelectedDirectory = project->GetAssetPath();
            }
            
            m_ExpandedNodes.clear();
            for (const auto& node : project->GetConfig().ExpandedNodes)
            {
                m_ExpandedNodes.insert(node);
            }
		}
	}

	void ContentBrowserPanel::FlushPendingCreateFolder()
	{
		std::filesystem::path parent = m_PendingCreateFolderParent;
		m_PendingCreateFolderParent.clear();
		if (parent.empty())
			return;

		Ref<Project> project = ProjectManager::Get().GetActiveProject();
		if (!project)
			return;

		std::error_code error;
		if (!std::filesystem::is_directory(parent, error))
			parent = project->GetAssetPath();

		const std::filesystem::path newFolder = MakeUniqueFolderPath(parent);
		error.clear();
		if (!std::filesystem::create_directory(newFolder, error) || error)
		{
			TC_Core_Error("Failed to create folder in {0}: {1}", parent.string(), error.message());
			return;
		}

		// 展开父目录，保证新建文件夹立即可见并进入行内重命名。
		const std::filesystem::path assetPath = project->GetAssetPath();
		std::filesystem::path walk = parent;
		while (!walk.empty())
		{
			m_ExpandedNodes.insert(walk.string());
			if (walk == assetPath)
				break;
			walk = walk.parent_path();
		}

		if (m_LayoutMode == OneColumn)
		{
			// One Column 模式下把新建文件夹本身作为选中项，行内重命名时显示蓝色选中态。
			m_SelectedDirectory = newFolder;
			m_UserSelectedDirectory = true;
			std::filesystem::path walk = parent;
			while (!walk.empty())
			{
				m_PendingOpenDirectories.insert(walk.string());
				if (walk == assetPath)
					break;
				walk = walk.parent_path();
			}
		}
		else
		{
			// Two Column 右侧仍停留在父目录，让新建文件夹出现在内容区。
			m_SelectedDirectory = parent;
			m_TwoColumnCurrentFolder = parent;
		}
		BeginRename(newFolder);
	}

	void ContentBrowserPanel::BeginRename(const std::filesystem::path& path)
	{
		if (path.empty())
			return;
		m_RenamePath = path;
		m_RenameFocus = true;
		const std::string name = path.filename().string();
		std::fill(std::begin(m_RenameBuffer), std::end(m_RenameBuffer), '\0');
		if (name.size() < sizeof(m_RenameBuffer))
			std::copy(name.begin(), name.end(), m_RenameBuffer);
	}

	void ContentBrowserPanel::CancelRename()
	{
		m_RenamePath.clear();
		m_RenameFocus = false;
	}

	std::filesystem::path ContentBrowserPanel::CommitRename()
	{
		if (m_RenamePath.empty())
			return {};

		const std::filesystem::path oldPath = m_RenamePath;
		const std::string newName = TrimCopy(m_RenameBuffer);
		if (!IsValidEntryName(newName) || newName == oldPath.filename().string())
		{
			m_RenamePath.clear();
			m_RenameFocus = false;
			return {};
		}

		const std::filesystem::path newPath = oldPath.parent_path() / newName;
		std::error_code error;
		if (std::filesystem::exists(newPath, error))
		{
			TC_Warn("Cannot rename to '{0}' - name already exists", newPath.string());
			m_RenamePath.clear();
			m_RenameFocus = false;
			return {};
		}

		std::filesystem::rename(oldPath, newPath, error);
		if (error)
		{
			TC_Core_Error("Failed to rename {0}: {1}", oldPath.string(), error.message());
			m_RenamePath.clear();
			m_RenameFocus = false;
			return {};
		}

		// 同步选择目录、当前目录以及展开节点里记录的所有旧路径。
		if (m_SelectedDirectory == oldPath)
			m_SelectedDirectory = newPath;
		else if (IsPathInside(oldPath, m_SelectedDirectory))
			m_SelectedDirectory = RemapPath(m_SelectedDirectory, oldPath, newPath);

		if (m_TwoColumnCurrentFolder == oldPath)
			m_TwoColumnCurrentFolder = newPath;
		else if (IsPathInside(oldPath, m_TwoColumnCurrentFolder))
			m_TwoColumnCurrentFolder = RemapPath(m_TwoColumnCurrentFolder, oldPath, newPath);

		if (!m_ExpandedNodes.empty())
		{
			std::unordered_set<std::string> remapped;
			remapped.reserve(m_ExpandedNodes.size());
			for (const std::string& key : m_ExpandedNodes)
			{
				const std::filesystem::path nodePath(key);
				if (nodePath == oldPath)
					remapped.insert(newPath.string());
				else if (IsPathInside(oldPath, nodePath))
					remapped.insert(RemapPath(nodePath, oldPath, newPath).string());
				else
					remapped.insert(key);
			}
			m_ExpandedNodes.swap(remapped);
		}

		m_RenamePath.clear();
		m_RenameFocus = false;
		return newPath;
	}

	void ContentBrowserPanel::OpenAsset(const std::filesystem::path& path, bool isDirectory)
	{
		if (isDirectory)
		{
			Ref<Project> project = ProjectManager::Get().GetActiveProject();
			m_SelectedDirectory = path;
			m_TwoColumnCurrentFolder = path;
			if (project)
			{
				const std::filesystem::path assetPath = project->GetAssetPath();
				std::filesystem::path walk = path;
				while (!walk.empty())
				{
					m_ExpandedNodes.insert(walk.string());
					if (walk == assetPath)
						break;
					walk = walk.parent_path();
				}
			}
			m_UserSelectedDirectory = true;
			return;
		}

		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if ((extension == ".tomcat" || extension == ".tcproj") && m_SceneOpenCallback)
			m_SceneOpenCallback(path);
		else
			TC_Warn("Opening this file type is not supported yet: {0}", path.filename().string());
	}

	void ContentBrowserPanel::DeleteAsset(const std::filesystem::path& path, bool isDirectory)
	{
		if (path.empty())
			return;

		Ref<Project> project = ProjectManager::Get().GetActiveProject();
		if (project && path == project->GetAssetPath())
			return;

		std::error_code error;
		if (isDirectory)
			std::filesystem::remove_all(path, error);
		else
			std::filesystem::remove(path, error);

		if (error)
		{
			TC_Core_Error("Failed to delete {0}: {1}", path.string(), error.message());
			return;
		}

		m_ImageCache.erase(path.string());

		if (m_RenamePath == path)
			m_RenamePath.clear();

		// 删除后清掉所有位于该路径下的展开节点。
		if (!m_ExpandedNodes.empty())
		{
			std::unordered_set<std::string> remaining;
			for (const std::string& key : m_ExpandedNodes)
			{
				const std::filesystem::path nodePath(key);
				if (nodePath != path && !IsPathInside(path, nodePath))
					remaining.insert(key);
			}
			m_ExpandedNodes.swap(remaining);
		}

		if (m_SelectedDirectory == path || IsPathInside(path, m_SelectedDirectory))
			m_SelectedDirectory = path.parent_path();
		if (m_TwoColumnCurrentFolder == path || IsPathInside(path, m_TwoColumnCurrentFolder))
			m_TwoColumnCurrentFolder = path.parent_path();

		if (m_ContextPath == path)
			m_ContextPath.clear();
	}

	std::filesystem::path ContentBrowserPanel::DrawInlineRename(const std::filesystem::path& path)
	{
		if (path != m_RenamePath)
			return {};

		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float textX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
		const float rowHeight = itemMax.y - itemMin.y;
		const float frameHeight = ImGui::GetFrameHeight();
		const float textY = itemMin.y + std::max(0.0f, (rowHeight - frameHeight) * 0.5f);
		const float availableWidth = std::max(80.0f, itemMax.x - textX - ImGui::GetStyle().ItemSpacing.x);

		ImGui::SetCursorScreenPos(ImVec2(textX, textY));
		if (m_RenameFocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_RenameFocus = false;
		}
		ImGui::SetNextItemWidth(availableWidth);

		const bool committed = ImGui::InputText("##AssetRename", m_RenameBuffer, sizeof(m_RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (committed)
			return CommitRename();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			CancelRename();
			return {};
		}
		else if (ImGui::IsItemDeactivated())
			return CommitRename();
		return {};
	}

	void ContentBrowserPanel::DrawContextMenuBody()
	{
		if (m_ContextPath.empty())
			return;

		const std::filesystem::path target = m_ContextPath;
		const bool isDirectory = m_ContextIsDirectory;
		const bool isRoot = m_ContextIsRoot;
		const std::filesystem::path createParent = isDirectory ? target : target.parent_path();

		if (ImGui::BeginMenu("Create"))
		{
			if (ImGui::MenuItem("Folder"))
			{
				m_PendingCreateFolderParent = createParent;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Open"))
		{
			OpenAsset(target, isDirectory);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::MenuItem("Delete", nullptr, false, !isRoot))
		{
			DeleteAsset(target, isDirectory);
			ImGui::CloseCurrentPopup();
		}

		if (ImGui::MenuItem("Rename", nullptr, false, !isRoot))
		{
			BeginRename(target);
			ImGui::CloseCurrentPopup();
		}
	}

	void ContentBrowserPanel::DrawNodeContextMenu()
	{
		if (m_ContextPath.empty())
			return;
		if (ImGui::BeginPopup("ProjectNodeContext"))
		{
			DrawContextMenuBody();
			ImGui::EndPopup();
		}
	}

	void ContentBrowserPanel::DrawEmptyContextMenu(const std::filesystem::path& assetRoot)
	{
		if (ImGui::BeginPopupContextWindow("ProjectEmptyContext",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			std::error_code error;
			const bool hasFolderSelection = !m_SelectedDirectory.empty() &&
				std::filesystem::is_directory(m_SelectedDirectory, error);
			m_ContextPath = hasFolderSelection ? m_SelectedDirectory : assetRoot;
			m_ContextIsDirectory = true;
			m_ContextIsRoot = (m_ContextPath == assetRoot);
			DrawContextMenuBody();
			ImGui::EndPopup();
		}
	}

	void ContentBrowserPanel::Serialize()
	{
		// Use our project if set, otherwise fall back to the active project so the
		// layout change is never lost.
		Ref<Project> proj = m_Project ? m_Project : ProjectManager::Get().GetActiveProject();
		if (proj)
		{
			ProjectConfig config = proj->GetConfig();
			// Note: layout is an editor-level setting (see EditorSettings.tomcat), not stored in the project.
			config.TwoColumnCurrentFolder = m_TwoColumnCurrentFolder.string();
			
			config.ExpandedNodes.clear();
			for (const auto& node : m_ExpandedNodes)
			{
				config.ExpandedNodes.push_back(node);
			}
			
			proj->SetConfig(config);
			proj->Save();
		}
	}

	static std::filesystem::path GetEditorIniPath()
	{
		// Editor-level settings live in <cwd>/imgui.ini (independent of any project)
		return std::filesystem::current_path() / "imgui.ini";
	}

	void ContentBrowserPanel::LoadLayoutSetting()
	{
		std::ifstream fin(GetEditorIniPath());
		std::string line;
		bool inSection = false;
		std::string layout;
		while (std::getline(fin, line))
		{
			if (line == "[ContentBrowser]") { inSection = true; continue; }
			if (inSection)
			{
				if (line.rfind("Layout=", 0) == 0) { layout = line.substr(7); break; }
				if (line.empty() || line[0] == '[') break;
			}
		}
		g_ContentBrowserLayout = (layout == "OneColumn") ? 1 : 0;
		m_LayoutMode = g_ContentBrowserLayout ? OneColumn : TwoColumn;
	}

	void ContentBrowserPanel::SaveLayoutSetting()
	{
		g_ContentBrowserLayout = (m_LayoutMode == OneColumn) ? 1 : 0;

		// Get ImGui's window settings text and append our custom [ContentBrowser] section
		std::string ini;
		if (const char* settings = ImGui::SaveIniSettingsToMemory())
			ini = settings;

		// remove an existing [ContentBrowser] block, then append the new one
		std::string::size_type pos = ini.find("[ContentBrowser]");
		if (pos != std::string::npos)
		{
			std::string::size_type next = ini.find("\n[", pos + 1);
			ini.erase(pos, (next == std::string::npos) ? std::string::npos : next - pos);
		}
		ini += "\n[ContentBrowser]\nLayout=" + std::string(m_LayoutMode == OneColumn ? "OneColumn" : "TwoColumn") + "\n";

		std::ofstream fout(GetEditorIniPath(), std::ios::trunc);
		fout << ini;
	}

	// 原有的递归函数，用于 One Column 模式（有折叠功能）
    void ContentBrowserPanel::DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot)
    {
        std::string displayName = isRoot ? "Assets" : directoryPath.filename().string();
        std::string nodePath = directoryPath.string();

        // 只有包含内容（文件或子目录）的文件夹才显示折叠箭头；
        // 空目录按叶子节点渲染。
        bool hasChildren = false;
        try
        {
            for (auto& entry : std::filesystem::directory_iterator(directoryPath))
            {
                hasChildren = true;
                break;
            }
        }
        catch (const std::filesystem::filesystem_error&)
        {
        }

        const bool isSelected = m_UserSelectedDirectory && m_SelectedDirectory == directoryPath;
        ImGuiTreeNodeFlags nodeFlags = hasChildren
            ? (ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth)
            : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        if (isSelected)
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (hasChildren && m_ExpandedNodes.find(nodePath) != m_ExpandedNodes.end())
        {
            nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        // 新建子对象时强制展开一次父目录，让新建项直接显示出来。
        if (m_PendingOpenDirectories.erase(nodePath) > 0 && hasChildren)
            ImGui::SetNextItemOpen(true);

        const bool renaming = (directoryPath == m_RenamePath);
        if (renaming)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (isSelected)
            PushSelectedTreeColors();
        bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), nodeFlags);
        if (isSelected)
            ImGui::PopStyleColor(3);
        if (renaming)
            ImGui::PopStyleColor();
        const ImVec2 afterHeaderCursor = ImGui::GetCursorPos();

        // 处理点击事件
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_SelectedDirectory = directoryPath;
            m_UserSelectedDirectory = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            m_ContextPath = directoryPath;
            m_ContextIsDirectory = true;
            m_ContextIsRoot = isRoot;
            m_SelectedDirectory = directoryPath;
            m_UserSelectedDirectory = true;
            ImGui::OpenPopup("ProjectNodeContext");
        }

        // 记录节点打开/关闭状态
        if (ImGui::IsItemToggledOpen())
        {
            if (nodeOpen)
            {
                m_ExpandedNodes.insert(nodePath);
            }
            else
            {
                m_ExpandedNodes.erase(nodePath);
            }
        }

        const std::filesystem::path renamedPath = DrawInlineRename(directoryPath);
        const std::filesystem::path contentDirectory = renamedPath.empty() ? directoryPath : renamedPath;
        ImGui::SetCursorPos(afterHeaderCursor);

        if (hasChildren && nodeOpen)
        {
            try
            {
                for (auto& entry : std::filesystem::directory_iterator(contentDirectory))
                {
                    const auto& path = entry.path();

                    if (entry.is_directory())
                    {
                        DisplayDirectoryRecursive(path, false);
                    }
                    else
                    {
                        DisplayFileNode(path);
                    }
                }
            }
            catch (const std::filesystem::filesystem_error& e)
            {
                TC_Core_Error("Failed to read directory: {0}", e.what());
            }

            ImGui::TreePop();
        }
    }

	// 新增：用于 Two Column 左侧面板的平面显示（只显示一级，无折叠）
    void ContentBrowserPanel::DisplayDirectoryFlat(const std::filesystem::path& directoryPath)
    {
        // 检查目录是否存在且可访问
        if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
        {
            TC_Core_Error("Directory does not exist or is not accessible: {0}", directoryPath.string());
            return;
        }

        // 根节点使用 Leaf 标志，无三角标，不可展开
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool rootIsSelected = m_UserSelectedDirectory && m_SelectedDirectory == directoryPath;
        if (rootIsSelected)
            rootFlags |= ImGuiTreeNodeFlags_Selected;

        float firstLevelIndent = -30.0f; // 改为你想要的数值
        ImGui::Indent(firstLevelIndent);

        // 显示 assets 根节点
        const bool renamingRoot = (directoryPath == m_RenamePath);
        if (renamingRoot)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (rootIsSelected)
            PushSelectedTreeColors();
        ImGui::TreeNodeEx("Assets", rootFlags);
        if (rootIsSelected)
            ImGui::PopStyleColor(3);
        if (renamingRoot)
            ImGui::PopStyleColor();
        const ImVec2 afterRootCursor = ImGui::GetCursorPos();

        // 处理根目录点击
        if (ImGui::IsItemClicked())
        {
            m_SelectedDirectory = directoryPath;
            m_TwoColumnCurrentFolder = directoryPath;
            m_UserSelectedDirectory = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            m_ContextPath = directoryPath;
            m_ContextIsDirectory = true;
            m_ContextIsRoot = true;
            ImGui::OpenPopup("ProjectNodeContext");
            m_UserSelectedDirectory = true;
        }
        DrawInlineRename(directoryPath);
        ImGui::SetCursorPos(afterRootCursor);

        // 遍历 assets 下的一级项目
        try
        {
            for (auto& entry : std::filesystem::directory_iterator(directoryPath))
            {
                const auto& path = entry.path();
                std::string name = path.filename().string();
                bool isDirectory = entry.is_directory();

                // 添加缩进
                ImGui::Indent(20.0f);

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_SpanAvailWidth;

                // 检查是否被选中
                const bool childIsSelected = m_UserSelectedDirectory && m_SelectedDirectory == path;
                if (childIsSelected)
                {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                }

                // 显示项目（目录或文件）
                if (isDirectory)
                {
                    const bool renaming = (path == m_RenamePath);
                    if (renaming)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    if (childIsSelected)
                        PushSelectedTreeColors();
                    ImGui::TreeNodeEx(name.c_str(), nodeFlags);
                    if (childIsSelected)
                        ImGui::PopStyleColor(3);
                    if (renaming)
                        ImGui::PopStyleColor();
                    const ImVec2 afterHeaderCursor = ImGui::GetCursorPos();

                    // 处理点击事件 - 只允许选择目录
                    if (ImGui::IsItemClicked())
                    {
                        m_SelectedDirectory = path;
                        m_TwoColumnCurrentFolder = path;
                        m_UserSelectedDirectory = true;
                    }
                    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
                    {
                        m_ContextPath = path;
                        m_ContextIsDirectory = true;
                        m_ContextIsRoot = false;
                        m_SelectedDirectory = path;
                        m_TwoColumnCurrentFolder = path;
                        m_UserSelectedDirectory = true;
                        ImGui::OpenPopup("ProjectNodeContext");
                    }

                    DrawInlineRename(path);
                    ImGui::SetCursorPos(afterHeaderCursor);
                }
                else
                {
                    DisplayFileNode(path);
                }

                // 取消缩进
                ImGui::Unindent(20.0f);
            }
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            TC_Core_Error("Failed to read directory: {0}", e.what());
        }
    }

	// 文件节点显示函数
    void ContentBrowserPanel::DisplayFileNode(const std::filesystem::path& path)
    {
        std::string name = path.filename().string();
        ImGuiTreeNodeFlags fileNodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth;

        // 检查是否为图片文件并显示预览
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (s_ImageExtensions.find(extension) != s_ImageExtensions.end())
        {
            // 尝试从缓存中获取
            std::string filepath = path.string();
            auto it = m_ImageCache.find(filepath);
            Ref<Texture2D> icon;

            if (it != m_ImageCache.end())
            {
                icon = it->second;
            }
            else
            {
                // 加载图片并缓存
                icon = Texture2D::Create(filepath);
                m_ImageCache[filepath] = icon;
            }

            // 获取当前字体的行高
            float textHeight = ImGui::GetFontSize();
            float previewSize = textHeight;

            // 在文件名左侧显示与文字高度相同的缩略图
            ImGui::Image((ImTextureID)icon->GetRendererID(), { previewSize, previewSize }, { 0, 1 }, { 1, 0 });
            ImGui::SameLine();
        }
        else
        {
            // 对于非图片文件，在左侧显示文件图标
            float textHeight = ImGui::GetFontSize();
            float iconSize = textHeight;
            ImGui::Image((ImTextureID)m_FileIcon->GetRendererID(), { iconSize, iconSize }, { 0, 1 }, { 1, 0 });
            ImGui::SameLine();
        }

        const bool renaming = (path == m_RenamePath);
        if (renaming)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::TreeNodeEx(name.c_str(), fileNodeFlags);
        if (renaming)
            ImGui::PopStyleColor();
        const ImVec2 afterHeaderCursor = ImGui::GetCursorPos();

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        {
            m_ContextPath = path;
            m_ContextIsDirectory = false;
            m_ContextIsRoot = false;
            ImGui::OpenPopup("ProjectNodeContext");
        }

        DrawInlineRename(path);
        ImGui::SetCursorPos(afterHeaderCursor);

        if (renaming)
            return;

        // 实现拖拽功能 - 修复点
        if (ImGui::BeginDragDropSource())
        {
            // 修复：直接从 ProjectManager 获取项目，而不是使用未初始化的 m_Project
            auto project = ProjectManager::Get().GetActiveProject();
            std::filesystem::path assetPath = project ? project->GetAssetPath() : g_AssetPath;
            auto relativePath = std::filesystem::relative(path, assetPath);
            const wchar_t* itemPath = relativePath.c_str();

            std::wstring ext = relativePath.extension().wstring();

            TC_Core_Assert(!ext.empty());

            if (ext == L".tomcat" || ext == L".tcproj")
            {
                ImGui::SetDragDropPayload("TOMCAT_SCENE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                ImGui::Image((ImTextureID)m_FileIcon->GetRendererID(), { 64, 64 }, { 0, 1 }, { 1, 0 });
            }
            else if (s_ImageExtensionsW.find(ext) != s_ImageExtensionsW.end())
            {
                ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                ImGui::Image((ImTextureID)m_ImageCache[path.string()]->GetRendererID(), { 64, 64 }, { 0, 1 }, { 1, 0 });
            }

            ImGui::EndDragDropSource();
        }

        // 图片悬浮预览功能
        if (ImGui::IsItemHovered() && s_ImageExtensions.find(extension) != s_ImageExtensions.end())
        {
            ImGui::BeginTooltip();
            float previewSize = 200.0f;
            ImGui::Image((ImTextureID)m_ImageCache[path.string()]->GetRendererID(), { previewSize, previewSize }, { 0, 1 }, { 1, 0 });
            ImGui::EndTooltip();
        }
    }

    void ContentBrowserPanel::OnImGuiRender()
    {
        static bool projectWindowOpen = true;
        ImGui::Begin("Project", &projectWindowOpen, ImGuiWindowFlags_MenuBar);

        FlushPendingCreateFolder();

        // 触发弹出菜单
        if (ShowMenu)
        {
            ImGui::OpenPopup("Project_menu");
            ImGui::SetNextWindowPos(MenuPosi);
            ShowMenu = false;
        }

        // 弹出菜单
        if (ImGui::BeginPopup("Project_menu", ImGuiWindowFlags_NoMove))
        {
                        if (ImGui::MenuItem("One Column Layout"))
            {
                m_LayoutMode = OneColumn;
                SaveLayoutSetting(); // editor-level setting
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Two Column Layout"))
            {
                m_LayoutMode = TwoColumn;
                SaveLayoutSetting(); // editor-level setting
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // 左右分栏布局
        static float leftPanelWidth = 250.0f;
        static bool isResizingSplitter = false;

        // 移除所有可能的内边距
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        // 左侧树状视图
        ImGui::BeginChild("LeftPanel", m_LayoutMode == TwoColumn ? ImVec2(leftPanelWidth, 0) : ImVec2(0, 0), false);

        auto project = ProjectManager::Get().GetActiveProject();
        if (project)
        {
            std::filesystem::path assetPath = project->GetAssetPath();
            if (m_LayoutMode == OneColumn)
            {
                DisplayDirectoryRecursive(assetPath, true);
            }
            else
            {
                DisplayDirectoryFlat(assetPath);
            }

            DrawNodeContextMenu();
            DrawEmptyContextMenu(assetPath);

            // 点击树面板空白处时取消选中高亮。
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemHovered())
            {
                m_UserSelectedDirectory = false;
            }
        }
        else
        {
            ImGui::TextDisabled("No project loaded");
            ImGui::TextDisabled("Open a project to view assets");
        }

        ImGui::EndChild();

        // 右侧预览区域（Two Column 模式）
        if (m_LayoutMode == TwoColumn)
        {
            ImGui::SameLine(0, 0);

            // ========== 可拖拽分隔条 ==========
            float availableHeight = ImGui::GetContentRegionAvail().y;
            float splitterHitAreaWidth = 10.0f;

            // 避免零尺寸导致 ImGui 断言失败
            if (availableHeight <= 0.0f)
                availableHeight = 1.0f;

            ImGui::InvisibleButton("Splitter", ImVec2(splitterHitAreaWidth, availableHeight));

            if (ImGui::IsItemActive())
            {
                isResizingSplitter = true;
                float delta = ImGui::GetIO().MouseDelta.x;
                leftPanelWidth += delta;
                leftPanelWidth = std::max(100.0f, std::min(ImGui::GetWindowWidth() - 200.0f, leftPanelWidth));
            }
            else
            {
                isResizingSplitter = false;
            }

            if (ImGui::IsItemHovered() || isResizingSplitter)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }

            ImVec2 buttonPos = ImGui::GetItemRectMin();
            ImVec2 buttonSize = ImGui::GetItemRectSize();

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float lineCenterX = buttonPos.x + (splitterHitAreaWidth * 0.5f);
            ImVec2 lineStart = ImVec2(lineCenterX, buttonPos.y);
            ImVec2 lineEnd = ImVec2(lineCenterX, buttonPos.y + buttonSize.y);

            ImU32 lineColor;
            if (isResizingSplitter)
                lineColor = IM_COL32(44, 93, 135, 255);      // Unity selection blue
            else if (ImGui::IsItemHovered())
                lineColor = IM_COL32(98, 98, 98, 255);       // #626262
            else
                lineColor = IM_COL32(25, 25, 25, 255);       // #191919

            drawList->AddLine(lineStart, lineEnd, lineColor, 2.0f);
            drawList->AddLine(ImVec2(lineCenterX - 1, lineStart.y),
                ImVec2(lineCenterX - 1, lineEnd.y),
                IM_COL32(25, 25, 25, 100), 1.0f);
            drawList->AddLine(ImVec2(lineCenterX + 1, lineStart.y),
                ImVec2(lineCenterX + 1, lineEnd.y),
                IM_COL32(137, 137, 137, 100), 1.0f);

            ImGui::SameLine(0, 0);
            // ========== 分隔条结束 ==========

            // RightPanel
            ImGui::BeginChild("RightPanel", ImVec2(0, 0), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            auto rightPanelProject = ProjectManager::Get().GetActiveProject();
            if (rightPanelProject)
            {
                std::filesystem::path assetPath = rightPanelProject->GetAssetPath();
                std::filesystem::path displayPath = m_SelectedDirectory.empty() ? assetPath : m_SelectedDirectory;

                // ========== 面包屑导航栏 ==========
                // 设置面包屑样式
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));

                // 计算面包屑栏高度
                float breadcrumbHeight = ImGui::GetFrameHeightWithSpacing();

                // 获取 RightPanel 的完整尺寸
                ImVec2 rightPanelPos = ImGui::GetWindowPos();
                ImVec2 rightPanelSize = ImGui::GetWindowSize();

                // 获取当前光标位置
                ImVec2 cursorPos = ImGui::GetCursorPos();

                // 计算绝对位置
                ImVec2 breadcrumbAbsPos = ImVec2(rightPanelPos.x, rightPanelPos.y + cursorPos.y);

                // 绘制面包屑背景
                ImDrawList* drawListBg = ImGui::GetWindowDrawList();
                ImRect breadcrumbRect(
                    breadcrumbAbsPos,
                    ImVec2(rightPanelPos.x + rightPanelSize.x, breadcrumbAbsPos.y + breadcrumbHeight)
                );
                drawListBg->AddRectFilled(breadcrumbRect.Min, breadcrumbRect.Max, IM_COL32(60, 60, 60, 255));

                // 创建面包屑内容区域
                ImGui::BeginChild("BreadcrumbContent", ImVec2(-FLT_MIN, breadcrumbHeight), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                    ImGuiWindowFlags_NoBackground);

                // 设置按钮样式 - 完全透明，无悬浮效果
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));  // 悬浮时也透明
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));   // 点击时也透明
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(196.0f / 255.0f, 196.0f / 255.0f, 196.0f / 255.0f, 1.0f));

                // 获取相对于 assets 的路径
                std::filesystem::path relativePath;
                if (displayPath == assetPath)
                {
                    relativePath = "";
                }
                else
                {
                    relativePath = std::filesystem::relative(displayPath, assetPath);
                }

                // 构建面包屑路径
                std::vector<std::string> breadcrumbs;
                std::filesystem::path currentPath;

                if (!relativePath.empty())
                {
                    for (const auto& part : relativePath)
                    {
                        currentPath /= part;
                        breadcrumbs.push_back(part.string());
                    }
                }

                // 垂直居中
                float buttonHeight = ImGui::GetFrameHeight();
                float centerOffset = (breadcrumbHeight - buttonHeight) * 0.5f;
                ImGui::SetCursorPosY(centerOffset);

                // 左对齐 - 从左边距开始
                ImGui::SetCursorPosX(8.0f);

                // 始终显示 "assets" 根目录
                if (ImGui::Button("Assets"))
                {
                    m_SelectedDirectory = assetPath;
                    m_UserSelectedDirectory = true;
                }

                // 显示路径分隔符和子目录
                std::filesystem::path accumulatedPath = assetPath;

                for (size_t i = 0; i < breadcrumbs.size(); ++i)
                {
                    ImGui::SameLine(0, 4);
                    ImGui::TextDisabled(">");

                    ImGui::SameLine(0, 4);
                    accumulatedPath /= breadcrumbs[i];

                    if (ImGui::Button(breadcrumbs[i].c_str()))
                    {
                        m_SelectedDirectory = accumulatedPath;
                        m_UserSelectedDirectory = true;
                    }
                }

                ImGui::PopStyleColor(4);
                ImGui::EndChild();

                // 弹出面包屑样式
                ImGui::PopStyleVar(2);

                // 关键：将光标位置设置到面包屑下方，不留空隙
                ImGui::SetCursorPosY(ImGui::GetCursorPosY());

                // ========== 内容显示区域 ==========
                static float padding = 16.0f;
                static float thumbnailSize = 128.0f;
                float cellSize = thumbnailSize + padding;

                // 获取内容区域的实际宽度
                float panelWidth = ImGui::GetContentRegionAvail().x;
                int columnCount = (int)(panelWidth / cellSize);
                if (columnCount < 1)
                    columnCount = 1;

                // 内容区域样式 - 移除顶部内边距
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);

                // 内容区域 - 从当前光标位置开始，紧贴面包屑
                ImGui::BeginChild("ContentArea", ImVec2(0, 0), false);

                ImGui::Columns(columnCount, 0, false);

                // ========== 路径回退逻辑 ==========
                std::filesystem::path actualDisplayPath = displayPath;
                bool useVirtualPath = false;

                // 检查物理目录是否存在
                if (!std::filesystem::exists(actualDisplayPath) || !std::filesystem::is_directory(actualDisplayPath))
                {
                    // 物理路径不存在，尝试使用相对于 exe 的虚拟路径
                    std::filesystem::path virtualPath = std::filesystem::current_path() / "Packages" / "TetxProject" / "Assets";

                    if (std::filesystem::exists(virtualPath) && std::filesystem::is_directory(virtualPath))
                    {
                        actualDisplayPath = virtualPath;
                        useVirtualPath = true;
                        TC_Core_Info("Using virtual path: {0}", actualDisplayPath.string());
                    }
                    else
                    {
                        // 如果虚拟路径也不存在，尝试使用 m_CurrentDirectory
                        if (std::filesystem::exists(m_CurrentDirectory) && std::filesystem::is_directory(m_CurrentDirectory))
                        {
                            actualDisplayPath = m_CurrentDirectory;
                            useVirtualPath = true;
                            TC_Core_Info("Using m_CurrentDirectory: {0}", actualDisplayPath.string());
                        }
                        else
                        {
                            TC_Core_Error("Display path does not exist or is not accessible: {0}", displayPath.string());
                            ImGui::TextDisabled("Cannot access assets directory");
                            ImGui::EndChild();
                            ImGui::PopStyleVar(3);
                            ImGui::EndChild();  // RightPanel
                            ImGui::PopStyleVar(3);  // 最外层样式
                            ImGui::End();  // Window
                            return;
                        }
                    }
                }

                try
                {
                    for (auto& directoryEntry : std::filesystem::directory_iterator(actualDisplayPath))
                    {
                        const auto& path = directoryEntry.path();

                        // 计算相对路径时，如果使用了虚拟路径，需要特殊处理
                        std::filesystem::path relativePathItem;
                        if (useVirtualPath)
                        {
                            // 如果使用虚拟路径，尝试从虚拟路径中提取相对部分
                            std::filesystem::path virtualBase = std::filesystem::current_path() / "Packages" / "TetxProject" / "Assets";
                            if (path.string().find(virtualBase.string()) == 0)
                            {
                                relativePathItem = std::filesystem::relative(path, virtualBase);
                            }
                            else
                            {
                                relativePathItem = path.filename();
                            }
                        }
                        else
                        {
                            relativePathItem = std::filesystem::relative(path, assetPath);
                        }

                        std::string filenameString = relativePathItem.filename().string();

                        ImGui::PushID(filenameString.c_str());
                        Ref<Texture2D> icon;

                        if (directoryEntry.is_directory())
                        {
                            icon = m_DirectoryIcon;
                        }
                        else
                        {
                            std::string extension = path.extension().string();
                            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                            if (s_ImageExtensions.find(extension) != s_ImageExtensions.end())
                            {
                                std::string filepath = path.string();
                                auto it = m_ImageCache.find(filepath);
                                if (it != m_ImageCache.end())
                                {
                                    icon = it->second;
                                }
                                else
                                {
                                    icon = Texture2D::Create(filepath);
                                    m_ImageCache[filepath] = icon;
                                }
                            }
                            else
                            {
                                icon = m_FileIcon;
                            }
                        }

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
                        ImGui::ImageButton((ImTextureID)icon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });

                        if (ImGui::BeginDragDropSource())
                        {
                            const wchar_t* itemPath = relativePathItem.c_str();
                            std::wstring extension = relativePathItem.extension().wstring();
                            TC_Core_Assert(!extension.empty());

                            if (extension == L".tomcat" || extension == L".tcproj")
                            {
                                ImGui::SetDragDropPayload("TOMCAT_SCENE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                                ImGui::Image((ImTextureID)m_FileIcon->GetRendererID(), { 64, 64 }, { 0, 1 }, { 1, 0 });
                            }
                            else if (s_ImageExtensionsW.find(extension) != s_ImageExtensionsW.end())
                            {
                                ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                                ImGui::Image((ImTextureID)icon->GetRendererID(), { 64, 64 }, { 0, 1 }, { 1, 0 });
                            }

                            ImGui::EndDragDropSource();
                        }

                        ImGui::PopStyleColor(3);

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            if (directoryEntry.is_directory())
                            {
                                m_SelectedDirectory = path;
                                m_UserSelectedDirectory = true;
                            }
                        }

                        ImGui::TextWrapped(filenameString.c_str());
                        ImGui::NextColumn();
                        ImGui::PopID();
                    }
                }
                catch (const std::filesystem::filesystem_error& e)
                {
                    TC_Core_Error("Failed to read display directory: {0}", e.what());
                    ImGui::TextDisabled("Error reading directory: %s", e.what());
                }

                ImGui::Columns(1);

                if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl)
                {
                    float scrollDelta = ImGui::GetIO().MouseWheel;
                    if (scrollDelta != 0)
                    {
                        thumbnailSize -= scrollDelta * 8.0f;
                        thumbnailSize = std::max(128.0f, std::min(512.0f, thumbnailSize));
                        float oldRatio = padding / thumbnailSize;
                        padding = thumbnailSize * oldRatio;
                        padding = std::max(0.0f, std::min(32.0f, padding));
                    }
                }

                ImGui::EndChild();  // ContentArea
                ImGui::PopStyleVar(3);  // 弹出 ContentArea 的样式
            }
            else
            {
                ImGui::TextDisabled("No project loaded");
                ImGui::TextDisabled("Open a project to view assets");
            }

            ImGui::EndChild();  // RightPanel
        }

        ImGui::PopStyleVar(3);  // 弹出最外层的样式
        ImGui::End();
    }

}
