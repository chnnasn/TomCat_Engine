#pragma once

#include <filesystem>
#include <functional>
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Project/Project.h"

namespace TomCat {

	class ContentBrowserPanel
	{
	public:
		enum LayoutMode
		{
			OneColumn,
			TwoColumn
		};

		ContentBrowserPanel();
		void SetProject(Ref<Project> project);
		void Serialize();

		using SceneOpenCallback = std::function<void(const std::filesystem::path&)>;
		void SetSceneOpenCallback(SceneOpenCallback callback) { m_SceneOpenCallback = std::move(callback); }

		// Editor-level layout setting (persisted inside imgui.ini)
		void LoadLayoutSetting();
		void SaveLayoutSetting();

		void OnImGuiRender();
	private:
		std::filesystem::path m_CurrentDirectory;
		std::filesystem::path m_SelectedDirectory;
		// 只有用户真正点击/右键选中的目录才显示蓝色选中态，
		// 避免项目刚打开时 Assets 只是作为默认当前目录而被高亮。
		bool m_UserSelectedDirectory = false;

		Ref<Texture2D> m_DirectoryIcon;
		Ref<Texture2D> m_FileIcon;

		std::unordered_map<std::string, Ref<Texture2D>> m_ImageCache;

		LayoutMode m_LayoutMode;
		Ref<Project> m_Project;

		// 存储树节点的打开状态
		std::unordered_set<std::string> m_ExpandedNodes;
		// 创建子文件夹后需要强制展开的一次性路径。
		std::unordered_set<std::string> m_PendingOpenDirectories;
		// 存储Two Column模式下当前打开的文件夹
		std::filesystem::path m_TwoColumnCurrentFolder;

		// 右键菜单目标
		std::filesystem::path m_ContextPath;
		bool m_ContextIsDirectory = false;
		bool m_ContextIsRoot = false;

		// 等待下一帧创建的文件夹（从右键菜单里创建后立即进入重命名）
		std::filesystem::path m_PendingCreateFolderParent;

		// 行内重命名状态
		std::filesystem::path m_RenamePath;
		char m_RenameBuffer[512] = {};
		bool m_RenameFocus = false;

		SceneOpenCallback m_SceneOpenCallback;

		void DisplayFileNode(const std::filesystem::path& path);
		void DisplayDirectoryRecursive(const std::filesystem::path& directoryPath, bool isRoot);
		void DisplayDirectoryFlat(const std::filesystem::path& directoryPath);

		void FlushPendingCreateFolder();
		std::filesystem::path DrawInlineRename(const std::filesystem::path& path);
		void DrawNodeContextMenu();
		void DrawContextMenuBody();
		void DrawEmptyContextMenu(const std::filesystem::path& assetRoot);

		void OpenAsset(const std::filesystem::path& path, bool isDirectory);
		void DeleteAsset(const std::filesystem::path& path, bool isDirectory);
		void BeginRename(const std::filesystem::path& path);
		std::filesystem::path CommitRename();
		void CancelRename();
	};
}

