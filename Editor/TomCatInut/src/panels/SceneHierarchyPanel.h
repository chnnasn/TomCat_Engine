#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "HierarchyPanelShared.h"
#include "SceneAuthoringEditorsPanel.h"
#include "SceneInspectorPanel.h"
#include "SceneHierarchyTreePanel.h"
#include "../EditorIcons.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace TomCat {
	class Project;

	// Facade that owns the shared panel state plus the three extracted panels
	// (Hierarchy tree, Inspector, Animator/animation/tile-palette authoring
	// editors). The public API is unchanged from the original monolithic
	// SceneHierarchyPanel, so EditorLayer and docked-window hosts do not care
	// about the split.
	class SceneHierarchyPanel
	{
	public:
		using SceneModificationPhase = ::TomCat::SceneModificationPhase;
		using ColliderEditMode = ::TomCat::ColliderEditMode;
		using SceneLoadCallback = ::TomCat::SceneLoadCallback;
		using SpriteCreateCallback = ::TomCat::SpriteCreateCallback;
		using AssetRevealCallback = ::TomCat::AssetRevealCallback;
		using SceneModifiedCallback = ::TomCat::SceneModifiedCallback;
		using PrefabCreateCallback = ::TomCat::PrefabCreateCallback;
		using PrefabInstantiateCallback = ::TomCat::PrefabInstantiateCallback;
		using ScriptMetadataProvider = ::TomCat::ScriptMetadataProvider;

		SceneHierarchyPanel();
		SceneHierarchyPanel(const Ref<Scene>& scene);

		void SetContext(const Ref<Scene>& scene, bool clearSelection = true, bool remapSelection = false);
		// Use after replacing a Scene registry in place. Entity wrappers from the
		// old registry cannot be queried; selection must be captured beforehand.
		void ResetForSceneReplacement(const Ref<Scene>& scene, UUID selectedEntity);
		void SetProject(const Ref<Project>& project);
		void SetIcons(const Ref<EditorIconSet>& icons) { m_Shared.Icons = icons; }

		void OnImGuiRender(bool* hierarchyOpen = nullptr, bool* inspectorOpen = nullptr,
			bool sceneDirty = false);
		void OnAnimatorGraphImGuiRender(bool* open = nullptr) { m_Authors.OnAnimatorGraphImGuiRender(open); }
		void OnAnimationImGuiRender(bool* open = nullptr) { m_Authors.OnAnimationImGuiRender(open); }
		void OnTilePaletteImGuiRender(bool* open = nullptr) { m_Authors.OnTilePaletteImGuiRender(open); }
		bool OpenAuthoringAsset(AssetHandle handle, AssetType type) { return m_Authors.OpenAuthoringAsset(handle, type); }
		bool ConsumeAnimatorGraphOpenRequest() { return m_Authors.ConsumeAnimatorGraphOpenRequest(); }
		bool ConsumeAnimationOpenRequest() { return m_Authors.ConsumeAnimationOpenRequest(); }
		bool ConsumeTilePaletteOpenRequest() { return m_Authors.ConsumeTilePaletteOpenRequest(); }
		UUID ConsumeFrameEntityRequest()
		{
			const UUID requested = m_Shared.FrameEntityRequest;
			m_Shared.FrameEntityRequest = UUID(0);
			return requested;
		}
		bool CanAddComponentToSelection() const
		{
			return m_Shared.SelectionContext && m_Shared.ColliderEditingAllowed;
		}
		void RequestAddComponentPopup()
		{
			if (CanAddComponentToSelection())
				m_Inspector.RequestAddComponentPopup();
		}
		// Draws the same command surface used by the Hierarchy context menus so the
		// editor's top-level GameObject menu cannot drift from them.
		// Returns true when an inline operation (create/rename) needs the Hierarchy
		// panel to be shown and focused.
		bool DrawGameObjectMenu();

		Entity GetSelectedEntity() const { return m_Shared.SelectionContext; }
		ColliderEditMode GetColliderEditMode() const;
		bool IsEditingCollider() const { return GetColliderEditMode() != ColliderEditMode::None; }
		bool IsColliderEditingAllowed() const { return m_Shared.ColliderEditingAllowed; }
		void SetColliderEditingAllowed(bool allowed)
		{
			m_Shared.ColliderEditingAllowed = allowed;
			if (!allowed)
				ClearColliderEditMode();
		}
		// Embedded hosts can omit specialized tools without disabling component editing.
		void SetColliderGizmosEnabled(bool enabled) { m_Shared.ColliderGizmosEnabled = enabled; if (!enabled) ClearColliderEditMode(); }
		void SetScriptEditingEnabled(bool enabled) { m_Shared.ScriptEditingEnabled = enabled; }
		void ClearColliderEditMode() { m_Shared.ClearColliderEdit(); }

		void SetAssetSelection(const std::filesystem::path& path) { m_Inspector.SetAssetSelection(path); }
		void SetAssetInspectorRenderer(std::function<void(const std::filesystem::path&)> renderer) { m_Inspector.SetAssetInspectorRenderer(std::move(renderer)); }
		void SetSelectedEntity(Entity entity);
		bool HandleShortcut(int keyCode, bool control) { return m_Tree.HandleShortcut(keyCode, control); }
		bool FlushPendingCommands();
		bool IsHierarchyFocused() const { return m_Tree.IsFocused(); }
		bool IsInspectorFocused() const { return m_Inspector.IsFocused(); }
		bool IsAnimatorGraphFocused() const { return m_Authors.IsAnimatorGraphFocused(); }
		bool IsAnimationFocused() const { return m_Authors.IsAnimationFocused(); }
		bool IsTilePaletteFocused() const { return m_Authors.IsTilePaletteFocused(); }
		bool HasPendingRenameFocus() const { return m_Tree.HasPendingRenameFocus(); }
		bool IsHierarchyDocked() const { return m_Tree.IsDocked(); }
		bool IsInspectorDocked() const { return m_Inspector.IsDocked(); }
		bool IsAnimatorGraphDocked() const { return m_Authors.IsAnimatorGraphDocked(); }
		bool IsAnimationDocked() const { return m_Authors.IsAnimationDocked(); }
		bool IsTilePaletteDocked() const { return m_Authors.IsTilePaletteDocked(); }
		void SetSceneLoadCallback(const SceneLoadCallback& callback) { m_Shared.SceneLoad = callback; }
		void SetSpriteCreateCallback(const SpriteCreateCallback& callback) { m_Shared.SpriteCreate = callback; }
		void SetScriptOpenCallback(const AssetRevealCallback& callback) { m_Shared.ScriptOpen = callback; }
		void SetAssetRevealCallback(const AssetRevealCallback& callback) { m_Shared.AssetReveal = callback; }
		void SetSceneModifiedCallback(const SceneModifiedCallback& callback) { m_Shared.SceneModified = callback; }
		void SetPrefabCreateCallback(PrefabCreateCallback callback)
		{
			m_Shared.PrefabCreate = std::move(callback);
		}
		void SetPrefabInstantiateCallback(PrefabInstantiateCallback callback)
		{
			m_Shared.PrefabInstantiate = std::move(callback);
		}
		void SetPrefabCreationAllowed(bool allowed) { m_Shared.PrefabCreationAllowed = allowed; }
		void SetPrefabActionCallback(std::function<void(Entity, int, UUID, UUID, UUID)> callback)
		{ m_Shared.PrefabAction = std::move(callback); }
		void SetScriptMetadataProvider(ScriptMetadataProvider provider)
		{
			m_Shared.ScriptMetadata = std::move(provider);
		}
	private:
		HierarchyPanelShared m_Shared;
		SceneAuthoringEditorsPanel m_Authors;
		SceneInspectorPanel m_Inspector;
		SceneHierarchyTreePanel m_Tree;
	};

}
