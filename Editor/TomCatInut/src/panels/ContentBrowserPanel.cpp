#include"tcpch.h"

#include "ContentBrowserPanel.h"

#include <imgui/imgui.h>
#include <unordered_map>

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

	ContentBrowserPanel::ContentBrowserPanel()
		: m_LayoutMode(TwoColumn)
	{
		auto project = ProjectManager::Get().GetActiveProject();
		if (project)
		{
            m_CurrentDirectory = project->GetAssetPath();
		}
        else
        {
            m_CurrentDirectory = g_AssetPath;
        }
		
		m_DirectoryIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = Texture2D::Create("Packages/Resources/Icons/ContentBrowser/FileIcon.png");

		TomCat::RegisterWindowMoreOptionsCallback("Project", [](ImVec2 pos) {

			MenuPosi = ImVec2{pos.x,pos.y+40};
			
			ShowMenu = !ShowMenu;

			});
	}

	// 原有的递归函数，用于 One Column 模式（有折叠功能）
    void ContentBrowserPanel::DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot)
    {
        std::string displayName = isRoot ? "Assets" : directoryPath.filename().string();
        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow;

        // 检查当前是否选中该目录
        if (m_SelectedDirectory == directoryPath)
        {
            nodeFlags |= ImGuiTreeNodeFlags_Selected;
        }

        bool nodeOpen = ImGui::TreeNodeEx(displayName.c_str(), nodeFlags);

        // 处理点击事件
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            m_SelectedDirectory = directoryPath;
        }

        if (nodeOpen)
        {
            try
            {
                // 添加调试输出
                TC_Core_Info("Scanning directory: {0}", directoryPath.string());
                int itemCount = 0;

                for (auto& entry : std::filesystem::directory_iterator(directoryPath))
                {
                    const auto& path = entry.path();
                    itemCount++;

                    // 调试输出每个找到的项目
                    TC_Core_Info("Found item: {0} (is_directory: {1})", path.string(), entry.is_directory());

                    if (entry.is_directory())
                    {
                        DisplayDirectoryRecursive(path, false);
                    }
                    else
                    {
                        DisplayFileNode(path);
                    }
                }

                TC_Core_Info("Total items found: {0}", itemCount);
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
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // 检查根目录是否被选中
        if (m_SelectedDirectory == directoryPath)
        {
            rootFlags |= ImGuiTreeNodeFlags_Selected;
        }

        float firstLevelIndent = -30.0f; // 改为你想要的数值
        ImGui::Indent(firstLevelIndent);

        // 显示 assets 根节点
        ImGui::TreeNodeEx("Assets", rootFlags);

        // 处理根目录点击
        if (ImGui::IsItemClicked())
        {
            m_SelectedDirectory = directoryPath;
        }

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

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

                // 检查是否被选中
                if (m_SelectedDirectory == path)
                {
                    nodeFlags |= ImGuiTreeNodeFlags_Selected;
                }

                // 显示项目（目录或文件）
                if (isDirectory)
                {
                    ImGui::TreeNodeEx(name.c_str(), nodeFlags);

                    // 处理点击事件 - 只允许选择目录
                    if (ImGui::IsItemClicked())
                    {
                        m_SelectedDirectory = path;
                    }
                }
                else
                {
                    // 文件：显示文件图标或图片预览
                    std::string extension = path.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                    if (s_ImageExtensions.find(extension) != s_ImageExtensions.end())
                    {
                        // 图片文件显示缩略图
                        std::string filepath = path.string();
                        auto it = m_ImageCache.find(filepath);
                        Ref<Texture2D> icon;

                        if (it != m_ImageCache.end())
                        {
                            icon = it->second;
                        }
                        else
                        {
                            icon = Texture2D::Create(filepath);
                            m_ImageCache[filepath] = icon;
                        }

                        float textHeight = ImGui::GetFontSize();
                        float previewSize = textHeight;
                        ImGui::Image((ImTextureID)icon->GetRendererID(), { previewSize, previewSize }, { 0, 1 }, { 1, 0 });
                        ImGui::SameLine();
                    }
                    else
                    {
                        // 普通文件显示文件图标
                        float textHeight = ImGui::GetFontSize();
                        float iconSize = textHeight;
                        ImGui::Image((ImTextureID)m_FileIcon->GetRendererID(), { iconSize, iconSize }, { 0, 1 }, { 1, 0 });
                        ImGui::SameLine();
                    }

                    ImGui::TreeNodeEx(name.c_str(), nodeFlags);

                    // 处理点击事件 - 文件不更新 m_SelectedDirectory
                    // 移除了文件点击时更新 m_SelectedDirectory 的代码
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
        ImGuiTreeNodeFlags fileNodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

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

        ImGui::TreeNodeEx(name.c_str(), fileNodeFlags);

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
            }
            else if (s_ImageExtensionsW.find(ext) != s_ImageExtensionsW.end())
            {
                ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
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
            }

            if (ImGui::MenuItem("Two Column Layout"))
            {
                m_LayoutMode = TwoColumn;
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
                lineColor = IM_COL32(100, 150, 255, 255);
            else if (ImGui::IsItemHovered())
                lineColor = IM_COL32(150, 150, 150, 255);
            else
                lineColor = IM_COL32(80, 80, 80, 255);

            drawList->AddLine(lineStart, lineEnd, lineColor, 2.0f);
            drawList->AddLine(ImVec2(lineCenterX - 1, lineStart.y),
                ImVec2(lineCenterX - 1, lineEnd.y),
                IM_COL32(40, 40, 40, 100), 1.0f);
            drawList->AddLine(ImVec2(lineCenterX + 1, lineStart.y),
                ImVec2(lineCenterX + 1, lineEnd.y),
                IM_COL32(120, 120, 120, 100), 1.0f);

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
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
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
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

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
                            }
                            else if (s_ImageExtensionsW.find(extension) != s_ImageExtensionsW.end())
                            {
                                ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
                            }

                            ImGui::EndDragDropSource();
                        }

                        ImGui::PopStyleColor(3);

                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            if (directoryEntry.is_directory())
                            {
                                m_SelectedDirectory = path;
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