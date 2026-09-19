#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Asset/Advanced2DAuthoringAssets.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "../EditorIcons.h"
#include "../Scripting/ScriptEditorMetadata.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace TomCat {
	class Project;

	class SceneHierarchyPanel
	{
	public:
		enum class SceneModificationPhase
		{
			Begin,
			Update,
			Commit,
			Instant,
			Cancel
		};

		enum class ColliderEditMode
		{
			None = 0,
			Box,
			Circle
		};

		using SceneLoadCallback = std::function<void(AssetHandle)>;
		using SpriteCreateCallback = std::function<void(AssetHandle)>;
		using AssetRevealCallback = std::function<void(AssetHandle)>;
		using SceneModifiedCallback =
			std::function<void(SceneModificationPhase)>;
		using PrefabCreateCallback = std::function<bool(Entity)>;
		using PrefabInstantiateCallback = std::function<Entity(AssetHandle, Entity)>;
		using ScriptMetadataProvider =
			std::function<std::optional<EditorScriptMetadata>(AssetHandle)>;

		SceneHierarchyPanel() = default;
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene, bool clearSelection = true, bool remapSelection = false);
		// Use after replacing a Scene registry in place. Entity wrappers from the
		// old registry cannot be queried; selection must be captured beforehand.
		void ResetForSceneReplacement(const Ref<Scene>& scene, UUID selectedEntity);
		void SetProject(const Ref<Project>& project);
		void SetIcons(const Ref<EditorIconSet>& icons) { m_Icons = icons; }

		void OnImGuiRender(bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr,
			bool sceneDirty = false);
		void OnAnimatorGraphImGuiRender(bool* open = nullptr);
		void OnAnimationImGuiRender(bool* open = nullptr);
		void OnTilePaletteImGuiRender(bool* open = nullptr);
		// Loads a standalone 2D authoring asset into its matching dockable editor.
		// Content Browser double-clicks use this instead of editing an entity's
		// embedded animator snapshot.
		bool OpenAuthoringAsset(AssetHandle handle, AssetType type);
		bool ConsumeAnimatorGraphOpenRequest()
		{
			const bool requested = m_AnimatorGraphOpenRequested;
			m_AnimatorGraphOpenRequested = false;
			return requested;
		}
		bool ConsumeAnimationOpenRequest()
		{
			const bool requested = m_AnimationOpenRequested;
			m_AnimationOpenRequested = false;
			return requested;
		}
		bool ConsumeTilePaletteOpenRequest()
		{
			const bool requested = m_TilePaletteOpenRequested;
			m_TilePaletteOpenRequested = false;
			return requested;
		}
		UUID ConsumeFrameEntityRequest()
		{
			const UUID requested = m_FrameEntityRequest;
			m_FrameEntityRequest = UUID(0);
			return requested;
		}
		bool CanAddComponentToSelection() const
		{
			return m_SelectionContext && m_ColliderEditingAllowed;
		}
		void RequestAddComponentPopup()
		{
			if (CanAddComponentToSelection())
				m_AddComponentPopupRequested = true;
		}
		// Draws the same command surface used by the Hierarchy context menus so the
		// editor's top-level GameObject menu cannot drift from them.
		// Returns true when an inline operation (create/rename) needs the Hierarchy
		// panel to be shown and focused.
		bool DrawGameObjectMenu();

		Entity GetSelectedEntity() const { return m_SelectionContext; };
		ColliderEditMode GetColliderEditMode() const;
		bool IsEditingCollider() const { return GetColliderEditMode() != ColliderEditMode::None; }
		bool IsColliderEditingAllowed() const { return m_ColliderEditingAllowed; }
		void SetColliderEditingAllowed(bool allowed)
		{
			m_ColliderEditingAllowed = allowed;
			if (!allowed)
				ClearColliderEditMode();
		}
		// Embedded hosts can omit specialized tools without disabling component editing.
		void SetColliderGizmosEnabled(bool enabled) { m_ColliderGizmosEnabled = enabled; if (!enabled) ClearColliderEditMode(); }
		void SetScriptEditingEnabled(bool enabled) { m_ScriptEditingEnabled = enabled; }
		void ClearColliderEditMode()
		{
			m_ColliderEditMode = ColliderEditMode::None;
			m_ColliderEditEntity = UUID(0);
		}

		void SetSelectedEntity(Entity entity);
		bool HandleShortcut(int keyCode, bool control);
		bool FlushPendingCommands();
		bool IsHierarchyFocused() const { return m_HierarchyFocused; }
		bool IsInspectorFocused() const { return m_InspectorFocused; }
		bool IsAnimatorGraphFocused() const { return m_AnimatorGraphFocused; }
		bool IsAnimationFocused() const { return m_AnimationFocused; }
		bool IsTilePaletteFocused() const { return m_TilePaletteFocused; }
		bool HasPendingRenameFocus() const { return m_RenameFocus; }
		bool IsHierarchyDocked() const { return m_HierarchyDocked; }
		bool IsInspectorDocked() const { return m_InspectorDocked; }
		bool IsAnimatorGraphDocked() const { return m_AnimatorGraphDocked; }
		bool IsAnimationDocked() const { return m_AnimationDocked; }
		bool IsTilePaletteDocked() const { return m_TilePaletteDocked; }
		void SetSceneLoadCallback(const SceneLoadCallback& callback) { m_SceneLoadCallback = callback; }
		void SetSpriteCreateCallback(const SpriteCreateCallback& callback) { m_SpriteCreateCallback = callback; }
		void SetAssetRevealCallback(const AssetRevealCallback& callback) { m_AssetRevealCallback = callback; }
		void SetSceneModifiedCallback(const SceneModifiedCallback& callback) { m_SceneModifiedCallback = callback; }
		void SetPrefabCreateCallback(PrefabCreateCallback callback)
		{
			m_PrefabCreateCallback = std::move(callback);
		}
		void SetPrefabInstantiateCallback(PrefabInstantiateCallback callback)
		{
			m_PrefabInstantiateCallback = std::move(callback);
		}
		void SetPrefabCreationAllowed(bool allowed) { m_PrefabCreationAllowed = allowed; }
		void SetPrefabActionCallback(std::function<void(Entity, int)> callback)
		{ m_PrefabActionCallback = std::move(callback); }
		void SetScriptMetadataProvider(ScriptMetadataProvider provider)
		{
			m_ScriptMetadataProvider = std::move(provider);
		}
	private:
		enum class AnimatorRenameTarget
		{
			None = 0,
			Clip,
			Parameter,
			State
		};

		struct AnimatorGraphEditorState
		{
			SpriteAnimatorAuthoring::AnimatorGraphLayout Layout;
			float PanX = 0.0f;
			float PanY = 0.0f;
			std::string SelectedState;
			std::string TransitionSource;
			size_t SelectedTransition = static_cast<size_t>(-1);
			bool SelectedAnyState = false;
			bool TransitionSourceAnyState = false;
		};

		struct TilemapBrushState
		{
			glm::ivec2 Coordinate{ 0, 0 };
			AssetHandle SpriteHandle = AssetHandle(0);
			glm::vec4 Tint{ 1.0f };
			bool FlipX = false;
			bool FlipY = false;
			int32_t RotationQuarterTurns = 0;
		};

		struct AnimationTimelineState
		{
			std::string SelectedClip;
			size_t SelectedFrame = 0;
			float FrameRate = 12.0f;
			double Time = 0.0;
			bool Preview = false;
			bool Recording = false;
			bool Playing = false;
		};

		enum class TilePaletteTool : uint8_t
		{
			Select = 0,
			Move,
			Paint,
			Box,
			Eyedropper,
			Erase,
			Fill
		};

		struct TilePaletteState
		{
			TilePaletteTool Tool = TilePaletteTool::Paint;
			glm::ivec2 Coordinate{ 0, 0 };
			glm::ivec2 BoxEnd{ 0, 0 };
			glm::ivec2 MoveDestination{ 0, 0 };
			AssetHandle SpriteHandle = AssetHandle(0);
			glm::vec4 Tint{ 1.0f };
		};

		struct AnimationClipAssetEditorState
		{
			AssetHandle Handle = AssetHandle(0);
			std::filesystem::path Path;
			AnimationClipAsset Asset;
			AssetHandle DraftSprite = AssetHandle(0);
			bool Loaded = false;
			bool Dirty = false;
			std::string Message;
		};

		struct AnimatorControllerAssetEditorState
		{
			AssetHandle Handle = AssetHandle(0);
			std::filesystem::path Path;
			AnimatorControllerAsset Asset;
			AssetHandle DraftClip = AssetHandle(0);
			bool Loaded = false;
			bool Dirty = false;
			std::string Message;
		};

		struct TilePaletteAssetEditorState
		{
			AssetHandle Handle = AssetHandle(0);
			std::filesystem::path Path;
			TilePaletteAsset Asset;
			glm::ivec2 DraftCoordinate{ 0, 0 };
			AssetHandle DraftSprite = AssetHandle(0);
			bool Loaded = false;
			bool Dirty = false;
			std::string Message;
		};

		void DrawEntityNode(Entity entity);
		void DrawComponents(Entity entity);
		void DrawSpriteAnimatorInspector(SpriteAnimator& animator, Entity entity);
		void DrawGrid2DInspector(Grid2D& grid);
		void DrawTilemap2DInspector(Tilemap2D& tilemap, Entity entity);
		void DrawTilemapRenderer2DInspector(TilemapRenderer2D& renderer);
		void DrawParticleSystem2DInspector(ParticleSystem2D& system);
		void DrawLight2DInspector(Light2D& light);
		void DrawUIButtonInspector(UIButton& button, Entity entity);
		void DrawAnimationClipAssetEditor();
		void DrawAnimatorControllerAssetEditor();
		void DrawTilePaletteAssetEditor();
		bool SaveAnimationClipAsset();
		bool SaveAnimatorControllerAsset();
		bool SaveTilePaletteAsset();
		void DrawSpriteAnimatorGraph(SpriteAnimator& animator, Entity entity,
			std::function<void()>& pendingMutation, bool standaloneWindow = false);
		void RequestAnimatorRename(AnimatorRenameTarget target, Entity entity,
			size_t index, const std::string& currentName, bool graphWindow);
		void DrawAnimatorRenamePopup(SpriteAnimator& animator, Entity entity,
			bool graphWindow);
		void DismissAnimatorRenamePopup(bool graphWindow);
		void ApplyAnimatorPendingMutation(Entity entity,
			std::function<void()>& pendingMutation);
		void DrawCSharpScripts(Entity entity);
		bool AttachCSharpScript(Entity entity, AssetHandle handle);
		bool AcceptCSharpScriptDrop(Entity entity);
		bool AcceptPrefabDrop(Entity parent);
		void DrawEntityOperationsMenu();
		void BeginRename(Entity entity);
		void CutSelectedEntity();
		void CopySelectedEntity();
		void PasteEntity();
		void DuplicateSelectedEntity();
		void DeleteSelectedEntity();
		bool CanPaste() const;
		bool FlushPendingDeletion();
		void ClearClipboard();
		void MarkModified(bool instant = false);
		void FinishModificationGesture();
	private:
		bool m_ColliderGizmosEnabled = true;
		bool m_ScriptEditingEnabled = true;
		Ref<Scene> m_Context;
		Ref<Project> m_Project;
		Entity m_SelectionContext;
		// 创建子对象后用于强制展开父节点的一次性标记。
		Entity m_ForceExpandParent;
		std::unordered_set<uint64_t> m_ForceOpenEntityNodes;
		bool m_ForceOpenSceneRoot = false;
		Entity m_EntityToDelete;
		Entity m_ClipboardEntity;
		Ref<Scene> m_ClipboardScene;
		bool m_ClipboardIsCut = false;
		Entity m_RenameEntity;
		char m_RenameBuffer[256] = {};
		bool m_RenameFocus = false;
		Entity m_NameEditingEntity;
		char m_NameEditBuffer[256] = {};
		bool m_HierarchyFocused = false;
		bool m_InspectorFocused = false;
		bool m_AnimatorGraphFocused = false;
		bool m_AnimationFocused = false;
		bool m_TilePaletteFocused = false;
		bool m_HierarchyDocked = true;
		bool m_InspectorDocked = true;
		bool m_AnimatorGraphDocked = true;
		bool m_AnimationDocked = true;
		bool m_TilePaletteDocked = true;
		bool m_AnimatorGraphOpenRequested = false;
		bool m_AnimationOpenRequested = false;
		bool m_TilePaletteOpenRequested = false;
		UUID m_FrameEntityRequest = UUID(0);
		bool m_ColliderEditingAllowed = true;
		ColliderEditMode m_ColliderEditMode = ColliderEditMode::None;
		UUID m_ColliderEditEntity = UUID(0);
		Ref<EditorIconSet> m_Icons;
		std::array<char, 256> m_SpriteSearch{};
		std::array<char, 128> m_AddComponentSearch{};
		bool m_AddComponentPopupRequested = false;
		bool m_AddComponentSearchFocusRequested = false;
		bool m_SpritePickerOpen = false;
		UUID m_SpritePickerEntity = UUID(0);
		AnimatorRenameTarget m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		UUID m_AnimatorRenameEntity = UUID(0);
		size_t m_AnimatorRenameIndex = 0;
		std::array<char, 128> m_AnimatorRenameBuffer{};
		std::string m_AnimatorRenameError;
		bool m_AnimatorRenamePopupRequested = false;
		bool m_AnimatorRenamePopupGraphOwner = false;
		std::unordered_map<uint64_t, AnimatorGraphEditorState> m_AnimatorGraphStates;
		std::unordered_map<uint64_t, TilemapBrushState> m_TilemapBrushStates;
		std::unordered_map<uint64_t, AnimationTimelineState> m_AnimationTimelineStates;
		std::unordered_map<uint64_t, TilePaletteState> m_TilePaletteStates;
		AnimationClipAssetEditorState m_AnimationClipAssetEditor;
		AnimatorControllerAssetEditorState m_AnimatorControllerAssetEditor;
		TilePaletteAssetEditorState m_TilePaletteAssetEditor;
		AssetHandle m_ActiveTilePaletteHandle = AssetHandle(0);
		TilePaletteAsset m_ActiveTilePalette;
		SceneLoadCallback m_SceneLoadCallback;
		SpriteCreateCallback m_SpriteCreateCallback;
		AssetRevealCallback m_AssetRevealCallback;
		SceneModifiedCallback m_SceneModifiedCallback;
		PrefabCreateCallback m_PrefabCreateCallback;
		std::function<void(Entity, int)> m_PrefabActionCallback;
		UUID m_PendingPrefabRoot{ 0 };
		int m_PendingPrefabAction = 0;
		PrefabInstantiateCallback m_PrefabInstantiateCallback;
		ScriptMetadataProvider m_ScriptMetadataProvider;
		bool m_PrefabCreationAllowed = true;
		bool m_ModificationGestureActive = false;
		bool m_CommitAfterPendingDeletion = false;

	};

}
