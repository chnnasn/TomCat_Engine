#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "TomCat/Scene/Components.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"

namespace TomCat {

	static constexpr const char* SceneEntityDragDropPayloadID = "SCENE_ENTITY_UUID";

	enum class HierarchyDropZone
	{
		Before,
		Child,
		After
	};

	static Entity GetDraggedSceneEntity(const ImGuiPayload* payload, const Ref<Scene>& scene)
	{
		if (!payload || !scene || payload->DataSize != sizeof(uint64_t))
			return {};

		const uint64_t rawUUID = *static_cast<const uint64_t*>(payload->Data);
		if (rawUUID == 0)
			return {};
		return scene->FindEntityByUUID(UUID(rawUUID));
	}

	static HierarchyDropZone GetHierarchyDropZone(const ImVec2& itemMin, const ImVec2& itemMax)
	{
		const float height = std::max(1.0f, itemMax.y - itemMin.y);
		const float relativeY = ImGui::GetMousePos().y - itemMin.y;
		if (relativeY < height / 3.0f)
			return HierarchyDropZone::Before;
		if (relativeY > height * 2.0f / 3.0f)
			return HierarchyDropZone::After;
		return HierarchyDropZone::Child;
	}

	static void DrawHierarchyDropPreview(const ImVec2& itemMin, const ImVec2& itemMax,
		HierarchyDropZone zone)
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
		if (zone == HierarchyDropZone::Child)
			drawList->AddRect(itemMin, itemMax, color, 2.0f, 0, 2.0f);
		else
		{
			const float y = zone == HierarchyDropZone::Before ? itemMin.y : itemMax.y;
			drawList->AddLine(ImVec2(itemMin.x, y), ImVec2(itemMax.x, y), color, 2.0f);
		}
	}


	// 前向声明DrawProperty函数
	static void DrawProperty(const std::string& label, float columnWidth = 100.0f);
	static ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
	{
		return texture
			? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
			: nullptr;
	}

	static void DrawIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& minimum, const ImVec2& maximum, ImU32 tint = IM_COL32_WHITE)
	{
		if (!icons)
			return;
		const Ref<Texture2D>& texture = icons->Get(icon);
		if (!texture)
			return;
		ImGui::GetWindowDrawList()->AddImage(ToImGuiTextureID(texture), minimum, maximum,
			ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), tint);
	}

	static std::string LowerASCII(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	static bool MatchesSpriteSearch(const AssetMetadata& metadata, const char* search)
	{
		if (!search || search[0] == '\0')
			return true;
		const std::string query = LowerASCII(search);
		return LowerASCII(PathToUTF8(metadata.FilePath)).find(query) != std::string::npos;
	}

	static float DrawTreeRowIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& itemMin, const ImVec2& itemMax, ImU32 tint = IM_COL32_WHITE)
	{
		const float iconSize = std::min(std::round(ImGui::GetFontSize() * 0.78f),
			std::max(1.0f, itemMax.y - itemMin.y - 4.0f));
		const float x = std::round(itemMin.x + ImGui::GetTreeNodeToLabelSpacing());
		const float y = std::round(itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
		DrawIcon(icons, icon, ImVec2(x, y), ImVec2(x + iconSize, y + iconSize), tint);
		return iconSize;
	}

	static float GetHierarchyIconTextGap()
	{
		return std::max(3.0f, std::round(ImGui::GetFontSize() * 0.12f));
	}

	static float GetCompactCheckboxWidth(float scale = 0.68f)
	{
		return std::max(10.0f, std::round(ImGui::GetFrameHeight() * scale));
	}

	static bool DrawCompactCheckbox(const char* id, bool* v, float scale = 0.68f)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID widgetID = window->GetID(id);

		const float frameHeight = ImGui::GetFrameHeight();
		const float squareSize = GetCompactCheckboxWidth(scale);
		const ImVec2 pos = window->DC.CursorPos;
		const ImRect bb(pos, ImVec2(pos.x + squareSize, pos.y + frameHeight));

		ImGui::ItemSize(bb, style.FramePadding.y);
		if (!ImGui::ItemAdd(bb, widgetID))
			return false;

		bool hovered = false;
		bool held = false;
		bool pressed = ImGui::ButtonBehavior(bb, widgetID, &hovered, &held);
		if (pressed)
		{
			*v = !(*v);
			ImGui::MarkItemEdited(widgetID);
		}

		const ImVec2 squareMin(bb.Min.x,
			std::round(bb.Min.y + (bb.GetHeight() - squareSize) * 0.5f));
		const ImVec2 squareMax(squareMin.x + squareSize, squareMin.y + squareSize);
		const ImU32 frameColor = ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive
			: hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
		ImGui::RenderFrame(squareMin, squareMax, frameColor, true,
			std::min(2.0f, style.FrameRounding));
		if (*v)
		{
			const float padding = std::max(2.0f, std::floor(squareSize / 6.0f));
			ImGui::RenderCheckMark(window->DrawList,
				ImVec2(squareMin.x + padding, squareMin.y + padding),
				ImGui::GetColorU32(ImGuiCol_CheckMark), squareSize - padding * 2.0f);
		}

		return pressed;
	}

	static EditorIcon ResolveAutomaticEntityEditorIcon(Entity entity)
	{
		if (entity.HasComponent<C_Camera>())
			return EditorIcon::Camera;
		if (entity.HasComponent<SpriteRenderer>())
			return EditorIcon::Sprite;
		if (entity.HasComponent<Rigidbody2D>())
			return EditorIcon::Rigidbody2D;
		if (entity.HasComponent<BoxCollider2D>() || entity.HasComponent<CircleCollider2D>())
			return EditorIcon::BoxCollider2D;
		return EditorIcon::Entity;
	}

	static EditorIcon ResolveEntityEditorIcon(Entity entity)
	{
		if (!entity || !entity.HasComponent<EntityMetadata>())
			return EditorIcon::Entity;

		switch (entity.GetComponent<EntityMetadata>().HierarchyIcon)
		{
			case EntityIconMode::Automatic: return ResolveAutomaticEntityEditorIcon(entity);
			case EntityIconMode::Entity: return EditorIcon::Entity;
			case EntityIconMode::Camera: return EditorIcon::Camera;
			case EntityIconMode::Sprite: return EditorIcon::Sprite;
			case EntityIconMode::Rigidbody2D: return EditorIcon::Rigidbody2D;
			case EntityIconMode::Collider2D: return EditorIcon::BoxCollider2D;
		}
		return EditorIcon::Entity;
	}

	static EditorIcon ResolveEntityIconChoice(Entity entity, EntityIconMode mode)
	{
		if (mode == EntityIconMode::Automatic)
			return ResolveAutomaticEntityEditorIcon(entity);
		switch (mode)
		{
			case EntityIconMode::Entity: return EditorIcon::Entity;
			case EntityIconMode::Camera: return EditorIcon::Camera;
			case EntityIconMode::Sprite: return EditorIcon::Sprite;
			case EntityIconMode::Rigidbody2D: return EditorIcon::Rigidbody2D;
			case EntityIconMode::Collider2D: return EditorIcon::BoxCollider2D;
			case EntityIconMode::Automatic: break;
		}
		return EditorIcon::Entity;
	}

	static const char* GetEntityIconModeLabel(EntityIconMode mode)
	{
		switch (mode)
		{
			case EntityIconMode::Automatic: return "Automatic";
			case EntityIconMode::Entity: return "Entity";
			case EntityIconMode::Camera: return "Camera";
			case EntityIconMode::Sprite: return "Sprite";
			case EntityIconMode::Rigidbody2D: return "Rigidbody 2D";
			case EntityIconMode::Collider2D: return "Collider 2D";
		}
		return "Entity";
	}

	static bool DrawEntityIconSelector(const Ref<EditorIconSet>& icons, Entity entity,
		bool editable)
	{
		if (!entity || !entity.HasComponent<EntityMetadata>())
			return false;

		auto& metadata = entity.GetComponent<EntityMetadata>();
		const float buttonSize = ImGui::GetFrameHeight();
		ImGui::PushID("EntityIconSelector");
		if (!editable)
			ImGui::BeginDisabled();
		const bool pressed = ImGui::InvisibleButton("##EntityIconButton",
			ImVec2(buttonSize, buttonSize));
		const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		if (!editable)
			ImGui::EndDisabled();

		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		if (hovered && editable)
			ImGui::GetWindowDrawList()->AddRectFilled(itemMin, itemMax,
				ImGui::GetColorU32(ImGuiCol_HeaderHovered), 2.0f);
		const float padding = std::max(2.0f, std::round(buttonSize * 0.14f));
		DrawIcon(icons, ResolveEntityEditorIcon(entity),
			ImVec2(itemMin.x + padding, itemMin.y + padding),
			ImVec2(itemMax.x - padding, itemMax.y - padding),
			editable ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 210));

		if (hovered)
		{
			if (editable)
				ImGui::SetTooltip("Entity icon: %s\nClick to change.",
					GetEntityIconModeLabel(metadata.HierarchyIcon));
			else
				ImGui::SetTooltip("Entity icons are read-only while the scene is running.");
		}
		if (pressed && editable)
			ImGui::OpenPopup("EntityIconPicker");

		bool changed = false;
		if (ImGui::BeginPopup("EntityIconPicker"))
		{
			ImGui::TextUnformatted("Entity Icon");
			ImGui::Separator();
			struct IconChoice
			{
				EntityIconMode Mode;
				const char* Label;
			};
			static constexpr IconChoice choices[] = {
				{ EntityIconMode::Automatic, "Automatic" },
				{ EntityIconMode::Entity, "Entity" },
				{ EntityIconMode::Camera, "Camera" },
				{ EntityIconMode::Sprite, "Sprite" },
				{ EntityIconMode::Rigidbody2D, "Rigidbody 2D" },
				{ EntityIconMode::Collider2D, "Collider 2D" }
			};

			if (!editable)
				ImGui::BeginDisabled();
			for (const IconChoice& choice : choices)
			{
				ImGui::PushID(static_cast<int>(choice.Mode));
				const float rowHeight = ImGui::GetFrameHeight() + 4.0f;
				const bool selected = metadata.HierarchyIcon == choice.Mode;
				const bool selectedNow = ImGui::Selectable("##IconChoice", selected,
					ImGuiSelectableFlags_None, ImVec2(190.0f, rowHeight));
				const ImVec2 rowMin = ImGui::GetItemRectMin();
				const float iconPadding = 3.0f;
				const float iconSize = rowHeight - iconPadding * 2.0f;
				DrawIcon(icons, ResolveEntityIconChoice(entity, choice.Mode),
					ImVec2(rowMin.x + iconPadding, rowMin.y + iconPadding),
					ImVec2(rowMin.x + iconPadding + iconSize, rowMin.y + iconPadding + iconSize));
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(rowMin.x + rowHeight + ImGui::GetStyle().ItemInnerSpacing.x,
						rowMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f),
					ImGui::GetColorU32(ImGuiCol_Text), choice.Label);
				if (selectedNow && editable)
				{
					if (!selected)
					{
						metadata.HierarchyIcon = choice.Mode;
						changed = true;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			if (!editable)
				ImGui::EndDisabled();
			ImGui::EndPopup();
		}
		ImGui::PopID();
		return changed;
	}

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);

	}

	void SceneHierarchyPanel::SetProject(const Ref<Project>& project)
	{
		m_Project = project;
		if (m_Context && m_Project)
			m_Context->SetPhysics2DSettings(m_Project->GetSettings().Physics2D);
	}

	SceneHierarchyPanel::ColliderEditMode SceneHierarchyPanel::GetColliderEditMode() const
	{
		if (!m_ColliderEditingAllowed || m_ColliderEditMode == ColliderEditMode::None || !m_SelectionContext
			|| m_SelectionContext.GetUUID() != m_ColliderEditEntity)
			return ColliderEditMode::None;

		if (m_ColliderEditMode == ColliderEditMode::Box
			&& !m_SelectionContext.HasComponent<BoxCollider2D>())
			return ColliderEditMode::None;
		if (m_ColliderEditMode == ColliderEditMode::Circle
			&& !m_SelectionContext.HasComponent<CircleCollider2D>())
			return ColliderEditMode::None;
		return m_ColliderEditMode;
	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context, bool clearSelection, bool remapSelection)
	{
		UUID selectedUUID{};
		const bool hadSelection = !clearSelection && remapSelection && (bool)m_SelectionContext;
		if (hadSelection)
			selectedUUID = m_SelectionContext.GetUUID();
		const bool contextChanged = m_Context != context;
		m_Context = context;
		if (m_Context && m_Project)
			m_Context->SetPhysics2DSettings(m_Project->GetSettings().Physics2D);
		m_ForceExpandParent = {};
		m_ForceOpenSceneRoot = false;
		m_EntityToDelete = {};
		m_RenameEntity = {};
		m_RenameFocus = false;
		m_NameEditingEntity = {};
		m_SpritePickerOpen = false;
		m_SpritePickerEntity = UUID(0);
		m_SpriteSearch.fill('\0');
		ClearColliderEditMode();
		if (contextChanged)
			ClearClipboard();
		if (clearSelection)
			m_SelectionContext = {};
		else if (hadSelection && m_Context)
		{
			Entity remappedEntity = m_Context->FindEntityByUUID(selectedUUID);
			if (remappedEntity)
				m_SelectionContext = remappedEntity;
			else
				m_SelectionContext = {};
		}
	}

	void SceneHierarchyPanel::ClearClipboard()
	{
		m_ClipboardEntity = {};
		m_ClipboardScene = nullptr;
		m_ClipboardIsCut = false;
	}

	void SceneHierarchyPanel::MarkModified()
	{
		if (m_SceneModifiedCallback)
			m_SceneModifiedCallback();
	}

	bool SceneHierarchyPanel::FlushPendingDeletion()
	{
		if (!m_Context || !m_EntityToDelete)
			return false;

		Entity entity = m_EntityToDelete;
		m_EntityToDelete = {};
		if (m_SpritePickerOpen && m_SpritePickerEntity == entity.GetUUID())
		{
			m_SpritePickerOpen = false;
			m_SpritePickerEntity = UUID(0);
		}
		UUID selectedUUID{};
		const bool hadOtherSelection = m_SelectionContext && m_SelectionContext != entity;
		if (hadOtherSelection)
			selectedUUID = m_SelectionContext.GetUUID();
		UUID clipboardUUID{};
		const bool hadClipboard = m_ClipboardEntity && m_ClipboardScene == m_Context;
		if (hadClipboard)
			clipboardUUID = m_ClipboardEntity.GetUUID();
		if (m_SelectionContext == entity)
			m_SelectionContext = {};
		if (m_ClipboardEntity == entity)
			ClearClipboard();
		m_Context->DestroyEntity(entity);
		if (hadOtherSelection && !m_Context->FindEntityByUUID(selectedUUID))
			m_SelectionContext = {};
		if (hadClipboard && !m_Context->FindEntityByUUID(clipboardUUID))
			ClearClipboard();
		MarkModified();
		return true;
	}

	void SceneHierarchyPanel::OnImGuiRender(bool* hierarchyOpen, bool* inspectorOpen)
	{
		if (m_ColliderEditMode != ColliderEditMode::None
			&& GetColliderEditMode() == ColliderEditMode::None)
			ClearColliderEditMode();
		m_HierarchyFocused = false;
		if (!hierarchyOpen || *hierarchyOpen)
		{
		const bool hierarchyVisible = ImGui::Begin("Hierarchy", hierarchyOpen);
		m_HierarchyFocused = hierarchyVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (hierarchyVisible && m_Context)
		{
			FlushPendingDeletion();
			const std::string& sceneName = m_Context->GetSceneName();
			if (m_ForceOpenSceneRoot)
			{
				ImGui::SetNextItemOpen(true);
				m_ForceOpenSceneRoot = false;
			}
			ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding;
			bool rootOpen = ImGui::TreeNodeEx((void*)m_Context.get(), rootFlags, "");
			const ImVec2 rootItemMin = ImGui::GetItemRectMin();
			const ImVec2 rootItemMax = ImGui::GetItemRectMax();
			const float rootIconSize = DrawTreeRowIcon(m_Icons, EditorIcon::SceneOpen,
				rootItemMin, rootItemMax);
			const float rootTextX = rootItemMin.x + ImGui::GetTreeNodeToLabelSpacing() +
				rootIconSize + GetHierarchyIconTextGap();
			const float rootTextY = std::round(rootItemMin.y +
				(rootItemMax.y - rootItemMin.y - ImGui::GetTextLineHeight()) * 0.5f);
			ImGui::GetWindowDrawList()->AddText(ImVec2(rootTextX, rootTextY),
				ImGui::GetColorU32(ImGuiCol_Text), sceneName.c_str());

			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				m_SelectionContext = {};
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				m_SelectionContext = {};

			if (ImGui::BeginPopupContextItem())
			{
				m_SelectionContext = {};
				DrawEntityOperationsMenu();
				ImGui::EndPopup();
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery |
					ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID, flags))
				{
					Entity draggedEntity = GetDraggedSceneEntity(payload, m_Context);
					if (draggedEntity && payload->IsDelivery() && m_Context->MoveEntity(
						draggedEntity, Entity{}, Scene::EntityPlacement::Root))
					{
						m_SelectionContext = draggedEntity;
						MarkModified();
					}
				}
				ImGui::EndDragDropTarget();
			}

			if (rootOpen)
			{
				for (UUID rootUUID : m_Context->GetRootEntityUUIDs())
				{
					Entity entity = m_Context->FindEntityByUUID(rootUUID);
					if (entity)
						DrawEntityNode(entity);
				}
				ImGui::TreePop();
			}

			FlushPendingDeletion();


			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
				m_SelectionContext = {};


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
					const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
					if (metadata && metadata->Type == AssetType::Scene && m_SceneLoadCallback)
						m_SceneLoadCallback(handle);
					else if (metadata && metadata->Type == AssetType::Texture2D && m_SpriteCreateCallback)
						m_SpriteCreateCallback(handle);
				}
			}
			else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				SceneEntityDragDropPayloadID, flags | ImGuiDragDropFlags_AcceptBeforeDelivery))
			{
				Entity draggedEntity = GetDraggedSceneEntity(payload, m_Context);
				if (draggedEntity && payload->IsDelivery() && m_Context->MoveEntity(
					draggedEntity, Entity{}, Scene::EntityPlacement::Root))
				{
					m_SelectionContext = draggedEntity;
					MarkModified();
				}
			}
			ImGui::EndDragDropTarget();
		}
		}

		ImGui::End();
		}

		if (!inspectorOpen || *inspectorOpen)
		{
			const bool inspectorVisible = ImGui::Begin("Inspector", inspectorOpen);
			if (inspectorVisible && m_SelectionContext)
				DrawComponents(m_SelectionContext);
			ImGui::End();
		}
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		if (m_SelectionContext != entity)
			ClearColliderEditMode();
		if (m_SpritePickerOpen && m_SelectionContext != entity)
		{
			m_SpritePickerOpen = false;
			m_SpritePickerEntity = UUID(0);
		}
		m_SelectionContext = entity;
	}

	bool SceneHierarchyPanel::CanPaste() const
	{
		if (!m_ClipboardEntity || !m_ClipboardScene || m_ClipboardScene != m_Context)
			return false;

		return (bool)m_ClipboardScene->FindEntityByUUID(m_ClipboardEntity.GetUUID());
	}

	void SceneHierarchyPanel::BeginRename(Entity entity)
	{
		if (!entity)
			return;
		m_RenameEntity = entity;
		strncpy_s(m_RenameBuffer, sizeof(m_RenameBuffer), entity.GetName().c_str(), _TRUNCATE);
		m_RenameFocus = true;
	}

	void SceneHierarchyPanel::CutSelectedEntity()
	{
		if (!m_SelectionContext)
			return;
		m_ClipboardEntity = m_SelectionContext;
		m_ClipboardScene = m_Context;
		m_ClipboardIsCut = true;
	}

	void SceneHierarchyPanel::CopySelectedEntity()
	{
		if (!m_SelectionContext)
			return;
		m_ClipboardEntity = m_SelectionContext;
		m_ClipboardScene = m_Context;
		m_ClipboardIsCut = false;
	}

	void SceneHierarchyPanel::PasteEntity()
	{
		if (!CanPaste())
			return;
		Entity pasted = m_Context->DuplicateEntity(m_ClipboardEntity);
		if (!pasted)
			return;
		m_SelectionContext = pasted;
		BeginRename(pasted);
		if (m_ClipboardIsCut)
		{
			m_EntityToDelete = m_ClipboardEntity;
			ClearClipboard();
		}
		MarkModified();
	}

	void SceneHierarchyPanel::DuplicateSelectedEntity()
	{
		if (m_SelectionContext)
		{
			Entity duplicate = m_Context->DuplicateEntity(m_SelectionContext);
			if (!duplicate)
				return;
			m_SelectionContext = duplicate;
			BeginRename(duplicate);
			MarkModified();
		}
	}

	void SceneHierarchyPanel::DeleteSelectedEntity()
	{
		if (m_SelectionContext)
			m_EntityToDelete = m_SelectionContext;
	}

	bool SceneHierarchyPanel::HandleShortcut(int keyCode, bool control)
	{
		switch (keyCode)
		{
		case Key::X: if (control && m_SelectionContext) { CutSelectedEntity(); return true; } break;
		case Key::C: if (control && m_SelectionContext) { CopySelectedEntity(); return true; } break;
		case Key::V: if (control && CanPaste()) { PasteEntity(); return true; } break;
		case Key::D: if (control && m_SelectionContext) { DuplicateSelectedEntity(); return true; } break;
		case Key::F2: if (m_SelectionContext) { BeginRename(m_SelectionContext); return true; } break;
		case Key::Delete: if (m_SelectionContext) { DeleteSelectedEntity(); return true; } break;
		}
		return false;
	}

	void SceneHierarchyPanel::DrawEntityOperationsMenu()
	{
		const bool hasSelection = (bool)m_SelectionContext;
		const bool canPaste = CanPaste();
		if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection)) CutSelectedEntity();
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection)) CopySelectedEntity();
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, canPaste)) PasteEntity();
		ImGui::Separator();
		if (ImGui::MenuItem("Rename", "F2", false, hasSelection)) BeginRename(m_SelectionContext);
		if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasSelection)) DuplicateSelectedEntity();
		if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) DeleteSelectedEntity();
		if (ImGui::MenuItem("Unparent", nullptr, false, hasSelection && m_Context && m_Context->GetParent(m_SelectionContext)))
		{
			if (m_Context->MoveEntity(m_SelectionContext, Entity{}, Scene::EntityPlacement::Root))
				MarkModified();
		}
		ImGui::Separator();

		// 从右键菜单创建的新对象直接作为当前选中实体的子对象；
		// 选中实体时在节点或空白处右键创建都遵循这个规则。
		Entity parentForNewObject = m_SelectionContext;
		auto CreateAsSelectedChild = [&](Entity entity)
		{
			if (parentForNewObject)
			{
				m_Context->SetParent(entity, parentForNewObject);
				m_ForceExpandParent = parentForNewObject;
			}
			else
			{
				m_ForceOpenSceneRoot = true;
			}
			m_SelectionContext = entity;
			BeginRename(entity);
			MarkModified();
		};

		if (ImGui::MenuItem("Create Empty Entity"))
		{
			Entity entity = m_Context->CreateEntity("Empty Entity");
			CreateAsSelectedChild(entity);
		}
		if (ImGui::MenuItem("Camera"))
		{
			const bool alreadyHasPrimary = m_Context && (bool)m_Context->GetPrimaryCameraEntity();
			Entity camera = m_Context->CreateEntity("Camera");
			auto& cameraComponent = camera.AddComponent<C_Camera>();
			cameraComponent.Primary = !alreadyHasPrimary;
			CreateAsSelectedChild(camera);
		}

		auto CreatePrimitiveSprite = [&](const char* primitiveName)
		{
			AssetManager& assets = AssetManager::Get();
			if (!m_Project || !assets.GetRegistry().IsInitialized() ||
				assets.IsCookedPackageMounted())
			{
				TC_Core_Warn("Open a writable project before creating a '{0}' Sprite", primitiveName);
				return;
			}

			const AssetHandle handle = EnsurePrimitiveSpriteAsset(assets, primitiveName);
			if (static_cast<uint64_t>(handle) == 0)
			{
				TC_Core_Error("Could not create or import the '{0}' Sprite asset", primitiveName);
				return;
			}
			const Ref<Texture2D> texture = assets.LoadTexture(handle);
			if (!texture || texture == assets.GetMissingTexture())
			{
				TC_Core_Error("Could not decode the '{0}' Sprite asset", primitiveName);
				return;
			}

			Entity entity = m_Context->CreateEntity(primitiveName);
			auto& sprite = entity.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f });
			sprite.SpriteHandle = handle;
			sprite.Sprite = texture;
			CreateAsSelectedChild(entity);
		};

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::BeginMenu("Sprites"))
			{
				AssetManager& assets = AssetManager::Get();
				const bool canCreatePrimitiveSprite = m_Project &&
					assets.GetRegistry().IsInitialized() && !assets.IsCookedPackageMounted();
				ImGui::BeginDisabled(!canCreatePrimitiveSprite);
				if (ImGui::MenuItem("Circle"))
					CreatePrimitiveSprite("Circle");
				if (ImGui::MenuItem("Square"))
					CreatePrimitiveSprite("Square");
				ImGui::EndDisabled();
				if (!canCreatePrimitiveSprite &&
					ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Open a writable project to create asset-backed Sprites.");
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Effects"))
		{
			if (ImGui::MenuItem("Line"))
			{
				Entity line = m_Context->CreateEntity("Line");
				line.AddComponent<LineRenderer>();
				CreateAsSelectedChild(line);
			}
			ImGui::EndMenu();
		}
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		if (!entity)
			return;

		auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		const bool visible = tagComponent.Visible;
		if (!visible)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));

		const bool isSelected = (m_SelectionContext == entity);
		const auto children = m_Context->GetChildrenUUIDs(entity);
		const bool hasChildren = !children.empty();

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding;
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
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float iconSize = DrawTreeRowIcon(m_Icons, ResolveEntityEditorIcon(entity), itemMin, itemMax,
			visible ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 210));
		const float textOffsetX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing() +
			iconSize + GetHierarchyIconTextGap();
		const float textOffsetY = std::round(itemMin.y +
			(itemMax.y - itemMin.y - ImGui::GetTextLineHeight()) * 0.5f);
		if (!renameActive)
			ImGui::GetWindowDrawList()->AddText(ImVec2(textOffsetX, textOffsetY),
				ImGui::GetColorU32(ImGuiCol_Text), tag.c_str());

		if (isSelected)
			ImGui::PopStyleColor(3);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_SelectionContext = entity;
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			m_SelectionContext = entity;

		if (ImGui::BeginPopupContextItem())
		{
			m_SelectionContext = entity;
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
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				SceneEntityDragDropPayloadID, dropFlags))
			{
				Entity draggedEntity = GetDraggedSceneEntity(payload, m_Context);
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

						if (m_Context->MoveEntity(draggedEntity, entity, placement))
						{
							m_SelectionContext = draggedEntity;
							if (placement == Scene::EntityPlacement::Child)
								m_ForceExpandParent = entity;
							MarkModified();
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (renameActive)
		{
			ImGui::SetCursorScreenPos(ImVec2(textOffsetX, textOffsetY));
			ImGui::SetNextItemWidth(std::max(40.0f, itemMax.x - textOffsetX - ImGui::GetStyle().ItemInnerSpacing.x));
			if (m_RenameFocus)
			{
				ImGui::SetKeyboardFocusHere();
				m_RenameFocus = false;
			}

			bool commit = ImGui::InputText("##EntityRename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			if (commit || ImGui::IsItemDeactivated())
			{
				if (m_Context->RenameEntity(entity, m_RenameBuffer))
					MarkModified();
				m_RenameEntity = {};
			}
		}

		if (open && hasChildren)
		{
			for (UUID childUUID : children)
			{
				Entity child = m_Context->FindEntityByUUID(childUUID);
				if (child)
					DrawEntityNode(child);
			}
			ImGui::TreePop();
		}

		if (!visible)
			ImGui::PopStyleColor();
	}

static bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
	{
		(void)resetValue;

		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGui::PushID(label.c_str());

		// 使用DrawProperty函数设置标签在左侧并右对齐
		DrawProperty(label, columnWidth);

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		float availableWidth = ImGui::GetContentRegionAvail().x;
		float valueWidth = std::max(20.0f, (availableWidth - buttonSize.x * 3.0f - spacing * 6.0f) / 3.0f);

		// X 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 196.0f / 255.0f, 90.0f / 255.0f, 90.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 210.0f / 255.0f, 105.0f / 255.0f, 105.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 164.0f / 255.0f, 72.0f / 255.0f, 72.0f / 255.0f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("X", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(valueWidth);
		bool changed = ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();

		// Y 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 111.0f / 255.0f, 175.0f / 255.0f, 111.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 126.0f / 255.0f, 190.0f / 255.0f, 126.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 88.0f / 255.0f, 145.0f / 255.0f, 88.0f / 255.0f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("Y", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(valueWidth);
		changed |= ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::SameLine();

		// Z 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 93.0f / 255.0f, 134.0f / 255.0f, 196.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 108.0f / 255.0f, 149.0f / 255.0f, 211.0f / 255.0f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 74.0f / 255.0f, 108.0f / 255.0f, 163.0f / 255.0f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("Z", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(valueWidth);
		changed |= ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");

		ImGui::PopStyleVar();

		ImGui::Columns(1);

		ImGui::PopID();
		return changed;
	}

	// 通用的两列布局绘制函数（标签在左侧，控件右对齐）
	static void DrawProperty(const std::string& label, float columnWidth)
	{
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth + 40.0f);

		// 保存当前光标位置
		ImVec2 initialCursorPos = ImGui::GetCursorPos();

		// 计算文本区域的可用宽度
		float textWidth = ImGui::GetColumnWidth() - ImGui::GetStyle().FramePadding.x * 2;
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textWidth);
		ImGui::TextWrapped("%s", label.c_str());
		ImGui::PopTextWrapPos();

		// 计算标签的实际高度
		float labelHeight = ImGui::GetItemRectSize().y;
		float singleLineHeight = ImGui::GetTextLineHeightWithSpacing();

		ImGui::NextColumn();

		// 如果标签是多行的，计算垂直偏移量使控件居中
		if (labelHeight > singleLineHeight) {
			float verticalOffset = (labelHeight - singleLineHeight) * 0.5f;
			ImGui::SetCursorPosY(initialCursorPos.y + verticalOffset);
		}

		// 设置右侧控件宽度并右对齐
		ImGui::SetNextItemWidth(-1);
		ImGui::SetCursorPosX(ImGui::GetCursorPosX());
	}

	template<typename T> static bool* GetComponentEnabledFlag(T&) { return nullptr; }
	template<> static bool* GetComponentEnabledFlag<C_Camera>(C_Camera& component) { return &component.Primary; }
	template<> static bool* GetComponentEnabledFlag<SpriteRenderer>(SpriteRenderer& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<LineRenderer>(LineRenderer& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<Rigidbody2D>(Rigidbody2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<BoxCollider2D>(BoxCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<CircleCollider2D>(CircleCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<DistanceJoint2D>(DistanceJoint2D& component) { return &component.Enabled; }

	static bool DrawColliderMaterialProperties(float& density, float& friction, float& restitution)
	{
		float editedDensity = density;
		float editedFriction = friction;
		float editedRestitution = restitution;
		bool edited = ImGui::DragFloat("Density", &editedDensity, 0.01f, 0.0f, 0.0f);
		edited |= ImGui::DragFloat("Friction", &editedFriction, 0.01f, 0.0f, 0.0f);
		edited |= ImGui::DragFloat("Restitution", &editedRestitution, 0.01f, 0.0f, 1.0f,
			"%.3f", ImGuiSliderFlags_AlwaysClamp);
		if (!edited)
			return false;

		if (!std::isfinite(editedDensity))
			editedDensity = density;
		if (!std::isfinite(editedFriction))
			editedFriction = friction;
		if (!std::isfinite(editedRestitution))
			editedRestitution = restitution;
		editedDensity = std::max(0.0f, editedDensity);
		editedFriction = std::max(0.0f, editedFriction);
		editedRestitution = std::clamp(editedRestitution, 0.0f, 1.0f);

		const bool changed = editedDensity != density || editedFriction != friction
			|| editedRestitution != restitution;
		if (changed)
		{
			density = editedDensity;
			friction = editedFriction;
			restitution = editedRestitution;
		}
		return changed;
	}

	static bool DrawCollisionFilterProperties(bool& isTrigger,
		uint16_t& collisionLayer, uint16_t& collisionMask)
	{
		bool changed = ImGui::Checkbox("Is Trigger", &isTrigger);
		if (ImGui::TreeNodeEx("Advanced Filter", ImGuiTreeNodeFlags_SpanAvailWidth))
		{
			ImGui::TextDisabled("Category Bits: 0x%04X", static_cast<unsigned int>(collisionLayer));
			ImGui::TextDisabled("Mask Bits: 0x%04X", static_cast<unsigned int>(collisionMask));
			ImGui::TextWrapped("Read-only fixture filter data. Both this Category/Mask filter and the Entity Layer/Project Physics 2D matrix must allow contact.");
			ImGui::TreePop();
		}
		return changed;
	}

template<typename T, typename UIFunction, typename ModifiedFunction>
static void DrawComponent(const std::string& name, Entity entity,
	const Ref<EditorIconSet>& icons, EditorIcon icon,
	UIFunction uiFunction, ModifiedFunction onModified, bool editable = true)
{
	// 检查实体是否有效
	if (!entity)
		return;

	const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
	if (entity.HasComponent<T>())
	{
		auto& component = entity.GetComponent<T>();
		ImGui::PushID(name.c_str());

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
		ImGui::Separator();
		// Let ImGui draw the framed, full-width tree row and folding arrow. The
		// remaining header content is drawn on top of the empty row so every part
		// stays in one aligned header instead of being laid out as separate rows.
		const ImGuiTreeNodeFlags headerFlags = treeNodeFlags | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), headerFlags, "##ComponentHeader");
		const ImVec2 headerMin = ImGui::GetItemRectMin();
		const ImVec2 headerMax = ImGui::GetItemRectMax();
		const float headerHeight = headerMax.y - headerMin.y;
		ImVec2 afterHeaderCursor = ImGui::GetCursorPos();
		ImGui::PopStyleVar();

		bool* enabled = GetComponentEnabledFlag(component);
		const float contentGap = std::max(3.0f,
			std::round(ImGui::GetFontSize() * 0.16f));
		float leftContentX = headerMin.x + ImGui::GetTreeNodeToLabelSpacing();
		if (icon != EditorIcon::Count)
		{
			const float iconSize = std::min(std::round(ImGui::GetFontSize() * 0.9f),
				std::max(1.0f, headerHeight - 8.0f));
			const float iconY = std::round(headerMin.y + (headerHeight - iconSize) * 0.5f);
			DrawIcon(icons, icon, ImVec2(leftContentX, iconY),
				ImVec2(leftContentX + iconSize, iconY + iconSize));
			leftContentX += iconSize + contentGap;
		}
		if (enabled)
		{
			const float checkboxPosY = headerMin.y +
				(headerHeight - ImGui::GetFrameHeight()) * 0.5f;
			ImGui::SetCursorScreenPos(ImVec2(leftContentX, checkboxPosY));
			ImGui::BeginDisabled(!editable);
			if (DrawCompactCheckbox("##Enabled", enabled))
				onModified();
			ImGui::EndDisabled();
			leftContentX += GetCompactCheckboxWidth() + contentGap;
		}
		const float textY = headerMin.y + (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
		ImGui::SetCursorScreenPos(ImVec2(leftContentX, textY));
		ImGui::TextUnformatted(name.c_str());

		// The component menu is the only right-side control. It uses the exact
		// header height and draws a vertical three-dot glyph in the same bar.
		const ImVec2 menuSize{ headerHeight, headerHeight };
		const ImVec2 menuPos{ headerMax.x - headerHeight, headerMin.y };
		ImGui::SetCursorScreenPos(menuPos);
		const std::string menuID = std::string("##ComponentMenu_") + name;
		ImGui::BeginDisabled(!editable);
		bool menuClicked = ImGui::InvisibleButton(menuID.c_str(), menuSize);
		const bool menuHovered = ImGui::IsItemHovered();
		const bool menuHeld = ImGui::IsItemActive();
		ImGui::EndDisabled();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImU32 menuBg = ImGui::GetColorU32(menuHeld ? ImGuiCol_HeaderActive : menuHovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
		drawList->AddRectFilled(menuPos, ImVec2(menuPos.x + headerHeight, menuPos.y + headerHeight), menuBg, ImGui::GetStyle().FrameRounding);
		const ImVec2 dotCenter{ menuPos.x + headerHeight * 0.5f, menuPos.y + headerHeight * 0.5f };
		const float dotRadius = std::max(1.5f, headerHeight * 0.07f);
		const float dotOffset = headerHeight * 0.2f;
		for (int dot = -1; dot <= 1; dot++)
			drawList->AddCircleFilled(ImVec2(dotCenter.x, dotCenter.y + dot * dotOffset), dotRadius, ImGui::GetColorU32(ImGuiCol_Text));
		if (menuClicked)
		{
			ImGui::OpenPopup("ComponentSettings");
		}
		ImGui::SetCursorPos(afterHeaderCursor);

		bool removeComponent = false;
		if (ImGui::BeginPopup("ComponentSettings"))
		{
			ImGui::BeginDisabled(!editable);
			if(name != "Transform")
				if (ImGui::MenuItem("Remove component"))
					removeComponent = true;
			ImGui::EndDisabled();

			ImGui::EndPopup();
		}

		if (open)
		{
			ImGui::TreePush((void*)typeid(T).hash_code());
			ImGui::BeginDisabled(!editable);
			uiFunction(component);
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		if (removeComponent)
		{
			entity.RemoveComponent<T>();
			onModified();
		}
		ImGui::PopID();
	}
	}


	void SceneHierarchyPanel::DrawComponents(Entity entity)
{
	// 检查实体是否有效
	if (!entity)
		return;

	ImGui::PushID(reinterpret_cast<void*>(
		static_cast<uintptr_t>(static_cast<uint64_t>(entity.GetUUID()))));
	if (entity.HasComponent<Tag>())
	{
		auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		if (m_NameEditingEntity != entity)
		{
			m_NameEditingEntity = entity;
			strncpy_s(m_NameEditBuffer, sizeof(m_NameEditBuffer), tag.c_str(), _TRUNCATE);
		}

		if (entity.HasComponent<EntityMetadata>())
		{
			if (DrawEntityIconSelector(m_Icons, entity, m_ColliderEditingAllowed))
				MarkModified();
			ImGui::SameLine(0.0f, 4.0f);
		}
		if (DrawCompactCheckbox("##Visible", &tagComponent.Visible))
			MarkModified();
		ImGui::SameLine(0.0f, 5.0f);
		ImGui::SetNextItemWidth(-1.0f);
		const bool committed = ImGui::InputText("##Name", m_NameEditBuffer, sizeof(m_NameEditBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (committed || ImGui::IsItemDeactivatedAfterEdit())
		{
			if (m_Context->RenameEntity(entity, m_NameEditBuffer))
				MarkModified();
			strncpy_s(m_NameEditBuffer, sizeof(m_NameEditBuffer), tag.c_str(), _TRUNCATE);
		}
	}

	if (entity.HasComponent<EntityMetadata>())
	{
		auto& metadata = entity.GetComponent<EntityMetadata>();
		const ProjectSettings* projectSettings = m_Project ? &m_Project->GetSettings() : nullptr;
		const bool metadataEditable = m_ColliderEditingAllowed && projectSettings != nullptr;
		ImGui::PushID("EntityMetadata");

		bool tagDefined = false;
		if (projectSettings)
		{
			const auto& projectTags = projectSettings->TagsAndLayers.Tags;
			tagDefined = std::find(projectTags.begin(), projectTags.end(), metadata.GameplayTag)
				!= projectTags.end();
		}
		std::string tagPreview = metadata.GameplayTag.empty() ? "<Empty>" : metadata.GameplayTag;
		if (projectSettings && !tagDefined)
			tagPreview += " (Undefined)";

		const uint8_t currentLayer = metadata.Layer;
		bool layerDefined = false;
		std::string layerPreview = "Layer " + std::to_string(static_cast<unsigned int>(currentLayer));
		if (projectSettings && currentLayer < Physics2DLayerCount)
		{
			const std::string& layerName = projectSettings->TagsAndLayers.LayerNames[currentLayer];
			if (!layerName.empty())
			{
				layerDefined = true;
				layerPreview = layerName;
			}
		}
		if (projectSettings && !layerDefined)
			layerPreview += " (Undefined)";

		const bool stackMetadataRows = ImGui::GetContentRegionAvail().x < 330.0f;
		const int metadataColumnCount = stackMetadataRows ? 2 : 4;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp
			| ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX;
		if (ImGui::BeginTable("##TagLayer", metadataColumnCount, tableFlags))
		{
			const float labelWidth = ImGui::CalcTextSize("Layer").x
				+ ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::TableSetupColumn("TagLabel", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("TagValue", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			if (!stackMetadataRows)
			{
				ImGui::TableSetupColumn("LayerLabel", ImGuiTableColumnFlags_WidthFixed, labelWidth);
				ImGui::TableSetupColumn("LayerValue", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			}
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Tag");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::BeginDisabled(!metadataEditable);
			if (ImGui::BeginCombo("##GameplayTag", tagPreview.c_str()))
			{
				if (!tagDefined)
					ImGui::Selectable(tagPreview.c_str(), true, ImGuiSelectableFlags_Disabled);
				if (projectSettings)
				{
					for (const std::string& projectTag : projectSettings->TagsAndLayers.Tags)
					{
						const bool selected = metadata.GameplayTag == projectTag;
						if (ImGui::Selectable(projectTag.c_str(), selected) && !selected)
						{
							metadata.GameplayTag = projectTag;
							MarkModified();
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			const bool tagHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();

			if (stackMetadataRows)
				ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(stackMetadataRows ? 0 : 2);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Layer");
			ImGui::TableSetColumnIndex(stackMetadataRows ? 1 : 3);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::BeginDisabled(!metadataEditable);
			if (ImGui::BeginCombo("##EntityLayer", layerPreview.c_str()))
			{
				if (!layerDefined)
					ImGui::Selectable(layerPreview.c_str(), true, ImGuiSelectableFlags_Disabled);
				if (projectSettings)
				{
					for (uint8_t layer = 0; layer < Physics2DLayerCount; ++layer)
					{
						const std::string& layerName = projectSettings->TagsAndLayers.LayerNames[layer];
						if (layerName.empty())
							continue;
						const bool selected = metadata.Layer == layer;
						if (ImGui::Selectable(layerName.c_str(), selected) && !selected)
						{
							metadata.Layer = layer;
							MarkModified();
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			const bool layerHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();

			if (!metadataEditable && (tagHovered || layerHovered))
			{
				ImGui::SetTooltip("%s", m_ColliderEditingAllowed
					? "Open a project to choose a Tag or Layer."
					: "Tag and Layer are read-only while the scene is running.");
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
			"Entity metadata is missing.");
	}
	ImGui::PopID();


		const auto onModified = [this]() { MarkModified(); };
		DrawComponent<Transform>("Transform", entity, m_Icons, EditorIcon::Move,
			[this, entity](auto& component)
		{
			Entity parent = m_Context ? m_Context->GetParent(entity) : Entity{};
			const bool hasParent = (bool)parent;
			glm::vec3 translation = hasParent ? component._LocalTranslation : component._Translation;
			glm::vec3 rotation = glm::degrees(hasParent ? component._LocalRotation : component._Rotation);
			glm::vec3 scale = hasParent ? component._LocalScale : component._Scale;

			bool changed = DrawVec3Control(hasParent ? "Local Translation" : "Translation", translation);
			changed |= DrawVec3Control(hasParent ? "Local Rotation" : "Rotation", rotation);
			changed |= DrawVec3Control(hasParent ? "Local Scale" : "Scale", scale, 1.0f);
			if (changed && m_Context)
			{
				const glm::mat4 transform = Math::ComposeTransform(translation, glm::radians(rotation), scale);
				if (hasParent)
				{
					if (m_Context->SetLocalTransform(entity, transform))
						MarkModified();
				}
				else if (m_Context->SetWorldTransform(entity, transform))
				{
					MarkModified();
				}
			}
		}, onModified);

		DrawComponent<C_Camera>("Camera", entity, m_Icons, EditorIcon::Camera,
			[this, entity](auto& component)
		{
			auto& camera = component._Camera;
			const float columnWidth = 100.0f;

			DrawProperty("Projection", columnWidth);
			const char* projectionTypes[] = { "Perspective", "Orthographic" };
			int projection = (int)camera.GetProjectionType();
			if (ImGui::BeginCombo("##Projection", projectionTypes[projection]))
			{
				for (int i = 0; i < 2; ++i)
				{
					const bool selected = projection == i;
					if (ImGui::Selectable(projectionTypes[i], selected))
					{
						if (projection != i && camera.SetProjectionType((SceneCamera::ProjectionType)i))
							MarkModified();
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);

			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				DrawProperty("Vertical FOV", columnWidth);
				float value = glm::degrees(camera.GetPerspectiveVerticalFOV());
				if (ImGui::DragFloat("##VerticalFOV", &value) && camera.SetPerspectiveVerticalFOV(glm::radians(value))) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Near", columnWidth);
				value = camera.GetPerspectiveNearClip();
				if (ImGui::DragFloat("##PerspectiveNear", &value) && camera.SetPerspectiveNearClip(value)) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetPerspectiveFarClip();
				if (ImGui::DragFloat("##PerspectiveFar", &value) && camera.SetPerspectiveFarClip(value)) MarkModified();
				ImGui::Columns(1);
			}
			else
			{
				DrawProperty("Size", columnWidth);
				float value = camera.GetOrthographicSize();
				if (ImGui::DragFloat("##OrthoSize", &value) && camera.SetOrthographicSize(value)) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Near", columnWidth);
				value = camera.GetOrthographicNearClip();
				if (ImGui::DragFloat("##OrthoNear", &value) && camera.SetOrthographicNearClip(value)) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetOrthographicFarClip();
				if (ImGui::DragFloat("##OrthoFar", &value) && camera.SetOrthographicFarClip(value)) MarkModified();
				ImGui::Columns(1);
			}

			DrawProperty("Fixed Aspect Ratio", columnWidth);
			if (ImGui::Checkbox("##FixedAspectRatio", &component.FixedAspectRatio)) MarkModified();
			ImGui::Columns(1);
			DrawProperty("Background Color", columnWidth);
			if (ImGui::ColorEdit4("##BackgroundColor", glm::value_ptr(component.BackgroundColor))) MarkModified();
			ImGui::Columns(1);
		}, [this, entity]() mutable
		{
			// Camera uses the compact header checkbox for its Primary state. Keep
			// the scene invariant that at most one camera can be primary.
			if (m_Context && entity && entity.HasComponent<C_Camera>())
			{
				auto& selectedCamera = entity.GetComponent<C_Camera>();
				if (selectedCamera.Primary)
				{
					auto cameras = m_Context->m_Registry.view<C_Camera>();
					for (auto handle : cameras)
						cameras.get<C_Camera>(handle).Primary = false;
					selectedCamera.Primary = true;
				}
			}
			MarkModified();
		});

		DrawComponent<SpriteRenderer>("Sprite Renderer", entity, m_Icons, EditorIcon::Sprite,
			[this, entity](auto& component)
		{
			const float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			if (ImGui::ColorEdit4("##Color", glm::value_ptr(component._Color))) MarkModified();
			ImGui::Columns(1);

			DrawProperty("Sprite", columnWidth);
			std::string spriteName = "None";
			EditorIcon spriteFieldIcon = EditorIcon::Sprite;
			Ref<Texture2D> spritePreview;
			const uint64_t rawSpriteHandle = static_cast<uint64_t>(component.SpriteHandle);
			if (rawSpriteHandle != 0)
			{
				const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(
					component.SpriteHandle);
				if (metadata && IsSpriteAsset(*metadata))
				{
					spriteName = PathToUTF8(metadata->FilePath.stem());
					if (metadata->IsMissing)
					{
						spriteName += " (Missing)";
						spriteFieldIcon = EditorIcon::Missing;
					}
					else
					{
						component.Sprite = AssetManager::Get().LoadTexture(component.SpriteHandle);
						spritePreview = component.Sprite;
					}
				}
				else if (metadata)
				{
					spriteName = PathToUTF8(metadata->FilePath.stem()) + " (Not a Sprite)";
					spriteFieldIcon = EditorIcon::Missing;
				}
				else
				{
					spriteName = "Missing #" + std::to_string(rawSpriteHandle);
					spriteFieldIcon = EditorIcon::Missing;
				}
			}

			const float pickerButtonWidth = ImGui::GetFrameHeight();
			const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x -
				pickerButtonWidth - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Button("##SpriteAssetField", ImVec2(fieldWidth, 0.0f));
			const ImVec2 spriteButtonMin = ImGui::GetItemRectMin();
			const ImVec2 spriteButtonMax = ImGui::GetItemRectMax();
			const float spriteIconSize = std::min(16.0f,
				std::max(1.0f, spriteButtonMax.y - spriteButtonMin.y - 4.0f));
			const ImVec2 spriteIconMin(spriteButtonMin.x + 6.0f,
				spriteButtonMin.y + (spriteButtonMax.y - spriteButtonMin.y - spriteIconSize) * 0.5f);
			const ImVec2 spriteIconMax(spriteIconMin.x + spriteIconSize, spriteIconMin.y + spriteIconSize);
			if (spritePreview)
				ImGui::GetWindowDrawList()->AddImage(ToImGuiTextureID(spritePreview),
					spriteIconMin, spriteIconMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			else
				DrawIcon(m_Icons, spriteFieldIcon, spriteIconMin, spriteIconMax);
			const ImRect spriteTextClip(
				ImVec2(spriteIconMax.x + 5.0f, spriteButtonMin.y),
				ImVec2(spriteButtonMax.x - 4.0f, spriteButtonMax.y));
			ImGui::RenderTextClipped(spriteTextClip.Min, spriteTextClip.Max,
				spriteName.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f), &spriteTextClip);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
						if (metadata && IsSpriteAsset(*metadata) && !metadata->IsMissing)
						{
							component.SpriteHandle = handle;
							component.Sprite = AssetManager::Get().LoadTexture(handle);
							MarkModified();
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("SpriteAssetContext"))
			{
				if (ImGui::MenuItem("Clear", nullptr, false, rawSpriteHandle != 0))
				{
					component.SpriteHandle = AssetHandle(0);
					component.Sprite.reset();
					MarkModified();
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Button("##OpenSpritePicker", ImVec2(pickerButtonWidth, 0.0f));
			const bool openSpritePicker = ImGui::IsItemClicked();
			const ImVec2 pickerMin = ImGui::GetItemRectMin();
			const ImVec2 pickerMax = ImGui::GetItemRectMax();
			const ImVec2 pickerCenter((pickerMin.x + pickerMax.x) * 0.5f,
				(pickerMin.y + pickerMax.y) * 0.5f);
			const ImU32 pickerGlyph = ImGui::GetColorU32(ImGuiCol_Text);
			const float pickerRadius = std::max(3.0f,
				std::min(pickerMax.x - pickerMin.x, pickerMax.y - pickerMin.y) * 0.28f);
			ImGui::GetWindowDrawList()->AddCircle(pickerCenter, pickerRadius, pickerGlyph, 16, 1.5f);
			ImGui::GetWindowDrawList()->AddCircleFilled(pickerCenter,
				std::max(1.0f, pickerRadius * 0.28f), pickerGlyph, 12);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Select Sprite");
			if (openSpritePicker)
			{
				m_SpriteSearch.fill('\0');
				m_SpritePickerOpen = true;
				m_SpritePickerEntity = entity.GetUUID();
				ImGui::OpenPopup("Select Sprite##SpritePicker");
			}
			if (m_SpritePickerOpen && m_SpritePickerEntity != entity.GetUUID())
				m_SpritePickerOpen = false;

			ImGui::SetNextWindowSize(ImVec2(680.0f, 460.0f), ImGuiCond_Appearing);
			if (ImGui::BeginPopupModal("Select Sprite##SpritePicker", &m_SpritePickerOpen,
				ImGuiWindowFlags_NoCollapse))
			{
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::InputTextWithHint("##SpriteSearch", "Search Sprites",
					m_SpriteSearch.data(), m_SpriteSearch.size());

				std::vector<const AssetMetadata*> sprites;
				for (const auto& [handle, metadata] :
					AssetManager::Get().GetRegistry().GetAssets())
				{
					if (IsSpriteAsset(metadata) && !metadata.IsMissing &&
						MatchesSpriteSearch(metadata, m_SpriteSearch.data()))
						sprites.push_back(&metadata);
				}
				std::sort(sprites.begin(), sprites.end(), [](const AssetMetadata* left,
					const AssetMetadata* right)
				{
					return LowerASCII(PathToUTF8(left->FilePath)) <
						LowerASCII(PathToUTF8(right->FilePath));
				});

				ImGui::TextDisabled("%zu Sprite%s", sprites.size(), sprites.size() == 1 ? "" : "s");
				ImGui::Separator();
				ImGui::BeginChild("SpriteGrid", ImVec2(0.0f, 0.0f), false);
				constexpr float cellWidth = 96.0f;
				constexpr float cellHeight = 112.0f;
				const int columns = std::max(1, static_cast<int>(
					ImGui::GetContentRegionAvail().x / cellWidth));
				ImGui::Columns(columns, "SpritePickerColumns", false);

				auto drawSpriteTile = [&](AssetHandle handle, const char* label,
					const std::filesystem::path* relativePath)
				{
					const std::string id = "##Sprite_" +
						std::to_string(static_cast<uint64_t>(handle));
					const float tileWidth = std::max(64.0f,
						ImGui::GetColumnWidth() - ImGui::GetStyle().ItemSpacing.x);
					const ImVec2 tileSize(tileWidth, cellHeight);
					const ImVec2 tileMin = ImGui::GetCursorScreenPos();
					ImGui::InvisibleButton(id.c_str(), tileSize);
					const bool hovered = ImGui::IsItemHovered();
					const bool clicked = ImGui::IsItemClicked();
					const bool selected = component.SpriteHandle == handle;
					const ImVec2 tileMax(tileMin.x + tileSize.x, tileMin.y + tileSize.y);
					const bool visible = ImGui::IsRectVisible(tileMin, tileMax);
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const ImU32 background = ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive :
						hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
					if (visible)
						drawList->AddRectFilled(tileMin, tileMax, background, 2.0f);

					constexpr float previewPadding = 8.0f;
					const float previewExtent = std::max(1.0f,
						std::min(tileSize.x - previewPadding * 2.0f, 76.0f));
					const ImVec2 previewMin(tileMin.x + (tileSize.x - previewExtent) * 0.5f,
						tileMin.y + previewPadding);
					const ImVec2 previewMax(previewMin.x + previewExtent,
						previewMin.y + previewExtent);
					if (visible && static_cast<uint64_t>(handle) != 0)
					{
						const Ref<Texture2D> texture = AssetManager::Get().LoadTexture(handle);
						if (texture)
							drawList->AddImage(ToImGuiTextureID(texture), previewMin, previewMax,
								ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
					}
					else if (visible)
						DrawIcon(m_Icons, EditorIcon::Sprite, previewMin, previewMax,
							ImGui::GetColorU32(ImGuiCol_TextDisabled));

					const ImRect labelClip(
						ImVec2(tileMin.x + 4.0f, previewMax.y + 5.0f),
						ImVec2(tileMin.x + tileSize.x - 4.0f, tileMin.y + tileSize.y - 4.0f));
					if (visible)
						ImGui::RenderTextClipped(labelClip.Min, labelClip.Max, label, nullptr,
							nullptr, ImVec2(0.5f, 0.0f), &labelClip);
					if (hovered && relativePath)
						ImGui::SetTooltip("%s", PathToUTF8(*relativePath).c_str());
					if (clicked)
					{
						if (component.SpriteHandle != handle)
						{
							component.SpriteHandle = handle;
							component.Sprite = static_cast<uint64_t>(handle) != 0
								? AssetManager::Get().LoadTexture(handle) : Ref<Texture2D>{};
							MarkModified();
						}
						m_SpritePickerOpen = false;
						m_SpritePickerEntity = UUID(0);
						ImGui::CloseCurrentPopup();
					}
					ImGui::NextColumn();
				};

				if (m_SpriteSearch[0] == '\0')
					drawSpriteTile(AssetHandle(0), "None", nullptr);
				for (const AssetMetadata* metadata : sprites)
				{
					const std::string label = PathToUTF8(metadata->FilePath.stem());
					drawSpriteTile(metadata->Handle, label.c_str(), &metadata->FilePath);
				}
				ImGui::Columns(1);
				ImGui::EndChild();
				ImGui::EndPopup();
			}
			if (!m_SpritePickerOpen)
				m_SpritePickerEntity = UUID(0);
			ImGui::Columns(1);

			DrawProperty("Tiling Factor", columnWidth);
			float tilingFactor = component.TilingFactor;
			if (ImGui::DragFloat("##TilingFactor", &tilingFactor, 0.1f, 0.0f, 100.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp))
			{
				if (!std::isfinite(tilingFactor))
					tilingFactor = component.TilingFactor;
				tilingFactor = std::max(0.0f, tilingFactor);
				if (tilingFactor != component.TilingFactor)
				{
					component.TilingFactor = tilingFactor;
					MarkModified();
				}
			}
			ImGui::Columns(1);
		}, onModified);

		DrawComponent<LineRenderer>("Line Renderer", entity, m_Icons, EditorIcon::Count,
			[this](auto& component)
		{
			const float columnWidth = 100.0f;

			DrawProperty("Color", columnWidth);
			if (ImGui::ColorEdit4("##Color", glm::value_ptr(component._Color)))
				MarkModified();
			ImGui::Columns(1);

			DrawProperty("Start", columnWidth);
			glm::vec3 start = component.Start;
			if (ImGui::DragFloat3("##Start", glm::value_ptr(start), 0.1f) &&
				std::isfinite(start.x) && std::isfinite(start.y) && std::isfinite(start.z) &&
				start != component.Start)
			{
				component.Start = start;
				MarkModified();
			}
			ImGui::Columns(1);

			DrawProperty("End", columnWidth);
			glm::vec3 end = component.End;
			if (ImGui::DragFloat3("##End", glm::value_ptr(end), 0.1f) &&
				std::isfinite(end.x) && std::isfinite(end.y) && std::isfinite(end.z) &&
				end != component.End)
			{
				component.End = end;
				MarkModified();
			}
			ImGui::Columns(1);

			DrawProperty("Width", columnWidth);
			float width = component.Width;
			if (ImGui::DragFloat("##Width", &width, 0.1f, 0.0f, 0.0f, "%.3f"))
			{
				if (!std::isfinite(width))
					width = component.Width;
				width = std::max(0.0001f, width);
				if (width != component.Width)
				{
					component.Width = width;
					MarkModified();
				}
			}
			ImGui::Columns(1);
		}, onModified);

		DrawComponent<Rigidbody2D>("Rigidbody 2D", entity, m_Icons, EditorIcon::Rigidbody2D,
			[this](auto& component)
		{
			const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
			int bodyType = (int)component.Type;
			if (ImGui::BeginCombo("Body Type", bodyTypes[bodyType]))
			{
				for (int i = 0; i < 3; ++i)
				{
					const bool selected = bodyType == i;
					if (ImGui::Selectable(bodyTypes[i], selected)) { component.Type = (Rigidbody2D::BodyType)i; MarkModified(); }
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Checkbox("Fixed Rotation", &component.FixedRotation)) MarkModified();
		}, onModified, m_ColliderEditingAllowed);

		DrawComponent<BoxCollider2D>("Box Collider 2D", entity, m_Icons, EditorIcon::BoxCollider2D,
			[this, entity](auto& component)
		{
			const bool editingCollider = GetColliderEditMode() == ColliderEditMode::Box;
			ImGui::BeginDisabled(!m_ColliderEditingAllowed);
			if (editingCollider)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
			if (ImGui::Button("Edit Collider", ImVec2(-1.0f, 0.0f)))
			{
				if (editingCollider)
					ClearColliderEditMode();
				else
				{
					m_ColliderEditMode = ColliderEditMode::Box;
					m_ColliderEditEntity = entity.GetUUID();
				}
			}
			if (editingCollider)
				ImGui::PopStyleColor();
			ImGui::EndDisabled();

			glm::vec2 offset = component.Offset;
			glm::vec2 size = component.Size;
			bool changed = false;
			changed |= DrawCollisionFilterProperties(component.IsTrigger,
				component.CollisionLayer, component.CollisionMask);
			bool geometryEdited = ImGui::DragFloat2("Offset", glm::value_ptr(offset));
			geometryEdited |= ImGui::DragFloat2("Size", glm::value_ptr(size));
			if (geometryEdited)
			{
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
					offset = component.Offset;
				if (!std::isfinite(size.x) || !std::isfinite(size.y))
					size = component.Size;
				constexpr float minimumSize = 0.0001f;
				size.x = std::max(minimumSize, size.x);
				size.y = std::max(minimumSize, size.y);
				if (offset.x != component.Offset.x || offset.y != component.Offset.y
					|| size.x != component.Size.x || size.y != component.Size.y)
				{
					component.Offset = offset;
					component.Size = size;
					changed = true;
				}
			}

			changed |= DrawColliderMaterialProperties(component.Density,
				component.Friction, component.Restitution);
			float restitutionThreshold = component.RestitutionThreshold;
			if (ImGui::DragFloat("Restitution Threshold", &restitutionThreshold, 0.01f, 0.0f, 0.0f))
			{
				if (!std::isfinite(restitutionThreshold))
					restitutionThreshold = component.RestitutionThreshold;
				restitutionThreshold = std::max(0.0f, restitutionThreshold);
				if (restitutionThreshold != component.RestitutionThreshold)
				{
					component.RestitutionThreshold = restitutionThreshold;
					changed = true;
				}
			}
			if (changed)
				MarkModified();
		}, onModified, m_ColliderEditingAllowed);

		DrawComponent<CircleCollider2D>("Circle Collider 2D", entity, m_Icons, EditorIcon::BoxCollider2D,
			[this, entity](auto& component)
		{
			const bool editingCollider = GetColliderEditMode() == ColliderEditMode::Circle;
			ImGui::BeginDisabled(!m_ColliderEditingAllowed);
			if (editingCollider)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
			if (ImGui::Button("Edit Collider", ImVec2(-1.0f, 0.0f)))
			{
				if (editingCollider)
					ClearColliderEditMode();
				else
				{
					m_ColliderEditMode = ColliderEditMode::Circle;
					m_ColliderEditEntity = entity.GetUUID();
				}
			}
			if (editingCollider)
				ImGui::PopStyleColor();
			ImGui::EndDisabled();

			glm::vec2 offset = component.Offset;
			float radius = component.Radius;
			bool changed = false;
			changed |= DrawCollisionFilterProperties(component.IsTrigger,
				component.CollisionLayer, component.CollisionMask);
			bool geometryEdited = ImGui::DragFloat2("Offset", glm::value_ptr(offset));
			geometryEdited |= ImGui::DragFloat("Radius", &radius, 0.01f, 0.0001f, 0.0f);
			if (geometryEdited)
			{
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
					offset = component.Offset;
				if (!std::isfinite(radius))
					radius = component.Radius;
				radius = std::max(0.0001f, radius);
				if (offset.x != component.Offset.x || offset.y != component.Offset.y
					|| radius != component.Radius)
				{
					component.Offset = offset;
					component.Radius = radius;
					changed = true;
				}
			}

			ImGui::TextDisabled("Non-uniform scale uses max(abs(X), abs(Y)).");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Box2D circles cannot become ellipses. The largest absolute world X/Y scale keeps the fixture circular.");
			changed |= DrawColliderMaterialProperties(component.Density,
				component.Friction, component.Restitution);
			if (changed)
				MarkModified();
		}, onModified, m_ColliderEditingAllowed);

		DrawComponent<DistanceJoint2D>("Distance Joint 2D", entity, m_Icons, EditorIcon::Rigidbody2D,
			[this, entity](auto& component)
		{
			bool changed = false;
			if (!entity.HasComponent<Rigidbody2D>())
				ImGui::TextDisabled("Owner Body: implicit static body");
			uint64_t connectedEntity = static_cast<uint64_t>(component.ConnectedEntity);
			if (ImGui::InputScalar("Connected Entity UUID", ImGuiDataType_U64, &connectedEntity))
			{
				component.ConnectedEntity = UUID(connectedEntity);
				changed = true;
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID))
				{
					Entity connected = GetDraggedSceneEntity(payload, m_Context);
					if (connected && connected != entity &&
						component.ConnectedEntity != connected.GetUUID())
					{
						component.ConnectedEntity = connected.GetUUID();
						connectedEntity = static_cast<uint64_t>(component.ConnectedEntity);
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Enter an entity UUID or drag an entity here from Hierarchy. Use 0 for no connection.");

			Entity connected;
			if (m_Context && connectedEntity != 0)
				connected = m_Context->FindEntityByUUID(UUID(connectedEntity));
			if (connectedEntity == 0)
				ImGui::TextDisabled("Connected Body: None");
			else if (!connected)
				ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
					"Connected Body: Missing entity");
			else if (connected == entity)
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
					"Connected Body cannot reference itself");
			else if (!connected.HasComponent<Rigidbody2D>())
				ImGui::TextDisabled("Connected Body: %s (implicit static)",
					connected.GetName().c_str());
			else
				ImGui::TextDisabled("Connected Body: %s", connected.GetName().c_str());

			glm::vec2 anchor = component.Anchor;
			if (ImGui::DragFloat2("Anchor", glm::value_ptr(anchor), 0.01f))
			{
				if (std::isfinite(anchor.x) && std::isfinite(anchor.y) && anchor != component.Anchor)
				{
					component.Anchor = anchor;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Unscaled local anchor on this physics body.");

			glm::vec2 connectedAnchor = component.ConnectedAnchor;
			if (ImGui::DragFloat2("Connected Anchor", glm::value_ptr(connectedAnchor), 0.01f))
			{
				if (std::isfinite(connectedAnchor.x) && std::isfinite(connectedAnchor.y)
					&& connectedAnchor != component.ConnectedAnchor)
				{
					component.ConnectedAnchor = connectedAnchor;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Unscaled local anchor on the connected physics body.");

			float distance = component.Distance;
			if (ImGui::DragFloat("Distance", &distance, 0.01f, 0.0f, 0.0f, "%.3f"))
			{
				if (!std::isfinite(distance))
					distance = component.Distance;
				distance = std::max(0.0001f, distance);
				if (distance != component.Distance)
				{
					component.Distance = distance;
					changed = true;
				}
			}

			float frequency = component.Frequency;
			if (ImGui::DragFloat("Frequency", &frequency, 0.01f, 0.0f, 0.0f, "%.3f Hz"))
			{
				if (!std::isfinite(frequency))
					frequency = component.Frequency;
				frequency = std::max(0.0f, frequency);
				if (frequency != component.Frequency)
				{
					component.Frequency = frequency;
					changed = true;
				}
			}

			float damping = component.Damping;
			if (ImGui::DragFloat("Damping", &damping, 0.01f, 0.0f, 1.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp))
			{
				if (!std::isfinite(damping))
					damping = component.Damping;
				damping = std::clamp(damping, 0.0f, 1.0f);
				if (damping != component.Damping)
				{
					component.Damping = damping;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Box2D damping ratio in the range 0 to 1.");

			if (ImGui::Checkbox("Collide Connected", &component.CollideConnected))
				changed = true;
			if (changed)
				MarkModified();
		}, onModified, m_ColliderEditingAllowed);

		ImGui::Spacing();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::BeginDisabled(!m_ColliderEditingAllowed);
		const bool addComponentPressed = ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f));
		ImGui::EndDisabled();
		if (addComponentPressed)
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			ImGui::BeginDisabled(!m_ColliderEditingAllowed);
			if (!entity.HasComponent<C_Camera>() && ImGui::MenuItem("Camera"))
			{
				const bool alreadyHasPrimary = m_Context && (bool)m_Context->GetPrimaryCameraEntity();
				auto& camera = entity.AddComponent<C_Camera>();
				camera.Primary = !alreadyHasPrimary;
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<SpriteRenderer>() && ImGui::MenuItem("Sprite Renderer"))
			{
				entity.AddComponent<SpriteRenderer>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<LineRenderer>() && ImGui::MenuItem("Line Renderer"))
			{
				entity.AddComponent<LineRenderer>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<Rigidbody2D>() && ImGui::MenuItem("Rigidbody 2D"))
			{
				entity.AddComponent<Rigidbody2D>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<BoxCollider2D>() && ImGui::MenuItem("Box Collider 2D"))
			{
				entity.AddComponent<BoxCollider2D>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<CircleCollider2D>() && ImGui::MenuItem("Circle Collider 2D"))
			{
				entity.AddComponent<CircleCollider2D>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<DistanceJoint2D>() && ImGui::MenuItem("Distance Joint 2D"))
			{
				if (!entity.HasComponent<Rigidbody2D>())
					entity.AddComponent<Rigidbody2D>();
				entity.AddComponent<DistanceJoint2D>();
				MarkModified();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

	}

}
