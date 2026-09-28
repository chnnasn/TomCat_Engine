#pragma once

// Animator Graph, Animation timeline and Tile Palette authoring editors,
// extracted from SceneHierarchyPanel. Owns all standalone authoring window
// state; embedded (Inspector) animator editing calls into this class.

#include "HierarchyPanelShared.h"
#include "TomCat/Asset/Advanced2DAuthoringAssets.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "TomCat/Scene/Components.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

namespace TomCat {

	class SceneAuthoringEditorsPanel
	{
	public:
		enum class AnimatorRenameTarget
		{
			None = 0,
			Clip,
			Parameter,
			State
		};

		explicit SceneAuthoringEditorsPanel(HierarchyPanelShared& shared)
			: m_Shared(shared) {}

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
		bool IsAnimatorGraphFocused() const { return m_AnimatorGraphFocused; }
		bool IsAnimationFocused() const { return m_AnimationFocused; }
		bool IsTilePaletteFocused() const { return m_TilePaletteFocused; }
		bool IsAnimatorGraphDocked() const { return m_AnimatorGraphDocked; }
		bool IsAnimationDocked() const { return m_AnimationDocked; }
		bool IsTilePaletteDocked() const { return m_TilePaletteDocked; }

		// Embedded SpriteAnimator editing, called by SceneInspectorPanel.
		void DrawSpriteAnimatorGraph(SpriteAnimator& animator, Entity entity,
			std::function<void()>& pendingMutation, bool standaloneWindow = false);
		void RequestAnimatorRename(AnimatorRenameTarget target, Entity entity,
			size_t index, const std::string& currentName, bool graphWindow);
		void DrawAnimatorRenamePopup(SpriteAnimator& animator, Entity entity,
			bool graphWindow);
		void DismissAnimatorRenamePopup(bool graphWindow);
		// Inspector-window counterpart of the stale-popup dismissal performed
		// before the standalone graph window existed.
		void DismissEmbeddedRenamePopupIfStale(Entity selection);
		void ApplyAnimatorPendingMutation(Entity entity,
			std::function<void()>& pendingMutation);

		// Facade lifecycle hooks. OnSceneContextChanging must run while
		// m_Shared.Context still refers to the outgoing scene.
		void OnSceneContextChanging();
		void OnSceneContextChanged(bool contextChanged);
		void ClearSceneState();
		void ResetRenamePopupState();
		void ResetRenamePopupSelection();
		void StopTimelinePreview(UUID entityId);

	public:
		// Rename-popup and open-request state is shared with SceneInspectorPanel
		// (embedded animator editing mutates it directly).
		AnimatorRenameTarget m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		UUID m_AnimatorRenameEntity = UUID(0);
		std::string m_AnimatorRenameError;
		bool m_AnimatorRenamePopupRequested = false;
		bool m_AnimatorRenamePopupGraphOwner = false;
		bool m_AnimatorGraphOpenRequested = false;
		bool m_AnimationOpenRequested = false;
		bool m_TilePaletteOpenRequested = false;

	private:
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
			size_t SelectedFrame = 0;
			bool PreviewPlaying = false;
			float PreviewTime = 0;
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

		void DrawAnimationClipAssetEditor();
		void DrawAnimatorControllerAssetEditor();
		void DrawTilePaletteAssetEditor();
		bool SaveAnimationClipAsset();
		bool SaveAnimatorControllerAsset();
		bool SaveTilePaletteAsset();

	private:
		HierarchyPanelShared& m_Shared;
		size_t m_AnimatorRenameIndex = 0;
		std::array<char, 128> m_AnimatorRenameBuffer{};
		std::unordered_map<uint64_t, AnimatorGraphEditorState> m_AnimatorGraphStates;
		std::unordered_map<uint64_t, AnimationTimelineState> m_AnimationTimelineStates;
		std::unordered_map<uint64_t, TilePaletteState> m_TilePaletteStates;
		AnimationClipAssetEditorState m_AnimationClipAssetEditor;
		AnimatorControllerAssetEditorState m_AnimatorControllerAssetEditor;
		TilePaletteAssetEditorState m_TilePaletteAssetEditor;
		AssetHandle m_ActiveTilePaletteHandle = AssetHandle(0);
		TilePaletteAsset m_ActiveTilePalette;
		bool m_AnimatorGraphFocused = false;
		bool m_AnimationFocused = false;
		bool m_TilePaletteFocused = false;
		bool m_AnimatorGraphDocked = true;
		bool m_AnimationDocked = true;
		bool m_TilePaletteDocked = true;
	};
}
