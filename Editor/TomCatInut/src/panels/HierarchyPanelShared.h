#pragma once

// State and services shared by the SceneHierarchyPanel family of panels.
// One instance is owned by the SceneHierarchyPanel facade and referenced by
// the tree, inspector and authoring panels.

#include "TomCat/Core/Base.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Asset/Asset.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "../EditorIcons.h"
#include "../Scripting/ScriptEditorMetadata.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace TomCat {
	class Project;

	using SceneLoadCallback = std::function<void(AssetHandle)>;
	using SpriteCreateCallback = std::function<void(AssetHandle)>;
	using AssetRevealCallback = std::function<void(AssetHandle)>;

	enum class SceneModificationPhase
	{
		Begin,
		Update,
		Commit,
		Instant,
		Cancel
	};

	using SceneModifiedCallback =
		std::function<void(SceneModificationPhase)>;

	using PrefabCreateCallback = std::function<bool(Entity)>;
	using PrefabInstantiateCallback = std::function<Entity(AssetHandle, Entity)>;
	using ScriptMetadataProvider =
		std::function<std::optional<EditorScriptMetadata>(AssetHandle)>;

	enum class ColliderEditMode
	{
		None = 0,
		Box,
		Circle
	};

	struct HierarchyPanelShared
	{
		// Scene / project / icons
		Ref<Scene> Context;
		Ref<Project> Project;
		Ref<EditorIconSet> Icons;

		// Selection model
		Entity SelectionContext;
		std::vector<UUID> MultiSelection;
		UUID SelectionAnchor{ 0 };

		// Sprite picker (Inspector content, closed from tree/facade lifecycles)
		std::array<char, 256> SpriteSearch{};
		bool SpritePickerOpen = false;
		UUID SpritePickerEntity{ 0 };

		// Editing capabilities
		bool ColliderGizmosEnabled = true;
		bool ScriptEditingEnabled = true;
		bool ColliderEditingAllowed = true;
		bool PrefabCreationAllowed = true;

		// Collider edit gizmo state
		ColliderEditMode ColliderEdit = ColliderEditMode::None;
		UUID ColliderEditEntity{ 0 };

		// One-shot "frame selected entity" request raised by the tree
		UUID FrameEntityRequest{ 0 };

		// Inline name-edit state shared by tree rows and Inspector fields
		Entity NameEditEntity;
		char NameEditBuffer[256] = {};

		// Scene modification gestures
		bool ModificationGestureActive = false;
		bool CommitAfterPendingDeletion = false;

		// Deferred prefab command drained by FlushPendingCommands
		UUID PendingPrefabRoot{ 0 };
		int PendingPrefabAction = 0;
		UUID PendingPrefabEntity{ 0 }, PendingPrefabComponent{ 0 }, PendingPrefabProperty{ 0 };
		bool PrefabOverridesDirty = true;

		// Callbacks into the editor host
		SceneLoadCallback SceneLoad;
		SpriteCreateCallback SpriteCreate;
		AssetRevealCallback AssetReveal;
		AssetRevealCallback ScriptOpen;
		SceneModifiedCallback SceneModified;
		PrefabCreateCallback PrefabCreate;
		PrefabInstantiateCallback PrefabInstantiate;
		std::function<void(Entity, int, UUID, UUID, UUID)> PrefabAction;
		ScriptMetadataProvider ScriptMetadata;

		void MarkModified(bool instant = false);
		void FinishModificationGesture();
		// Port of the original collider-edit validation: the requested edit mode
		// is only honored while the owning selection still supports it.
		ColliderEditMode GetColliderEdit() const;
		void ClearColliderEdit()
		{
			ColliderEdit = ColliderEditMode::None;
			ColliderEditEntity = UUID(0);
		}
	};
}
