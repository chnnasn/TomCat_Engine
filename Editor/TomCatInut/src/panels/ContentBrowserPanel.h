#pragma once

#include <filesystem>
#include <functional>
#include <unordered_map>
#include <unordered_set>
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
		// Saves non-layout navigation state to project-local UserSettings/editor.json.
		bool Serialize();

		using SceneOpenCallback = std::function<void(const std::filesystem::path&)>;
		using AssetRenamedCallback = std::function<void(const std::filesystem::path&, const std::filesystem::path&)>;
		using AssetDeletedCallback = std::function<void(const std::filesystem::path&)>;
		void SetSceneOpenCallback(SceneOpenCallback callback) { m_SceneOpenCallback = std::move(callback); }
		void SetAssetRenamedCallback(AssetRenamedCallback callback) { m_AssetRenamedCallback = std::move(callback); }
		void SetAssetDeletedCallback(AssetDeletedCallback callback) { m_AssetDeletedCallback = std::move(callback); }

		// Layout uses LocalAppData without a project, otherwise project UserSettings/imgui.ini.
		void LoadLayoutSetting();
		void SaveLayoutSetting();

		void OnImGuiRender(bool* open = nullptr);
	private:
		std::filesystem::path m_CurrentDirectory;
		std::filesystem::path m_SelectedPath;
		// 只有用户真正点击/右键选中的目录才显示蓝色选中态，
		// 避免项目刚打开时 Assets 只是作为默认当前目录而被高亮。
		bool m_UserSelectedDirectory = false;

		Ref<Texture2D> m_DirectoryIcon;
		Ref<Texture2D> m_FileIcon;

		std::unordered_map<std::string, Ref<Texture2D>> m_ImageCache;

		LayoutMode m_LayoutMode;
		Ref<Project> m_Project;
		bool m_ProjectStateWritable = true;

		// 存储树节点的打开状态
		std::unordered_set<std::string> m_ExpandedNodes;
		// 创建子文件夹后需要强制展开的一次性路径。
		std::unordered_set<std::string> m_PendingOpenDirectories;
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
		bool m_OpenRenamePopup = false;

		std::filesystem::path m_DeletePath;
		bool m_DeleteIsDirectory = false;
		bool m_OpenDeletePopup = false;
		float m_LeftPanelWidth = 250.0f;
		float m_ThumbnailSize = 128.0f;

		SceneOpenCallback m_SceneOpenCallback;
		AssetRenamedCallback m_AssetRenamedCallback;
		AssetDeletedCallback m_AssetDeletedCallback;

		std::filesystem::path GetAssetRoot() const;
		void RestoreProjectState();
		void DrawDirectoryTree(const std::filesystem::path& directoryPath, bool isRoot, bool includeFiles);
		void DrawFileTreeNode(const std::filesystem::path& path);
		void DrawBreadcrumbs(const std::filesystem::path& assetRoot);
		void DrawAssetGrid(const std::filesystem::path& assetRoot);
		void DrawAssetItem(const std::filesystem::directory_entry& entry, const std::filesystem::path& assetRoot);
		Ref<Texture2D> GetAssetIcon(const std::filesystem::path& path, bool isDirectory);
		void SubmitDragPayload(const std::filesystem::path& path, const std::filesystem::path& assetRoot, const Ref<Texture2D>& icon);

		void FlushPendingCreateFolder();
		void DrawNodeContextMenu();
		void DrawContextMenuBody();
		void DrawEmptyContextMenu(const std::filesystem::path& assetRoot);
		void DrawRenamePopup();
		void DrawDeleteConfirmation();

		void OpenAsset(const std::filesystem::path& path, bool isDirectory);
		void RequestDeleteAsset(const std::filesystem::path& path, bool isDirectory);
		void DeleteAsset(const std::filesystem::path& path, bool isDirectory);
		void EraseCachedImagesUnder(const std::filesystem::path& path);
		void BeginRename(const std::filesystem::path& path);
		std::filesystem::path CommitRename();
		void CancelRename();
	};
}

