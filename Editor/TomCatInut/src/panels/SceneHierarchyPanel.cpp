#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include "TomCat/Scene/Components.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Math/Math.h"

#include<filesystem>

namespace TomCat {

	extern const std::filesystem::path g_AssetPath;


	// 前向声明DrawProperty函数
	static void DrawProperty(const std::string& label, float columnWidth = 100.0f);
	static bool DrawCompactCheckbox(const char* id, bool* v, float scale = 0.7f)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID widgetID = window->GetID(id);

		const float frameHeight = ImGui::GetFrameHeight();
		const float squareSize = std::max(1.0f, frameHeight * scale);
		const ImVec2 pos = window->DC.CursorPos;
		const ImRect bb(pos, ImVec2(pos.x + frameHeight, pos.y + frameHeight));

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

		const ImVec2 boxMin(bb.Min.x + (bb.GetWidth() - squareSize) * 0.5f, bb.Min.y + (bb.GetHeight() - squareSize) * 0.5f);
		const ImVec2 boxMax(boxMin.x + squareSize, boxMin.y + squareSize);
		const ImU32 fillColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
		const ImU32 borderColor = ImGui::GetColorU32(held && hovered ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Border);
		const float rounding = style.FrameRounding * 0.75f;

		window->DrawList->AddRectFilled(boxMin, boxMax, fillColor, rounding);
		window->DrawList->AddRect(boxMin, boxMax, borderColor, rounding, 0, 1.0f);

		if (*v)
		{
			const float pad = std::max(1.0f, IM_FLOOR(squareSize / 6.0f));
			// Draw a slightly smaller checkmark so the box can stay compact.
			ImGui::RenderCheckMark(window->DrawList, ImVec2(boxMin.x + pad, boxMin.y + pad), ImGui::GetColorU32(ImGuiCol_CheckMark), squareSize - pad * 2.0f);
		}

		return pressed;
	}

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);

	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context, bool clearSelection, bool remapSelection)
	{

		m_Context = context;
		if (clearSelection)
		{
			m_SelectionContext = {};
		}
		else if (remapSelection && m_SelectionContext)
		{
			// Try to find the same entity in the new scene by UUID
			UUID selectedUUID = m_SelectionContext.GetUUID();
			Entity remappedEntity = m_Context->FindEntityByUUID(selectedUUID);
			if (remappedEntity)
				m_SelectionContext = remappedEntity;
			else
				m_SelectionContext = {};
		}
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		static bool hierarchyWindowOpen = true;

		ImGui::Begin("Hierarchy", &hierarchyWindowOpen, ImGuiWindowFlags_MenuBar);

		if (m_Context)
		{
			if (m_EntityToDelete)
			{
				Entity entity = m_EntityToDelete;
				m_EntityToDelete = {};
				if (m_SelectionContext == entity)
					m_SelectionContext = {};
				if (m_ClipboardEntity == entity)
				{
					m_ClipboardEntity = {};
					m_ClipboardScene = nullptr;
					m_ClipboardIsCut = false;
				}
				m_Context->DestroyEntity(entity);
				if (m_SelectionContext && !m_Context->FindEntityByUUID(m_SelectionContext.GetUUID()))
					m_SelectionContext = {};
			}
			m_EntityToDelete = {};
			const std::string& sceneName = m_Context->GetSceneName();
			ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_FramePadding;
			bool rootOpen = ImGui::TreeNodeEx((void*)m_Context.get(), rootFlags, "%s", sceneName.c_str());

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
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY"))
				{
					Entity draggedEntity = *(const Entity*)payload->Data;
					if (draggedEntity)
						m_Context->SetParent(draggedEntity, Entity{});
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

			if (m_EntityToDelete)
			{
				Entity entity = m_EntityToDelete;
				m_EntityToDelete = {};
				if (m_SelectionContext == entity)
					m_SelectionContext = {};
				m_Context->DestroyEntity(entity);
				if (m_SelectionContext && !m_Context->FindEntityByUUID(m_SelectionContext.GetUUID()))
					m_SelectionContext = {};
			}


			if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
				m_SelectionContext = {};


			if (ImGui::BeginPopupContextWindow(0, 1))
			{
				DrawEntityOperationsMenu();
				ImGui::EndPopup();
			}
		}
		else
		{
			ImGui::TextDisabled("Drag a scene file here to load");
		}

		ImVec2 available = ImGui::GetContentRegionAvail();
		if (available.y > 0)
		{
			ImGui::Dummy(available);
		}

		if (ImGui::BeginDragDropTarget())
		{
			ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TOMCAT_SCENE", flags))
			{
				const wchar_t* path = (const wchar_t*)payload->Data;
				if (m_SceneLoadCallback)
				{
					auto project = ProjectManager::Get().GetActiveProject();
					std::filesystem::path assetPath = project ? project->GetAssetPath() : g_AssetPath;
					m_SceneLoadCallback(assetPath / path);
				}
			}
			else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE", flags))
			{
				const wchar_t* path = (const wchar_t*)payload->Data;
				if (m_SpriteCreateCallback)
				{
					auto project = ProjectManager::Get().GetActiveProject();
					std::filesystem::path assetPath = project ? project->GetAssetPath() : g_AssetPath;
					m_SpriteCreateCallback(assetPath / path);
				}
			}
			else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY", flags))
			{
				Entity draggedEntity = *(const Entity*)payload->Data;
				if (draggedEntity)
					m_Context->SetParent(draggedEntity, Entity{});
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::End();

		static bool inspectorWindowOpen = true;

		ImGui::Begin("Inspector",&inspectorWindowOpen, ImGuiWindowFlags_MenuBar);
		if (m_SelectionContext)
		{
			DrawComponents(m_SelectionContext);

		}

		ImGui::End();
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
		m_SelectionContext = pasted;
		BeginRename(pasted);
		if (m_ClipboardIsCut)
		{
			m_EntityToDelete = m_ClipboardEntity;
			m_ClipboardEntity = {};
			m_ClipboardScene = nullptr;
			m_ClipboardIsCut = false;
		}
	}

	void SceneHierarchyPanel::DuplicateSelectedEntity()
	{
		if (m_SelectionContext)
		{
			m_SelectionContext = m_Context->DuplicateEntity(m_SelectionContext);
			BeginRename(m_SelectionContext);
		}
	}

	void SceneHierarchyPanel::DeleteSelectedEntity()
	{
		if (m_SelectionContext)
			m_EntityToDelete = m_SelectionContext;
	}

	void SceneHierarchyPanel::HandleShortcut(int keyCode, bool control)
	{
		switch (keyCode)
		{
		case Key::X: if (control) CutSelectedEntity(); break;
		case Key::C: if (control) CopySelectedEntity(); break;
		case Key::V: if (control) PasteEntity(); break;
		case Key::D: if (control) DuplicateSelectedEntity(); break;
		case Key::F2: BeginRename(m_SelectionContext); break;
		case Key::Delete: DeleteSelectedEntity(); break;
		}
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
			m_Context->SetParent(m_SelectionContext, Entity{});
		ImGui::Separator();

		if (ImGui::MenuItem("Create Empty Entity"))
		{
			Entity entity = m_Context->CreateEntity("Empty Entity");
			m_SelectionContext = entity;
			BeginRename(entity);
		}
		if (ImGui::MenuItem("Camera"))
		{
			Entity camera = m_Context->CreateEntity("Camera");
			camera.AddComponent<C_Camera>();
			m_SelectionContext = camera;
			BeginRename(camera);
		}

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::BeginMenu("Sprites"))
			{
				if (ImGui::MenuItem("Square"))
				{
					Entity square = m_Context->CreateEntity("Square");
					square.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
					m_SelectionContext = square;
					BeginRename(square);
				}
				ImGui::EndMenu();
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
		if (renameActive)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

		bool open = ImGui::TreeNodeEx((void*)(uint64_t)entity.GetUUID(), flags, "%s", tag.c_str());
		const ImVec2 itemMin = ImGui::GetItemRectMin();
		const ImVec2 itemMax = ImGui::GetItemRectMax();
		const float textOffsetX = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
		const float textOffsetY = itemMin.y + (itemMax.y - itemMin.y - ImGui::GetTextLineHeight()) * 0.5f;

		if (renameActive)
			ImGui::PopStyleColor();
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
			ImGui::SetDragDropPayload("SCENE_ENTITY", &entity, sizeof(Entity));
			ImGui::Text("Move %s", tag.c_str());
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY"))
			{
				Entity draggedEntity = *(const Entity*)payload->Data;
				if (draggedEntity && draggedEntity != entity)
					m_Context->SetParent(draggedEntity, entity);
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
				if (m_RenameBuffer[0] != '\0')
					tag = m_RenameBuffer;
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
	template<> static bool* GetComponentEnabledFlag<Rigidbody2D>(Rigidbody2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<BoxCollider2D>(BoxCollider2D& component) { return &component.Enabled; }

	template<typename T, typename UIFunction>
static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
{
	// 检查实体是否有效
	if (!entity)
		return;

	const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
	if (entity.HasComponent<T>())
	{
		auto& component = entity.GetComponent<T>();

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

		if (bool* enabled = GetComponentEnabledFlag(component))
		{
			const float checkboxPosY = headerMin.y + (headerHeight - ImGui::GetFrameHeight()) * 0.5f;
			ImGui::SetCursorScreenPos(ImVec2(headerMin.x + headerHeight, checkboxPosY));
			DrawCompactCheckbox((std::string("##") + name + "_Enabled").c_str(), enabled);
		}

		const float leftContentX = headerMin.x + headerHeight +
			(GetComponentEnabledFlag(component) ? headerHeight + ImGui::GetStyle().ItemSpacing.x : 0.0f);
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
			entity.RemoveComponent<T>();
	}
	}


	void SceneHierarchyPanel::DrawComponents(Entity entity)
{
	// 检查实体是否有效
	if (!entity)
		return;

	if (entity.HasComponent<Tag>())
	{
		auto& tag = entity.GetComponent<Tag>()._Tag;

			// 使用string作为中间缓冲，避免直接操作char数组的问题
			static std::string editBuffer;
			static Entity editingEntity; // 跟踪正在编辑的实体
			static bool isEditing = false;

			// 开始编辑时保存当前值到缓冲
			if (ImGui::IsItemActivated()) {
				editBuffer = tag;
				editingEntity = entity;
				isEditing = true;
			}

			// 安全的char数组处理
			char buffer[256];
			memset(buffer, 0, sizeof(buffer));

			// 安全复制 - 确保即使空字符串也能正确处理
			const std::string& source = (isEditing && editingEntity == entity) ? editBuffer : tag;
			if (!source.empty()) {
				strncpy_s(buffer, sizeof(buffer), source.c_str(), _TRUNCATE);
			}

			// 设置输入文本标志，允许空输入
			ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;

			DrawCompactCheckbox("##Visible", &entity.GetComponent<Tag>().Visible);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer), flags))
			{
				// 直接设置，允许空字符串
				tag = std::string(buffer);
				isEditing = false;
				editingEntity = {};
			}

			// 失去焦点时也确认修改
			if (isEditing && editingEntity == entity && ImGui::IsItemDeactivated())
			{
				tag = std::string(buffer);
				isEditing = false;
				editingEntity = {};
			}

			// 确保Tag永远不会完全为空，提供默认值
			if (tag.empty()) {
				tag = "Entity";
			}
		}


		DrawComponent<Transform>("Transform", entity, [this, entity](auto& component)
		{
			Entity parent = m_Context ? m_Context->GetParent(entity) : Entity{};
			const bool hasParent = (bool)parent;

			glm::vec3 translation = hasParent ? component._LocalTranslation : component._Translation;
			glm::vec3 rotation = glm::degrees(hasParent ? component._LocalRotation : component._Rotation);
			glm::vec3 scale = hasParent ? component._LocalScale : component._Scale;

			bool changed = false;
			changed |= DrawVec3Control(hasParent ? "Local Translation" : "Translation", translation);
			changed |= DrawVec3Control(hasParent ? "Local Rotation" : "Rotation", rotation);
			changed |= DrawVec3Control(hasParent ? "Local Scale" : "Scale", scale, 1.0f);

			if (changed && m_Context)
			{
				glm::mat4 transformMatrix = Math::ComposeTransform(translation, glm::radians(rotation), scale);
				if (hasParent)
					m_Context->SetLocalTransform(entity, transformMatrix);
				else
					m_Context->SetWorldTransform(entity, transformMatrix);
			}
		});

		DrawComponent<C_Camera>("Camera", entity, [](auto& component)
		{
			auto& camera = component._Camera;
			float columnWidth = 100.0f;

			// Projection Type
			DrawProperty("Projection", columnWidth);
			const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
			const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
			if (ImGui::BeginCombo("##Projection", currentProjectionTypeString))
			{
				for (int i = 0; i < 2; i++)
				{
					bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
					if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
					{
						currentProjectionTypeString = projectionTypeStrings[i];
						camera.SetProjectionType((SceneCamera::ProjectionType)i);
					}

					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}

				ImGui::EndCombo();
			}
			ImGui::Columns(1);

			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				// Vertical FOV
				DrawProperty("Vertical FOV", columnWidth);
				float perspectiveVerticalFov = glm::degrees(camera.GetPerspectiveVerticalFOV());
				if (ImGui::DragFloat("##VerticalFOV", &perspectiveVerticalFov))
					camera.SetPerspectiveVerticalFOV(glm::radians(perspectiveVerticalFov));
				ImGui::Columns(1);

				// Near
				DrawProperty("Near", columnWidth);
				float perspectiveNear = camera.GetPerspectiveNearClip();
				if (ImGui::DragFloat("##PerspectiveNear", &perspectiveNear))
					camera.SetPerspectiveNearClip(perspectiveNear);
				ImGui::Columns(1);

				// Far
			DrawProperty("Far", columnWidth);
			float perspectiveFar = camera.GetPerspectiveFarClip();
			if (ImGui::DragFloat("##PerspectiveFar", &perspectiveFar))
				camera.SetPerspectiveFarClip(perspectiveFar);
			ImGui::Columns(1);

			// Background Color
			DrawProperty("Background Color", columnWidth);
			ImGui::ColorEdit4("##BackgroundColor", glm::value_ptr(component.BackgroundColor));
			ImGui::Columns(1);
		}

		if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
			{
				// Size
				DrawProperty("Size", columnWidth);
				float orthoSize = camera.GetOrthographicSize();
				if (ImGui::DragFloat("##OrthoSize", &orthoSize))
					camera.SetOrthographicSize(orthoSize);
				ImGui::Columns(1);

				// Near
				DrawProperty("Near", columnWidth);
				float orthoNear = camera.GetOrthographicNearClip();
				if (ImGui::DragFloat("##OrthoNear", &orthoNear))
					camera.SetOrthographicNearClip(orthoNear);
				ImGui::Columns(1);

				// Far
				DrawProperty("Far", columnWidth);
				float orthoFar = camera.GetOrthographicFarClip();
				if (ImGui::DragFloat("##OrthoFar", &orthoFar))
					camera.SetOrthographicFarClip(orthoFar);
				ImGui::Columns(1);

				// Fixed Aspect Ratio
			DrawProperty("Fixed Aspect Ratio", columnWidth);
			ImGui::Checkbox("##FixedAspectRatio", &component.FixedAspectRatio);
			ImGui::Columns(1);

			// Background Color
			DrawProperty("Background Color", columnWidth);
			ImGui::ColorEdit4("##BackgroundColor", glm::value_ptr(component.BackgroundColor));
			ImGui::Columns(1);
		}
	});

		DrawComponent<SpriteRenderer>("Sprite Renderer", entity, [](auto& component)
		{

			float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			ImGui::ColorEdit4("##Color", glm::value_ptr(component._Color));
			ImGui::Columns(1);


			DrawProperty("Sprite", columnWidth);

			std::string textureName = "None";
			if (component.Texture)
			{
				if (component.Texture->GetPath().length() > 0)
				{
					textureName = std::filesystem::path(component.Texture->GetPath()).stem().string();
				}
				else
				{
					textureName = "Missing";
				}
			}


			ImGui::Button(textureName.c_str(), ImVec2(-1, 0));

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SPRITE"))
				{
					auto project = ProjectManager::Get().GetActiveProject();
					std::filesystem::path assetPath = project ? project->GetAssetPath() : g_AssetPath;
					const wchar_t* path = (const wchar_t*)payload->Data;
					std::filesystem::path texturePath = assetPath / path;

					Ref<Texture2D> texture = Texture2D::Create(texturePath.string());
					if (texture->IsLoaded())
						component.Texture = texture;
					else
						TC_Warn("Could not load texture {0}", texturePath.filename().string());
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Columns(1);

			DrawProperty("Tiling Factor", columnWidth);
			ImGui::DragFloat("##Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.0f);
			ImGui::Columns(1);

		});

		DrawComponent<Rigidbody2D>("Rigidbody 2D", entity, [](auto& component)
			{
				const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
				const char* currentBodyTypeString = bodyTypeStrings[(int)component.Type];
				if (ImGui::BeginCombo("Body Type", currentBodyTypeString))
				{
					for (int i = 0; i < 2; i++)
					{
						bool isSelected = currentBodyTypeString == bodyTypeStrings[i];
						if (ImGui::Selectable(bodyTypeStrings[i], isSelected))
						{
							currentBodyTypeString = bodyTypeStrings[i];
							component.Type = (Rigidbody2D::BodyType)i;
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}

					ImGui::EndCombo();
				}

				ImGui::Checkbox("Fixed Rotation", &component.FixedRotation);
			});

		DrawComponent<BoxCollider2D>("Box Collider 2D", entity, [](auto& component)
			{
				ImGui::DragFloat2("Offset", glm::value_ptr(component.Offset));
				ImGui::DragFloat2("Size", glm::value_ptr(component.Size));
				ImGui::DragFloat("Density", &component.Density, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Friction", &component.Friction, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Restitution", &component.Restitution, 0.01f, 0.0f, 1.0f);
				ImGui::DragFloat("Restitution Threshold", &component.RestitutionThreshold, 0.01f, 0.0f);
			});

		ImGui::Spacing();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			if (!entity.HasComponent<C_Camera>() && ImGui::MenuItem("Camera"))
			{
				entity.AddComponent<C_Camera>();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<SpriteRenderer>() && ImGui::MenuItem("Sprite Renderer"))
			{
				entity.AddComponent<SpriteRenderer>();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<Rigidbody2D>() && ImGui::MenuItem("Rigidbody 2D"))
			{
				entity.AddComponent<Rigidbody2D>();
				ImGui::CloseCurrentPopup();
			}
			if (!entity.HasComponent<BoxCollider2D>() && ImGui::MenuItem("Box Collider 2D"))
			{
				entity.AddComponent<BoxCollider2D>();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

	}

}
