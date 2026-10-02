#pragma once

// Inspector: component editing, multi-selection, C# scripts, asset inspector
// and tilemap brush state, extracted from SceneHierarchyPanel.

#include "HierarchyPanelShared.h"
#include "SceneAuthoringEditorsPanel.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Advanced2D.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace TomCat {

	class SceneInspectorPanel
	{
	public:
		SceneInspectorPanel(HierarchyPanelShared& shared,
			SceneAuthoringEditorsPanel& authors)
			: m_Shared(shared), m_Authors(authors) {}

		void DrawInspectorWindow(bool* open);
		void SetAssetSelection(const std::filesystem::path& path) { m_InspectorAssetPath = path; }
		void SetAssetInspectorRenderer(std::function<void(const std::filesystem::path&)> renderer) { m_AssetInspectorRenderer = std::move(renderer); }
		bool IsFocused() const { return m_InspectorFocused; }
		bool IsDocked() const { return m_InspectorDocked; }
		void RequestAddComponentPopup() { m_AddComponentPopupRequested = true; }
		void ResetAddComponentPopup() { m_AddComponentPopupRequested = false; }
		void ClearAssetSelection() { m_InspectorAssetPath.clear(); }
		void OnSceneContextChanged();
		void ClearSceneState();
		// Called by the tree when a C# script payload is dropped on an entity node.
		bool AcceptCSharpScriptDrop(Entity entity);

	private:
		struct TilemapBrushState
		{
			glm::ivec2 Coordinate{ 0, 0 };
			AssetHandle SpriteHandle = AssetHandle(0);
			glm::vec4 Tint{ 1.0f };
			bool FlipX = false;
			bool FlipY = false;
			int32_t RotationQuarterTurns = 0;
		};

		bool AttachCSharpScript(Entity entity, AssetHandle handle);
		void DrawMultiSelectionInspector();
		void DrawComponents(Entity entity);
		void DrawSpriteAnimatorInspector(SpriteAnimator& animator, Entity entity);
		void DrawGrid2DInspector(Grid2D& grid);
		void DrawTilemap2DInspector(Tilemap2D& tilemap, Entity entity);
		void DrawTilemapRenderer2DInspector(TilemapRenderer2D& renderer);
		void DrawParticleSystem2DInspector(ParticleSystem2D& system);
		void DrawLight2DInspector(Light2D& light);
		void DrawUIButtonInspector(UIButton& button, Entity entity);
		void DrawCSharpScripts(Entity entity);

	private:
		std::optional<CSharpScriptEntry> m_ScriptClipboard;
        uint64_t m_MetadataRevision = ~uint64_t(0);
        const Scene* m_MetadataScene = nullptr;
        std::unordered_map<uint64_t, std::optional<EditorScriptMetadata>> m_MetadataCache;
        std::unordered_map<uint64_t, size_t> m_ReconciledFields;
		HierarchyPanelShared& m_Shared;
		SceneAuthoringEditorsPanel& m_Authors;
		std::filesystem::path m_InspectorAssetPath, m_LockedInspectorAssetPath;
		std::function<void(const std::filesystem::path&)> m_AssetInspectorRenderer;
		bool m_InspectorLocked = false;
		UUID m_InspectedEntity{ 0 };
		bool m_InspectorFocused = false;
		bool m_InspectorDocked = true;
		std::array<char, 256> m_AddComponentSearch{};
		bool m_AddComponentPopupRequested = false;
		bool m_AddComponentSearchFocusRequested = false;
		std::unordered_map<uint64_t, TilemapBrushState> m_TilemapBrushStates;
	};
}
