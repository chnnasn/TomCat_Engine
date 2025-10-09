#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include "TomCat/Scene/Components.h"

#include<filesystem>

namespace TomCat {

	extern const std::filesystem::path g_AssetPath;


	// 前向声明DrawProperty函数
	static void DrawProperty(const std::string& label, float columnWidth = 100.0f);

	SceneHierarchyPanel::SceneHierarchyPanel(const Ref<Scene>& context)
	{
		SetContext(context);

	}

	void SceneHierarchyPanel::SetContext(const Ref<Scene>& context)
	{

		m_Context = context;
		m_SelectionContext = {};
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Hierarchy");

		m_Context->m_Registry.view<entt::entity>().each([&](auto entityID)
			{
				Entity entity{ entityID , m_Context.get() };
				DrawEntityNode(entity);
			});

		if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
			m_SelectionContext = {};


		if (ImGui::BeginPopupContextWindow(0, 1 | ImGuiPopupFlags_NoOpenOverItems))
		{
			if (ImGui::MenuItem("Create Empty Entity"))
				m_Context->CreateEntity("Empty Entity");

			ImGui::EndPopup();
		}

		ImGui::End();

		ImGui::Begin("Inspector");
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

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		auto& tag = entity.GetComponent<Tag>()._Tag;

		// 设置选中状态
		ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		bool isSelected = (m_SelectionContext == entity);

		// 绘制实体项
		if (ImGui::Selectable(tag.c_str(), isSelected, flags))
		{
			m_SelectionContext = entity;

			// 双击重命名（可选功能）
			if (ImGui::IsMouseDoubleClicked(0))
			{
				// 可以在这里添加重命名逻辑
			}
		}

		// 右键菜单
		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();
		}

		// 拖拽功能（可选）
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("SCENE_ENTITY", &entity, sizeof(Entity));
			ImGui::Text("Move %s", tag.c_str());
			ImGui::EndDragDropSource();
		}

		// 处理删除
		if (entityDeleted)
		{
			m_Context->DestroyEntity(entity);
			if (m_SelectionContext == entity)
				m_SelectionContext = {};
		}
	}

	static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
	{

		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGui::PushID(label.c_str());

		// 使用DrawProperty函数设置标签在左侧并右对齐
		DrawProperty(label, columnWidth);

		// 为三个滑块设置宽度
		ImGui::PushMultiItemsWidths(3, ImGui::GetContentRegionAvail().x);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

		// X 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("X", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		// Y 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("Y", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		// Z 按钮（无交互）
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushFont(boldFont);
		ImGui::Button("Z", buttonSize);
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PopStyleVar();

		ImGui::Columns(1);

		ImGui::PopID();
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

	template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
	{
		const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_FramePadding;
		if (entity.HasComponent<T>())
		{
			auto& component = entity.GetComponent<T>();
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
			ImGui::PopStyleVar(
			);
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}

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
				uiFunction(component);
				ImGui::TreePop();
			}

			if (removeComponent)
				entity.RemoveComponent<T>();
		}
	}


	void SceneHierarchyPanel::DrawComponents(Entity entity)
	{
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


		ImGui::SameLine();
		ImGui::PushItemWidth(-1);

		if (ImGui::Button("Add Component"))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			if (ImGui::MenuItem("Camera"))
			{
				if (!m_SelectionContext.HasComponent<C_Camera>())
					m_SelectionContext.AddComponent<C_Camera>();
				else
					TC_Core_Warn("This entity already has the Camera Component!");
				ImGui::CloseCurrentPopup();
			}

			if (ImGui::MenuItem("Sprite Renderer"))
			{
				if (!m_SelectionContext.HasComponent<SpriteRenderer>())
					m_SelectionContext.AddComponent<SpriteRenderer>();
				else
					TC_Core_Warn("This entity already has the Sprite Renderer Component!");
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}

		ImGui::PopItemWidth();

		DrawComponent<Transform>("Transform", entity, [](auto& component)
		{
			DrawVec3Control("Translation", component._Translation);
			glm::vec3 rotation = glm::degrees(component._Rotation);
			DrawVec3Control("Rotation", rotation);
			component._Rotation = glm::radians(rotation);
			DrawVec3Control("Scale", component._Scale, 1.0f);
		});

		DrawComponent<C_Camera>("Camera", entity, [](auto& component)
		{
			auto& camera = component._Camera;
			float columnWidth = 100.0f;

			// Primary
			DrawProperty("Primary", columnWidth);
			ImGui::Checkbox("##Primary", &component.Primary);
			ImGui::Columns(1);

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
			}
		});

		DrawComponent<SpriteRenderer>("Sprite Renderer", entity, [](auto& component)
		{

			float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			ImGui::ColorEdit4("##Color", glm::value_ptr(component._Color));
			ImGui::Columns(1);


			DrawProperty("Sprite", columnWidth);

			// 显示纹理名称的输入框（只读）
			std::string textureName = "None";
			if (component.Texture)
			{
				// 直接使用文件路径来提取文件名
				if (component.Texture->GetPath().length() > 0)
				{

					textureName = std::filesystem::path(component.Texture->GetPath()).stem().string();
				}
				else
				{
					textureName = "Missing";
				}
			}


			// 将输入框替换为按钮
			ImGui::Button(textureName.c_str(), ImVec2(-1, 0));

			// 拖拽目标
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PROJECT_ITEM"))
				{
					const wchar_t* path = (const wchar_t*)payload->Data;
					std::filesystem::path texturePath = std::filesystem::path(g_AssetPath) / path;
					component.Texture = Texture2D::Create(texturePath.string());
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Columns(1);

			DrawProperty("Tiling Factor", columnWidth);
			ImGui::DragFloat("##Tiling Factor", &component.TilingFactor, 0.1f, 0.0f, 100.0f);
			ImGui::Columns(1);

		});

	}

}