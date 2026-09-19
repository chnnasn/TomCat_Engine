#pragma once

#include <filesystem>
#include <array>
#include <functional>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "TomCat/Asset/Asset.h"
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Project/Project.h"
#include "../EditorIcons.h"

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
		~ContentBrowserPanel();
		void SetProject(Ref<Project> project);
		// Embedded hosts may own persistence/import and expose browsing only.
		void SetAssetMutationsEnabled(bool enabled) { m_AssetMutationsEnabled = enabled; }
		void SetIcons(const Ref<EditorIconSet>& icons) { m_Icons = icons; }
		void SetActiveScenePath(const std::filesystem::path& path);
		// Saves non-layout navigation state to project-local UserSettings/editor.json.
		bool Serialize();

		using SceneOpenCallback = std::function<void(AssetHandle)>;
		using AuthoringAssetOpenCallback = std::function<void(AssetHandle, AssetType)>;
		using AssetRenamedCallback = std::function<bool(const std::filesystem::path&, const std::filesystem::path&)>;
		using AssetDeletedCallback = std::function<void(const std::filesystem::path&)>;
		using EntityPrefabCreateCallback =
			std::function<bool(UUID, const std::filesystem::path&)>;
		void SetSceneOpenCallback(SceneOpenCallback callback) { m_SceneOpenCallback = std::move(callback); }
		void SetAuthoringAssetOpenCallback(AuthoringAssetOpenCallback callback)
		{
			m_AuthoringAssetOpenCallback = std::move(callback);
		}
		void SetAssetRenamedCallback(AssetRenamedCallback callback) { m_AssetRenamedCallback = std::move(callback); }
		void SetAssetDeletedCallback(AssetDeletedCallback callback) { m_AssetDeletedCallback = std::move(callback); }
		void SetEntityPrefabCreateCallback(EntityPrefabCreateCallback callback)
		{
			m_EntityPrefabCreateCallback = std::move(callback);
		}
		std::filesystem::path GetWritableCreationDirectory() const;
		void RevealAsset(const std::filesystem::path& path);

		// Layout uses LocalAppData without a project, otherwise project UserSettings/imgui.ini.
		void LoadLayoutSetting();
		void SaveLayoutSetting();

		// Draws the top-level Assets menu using the same commands and target
		// semantics as the Project panel context menus. The caller owns BeginMenu.
		void DrawAssetsMenu();
		bool CanOpenSpriteAtlasTools() const;
		bool OpenSpriteAtlasToolsForSelection();
		bool IsFocused() const { return m_Focused; }
		bool IsDocked() const { return m_Docked; }
        void OnAssetInspectorRender(bool* open);
        bool OpenDiagnosticSource(const std::filesystem::path& path) { return path.extension()==".cs" && OpenCSharpScript(path); }
        void OnImGuiRender(bool* open = nullptr);
	private:
        AssetHandle m_InspectedAsset{0};
        AssetImportSettings m_AssetSettingsDraft;
        bool m_AssetSettingsDirty=false;
        std::string m_AssetInspectorMessage;
        std::array<char, 256> m_Search{};
        int m_TypeFilter = 0;
        size_t m_AtlasSelectedSlice = 0;
        float m_AtlasZoom=1.0f;
        bool m_AtlasDragging=false, m_AtlasDragResize=false, m_AtlasDragPivot=false;
        std::array<float,2> m_AtlasDragStart{};
        std::array<int,4> m_AtlasDragRect{};
        std::filesystem::path m_CurrentDirectory;
		std::filesystem::path m_SelectedPath;
		std::filesystem::path m_PendingRevealPath;
		// 只有用户真正点击/右键选中的目录才显示蓝色选中态，
		// 避免项目刚打开时 Assets 只是作为默认当前目录而被高亮。
		bool m_UserSelectedDirectory = false;

		struct PreviewEntry
		{
			Ref<Texture2D> Texture;
			std::filesystem::file_time_type Modified{};
			int LastUsedFrame = 0;
		};
		std::unordered_map<std::filesystem::path, PreviewEntry> m_Previews;
		double m_NextPreviewRefresh = 0.0;
		Ref<Texture2D> GetImagePreview(const std::filesystem::path& path);
		void RefreshImagePreviews();
		Ref<EditorIconSet> m_Icons;
		std::filesystem::path m_ActiveScenePath;

		LayoutMode m_LayoutMode;
		Ref<Project> m_Project;
		bool m_ProjectStateWritable = true;
		bool m_AssetMutationsEnabled = true;
		std::filesystem::path m_ExternalScriptEditor;
		bool m_Focused = false;
		bool m_Docked = true;

		// 存储树节点的打开状态
		std::unordered_set<std::string> m_ExpandedNodes;
		// 创建子文件夹后需要强制展开的一次性路径。
		std::unordered_set<std::string> m_PendingOpenDirectories;
		// 等待下一帧创建的文件夹（从右键菜单里创建后立即进入重命名）
		std::filesystem::path m_PendingCreateFolderParent;
		std::filesystem::path m_PendingCreateScriptParent;
		enum class AuthoringAssetKind : uint8_t
		{
			None = 0,
			AnimationClip,
			AnimatorController,
			TilePalette
		};
		std::filesystem::path m_PendingCreateAuthoringAssetParent;
		AuthoringAssetKind m_PendingCreateAuthoringAsset = AuthoringAssetKind::None;

		// 行内重命名状态
		std::filesystem::path m_RenamePath;
		AssetHandle m_RenameHandle = AssetHandle(0);
		char m_RenameBuffer[512] = {};
		bool m_RenameFocus = false;
		bool m_OpenRenamePopup = false;

		std::filesystem::path m_DeletePath;
		AssetHandle m_DeleteHandle = AssetHandle(0);
		bool m_DeleteIsDirectory = false;
		bool m_OpenDeletePopup = false;
		std::vector<AssetReference> m_DeleteReferences;
		struct AtlasSliceDraft
		{
			std::string StableID;
			std::string Name;
			int Rect[4] = { 0, 0, 1, 1 };
			float Pivot[2] = { 0.5f, 0.5f };
			float PixelsPerUnit = 100.0f;
			float Border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		};
		std::filesystem::path m_AtlasEditorPath;
		AssetHandle m_AtlasEditorHandle = AssetHandle(0);
		AssetImportSettings m_AtlasBaseSettings;
		std::vector<AtlasSliceDraft> m_AtlasSlices;
		std::string m_AtlasEditorError;
		uint32_t m_AtlasWidth = 1;
		uint32_t m_AtlasHeight = 1;
		int m_AtlasAlphaThreshold = 1;
		int m_AtlasMinimumOpaquePixels = 1;
		int m_AtlasSlicePadding = 0;
		int m_AtlasGridCell[2] = { 32, 32 };
		int m_AtlasGridOffset[2] = { 0, 0 };
		int m_AtlasGridSpacing[2] = { 0, 0 };
		bool m_AtlasGridIncludePartial = false;
		int m_AtlasPackMaximumSize = 2048;
		int m_AtlasPackPadding = 1;
		bool m_AtlasPackPowerOfTwo = true;
		bool m_OpenAtlasEditorPopup = false;
		float m_LeftPanelWidth = 250.0f;
		float m_ThumbnailSize = 128.0f;
		bool m_OpenLayoutOptions = false;
		float m_LayoutOptionsX = 0.0f;
		float m_LayoutOptionsY = 0.0f;

		SceneOpenCallback m_SceneOpenCallback;
		AuthoringAssetOpenCallback m_AuthoringAssetOpenCallback;
		AssetRenamedCallback m_AssetRenamedCallback;
		AssetDeletedCallback m_AssetDeletedCallback;
		EntityPrefabCreateCallback m_EntityPrefabCreateCallback;

		std::filesystem::path GetAssetRoot() const;
		std::filesystem::path GetPackagesRoot() const;
		std::filesystem::path GetRootForPath(const std::filesystem::path& path) const;
		bool IsWritablePath(const std::filesystem::path& path) const;
		void RestoreProjectState();
		void DrawDirectoryTree(const std::filesystem::path& directoryPath,
			const std::filesystem::path& root, const char* rootLabel, bool isRoot, bool includeFiles);
		void DrawFileTreeNode(const std::filesystem::path& path, const std::filesystem::path& root);
		void DrawBreadcrumbs(const std::filesystem::path& root, const char* rootLabel);
		void DrawAssetGrid(const std::filesystem::path& root, const char* rootLabel);
		void DrawAssetItem(const std::filesystem::directory_entry& entry, const std::filesystem::path& root);
		Ref<Texture2D> GetAssetIcon(const std::filesystem::path& path, bool isDirectory,
			bool isOpen = false);
		void SubmitDragPayload(const std::filesystem::path& path, const std::filesystem::path& assetRoot, const Ref<Texture2D>& icon);
		void SubmitDirectoryDragPayload(const std::filesystem::path& path, const std::filesystem::path& assetRoot);
		void AcceptAssetMoveTarget(const std::filesystem::path& destinationDirectory);
		bool ApplyMovedPath(const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath, AssetHandle movedHandle = AssetHandle(0));

		void FlushPendingCreateFolder();
		void FlushPendingCreateScript();
		void FlushPendingCreateAuthoringAsset();
		void DrawNodeContextMenu();
		void DrawContextMenuBody(const std::filesystem::path& target,
			bool isDirectory, bool isRoot);
		void DrawLayoutMenu();
		void DrawEmptyContextMenu(const std::filesystem::path& assetRoot);
		void DrawRenamePopup();
		void DrawDeleteConfirmation();
		void BeginAtlasEditor(const std::filesystem::path& path);
		void DrawAtlasEditorPopup();
		bool SaveAtlasEditor();
		bool AutoSliceAtlas(bool grid);
		bool ExportPackedAtlas();

		void OpenAsset(const std::filesystem::path& path, bool isDirectory);
		bool OpenCSharpScript(const std::filesystem::path& path);
		void ChooseExternalScriptEditor(const std::filesystem::path& scriptPath);
		void RequestDeleteAsset(const std::filesystem::path& path, bool isDirectory);
		void DeleteAsset(const std::filesystem::path& path, bool force);
		void BeginRename(const std::filesystem::path& path);
		std::filesystem::path CommitRename();
		void CancelRename();
	};
}

