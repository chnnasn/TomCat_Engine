#include"tcpch.h"

#include "ContentBrowserPanel.h"

#include <imgui/imgui.h>
#include <unordered_map>

#include "TomCat/ImGui/ImGuiCallback.h"

namespace TomCat {

	// Once we have projects, change this
	extern const std::filesystem::path g_AssetPath = "assets";

	// 常见的图片文件扩展名
	static const std::unordered_set<std::string> s_ImageExtensions = {
		".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".webp", ".psd", ".hdr", ".pic"
	};

	static const std::unordered_set<std::wstring> s_ImageExtensionsW = {
	L".png", L".jpg", L".jpeg", L".bmp", L".tga", L".gif", L".webp", L".psd", L".hdr", L".pic"
	};

	bool ShowMenu = false;

	ImVec2 MenuPosi;

	ContentBrowserPanel::ContentBrowserPanel()
		: m_CurrentDirectory(g_AssetPath), m_LayoutMode(TwoColumn)
	{
		m_DirectoryIcon = Texture2D::Create("Resources/Icons/ContentBrowser/DirectoryIcon.png");
		m_FileIcon = Texture2D::Create("Resources/Icons/ContentBrowser/FileIcon.png");

		TomCat::RegisterWindowMoreOptionsCallback("Project", [](ImVec2 pos) {

			MenuPosi = ImVec2{pos.x,pos.y+40};
			
			ShowMenu = !ShowMenu;

			});
	}

	// 递归函数，用于显示多级目录结构
	void ContentBrowserPanel::DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot)
	{
		std::string displayName = isRoot ? "assets" : directoryPath.filename().string();
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
				// 遍历目录下的所有文件和子目录
				for (auto& entry : std::filesystem::directory_iterator(directoryPath))
				{
					const auto& path = entry.path();
					std::string name = path.filename().string();

					if (entry.is_directory())
					{
						// 递归显示子目录
						DisplayDirectoryRecursive(path, false);
					}
					else
					{
						// 显示文件
						ImGuiTreeNodeFlags fileNodeFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
						ImGui::AlignTextToFramePadding();

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

						// 处理文件点击事件
						if (ImGui::IsItemClicked())
						{
							m_SelectedDirectory = path;
						}

						// 实现拖拽功能
						if (ImGui::BeginDragDropSource())
						{
							auto relativePath = std::filesystem::relative(path, g_AssetPath);
							const wchar_t* itemPath = relativePath.c_str();

							std::wstring extension = relativePath.extension().wstring();

							TC_Core_Assert(extension);

							if (extension == L".tomcat")
							{
								ImGui::SetDragDropPayload("TOMCAT_SCENE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
							}
							else if (s_ImageExtensionsW.find(extension) != s_ImageExtensionsW.end())
							{
								ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
							}

							ImGui::EndDragDropSource();
						}

						// 图片悬浮预览功能 - 当鼠标悬停在图片文件上时显示更大的预览
						if (ImGui::IsItemHovered() && s_ImageExtensions.find(extension) != s_ImageExtensions.end())
						{
							ImGui::BeginTooltip();
							float previewSize = 200.0f;
							ImGui::Image((ImTextureID)m_ImageCache[path.string()]->GetRendererID(), { previewSize, previewSize }, { 0, 1 }, { 1, 0 });
							ImGui::EndTooltip();
						}
					}
				}
			}
			catch (const std::filesystem::filesystem_error& e)
			{
				// 处理可能的文件系统错误
				TC_Core_Error("Failed to read directory: {0}", e.what());
			}

			ImGui::TreePop();
		}
	}

	void ContentBrowserPanel::OnImGuiRender()
	{

		static bool projectWindowOpen = true;

		ImGui::Begin("Project",&projectWindowOpen, ImGuiWindowFlags_MenuBar);


		// 触发弹出菜单
		if (ShowMenu)
		{
		    ImGui::OpenPopup("Project_menu");
		    // 设置固定位置（例如在鼠标位置显示）
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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		// 左侧树状视图
		ImGui::BeginChild("LeftPanel", m_LayoutMode == TwoColumn ? ImVec2(leftPanelWidth, 0) : ImVec2(0, 0), true);

		if (m_LayoutMode == OneColumn)
		{
			// 单列模式下使用递归显示完整目录树
			DisplayDirectoryRecursive(g_AssetPath, true);
		}
		else
		{
			// 双列模式下保持原有的一级目录显示
			// 绘制根目录
			if (ImGui::TreeNodeEx("assets", ImGuiTreeNodeFlags_DefaultOpen))
			{
				// 遍历assets目录下的一级文件和目录
				for (auto& directoryEntry : std::filesystem::directory_iterator(g_AssetPath))
				{
					const auto& path = directoryEntry.path();
					std::string name = path.filename().string();

					// 判断是否是目录
					bool isDirectory = directoryEntry.is_directory();
					ImGuiTreeNodeFlags nodeFlags = isDirectory ? ImGuiTreeNodeFlags_OpenOnArrow : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

					// 如果当前选中的是这个目录，高亮显示
					if (m_SelectedDirectory == path)
					{
						nodeFlags |= ImGuiTreeNodeFlags_Selected;
					}

					bool nodeOpen = ImGui::TreeNodeEx(name.c_str(), nodeFlags);

					// 处理点击事件
					if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
					{
						m_SelectedDirectory = path;
						// 如果是文件，设置当前目录为其父目录
						if (!isDirectory)
						{
							m_CurrentDirectory = path.parent_path();
						}
					}

					// 如果是目录并且被展开，递归显示子目录（这里我们只显示一级，所以不需要递归）
					if (nodeOpen && isDirectory)
					{
						ImGui::TreePop();
					}
				}

				ImGui::TreePop();
			}
		}

		ImGui::EndChild();

		// 右侧预览区域
		if (m_LayoutMode == TwoColumn)
		{
			ImGui::SameLine();
			ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);

			// 如果选中了目录，显示其内容；否则显示根目录内容
			std::filesystem::path displayPath = m_SelectedDirectory.empty() ? g_AssetPath : m_SelectedDirectory;

			// 显示当前目录路径
			if (displayPath != std::filesystem::path(g_AssetPath))
			{
				if (ImGui::Button("<-"))
				{
					// 如果当前显示的是一级目录，则返回到根目录
					if (displayPath.parent_path() == g_AssetPath)
					{
						m_SelectedDirectory.clear();
					}
					else
					{
						m_SelectedDirectory = displayPath.parent_path();
					}
				}
			}

			static float padding = 16.0f;
			static float thumbnailSize = 128.0f;
			float cellSize = thumbnailSize + padding;

			float panelWidth = ImGui::GetContentRegionAvail().x;
			int columnCount = (int)(panelWidth / cellSize);
			if (columnCount < 1)
				columnCount = 1;

			ImGui::Columns(columnCount, 0, false);

			// 遍历当前选中目录的内容
			for (auto& directoryEntry : std::filesystem::directory_iterator(displayPath))
			{
				const auto& path = directoryEntry.path();
				auto relativePath = std::filesystem::relative(path, g_AssetPath);
				std::string filenameString = relativePath.filename().string(); 

				ImGui::PushID(filenameString.c_str());
				Ref<Texture2D> icon;

				if (directoryEntry.is_directory())
				{
					icon = m_DirectoryIcon;
				}
				else
				{
					// 检查是否为图片文件
					std::string extension = path.extension().string();
					std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
					
					if (s_ImageExtensions.find(extension) != s_ImageExtensions.end())
					{
						// 尝试从缓存中获取
						std::string filepath = path.string();
						auto it = m_ImageCache.find(filepath);
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
					}
					else
					{
						icon = m_FileIcon;
					}
				}

				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
				ImGui::ImageButton((ImTextureID)icon->GetRendererID(), { thumbnailSize, thumbnailSize }, { 0, 1 }, { 1, 0 });

				if (ImGui::BeginDragDropSource())
				{
					const wchar_t* itemPath = relativePath.c_str();

					std::wstring extension = relativePath.extension().wstring();

					TC_Core_Assert(extension);

					if (extension == L".tomcat")
					{
						ImGui::SetDragDropPayload("TOMCAT_SCENE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
					}
					else if (s_ImageExtensionsW.find(extension) != s_ImageExtensionsW.end())
					{
						ImGui::SetDragDropPayload("SPRITE", itemPath, (wcslen(itemPath) + 1) * sizeof(wchar_t));
					}

					ImGui::EndDragDropSource();
				}

				ImGui::PopStyleColor();

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

			ImGui::Columns(1);

			// 添加Ctrl+鼠标滚轮调整thumbnailSize和padding的功能
			if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) 
			{
				float scrollDelta = ImGui::GetIO().MouseWheel;
				if (scrollDelta != 0) 
				{
					// 调整缩略图大小
					thumbnailSize -= scrollDelta * 8.0f; // 调整步长
					thumbnailSize = std::max(128.0f, std::min(512.0f, thumbnailSize));
					
					// 调整padding（与缩略图大小成比例调整）
					float oldRatio = padding / thumbnailSize;
					padding = thumbnailSize * oldRatio;
					padding = std::max(0.0f, std::min(32.0f, padding));
				}
			}

			ImGui::EndChild();
		}

		// 窗口大小调整时调整左侧面板宽度
		if (m_LayoutMode == TwoColumn)
		{
			static bool isResizing = false;
			if (isResizing)
			{
				// 正在调整大小时，根据鼠标移动调整面板宽度
				leftPanelWidth += ImGui::GetIO().MouseDelta.x;
				leftPanelWidth = std::max(100.0f, std::min(ImGui::GetWindowWidth() - 200.0f, leftPanelWidth));
				
				// 释放鼠标时结束调整
				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					isResizing = false;
				}
			}
			else if (ImGui::GetIO().KeyShift && ImGui::IsMouseHoveringRect(ImVec2(leftPanelWidth - 5, 0), ImVec2(leftPanelWidth + 5, ImGui::GetWindowHeight())))
			{
				// 鼠标悬停在分隔线上且按下Shift键，显示调整大小光标
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					isResizing = true;
				}
			}
		}

		ImGui::PopStyleVar();

		// TODO: status bar
		ImGui::End();
}

}