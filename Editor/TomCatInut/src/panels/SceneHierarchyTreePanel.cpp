#include "TomCat/Core/Log.h"
#include "TomCat/Debug/Instrumentor.h"
#include "SceneHierarchyTreePanel.h"
#include "SceneHierarchyDetail.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"
#include "../EditorDragDrop.h"
#include "../EditorVisuals.h"

namespace TomCat {
	using namespace HierarchyDetail;

	void SceneHierarchyTreePanel::DrawHierarchyWindow(bool* hierarchyOpen, bool sceneDirty)
	{
        TC_PROFILE_SCOPE("Panel Hierarchy");
		m_HierarchyFocused = false;
		if (!hierarchyOpen || *hierarchyOpen)
		{
		const bool hierarchyVisible = BeginEditorWindow("Hierarchy", hierarchyOpen);
		m_HierarchyDocked = ImGui::IsWindowDocked();
        if (hierarchyVisible && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            m_Inspector.ClearAssetSelection();
		m_HierarchyFocused = hierarchyVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (hierarchyVisible && m_Shared.Context)
        {
            m_PreviousHierarchyOrder.swap(m_HierarchyVisibleOrder);
            m_HierarchyVisibleOrder.clear();
            ImGui::SetNextItemWidth(-1.0f);
            EditorSearchField("##HierarchySearch", "Search objects...", m_HierarchySearch.data(), m_HierarchySearch.size());
            FlushPendingDeletion();
            std::string sceneName = m_Shared.Context->GetSceneName();
			if (sceneDirty)
				sceneName += "*";
			if (m_ForceOpenSceneRoot)
			{
				ImGui::SetNextItemOpen(true);
				m_ForceOpenSceneRoot = false;
			}
			ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen |
				ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding |
				ImGuiTreeNodeFlags_Selected;
			ImGui::PushStyleColor(ImGuiCol_Header,
				ImVec4(0.145f, 0.145f, 0.145f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
				ImVec4(0.20f, 0.20f, 0.20f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,
				ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
			bool rootOpen = ImGui::TreeNodeEx((void*)m_Shared.Context.get(), rootFlags, "");
			ImGui::PopStyleColor(3);
			const ImVec2 rootItemMin = ImGui::GetItemRectMin();
			const ImVec2 rootItemMax = ImGui::GetItemRectMax();
			const float rootIconSize = DrawTreeRowIcon(m_Shared.Icons, EditorIcon::SceneOpen,
				rootItemMin, rootItemMax);
			const float rootTextX = rootItemMin.x + ImGui::GetTreeNodeToLabelSpacing() +
				rootIconSize + GetHierarchyIconTextGap();
			const float rootTextY = std::round(rootItemMin.y +
				(rootItemMax.y - rootItemMin.y - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::GetWindowDrawList()->AddText(ImVec2(rootTextX, rootTextY),
				ImGui::GetColorU32(ImGuiCol_Text), sceneName.c_str());

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				m_Shared.SelectionContext = {};
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				m_Shared.SelectionContext = {};

			if (ImGui::BeginPopupContextItem())
			{
				m_Shared.SelectionContext = {};
				DrawEntityOperationsMenu();
				ImGui::EndPopup();
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery |
					ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
				const bool acceptedPrefab = AcceptPrefabDrop({});
				if (!acceptedPrefab)
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID, flags))
				{
					Entity draggedEntity = GetDraggedSceneEntity(payload, m_Shared.Context);
					if (draggedEntity && payload->IsDelivery() && m_Shared.Context->MoveEntity(
						draggedEntity, Entity{}, Scene::EntityPlacement::Root))
					{
						m_Shared.SelectionContext = draggedEntity;
						m_Shared.MarkModified();
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (rootOpen)
			{
                TC_PROFILE_SCOPE("Hierarchy visible row layout");
                struct Row { Entity Item; int Depth; };
                std::vector<Row> rows;
                std::unordered_map<uint64_t, bool> searchMatches;
                const std::string query = LowerASCII(m_HierarchySearch.data());
                std::function<bool(Entity)> matches = [&](Entity item) {
                    if (query.empty()) return true;
                    const uint64_t id = static_cast<uint64_t>(item.GetUUID());
                    if (auto found = searchMatches.find(id); found != searchMatches.end()) return found->second;
                    bool result = LowerASCII(item.GetName()).find(query) != std::string::npos;
                    for (UUID childID : m_Shared.Context->GetChildrenUUIDs(item))
                        if (Entity child = m_Shared.Context->FindEntityByUUID(childID)) result |= matches(child);
                    searchMatches[id] = result; return result;
                };
                std::function<void(Entity, int)> append = [&](Entity item, int depth) {
                    if (!matches(item)) return;
                    const uint64_t id = static_cast<uint64_t>(item.GetUUID());
                    rows.push_back({item, depth}); m_HierarchyVisibleOrder.push_back(item.GetUUID());
                    if (item == m_ForceExpandParent || m_ForceOpenEntityNodes.contains(id)) m_ExpandedEntities.insert(id);
                    if (!query.empty() || m_ExpandedEntities.contains(id))
                        for (UUID childID : m_Shared.Context->GetChildrenUUIDs(item))
                            if (Entity child = m_Shared.Context->FindEntityByUUID(childID)) append(child, depth + 1);
                };
                for (UUID rootID : m_Shared.Context->GetRootEntityUUIDs())
                    if (Entity item = m_Shared.Context->FindEntityByUUID(rootID)) append(item, 0);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());
                for (size_t index = 0; index < rows.size(); ++index)
                    if (rows[index].Item == m_RenameEntity || rows[index].Item == m_Shared.SelectionContext)
                        clipper.ForceDisplayRangeByIndices(static_cast<int>(index), static_cast<int>(index + 1));
                while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const float indent = rows[index].Depth * ImGui::GetStyle().IndentSpacing;
                    if (indent > 0) ImGui::Indent(indent);
                    DrawEntityNode(rows[index].Item);
                    if (indent > 0) ImGui::Unindent(indent);
                }
				ImGui::TreePop();
			}

			FlushPendingDeletion();


			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
				m_Shared.SelectionContext = {};


			if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				DrawEntityOperationsMenu();
				ImGui::EndPopup();
			}
		}
		else if (hierarchyVisible)
		{
			ImGui::TextDisabled("Drag a scene file here to load");
		}

		if (hierarchyVisible)
		{
		ImVec2 available = ImGui::GetContentRegionAvail();
		if (available.y > 0)
		{
			ImGui::Dummy(available);
		}

		if (ImGui::BeginDragDropTarget())
		{
			ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID, flags))
			{
				if (payload->DataSize == sizeof(uint64_t))
				{
					const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
					const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(handle);
					const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
					if (builtIn && m_Shared.SpriteCreate)
						m_Shared.SpriteCreate(handle);
					else if (metadata && metadata->Type == AssetType::Scene && m_Shared.SceneLoad)
						m_Shared.SceneLoad(handle);
					else if (metadata && metadata->Type == AssetType::Texture2D && m_Shared.SpriteCreate)
						m_Shared.SpriteCreate(handle);
					else if (metadata && !metadata->IsMissing
						&& metadata->Type == AssetType::Prefab
						&& m_Shared.PrefabInstantiate)
					{
						Entity root = m_Shared.PrefabInstantiate(handle, {});
						if (root)
						{
							m_Shared.SelectionContext = root;
							m_ForceOpenSceneRoot = true;
						}
					}
				}
			}
			else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				SceneEntityDragDropPayloadID, flags | ImGuiDragDropFlags_AcceptBeforeDelivery))
			{
				Entity draggedEntity = GetDraggedSceneEntity(payload, m_Shared.Context);
				if (draggedEntity && payload->IsDelivery() && m_Shared.Context->MoveEntity(
					draggedEntity, Entity{}, Scene::EntityPlacement::Root))
				{
					m_Shared.SelectionContext = draggedEntity;
					m_Shared.MarkModified();
				}
			}
			ImGui::EndDragDropTarget();
		}
		}

		ImGui::End();
		}}

	bool SceneHierarchyTreePanel::AcceptPrefabDrop(Entity parent)
	{
		if (!m_Shared.Context || !m_Shared.PrefabInstantiate)
			return false;
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			AssetDragDropPayloadID, ImGuiDragDropFlags_AcceptBeforeDelivery);
		if (!payload || payload->DataSize != sizeof(uint64_t))
			return false;
		const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
		const AssetMetadata* metadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Prefab)
			return false;
		if (payload->IsDelivery())
		{
			Entity root = m_Shared.PrefabInstantiate(handle, parent);
			if (root)
			{
				m_Shared.SelectionContext = root;
				if (parent)
					m_ForceExpandParent = parent;
				else
					m_ForceOpenSceneRoot = true;
			}
		}
		return true;
	}

	bool SceneHierarchyTreePanel::FlushPendingDeletion()
	{
		if (!m_Shared.Context || !m_EntityToDelete)
			return false;

		Entity entity = m_EntityToDelete;
		m_EntityToDelete = {};
		if (m_Shared.SpritePickerOpen && m_Shared.SpritePickerEntity == entity.GetUUID())
		{
			m_Shared.SpritePickerOpen = false;
			m_Shared.SpritePickerEntity = UUID(0);
		}
		UUID selectedUUID{};
		const bool hadOtherSelection = m_Shared.SelectionContext && m_Shared.SelectionContext != entity;
		if (hadOtherSelection)
			selectedUUID = m_Shared.SelectionContext.GetUUID();
		UUID clipboardUUID{};
		const bool hadClipboard = m_ClipboardEntity && m_ClipboardScene == m_Shared.Context;
		if (hadClipboard)
			clipboardUUID = m_ClipboardEntity.GetUUID();
		if (m_Shared.SelectionContext == entity)
			m_Shared.SelectionContext = {};
		if (m_ClipboardEntity == entity)
			ClearClipboard();
		m_Shared.Context->DestroyEntity(entity);
		if (hadOtherSelection && !m_Shared.Context->FindEntityByUUID(selectedUUID))
			m_Shared.SelectionContext = {};
		if (hadClipboard && !m_Shared.Context->FindEntityByUUID(clipboardUUID))
			ClearClipboard();
		if (m_Shared.CommitAfterPendingDeletion && m_Shared.SceneModified)
		{
			m_Shared.SceneModified(SceneModificationPhase::Update);
			m_Shared.SceneModified(SceneModificationPhase::Commit);
			m_Shared.CommitAfterPendingDeletion = false;
		}
		else
			m_Shared.MarkModified(true);
		return true;
	}


	bool SceneHierarchyTreePanel::DrawGameObjectMenu()
	{
		if (!m_Shared.Context)
		{
			ImGui::BeginDisabled();
			DrawEntityOperationsMenu();
			ImGui::EndDisabled();
			return false;
		}

		// Hierarchy normally drains this deferred command while rendering. The
		// top-level menu must do the same when that panel is hidden.
		FlushPendingDeletion();
		DrawEntityOperationsMenu();
		FlushPendingDeletion();
		// BeginRename always raises this flag, including a repeated Rename command
		// for the same entity. Bring the Hierarchy forward so its inline editor is
		// never left waiting invisibly behind a hidden or inactive dock tab.
		return m_RenameFocus;
	}

	bool SceneHierarchyTreePanel::CanPaste() const
	{
		if (!m_ClipboardEntity || !m_ClipboardScene || m_ClipboardScene != m_Shared.Context)
			return false;

		return (bool)m_ClipboardScene->FindEntityByUUID(m_ClipboardEntity.GetUUID());
	}

	void SceneHierarchyTreePanel::BeginRename(Entity entity)
	{
		if (!entity)
			return;
		m_ForceOpenSceneRoot = true;
		if (m_Shared.Context)
		{
			Entity ancestor = m_Shared.Context->GetParent(entity);
			while (ancestor)
			{
				const uint64_t ancestorID = static_cast<uint64_t>(ancestor.GetUUID());
				if (!m_ForceOpenEntityNodes.insert(ancestorID).second)
					break;
				ancestor = m_Shared.Context->GetParent(ancestor);
			}
		}
		m_RenameEntity = entity;
		std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", entity.GetName().c_str());
		m_RenameFocus = true;
	}

	void SceneHierarchyTreePanel::CutSelectedEntity()
	{
		if (!m_Shared.SelectionContext)
			return;
		m_ClipboardEntity = m_Shared.SelectionContext;
		m_ClipboardScene = m_Shared.Context;
		m_ClipboardIsCut = true;
	}

	void SceneHierarchyTreePanel::CopySelectedEntity()
	{
		if (!m_Shared.SelectionContext)
			return;
		m_ClipboardEntity = m_Shared.SelectionContext;
		m_ClipboardScene = m_Shared.Context;
		m_ClipboardIsCut = false;
	}

	void SceneHierarchyTreePanel::PasteEntity()
	{
		if (!CanPaste())
			return;
		Entity pasted = m_Shared.Context->DuplicateEntity(m_ClipboardEntity);
		if (!pasted)
			return;
		m_Shared.SelectionContext = pasted;
		BeginRename(pasted);
		const bool cutPaste = m_ClipboardIsCut;
		if (cutPaste)
		{
			if (m_Shared.SceneModified)
				m_Shared.SceneModified(SceneModificationPhase::Begin);
			m_EntityToDelete = m_ClipboardEntity;
			ClearClipboard();
			m_Shared.CommitAfterPendingDeletion = true;
			if (m_Shared.SceneModified)
				m_Shared.SceneModified(SceneModificationPhase::Update);
		}
		else
			m_Shared.MarkModified(true);
	}

	void SceneHierarchyTreePanel::DuplicateSelectedEntity()
	{
		if (m_Shared.SelectionContext)
		{
			Entity duplicate = m_Shared.Context->DuplicateEntity(m_Shared.SelectionContext);
			if (!duplicate)
				return;
			m_Shared.SelectionContext = duplicate;
			BeginRename(duplicate);
			m_Shared.MarkModified(true);
		}
	}

	void SceneHierarchyTreePanel::DeleteSelectedEntity()
	{
		if (m_Shared.SelectionContext)
			m_EntityToDelete = m_Shared.SelectionContext;
	}

	bool SceneHierarchyTreePanel::HandleShortcut(int keyCode, bool control)
	{
		switch (keyCode)
		{
		case Key::X: if (control && m_Shared.SelectionContext) { CutSelectedEntity(); return true; } break;
		case Key::C: if (control && m_Shared.SelectionContext) { CopySelectedEntity(); return true; } break;
		case Key::V: if (control && CanPaste()) { PasteEntity(); return true; } break;
		case Key::D: if (control && m_Shared.SelectionContext) { DuplicateSelectedEntity(); return true; } break;
		case Key::F2: if (m_Shared.SelectionContext) { BeginRename(m_Shared.SelectionContext); return true; } break;
		case Key::Delete: if (m_Shared.SelectionContext) { DeleteSelectedEntity(); return true; } break;
		}
		return false;
	}


	void SceneHierarchyTreePanel::DrawEntityOperationsMenu()
	{
		const bool hasSelection = (bool)m_Shared.SelectionContext;
		const bool canPaste = CanPaste();
		if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection)) CutSelectedEntity();
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection)) CopySelectedEntity();
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) PasteEntity();
		ImGui::Separator();
		if (ImGui::MenuItem("Rename", "F2", false, hasSelection)) BeginRename(m_Shared.SelectionContext);
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) DuplicateSelectedEntity();
		const bool canCreatePrefab = hasSelection && m_Shared.PrefabCreationAllowed
			&& static_cast<bool>(m_Shared.PrefabCreate);
		if (ImGui::MenuItem(hasSelection && m_Shared.SelectionContext.HasComponent<PrefabLink>()
			? "Create Prefab Variant From Selection" : "Create Prefab From Selection", nullptr, false,
			canCreatePrefab))
			m_Shared.PrefabCreate(m_Shared.SelectionContext);
		if (hasSelection && m_Shared.PrefabCreationAllowed && m_Shared.PrefabAction
			&& m_Shared.SelectionContext.HasComponent<PrefabLink>())
		{
			const char* labels[] = { "Update Prefab (Keep Overrides)", "Revert All Prefab Overrides",
				"Apply All Overrides to Prefab", "Unpack Prefab", "Show Prefab Overrides in Console" };
			for (int action = 0; action < 5; ++action)
				if (ImGui::MenuItem(labels[action]))
				{
					m_Shared.PendingPrefabRoot = m_Shared.SelectionContext.GetUUID();
					m_Shared.PendingPrefabAction = action;
				}
		}
		if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) DeleteSelectedEntity();
		if (hasSelection && m_Shared.Context)
		{
			const bool hiddenSelf = m_Shared.Context->IsEditorHidden(m_Shared.SelectionContext);
			const bool hiddenByParent = !hiddenSelf
				&& !m_Shared.Context->IsVisibleInEditorHierarchy(m_Shared.SelectionContext);
			if (hiddenByParent)
				ImGui::MenuItem("Hidden in Scene by Parent", nullptr, false, false);
			else if (ImGui::MenuItem(hiddenSelf ? "Show in Scene" : "Hide in Scene"))
			{
				if (m_Shared.Context->SetEditorHidden(m_Shared.SelectionContext, !hiddenSelf))
					m_Shared.MarkModified();
			}
		}
		if (ImGui::MenuItem("Unparent", nullptr, false, hasSelection && m_Shared.Context && m_Shared.Context->GetParent(m_Shared.SelectionContext)))
		{
			if (m_Shared.Context->MoveEntity(m_Shared.SelectionContext, Entity{}, Scene::EntityPlacement::Root))
				m_Shared.MarkModified();
		}
		ImGui::Separator();

		// 从右键菜单创建的新对象直接作为当前选中实体的子对象；
		// 选中实体时在节点或空白处右键创建都遵循这个规则。
		Entity parentForNewObject = m_Shared.SelectionContext;
		auto CreateAsSelectedChild = [&](Entity entity)
		{
			if (parentForNewObject)
			{
				m_Shared.Context->SetParent(entity, parentForNewObject);
				m_ForceExpandParent = parentForNewObject;
			}
			else
			{
				m_ForceOpenSceneRoot = true;
			}
			m_Shared.SelectionContext = entity;
			BeginRename(entity);
			m_Shared.MarkModified(true);
		};

		if (ImGui::MenuItem("Create Empty Entity"))
		{
			Entity entity = m_Shared.Context->CreateEntity("Empty Entity");
			CreateAsSelectedChild(entity);
		}
		if (ImGui::MenuItem("Camera"))
		{
			const bool alreadyHasPrimary = m_Shared.Context
				&& m_Shared.Context->HasAuthoredPrimaryCamera();
			Entity camera = m_Shared.Context->CreateEntity("Camera");
			auto& cameraComponent = camera.AddComponent<C_Camera>();
			cameraComponent.Primary = !alreadyHasPrimary;
			CreateAsSelectedChild(camera);
		}

		auto CreatePrimitiveSprite = [&](const char* primitiveName)
		{
			AssetManager& assets = AssetManager::Get();
			if (!m_Shared.Project || !assets.GetRegistry().IsInitialized() ||
				assets.IsCookedPackageMounted())
			{
				TC_Core_Warn("Open a project before creating a '{0}' Sprite", primitiveName);
				return;
			}

			const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(primitiveName);
			if (!builtIn)
			{
				TC_Core_Error("Unknown built-in Sprite '{0}'", primitiveName);
				return;
			}
			const AssetHandle handle = builtIn->Handle;
			const Ref<Texture2D> texture = assets.LoadTexture(handle);
			if (!texture || texture == assets.GetMissingTexture())
			{
				TC_Core_Error("Could not decode the '{0}' Sprite asset", primitiveName);
				return;
			}

			Entity entity = m_Shared.Context->CreateEntity(primitiveName);
			auto& sprite = entity.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f });
			sprite.SpriteHandle = handle;
			sprite.Sprite = texture;
			CreateAsSelectedChild(entity);
		};

		auto FindCanvasAncestor = [&](Entity entity)
		{
			while (entity)
			{
				if (entity.HasComponent<Canvas>())
					return entity;
				entity = m_Shared.Context->GetParent(entity);
			}
			return Entity{};
		};
		auto FindReusableRootCanvas = [&]()
		{
			// Root order is the authored Hierarchy order, so multiple eligible Canvases
			// resolve predictably. Disabled, inactive, or editor-hidden roots are skipped
			// because placing a new control there would make it appear to be missing.
			for (UUID rootID : m_Shared.Context->GetRootEntityUUIDs())
			{
				Entity root = m_Shared.Context->FindEntityByUUID(rootID);
				if (root && root.HasComponent<Canvas>()
					&& root.GetComponent<Canvas>().Enabled
					&& m_Shared.Context->IsActiveInHierarchy(root)
					&& m_Shared.Context->IsVisibleInEditorHierarchy(root))
					return root;
			}
			return Entity{};
		};

		auto CreateUIElement = [&](const char* name, bool addImage,
			bool addText, bool addButton, int preset = 0)
		{
			Entity uiParent = parentForNewObject;
			Entity canvasOwner = FindCanvasAncestor(uiParent);
			if (!canvasOwner)
			{
				uiParent = FindReusableRootCanvas();
				if (!uiParent)
				{
					Entity canvas = m_Shared.Context->CreateEntity("Canvas");
					canvas.AddComponent<Canvas>();
					canvas.AddComponent<UIEventSystem>();
					uiParent = canvas;
					m_ForceOpenSceneRoot = true;
				}
				canvasOwner = uiParent;
			}
			// Older scenes may contain a Canvas authored before the event-system
			// dependency was automatic. Repair it while creating a control so a new
			// Button is immediately interactive.
			if (!canvasOwner.HasComponent<UIEventSystem>())
				canvasOwner.AddComponent<UIEventSystem>();

			Entity element = m_Shared.Context->CreateEntity(name);
			auto& rect = element.AddComponent<RectTransform>();
			if (addButton)
				rect.SizeDelta = { 160.0f, 36.0f };
			else if (addText)
				rect.SizeDelta = { 160.0f, 30.0f };

			if (addImage)
			{
				auto& image = element.AddComponent<UIImage>();
				if (addButton)
					image.Color = { 0.82f, 0.82f, 0.82f, 1.0f };
			}
			if (addButton)
				element.AddComponent<UIButton>();
			if (addText)
			{
				auto& text = element.AddComponent<UIText>();
				text.Text = "Text";
			}

            if(preset==1) { element.AddComponent<UISlider>(); rect.SizeDelta={200,24}; }
            if(preset==2)
            {
                element.AddComponent<UIInputField>(); rect.SizeDelta={240,36};
                element.GetComponent<UIImage>().Color={0.15f,0.15f,0.15f,1};
                element.GetComponent<UIText>().Text.clear();
            }
            if(preset==3) { element.AddComponent<UIScrollView>(); rect.SizeDelta={300,240}; rect.ClipChildren=true; }
			m_Shared.Context->SetParent(element, uiParent);
			// Match Unity's Button hierarchy: the selectable graphic lives on the
			// Button entity and the label is a separate, stretched child. This keeps
			// the label independently editable without making it intercept clicks.
			if (addButton)
			{
				Entity label = m_Shared.Context->CreateEntity("Text");
				auto& labelRect = label.AddComponent<RectTransform>();
				labelRect.AnchorMin = { 0.0f, 0.0f };
				labelRect.AnchorMax = { 1.0f, 1.0f };
				labelRect.SizeDelta = { 0.0f, 0.0f };
				auto& labelText = label.AddComponent<UIText>();
				labelText.Text = "Button";
				labelText.Alignment = TextAlignment::Center;
				labelText.Color = { 0.1f, 0.1f, 0.1f, 1.0f };
				m_Shared.Context->SetParent(label, element);
				m_ForceOpenEntityNodes.emplace(
					static_cast<uint64_t>(element.GetUUID()));
			}
			m_ForceExpandParent = uiParent;
			m_Shared.SelectionContext = element;
			BeginRename(element);
			m_Shared.MarkModified(true);
            return element;
		};

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::MenuItem("World Text"))
			{
				Entity text = m_Shared.Context->CreateEntity("World Text");
				text.AddComponent<TextRenderer>();
				// World Text participates in the camera/world transform pass. Keep it
				// outside a Canvas hierarchy even when a UI element is selected.
				if (FindCanvasAncestor(parentForNewObject))
				{
					m_ForceOpenSceneRoot = true;
					m_Shared.SelectionContext = text;
					BeginRename(text);
					m_Shared.MarkModified(true);
				}
				else
					CreateAsSelectedChild(text);
			}
			if (ImGui::BeginMenu("Sprites"))
			{
				AssetManager& assets = AssetManager::Get();
				const bool canCreatePrimitiveSprite = m_Shared.Project &&
					assets.GetRegistry().IsInitialized() && !assets.IsCookedPackageMounted();
				ImGui::BeginDisabled(!canCreatePrimitiveSprite);
				if (ImGui::MenuItem("Circle"))
					CreatePrimitiveSprite("Circle");
				if (ImGui::MenuItem("Square"))
					CreatePrimitiveSprite("Square");
				ImGui::EndDisabled();
				if (!canCreatePrimitiveSprite &&
					ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Open a project to create Sprites.");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Tilemap"))
			{
				if (ImGui::MenuItem("Rectangular"))
				{
					Entity grid = m_Shared.Context->CreateEntity("Grid");
					grid.AddComponent<Grid2D>();
					if (parentForNewObject)
						m_Shared.Context->SetParent(grid, parentForNewObject);
					else
						m_ForceOpenSceneRoot = true;
					Entity tilemap = m_Shared.Context->CreateEntity("Tilemap");
					tilemap.AddComponent<Tilemap2D>();
					tilemap.AddComponent<TilemapRenderer2D>();
					m_Shared.Context->SetParent(tilemap, grid);
					m_ForceExpandParent = grid;
					m_Shared.SelectionContext = tilemap;
					BeginRename(tilemap);
					m_Shared.MarkModified(true);
					m_Authors.m_TilePaletteOpenRequested = true;
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Effects"))
		{
			if (ImGui::MenuItem("Line"))
			{
				Entity line = m_Shared.Context->CreateEntity("Line");
				line.AddComponent<LineRenderer>();
				CreateAsSelectedChild(line);
			}
			if (ImGui::MenuItem("Particle System 2D"))
			{
				Entity particles = m_Shared.Context->CreateEntity("Particle System 2D");
				particles.AddComponent<ParticleSystem2D>();
				CreateAsSelectedChild(particles);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Light"))
		{
			if (ImGui::BeginMenu("2D"))
			{
				if (ImGui::MenuItem("Global Light 2D"))
				{
					Entity light = m_Shared.Context->CreateEntity("Global Light 2D");
					auto& component = light.AddComponent<Light2D>();
					component.Type = Light2DType::Global;
					CreateAsSelectedChild(light);
				}
				if (ImGui::MenuItem("Point Light 2D"))
				{
					Entity light = m_Shared.Context->CreateEntity("Point Light 2D");
					auto& component = light.AddComponent<Light2D>();
					component.Type = Light2DType::Point;
					CreateAsSelectedChild(light);
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Audio"))
		{
			if (ImGui::MenuItem("Audio Source"))
			{
				Entity source = m_Shared.Context->CreateEntity("Audio Source");
				source.AddComponent<AudioSource>();
				CreateAsSelectedChild(source);
			}
			if (ImGui::MenuItem("Audio Listener"))
			{
				Entity listener = m_Shared.Context->CreateEntity("Audio Listener");
				listener.AddComponent<AudioListener>();
				CreateAsSelectedChild(listener);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("UI (Canvas)"))
		{
			if (ImGui::MenuItem("Canvas"))
			{
				Entity canvas = m_Shared.Context->CreateEntity("Canvas");
				canvas.AddComponent<Canvas>();
				canvas.AddComponent<UIEventSystem>();
				CreateAsSelectedChild(canvas);
			}
			if (ImGui::MenuItem("Image"))
				CreateUIElement("Image", true, false, false);
			if (ImGui::MenuItem("Text"))
				CreateUIElement("Text", false, true, false);
            if (ImGui::MenuItem("Button"))
                CreateUIElement("Button", true, false, true);
            if (ImGui::MenuItem("Slider")) CreateUIElement("Slider",false,false,false,1);
            if (ImGui::MenuItem("Input Field")) CreateUIElement("Input Field",true,true,false,2);
            if (ImGui::MenuItem("Scroll View")) CreateUIElement("Scroll View",true,false,false,3);
			ImGui::EndMenu();
		}
	}

	void SceneHierarchyTreePanel::DrawEntityNode(Entity entity)
	{
		if (!entity)
			return;

        const uint64_t nodeID = static_cast<uint64_t>(entity.GetUUID());
        ImGui::SetNextItemOpen(m_HierarchySearch[0] || m_ExpandedEntities.contains(nodeID));
        auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		const bool activeInHierarchy = m_Shared.Context->IsActiveInHierarchy(entity);
		const bool hiddenSelf = m_Shared.Context->IsEditorHidden(entity);
		const bool visibleInEditor = m_Shared.Context->IsVisibleInEditorHierarchy(entity);
		const bool hiddenByParent = !hiddenSelf && !visibleInEditor;
		const bool dimmed = !activeInHierarchy || !visibleInEditor;
		if (dimmed)
		{
			const float alpha = visibleInEditor ? 1.0f : 0.68f;
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.45f, 0.45f, 0.45f, alpha));
		}

		const bool isSelected = (m_Shared.SelectionContext == entity) || std::find(m_Shared.MultiSelection.begin(),m_Shared.MultiSelection.end(),entity.GetUUID()) != m_Shared.MultiSelection.end();
		const auto children = m_Shared.Context->GetChildrenUUIDs(entity);
		const bool hasChildren = !children.empty();

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (isSelected)
			flags |= ImGuiTreeNodeFlags_Selected;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		// 新建的子对象刚创建时，父节点强制展开一次，确保子对象立即可见。
		if (m_ForceExpandParent == entity)
		{
			ImGui::SetNextItemOpen(true);
			m_ForceExpandParent = {};
		}
		if (m_ForceOpenEntityNodes.erase(static_cast<uint64_t>(entity.GetUUID())) > 0)
			ImGui::SetNextItemOpen(true);
		// ImGui uses Header (rather than HeaderActive) for an idle selected
		// tree item.  Scope Unity's blue selection colors to the hierarchy row so
		// component headers and menus keep their neutral gray treatment.
		if (isSelected)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(44.0f / 255.0f, 93.0f / 255.0f, 135.0f / 255.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(58.0f / 255.0f, 112.0f / 255.0f, 157.0f / 255.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(36.0f / 255.0f, 79.0f / 255.0f, 115.0f / 255.0f, 1.0f));
		}

		const bool renameActive = (m_RenameEntity == entity);
		bool open = ImGui::TreeNodeEx((void*)(uint64_t)entity.GetUUID(), flags, "");
        if (!m_HierarchySearch[0]) {
            if (open) m_ExpandedEntities.insert(nodeID); else m_ExpandedEntities.erase(nodeID);
        }
        if (!ImGui::IsItemVisible() && !renameActive && !ImGui::IsItemActive()) {
            if (isSelected) ImGui::PopStyleColor(3);
            if (dimmed) ImGui::PopStyleColor();
            return;
        }
        const bool rowHovered = ImGui::IsItemHovered();
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float iconSize = std::min(std::round(ImGui::GetFontSize() * 0.95f),
			std::max(1.0f, itemMax.y - itemMin.y - 4.0f));
		const float iconY = std::round(itemMin.y +
			(itemMax.y - itemMin.y - iconSize) * 0.5f);
		const float visibilityIconX = std::round(itemMin.x +
			ImGui::GetTreeNodeToLabelSpacing());
		const ImVec2 visibilityMinimum(visibilityIconX, iconY);
		const ImVec2 visibilityMaximum(visibilityIconX + iconSize, iconY + iconSize);
		DrawIcon(m_Shared.Icons, visibleInEditor ? EditorIcon::EyeOn : EditorIcon::EyeOff,
			visibilityMinimum, visibilityMaximum,
			hiddenByParent ? IM_COL32(120, 120, 120, 115)
				: hiddenSelf ? IM_COL32(155, 155, 155, 190)
				: IM_COL32(175, 175, 175, 210));
		const float entityIconX = visibilityMaximum.x + GetHierarchyIconTextGap();
		DrawIcon(m_Shared.Icons, ResolveEntityEditorIcon(entity),
			ImVec2(entityIconX, iconY), ImVec2(entityIconX + iconSize, iconY + iconSize),
			!dimmed ? IM_COL32_WHITE
				: visibleInEditor ? IM_COL32(150, 150, 150, 210)
				: IM_COL32(125, 125, 125, 150));
		const float textOffsetX = entityIconX + iconSize + GetHierarchyIconTextGap();
		const float textOffsetY = std::round(itemMin.y +
			(itemMax.y - itemMin.y - ImGui::GetTextLineHeight()) * 0.5f);
		if (!renameActive)
			ImGui::GetWindowDrawList()->AddText(ImVec2(textOffsetX, textOffsetY),
				ImGui::GetColorU32(ImGuiCol_Text), tag.c_str());

		const bool visibilityHovered = ImRect(visibilityMinimum,
			visibilityMaximum).Contains(ImGui::GetMousePos());
		bool visibilityClicked = false;
		if (visibilityHovered)
		{
			ImGui::SetMouseCursor(hiddenByParent ? ImGuiMouseCursor_Arrow
				: ImGuiMouseCursor_Hand);
			visibilityClicked = !hiddenByParent &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			if (visibilityClicked && m_Shared.Context->SetEditorHidden(entity, !hiddenSelf))
				m_Shared.MarkModified();
		}

		if (isSelected)
			ImGui::PopStyleColor(3);

        if (!visibilityClicked && ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (ImGui::GetIO().KeyShift && static_cast<uint64_t>(m_Shared.SelectionAnchor) != 0)
            {
                auto first = std::find(m_PreviousHierarchyOrder.begin(), m_PreviousHierarchyOrder.end(), m_Shared.SelectionAnchor);
                auto last = std::find(m_PreviousHierarchyOrder.begin(), m_PreviousHierarchyOrder.end(), entity.GetUUID());
                if (first != m_PreviousHierarchyOrder.end() && last != m_PreviousHierarchyOrder.end())
                {
                    if (first > last) std::swap(first, last);
                    if (!ImGui::GetIO().KeyCtrl) m_Shared.MultiSelection.clear();
                    for (auto current = first; current <= last; ++current)
                        if (std::find(m_Shared.MultiSelection.begin(), m_Shared.MultiSelection.end(), *current) == m_Shared.MultiSelection.end())
                            m_Shared.MultiSelection.push_back(*current);
                }
                else { m_Shared.MultiSelection.clear(); m_Shared.SelectionAnchor = entity.GetUUID(); }
                m_Shared.SelectionContext = entity;
            }
            else if (ImGui::GetIO().KeyCtrl)
            {
                m_Shared.SelectionAnchor = entity.GetUUID();
                if(m_Shared.MultiSelection.empty() && m_Shared.SelectionContext) m_Shared.MultiSelection.push_back(m_Shared.SelectionContext.GetUUID());
                auto found=std::find(m_Shared.MultiSelection.begin(),m_Shared.MultiSelection.end(),entity.GetUUID());
                if(found==m_Shared.MultiSelection.end()) { m_Shared.MultiSelection.push_back(entity.GetUUID()); m_Shared.SelectionContext=entity; }
                else { m_Shared.MultiSelection.erase(found); m_Shared.SelectionContext=m_Shared.MultiSelection.empty()?Entity{}:m_Shared.Context->FindEntityByUUID(m_Shared.MultiSelection.back()); }
            }
            else { m_Shared.MultiSelection.clear(); m_Shared.SelectionContext=entity; m_Shared.SelectionAnchor=entity.GetUUID(); }
        }
		if (!renameActive && !visibilityHovered && rowHovered
			&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_Shared.SelectionContext = entity;
			m_Shared.FrameEntityRequest = entity.GetUUID();
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			m_Shared.SelectionContext = entity;

		if (ImGui::BeginPopupContextItem())
		{
			m_Shared.SelectionContext = entity;
			DrawEntityOperationsMenu();
			ImGui::EndPopup();
		}

		if (ImGui::BeginDragDropSource())
		{
			const uint64_t entityUUID = static_cast<uint64_t>(entity.GetUUID());
			ImGui::SetDragDropPayload(SceneEntityDragDropPayloadID, &entityUUID, sizeof(entityUUID));
			ImGui::Text("Move %s", tag.c_str());
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiDragDropFlags dropFlags = ImGuiDragDropFlags_AcceptBeforeDelivery |
				ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			const bool acceptedAsset = m_Inspector.AcceptCSharpScriptDrop(entity)
				|| AcceptPrefabDrop(entity);
			if (!acceptedAsset)
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				SceneEntityDragDropPayloadID, dropFlags))
			{
				Entity draggedEntity = GetDraggedSceneEntity(payload, m_Shared.Context);
				if (draggedEntity && draggedEntity != entity)
				{
					const HierarchyDropZone zone = GetHierarchyDropZone(itemMin, itemMax);
					DrawHierarchyDropPreview(itemMin, itemMax, zone);
					if (payload->IsDelivery())
					{
						Scene::EntityPlacement placement = Scene::EntityPlacement::Child;
						if (zone == HierarchyDropZone::Before)
							placement = Scene::EntityPlacement::Before;
						else if (zone == HierarchyDropZone::After)
							placement = Scene::EntityPlacement::After;

						if (m_Shared.Context->MoveEntity(draggedEntity, entity, placement))
						{
							m_Shared.SelectionContext = draggedEntity;
							if (placement == Scene::EntityPlacement::Child)
								m_ForceExpandParent = entity;
							m_Shared.MarkModified();
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (visibilityHovered)
		{
			if (hiddenByParent)
				ImGui::SetTooltip("Hidden in Scene by parent");
			else
				ImGui::SetTooltip(hiddenSelf ? "Show in Scene" : "Hide in Scene");
		}

		if (renameActive)
		{
			ImGui::SetCursorScreenPos(ImVec2(textOffsetX, textOffsetY));
			ImGui::SetNextItemWidth(std::max(40.0f, itemMax.x - textOffsetX - ImGui::GetStyle().ItemInnerSpacing.x));
			if (m_RenameFocus)
			{
				ImGui::SetScrollHereY(0.5f);
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}

			bool commit = ImGui::InputText("##EntityRename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			if (commit || ImGui::IsItemDeactivated())
			{
				if (m_Shared.Context->RenameEntity(entity, m_RenameBuffer))
					m_Shared.MarkModified();
				m_RenameEntity = {};
			}
		}


		if (dimmed)
			ImGui::PopStyleColor();
	}
}
