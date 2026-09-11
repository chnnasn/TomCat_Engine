#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

#include "TomCat/Scene/Components.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Math/Math.h"
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

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);

	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context, bool clearSelection, bool remapSelection)
	{
		UUID selectedUUID{};
		const bool hadSelection = !clearSelection && remapSelection && (bool)m_SelectionContext;
		if (hadSelection)
			selectedUUID = m_SelectionContext.GetUUID();
		const bool contextChanged = m_Context != context;
		m_Context = context;
		m_ForceExpandParent = {};
		m_ForceOpenSceneRoot = false;
		m_EntityToDelete = {};
		m_RenameEntity = {};
		m_RenameFocus = false;
		m_TagEditingEntity = {};
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
		m_HierarchyFocused = false;
		if (!hierarchyOpen || *hierarchyOpen)
		{
		const bool hierarchyVisible = ImGui::Begin("Hierarchy", hierarchyOpen, ImGuiWindowFlags_MenuBar);
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
			const bool inspectorVisible = ImGui::Begin("Inspector", inspectorOpen, ImGuiWindowFlags_MenuBar);
			if (inspectorVisible && m_SelectionContext)
				DrawComponents(m_SelectionContext);
			ImGui::End();
		}
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
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

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::BeginMenu("Sprites"))
			{
				if (ImGui::MenuItem("Rectangle"))
				{
					Entity rectangle = m_Context->CreateEntity("Rectangle");
					auto& sprite = rectangle.AddComponent<SpriteRenderer>(
						glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
					sprite.Shape = SpriteShape::Quad;
					CreateAsSelectedChild(rectangle);
				}
				if (ImGui::MenuItem("Circle"))
				{
					Entity circle = m_Context->CreateEntity("Circle");
					auto& sprite = circle.AddComponent<SpriteRenderer>(
						glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
					sprite.Shape = SpriteShape::Circle;
					CreateAsSelectedChild(circle);
				}
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
		const float iconSize = DrawTreeRowIcon(m_Icons, EditorIcon::Entity, itemMin, itemMax,
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

	template<typename T, typename UIFunction, typename ModifiedFunction>
static void DrawComponent(const std::string& name, Entity entity,
	const Ref<EditorIconSet>& icons, EditorIcon icon,
	UIFunction uiFunction, ModifiedFunction onModified)
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
			if (DrawCompactCheckbox("##Enabled", enabled))
				onModified();
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
		bool menuClicked = ImGui::InvisibleButton(menuID.c_str(), menuSize);
		const bool menuHovered = ImGui::IsItemHovered();
		const bool menuHeld = ImGui::IsItemActive();
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
			if(name != "Transform")
				if (ImGui::MenuItem("Remove component"))
					removeComponent = true;

			ImGui::EndPopup();
		}

		if (open)
		{
			ImGui::TreePush((void*)typeid(T).hash_code());
			uiFunction(component);
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

	if (entity.HasComponent<Tag>())
	{
		auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		if (m_TagEditingEntity != entity)
		{
			m_TagEditingEntity = entity;
			strncpy_s(m_TagEditBuffer, sizeof(m_TagEditBuffer), tag.c_str(), _TRUNCATE);
		}

		if (DrawCompactCheckbox("##Visible", &tagComponent.Visible))
			MarkModified();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		const bool committed = ImGui::InputText("##Tag", m_TagEditBuffer, sizeof(m_TagEditBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (committed || ImGui::IsItemDeactivatedAfterEdit())
		{
			if (m_Context->RenameEntity(entity, m_TagEditBuffer))
				MarkModified();
			strncpy_s(m_TagEditBuffer, sizeof(m_TagEditBuffer), tag.c_str(), _TRUNCATE);
		}
		}


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
			[this](auto& component)
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
			[this](auto& component)
		{
			const float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			if (ImGui::ColorEdit4("##Color", glm::value_ptr(component._Color))) MarkModified();
			ImGui::Columns(1);

			DrawProperty("Shape", columnWidth);
			const char* shapeNames[] = { "Rectangle", "Circle" };
			int shapeIndex = component.Shape == SpriteShape::Circle ? 1 : 0;
			if (ImGui::BeginCombo("##Shape", shapeNames[shapeIndex]))
			{
				for (int i = 0; i < 2; ++i)
				{
					const bool selected = shapeIndex == i;
					if (ImGui::Selectable(shapeNames[i], selected) && !selected)
					{
						component.Shape = i == 0 ? SpriteShape::Quad : SpriteShape::Circle;
						MarkModified();
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);

			if (component.Shape == SpriteShape::Quad)
			{
				DrawProperty("Sprite", columnWidth);
				std::string textureName = "None";
				const uint64_t rawTextureHandle = static_cast<uint64_t>(component.TextureHandle);
				if (rawTextureHandle != 0)
				{
					const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(component.TextureHandle);
					if (metadata)
					{
						textureName = PathToUTF8(metadata->FilePath.stem());
						if (metadata->IsMissing)
							textureName += " (Missing)";
					}
					else
						textureName = "Missing #" + std::to_string(rawTextureHandle);
				}
				ImGui::Button(textureName.c_str(), ImVec2(-1, 0));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID))
					{
						if (payload->DataSize == sizeof(uint64_t))
						{
							const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
							const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
							if (metadata && metadata->Type == AssetType::Texture2D)
							{
								component.TextureHandle = handle;
								component.Texture = AssetManager::Get().LoadTexture(handle);
								MarkModified();
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (ImGui::BeginPopupContextItem("SpriteAssetContext"))
				{
					if (ImGui::MenuItem("Clear", nullptr, false, rawTextureHandle != 0))
					{
						component.TextureHandle = AssetHandle(0);
						component.Texture.reset();
						MarkModified();
					}
					ImGui::EndPopup();
				}
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
			}
			else
			{
				DrawProperty("Thickness", columnWidth);
				float thickness = component.Thickness;
				if (ImGui::DragFloat("##Thickness", &thickness, 0.01f, 0.0f, 1.0f,
					"%.3f", ImGuiSliderFlags_AlwaysClamp))
				{
					if (!std::isfinite(thickness))
						thickness = component.Thickness;
					thickness = std::clamp(thickness, 0.0f, 1.0f);
					if (thickness != component.Thickness)
					{
						component.Thickness = thickness;
						MarkModified();
					}
				}
				ImGui::Columns(1);

				DrawProperty("Fade", columnWidth);
				float fade = component.Fade;
				if (ImGui::DragFloat("##Fade", &fade, 0.001f, 0.0f, 0.0f, "%.4f"))
				{
					if (!std::isfinite(fade))
						fade = component.Fade;
					fade = std::max(0.0001f, fade);
					if (fade != component.Fade)
					{
						component.Fade = fade;
						MarkModified();
					}
				}
				ImGui::Columns(1);
			}
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
		}, onModified);

		DrawComponent<BoxCollider2D>("Box Collider 2D", entity, m_Icons, EditorIcon::BoxCollider2D,
			[this](auto& component)
		{
			glm::vec2 offset = component.Offset;
			glm::vec2 size = component.Size;
			float density = component.Density;
			float friction = component.Friction;
			float restitution = component.Restitution;
			float restitutionThreshold = component.RestitutionThreshold;

			bool edited = ImGui::DragFloat2("Offset", glm::value_ptr(offset));
			edited |= ImGui::DragFloat2("Size", glm::value_ptr(size));
			edited |= ImGui::DragFloat("Density", &density, 0.01f, 0.0f, 0.0f);
			edited |= ImGui::DragFloat("Friction", &friction, 0.01f, 0.0f, 1.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp);
			edited |= ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp);
			edited |= ImGui::DragFloat("Restitution Threshold", &restitutionThreshold, 0.01f, 0.0f, 0.0f);
			if (edited)
			{
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
					offset = component.Offset;
				if (!std::isfinite(size.x) || !std::isfinite(size.y))
					size = component.Size;
				constexpr float minimumSize = 0.0001f;
				size.x = std::max(minimumSize, size.x);
				size.y = std::max(minimumSize, size.y);
				if (!std::isfinite(density)) density = component.Density;
				if (!std::isfinite(friction)) friction = component.Friction;
				if (!std::isfinite(restitution)) restitution = component.Restitution;
				if (!std::isfinite(restitutionThreshold)) restitutionThreshold = component.RestitutionThreshold;
				density = std::max(0.0f, density);
				friction = std::clamp(friction, 0.0f, 1.0f);
				restitution = std::clamp(restitution, 0.0f, 1.0f);
				restitutionThreshold = std::max(0.0f, restitutionThreshold);

				const bool changed = offset.x != component.Offset.x || offset.y != component.Offset.y ||
					size.x != component.Size.x || size.y != component.Size.y || density != component.Density ||
					friction != component.Friction || restitution != component.Restitution ||
					restitutionThreshold != component.RestitutionThreshold;
				if (changed)
				{
					component.Offset = offset;
					component.Size = size;
					component.Density = density;
					component.Friction = friction;
					component.Restitution = restitution;
					component.RestitutionThreshold = restitutionThreshold;
					MarkModified();
				}
			}
		}, onModified);

		ImGui::Spacing();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
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
			ImGui::EndPopup();
		}

	}

}
