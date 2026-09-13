#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"
#include "../EditorDragDrop.h"

namespace TomCat {

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

	template<typename T>
	static T ScriptRangeEndpoint(double value)
	{
		if (!std::isfinite(value))
			return T{};
		if (value <= static_cast<double>(std::numeric_limits<T>::lowest()))
			return std::numeric_limits<T>::lowest();
		if (value >= static_cast<double>(std::numeric_limits<T>::max()))
			return std::numeric_limits<T>::max();
		return static_cast<T>(value);
	}

	static void DrawScriptFieldTooltip(const EditorScriptFieldMetadata* metadata)
	{
		if (metadata && !metadata->Tooltip.empty() &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", metadata->Tooltip.c_str());
	}

	static bool DrawScriptFieldValue(ScriptField& field,
		const EditorScriptFieldMetadata* metadata, bool orphan)
	{
		ImGui::PushID(field.FieldID.empty() ? field.Name.c_str() : field.FieldID.c_str());
		std::string label = field.Name.empty() ? "Unnamed Field" : field.Name;
		if (metadata && !metadata->Name.empty())
			label = metadata->Name;
		if (orphan)
			label += " (Orphan)";

		if (orphan)
			ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.25f, 1.0f), "%s", label.c_str());
		else
			ImGui::TextUnformatted(label.c_str());
		if (metadata && field.Type == ScriptFieldType::Enum && !metadata->TypeName.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", metadata->TypeName.c_str());
		}
		DrawScriptFieldTooltip(metadata);
		ImGui::SetNextItemWidth(-1.0f);

		if (!IsScriptFieldValueCompatible(field.Type, field.Value))
		{
			ImGui::TextDisabled("Stored value is incompatible with %s",
				ScriptFieldTypeToString(field.Type));
			bool changed = false;
			if (ImGui::SmallButton("Reset value"))
			{
				field.Value = DefaultScriptFieldValue(field.Type);
				changed = true;
			}
			ImGui::PopID();
			return changed;
		}

		const bool hasRange = metadata && metadata->RangeMinimum && metadata->RangeMaximum &&
			*metadata->RangeMinimum <= *metadata->RangeMaximum;
		bool changed = false;
		switch (field.Type)
		{
			case ScriptFieldType::Bool:
				changed = ImGui::Checkbox("##Value", &std::get<bool>(field.Value));
				break;
			case ScriptFieldType::Int32:
			{
				auto& value = std::get<int32_t>(field.Value);
				if (hasRange)
				{
					const int32_t minimum = ScriptRangeEndpoint<int32_t>(*metadata->RangeMinimum);
					const int32_t maximum = ScriptRangeEndpoint<int32_t>(*metadata->RangeMaximum);
					changed = ImGui::SliderScalar("##Value", ImGuiDataType_S32, &value,
						&minimum, &maximum);
				}
				else
					changed = ImGui::InputScalar("##Value", ImGuiDataType_S32, &value);
				break;
			}
			case ScriptFieldType::Int64:
			case ScriptFieldType::Enum:
			{
				auto& value = std::get<int64_t>(field.Value);
				if (hasRange)
				{
					const int64_t minimum = ScriptRangeEndpoint<int64_t>(*metadata->RangeMinimum);
					const int64_t maximum = ScriptRangeEndpoint<int64_t>(*metadata->RangeMaximum);
					changed = ImGui::SliderScalar("##Value", ImGuiDataType_S64, &value,
						&minimum, &maximum);
				}
				else
					changed = ImGui::InputScalar("##Value", ImGuiDataType_S64, &value);
				break;
			}
			case ScriptFieldType::Float:
			{
				auto& value = std::get<float>(field.Value);
				if (hasRange)
				{
					const float minimum = static_cast<float>(*metadata->RangeMinimum);
					const float maximum = static_cast<float>(*metadata->RangeMaximum);
					changed = ImGui::SliderFloat("##Value", &value, minimum, maximum);
				}
				else
					changed = ImGui::DragFloat("##Value", &value, 0.01f);
				break;
			}
			case ScriptFieldType::Double:
			{
				auto& value = std::get<double>(field.Value);
				if (hasRange)
				{
					const double minimum = *metadata->RangeMinimum;
					const double maximum = *metadata->RangeMaximum;
					changed = ImGui::SliderScalar("##Value", ImGuiDataType_Double, &value,
						&minimum, &maximum, "%.6f");
				}
				else
					changed = ImGui::DragScalar("##Value", ImGuiDataType_Double, &value,
						0.01f, nullptr, nullptr, "%.6f");
				break;
			}
			case ScriptFieldType::String:
			{
				auto& value = std::get<std::string>(field.Value);
				std::vector<char> buffer(std::max<size_t>(1024, value.size() + 256), '\0');
				std::copy(value.begin(), value.end(), buffer.begin());
				if (ImGui::InputText("##Value", buffer.data(), buffer.size()))
				{
					value = buffer.data();
					changed = true;
				}
				break;
			}
			case ScriptFieldType::Vector2:
			{
				auto& value = std::get<glm::vec2>(field.Value);
				changed = hasRange
					? ImGui::SliderFloat2("##Value", glm::value_ptr(value),
						static_cast<float>(*metadata->RangeMinimum),
						static_cast<float>(*metadata->RangeMaximum))
					: ImGui::DragFloat2("##Value", glm::value_ptr(value), 0.01f);
				break;
			}
			case ScriptFieldType::Vector3:
			{
				auto& value = std::get<glm::vec3>(field.Value);
				changed = hasRange
					? ImGui::SliderFloat3("##Value", glm::value_ptr(value),
						static_cast<float>(*metadata->RangeMinimum),
						static_cast<float>(*metadata->RangeMaximum))
					: ImGui::DragFloat3("##Value", glm::value_ptr(value), 0.01f);
				break;
			}
			case ScriptFieldType::Vector4:
			{
				auto& value = std::get<glm::vec4>(field.Value);
				changed = hasRange
					? ImGui::SliderFloat4("##Value", glm::value_ptr(value),
						static_cast<float>(*metadata->RangeMinimum),
						static_cast<float>(*metadata->RangeMaximum))
					: ImGui::DragFloat4("##Value", glm::value_ptr(value), 0.01f);
				break;
			}
			case ScriptFieldType::Color:
				changed = ImGui::ColorEdit4("##Value",
					glm::value_ptr(std::get<glm::vec4>(field.Value)));
				break;
			case ScriptFieldType::Entity:
			case ScriptFieldType::AssetRef:
			{
				auto& value = std::get<uint64_t>(field.Value);
				changed = ImGui::InputScalar("##Value", ImGuiDataType_U64, &value);
				if (ImGui::BeginDragDropTarget())
				{
					const char* payloadID = field.Type == ScriptFieldType::Entity
						? SceneEntityDragDropPayloadID : AssetDragDropPayloadID;
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadID))
					{
						if (payload->DataSize == sizeof(uint64_t))
						{
							value = *static_cast<const uint64_t*>(payload->Data);
							changed = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
				break;
			}
		}
		DrawScriptFieldTooltip(metadata);
		ImGui::PopID();
		return changed;
	}

	static bool MatchesSpriteSearch(const AssetMetadata& metadata, const char* search)
	{
		if (!search || search[0] == '\0')
			return true;
		const std::string query = LowerASCII(search);
		return LowerASCII(PathToUTF8(metadata.FilePath)).find(query) != std::string::npos;
	}

	static bool ResolveSelectableSprite(AssetHandle handle,
		const AssetMetadata*& metadata, const AssetSubAsset*& subAsset)
	{
		metadata = nullptr;
		subAsset = nullptr;
		if (static_cast<uint64_t>(handle) == 0)
			return true;
		AssetRegistry& registry = AssetManager::Get().GetRegistry();
		metadata = registry.GetMetadata(handle);
		if (!metadata)
			metadata = registry.GetSubAssetOwner(handle, &subAsset);
		return metadata && !metadata->IsMissing && IsSpriteAsset(*metadata)
			&& (!subAsset || subAsset->Type == AssetType::Texture2D);
	}

	static std::string AnimatorSpriteLabel(AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return "None";
		const AssetMetadata* metadata = nullptr;
		const AssetSubAsset* subAsset = nullptr;
		if (!ResolveSelectableSprite(handle, metadata, subAsset))
			return "Missing #" + std::to_string(static_cast<uint64_t>(handle));
		std::string label = PathToUTF8(metadata->FilePath.filename());
		if (subAsset)
			label += " / " + subAsset->Name;
		return label;
	}

	static bool DrawAnimatorSpriteField(const char* id, AssetHandle& handle)
	{
		bool changed = false;
		const std::string label = AnimatorSpriteLabel(handle);
		const std::string buttonLabel = label + "###" + id;
		if (ImGui::Button(buttonLabel.c_str(), ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("Select Sprite");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				AssetDragDropPayloadID))
			{
				if (payload->DataSize == sizeof(uint64_t))
				{
					const AssetHandle candidate(
						*static_cast<const uint64_t*>(payload->Data));
					const AssetMetadata* metadata = nullptr;
					const AssetSubAsset* subAsset = nullptr;
					if (ResolveSelectableSprite(candidate, metadata, subAsset)
						&& candidate != handle)
					{
						handle = candidate;
						changed = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("Sprite Context"))
		{
			if (ImGui::MenuItem("Clear", nullptr, false,
				static_cast<uint64_t>(handle) != 0))
			{
				handle = AssetHandle(0);
				changed = true;
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("Select Sprite"))
		{
			if (ImGui::Selectable("None", static_cast<uint64_t>(handle) == 0))
			{
				handle = AssetHandle(0);
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			std::vector<const AssetMetadata*> sprites;
			for (const auto& [assetHandle, metadata] :
				AssetManager::Get().GetRegistry().GetAssets())
			{
				(void)assetHandle;
				if (IsSpriteAsset(metadata) && !metadata.IsMissing)
					sprites.push_back(&metadata);
			}
			std::sort(sprites.begin(), sprites.end(), [](const AssetMetadata* left,
				const AssetMetadata* right)
			{
				return LowerASCII(PathToUTF8(left->FilePath))
					< LowerASCII(PathToUTF8(right->FilePath));
			});
			for (const AssetMetadata* metadata : sprites)
			{
				const std::string parentName = PathToUTF8(metadata->FilePath.filename());
				const std::string parentID = parentName + "###Sprite_"
					+ std::to_string(static_cast<uint64_t>(metadata->Handle));
				if (ImGui::Selectable(parentID.c_str(), handle == metadata->Handle))
				{
					handle = metadata->Handle;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				for (const AssetSubAsset& child : metadata->SubAssets)
				{
					if (child.Type != AssetType::Texture2D)
						continue;
					ImGui::Indent();
					const std::string childName = child.Name + "###Sprite_"
						+ std::to_string(static_cast<uint64_t>(child.Handle));
					if (ImGui::Selectable(childName.c_str(), handle == child.Handle))
					{
						handle = child.Handle;
						changed = true;
						ImGui::CloseCurrentPopup();
					}
					ImGui::Unindent();
				}
			}
			ImGui::EndPopup();
		}
		return changed;
	}

	static void DrawAnimatorSectionLabel(const char* label)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("%s", label);
		ImGui::Separator();
	}

	static bool ResolveRegisteredAssetReference(
		const AssetPropertyMetadata& semantics, AssetHandle handle,
		const AssetMetadata*& metadata, const AssetSubAsset*& subAsset)
	{
		metadata = nullptr;
		subAsset = nullptr;
		if (static_cast<uint64_t>(handle) == 0)
			return true;
		AssetRegistry& registry = AssetManager::Get().GetRegistry();
		metadata = registry.GetMetadata(handle);
		if (!metadata)
			metadata = registry.GetSubAssetOwner(handle, &subAsset);
		const AssetType type = subAsset ? subAsset->Type
			: metadata ? metadata->Type : AssetType::None;
		return metadata && !metadata->IsMissing
			&& semantics.Accepts(type, subAsset != nullptr);
	}

	static std::string RegisteredAssetReferenceLabel(
		const AssetPropertyMetadata& semantics, AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return "None";
		const AssetMetadata* metadata = nullptr;
		const AssetSubAsset* subAsset = nullptr;
		const bool compatible = ResolveRegisteredAssetReference(semantics,
			handle, metadata, subAsset);
		if (!metadata)
			return "Missing #" + std::to_string(static_cast<uint64_t>(handle));
		std::string label = PathToUTF8(metadata->FilePath.filename());
		if (subAsset)
			label += " / " + subAsset->Name;
		if (metadata->IsMissing)
			label += " (Missing)";
		else if (!compatible)
			label += " (Incompatible)";
		return label;
	}

	static bool DrawRegisteredAssetReference(const PropertyDescriptor& property,
		AssetHandle& handle)
	{
		const AssetPropertyMetadata& semantics = *property.AssetReference;
		ImGui::TextUnformatted(property.DisplayName.c_str());
		if (semantics.AllowSubAssets && semantics.AcceptedTypes.size() == 1
			&& semantics.AcceptedTypes.front() == AssetType::Texture2D)
			return DrawAnimatorSpriteField("RegisteredSpriteReference", handle);

		bool changed = false;
		const std::string label = RegisteredAssetReferenceLabel(semantics, handle)
			+ "###RegisteredAssetReference";
		ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f));
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
		{
			std::string accepted;
			for (const AssetType type : semantics.AcceptedTypes)
			{
				if (!accepted.empty())
					accepted += ", ";
				accepted += AssetTypeToString(type);
			}
			ImGui::SetTooltip("Accepted: %s\nDrag an asset here. Right-click to clear.",
				accepted.c_str());
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				AssetDragDropPayloadID, ImGuiDragDropFlags_AcceptBeforeDelivery))
			{
				if (payload->DataSize == sizeof(uint64_t))
				{
					const AssetHandle candidate(
						*static_cast<const uint64_t*>(payload->Data));
					const AssetMetadata* metadata = nullptr;
					const AssetSubAsset* subAsset = nullptr;
					if (ResolveRegisteredAssetReference(semantics, candidate,
						metadata, subAsset) && payload->IsDelivery()
						&& candidate != handle)
					{
						handle = candidate;
						changed = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("RegisteredAssetReferenceContext"))
		{
			if (ImGui::MenuItem("Clear", nullptr, false,
				static_cast<uint64_t>(handle) != 0))
			{
				handle = AssetHandle(0);
				changed = true;
			}
			ImGui::EndPopup();
		}
		return changed;
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
		if (m_ModificationGestureActive && m_SceneModifiedCallback)
			m_SceneModifiedCallback(SceneModificationPhase::Cancel);
		m_ModificationGestureActive = false;
		m_CommitAfterPendingDeletion = false;
		UUID selectedUUID{};
		const bool hadSelection = !clearSelection && remapSelection && (bool)m_SelectionContext;
		if (hadSelection)
			selectedUUID = m_SelectionContext.GetUUID();
		const bool contextChanged = m_Context != context;
		m_Context = context;
		if (m_Context && m_Project)
			m_Context->SetPhysics2DSettings(m_Project->GetSettings().Physics2D);
		m_ForceExpandParent = {};
		m_ForceOpenEntityNodes.clear();
		m_ForceOpenSceneRoot = false;
		m_EntityToDelete = {};
		m_RenameEntity = {};
		m_RenameFocus = false;
		m_NameEditingEntity = {};
		m_SpritePickerOpen = false;
		m_SpritePickerEntity = UUID(0);
		m_SpriteSearch.fill('\0');
		m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		m_AnimatorRenameEntity = UUID(0);
		m_AnimatorRenameBuffer.fill('\0');
		m_AnimatorRenameError.clear();
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

	void SceneHierarchyPanel::MarkModified(bool instant)
	{
		if (!m_SceneModifiedCallback)
			return;
		if (instant)
		{
			if (m_ModificationGestureActive)
				m_SceneModifiedCallback(SceneModificationPhase::Commit);
			m_ModificationGestureActive = false;
			m_SceneModifiedCallback(SceneModificationPhase::Instant);
			return;
		}

		const bool hasImGui = ImGui::GetCurrentContext() != nullptr;
		const bool itemActivated = hasImGui && ImGui::IsItemActivated();
		if (m_ModificationGestureActive && itemActivated)
		{
			m_SceneModifiedCallback(SceneModificationPhase::Commit);
			m_ModificationGestureActive = false;
		}
		if (!hasImGui || !ImGui::IsAnyItemActive())
		{
			m_SceneModifiedCallback(SceneModificationPhase::Instant);
			return;
		}
		if (!m_ModificationGestureActive)
		{
			m_SceneModifiedCallback(SceneModificationPhase::Begin);
			m_ModificationGestureActive = true;
		}
		m_SceneModifiedCallback(SceneModificationPhase::Update);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			m_SceneModifiedCallback(SceneModificationPhase::Commit);
			m_ModificationGestureActive = false;
		}
	}

	void SceneHierarchyPanel::FinishModificationGesture()
	{
		if (!m_ModificationGestureActive || !m_SceneModifiedCallback)
			return;
		if (!ImGui::GetCurrentContext() || !ImGui::IsAnyItemActive())
		{
			m_SceneModifiedCallback(SceneModificationPhase::Commit);
			m_ModificationGestureActive = false;
		}
	}

	bool SceneHierarchyPanel::AttachCSharpScript(Entity entity, AssetHandle handle)
	{
		if (!entity || !m_Context || !m_ColliderEditingAllowed ||
			static_cast<uint64_t>(handle) == 0)
			return false;
		const AssetMetadata* assetMetadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!assetMetadata || assetMetadata->IsMissing ||
			assetMetadata->Type != AssetType::CSharpScript)
			return false;

		std::optional<EditorScriptMetadata> scriptMetadata;
		if (m_ScriptMetadataProvider)
			scriptMetadata = m_ScriptMetadataProvider(handle);

		auto& component = entity.HasComponent<CSharpScripts>()
			? entity.GetComponent<CSharpScripts>()
			: entity.AddComponent<CSharpScripts>();
		if (scriptMetadata && scriptMetadata->DisallowMultiple)
		{
			const bool alreadyAttached = std::any_of(component.Scripts.begin(),
				component.Scripts.end(), [handle](const CSharpScriptEntry& entry)
				{
					return entry.ScriptAsset == handle;
				});
			if (alreadyAttached)
			{
				TC_Warn("Script '{0}' disallows multiple attachments on one entity",
					PathToUTF8(assetMetadata->FilePath));
				return false;
			}
		}

		CSharpScriptEntry entry;
		auto attachmentIDIsAvailable = [this](uint64_t candidate)
		{
			if (candidate == 0)
				return false;
			const auto view = m_Context->m_Registry.view<CSharpScripts>();
			for (const entt::entity entityHandle : view)
			{
				const auto& existingScripts =
					view.get<CSharpScripts>(entityHandle).Scripts;
				if (std::any_of(existingScripts.begin(), existingScripts.end(),
					[candidate](const CSharpScriptEntry& existing)
					{
						return static_cast<uint64_t>(existing.AttachmentID) ==
							candidate;
					}))
					return false;
			}
			return true;
		};
		bool hasUniqueAttachmentID = false;
		for (uint32_t attempt = 0; attempt < 64; ++attempt)
		{
			const uint64_t rawID = static_cast<uint64_t>(entry.AttachmentID);
			hasUniqueAttachmentID = attachmentIDIsAvailable(rawID);
			if (hasUniqueAttachmentID)
				break;
			entry.AttachmentID = UUID();
		}
		if (!hasUniqueAttachmentID)
		{
			TC_Core_Error("Could not allocate a unique C# script attachment ID");
			return false;
		}

		entry.ScriptAsset = handle;
		entry.Enabled = true;
		entry.LastKnownClassName = scriptMetadata && !scriptMetadata->TypeName.empty()
			? scriptMetadata->TypeName
			: PathToUTF8(AssetManager::Get().ResolvePath(handle).stem());
		if (scriptMetadata)
		{
			entry.Fields.reserve(scriptMetadata->Fields.size());
			for (const EditorScriptFieldMetadata& fieldMetadata : scriptMetadata->Fields)
			{
				ScriptFieldValue value = fieldMetadata.DefaultValue &&
					IsScriptFieldValueCompatible(fieldMetadata.Type,
						*fieldMetadata.DefaultValue)
					? *fieldMetadata.DefaultValue
					: DefaultScriptFieldValue(fieldMetadata.Type);
				entry.Fields.emplace_back(fieldMetadata.FieldID, fieldMetadata.Name,
					fieldMetadata.Type, std::move(value));
			}
		}
		component.Scripts.push_back(std::move(entry));
		MarkModified();
		return true;
	}

	bool SceneHierarchyPanel::AcceptCSharpScriptDrop(Entity entity)
	{
		if (!entity || !m_ColliderEditingAllowed)
			return false;
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			AssetDragDropPayloadID, ImGuiDragDropFlags_AcceptBeforeDelivery);
		if (!payload || payload->DataSize != sizeof(uint64_t))
			return false;
		const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
		const AssetMetadata* metadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!metadata || metadata->Type != AssetType::CSharpScript)
			return false;
		if (payload->IsDelivery())
			AttachCSharpScript(entity, handle);
		return true;
	}

	bool SceneHierarchyPanel::AcceptPrefabDrop(Entity parent)
	{
		if (!m_Context || !m_PrefabInstantiateCallback)
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
			Entity root = m_PrefabInstantiateCallback(handle, parent);
			if (root)
			{
				m_SelectionContext = root;
				if (parent)
					m_ForceExpandParent = parent;
				else
					m_ForceOpenSceneRoot = true;
			}
		}
		return true;
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
		if (m_CommitAfterPendingDeletion && m_SceneModifiedCallback)
		{
			m_SceneModifiedCallback(SceneModificationPhase::Update);
			m_SceneModifiedCallback(SceneModificationPhase::Commit);
			m_CommitAfterPendingDeletion = false;
		}
		else
			MarkModified(true);
		return true;
	}

	bool SceneHierarchyPanel::DrawGameObjectMenu()
	{
		if (!m_Context)
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

	void SceneHierarchyPanel::OnImGuiRender(bool* hierarchyOpen, bool* inspectorOpen)
	{
		if (m_ColliderEditMode != ColliderEditMode::None
			&& GetColliderEditMode() == ColliderEditMode::None)
			ClearColliderEditMode();
		// Keyboard commands can originate from the Scene viewport while Hierarchy
		// is hidden or covered by another dock tab. Drain deletion before any tree
		// traversal so it never remains queued until the panel becomes visible.
		FlushPendingDeletion();
		m_HierarchyFocused = false;
		m_InspectorFocused = false;
		if (!hierarchyOpen || *hierarchyOpen)
		{
		const bool hierarchyVisible = ImGui::Begin("Hierarchy", hierarchyOpen);
		m_HierarchyDocked = ImGui::IsWindowDocked();
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
				const bool acceptedPrefab = AcceptPrefabDrop({});
				if (!acceptedPrefab)
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
					else if (metadata && !metadata->IsMissing
						&& metadata->Type == AssetType::Prefab
						&& m_PrefabInstantiateCallback)
					{
						Entity root = m_PrefabInstantiateCallback(handle, {});
						if (root)
						{
							m_SelectionContext = root;
							m_ForceOpenSceneRoot = true;
						}
					}
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
			m_InspectorDocked = ImGui::IsWindowDocked();
			m_InspectorFocused = inspectorVisible &&
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			if (inspectorVisible && m_SelectionContext)
			{
				DrawComponents(m_SelectionContext);
				const ImVec2 windowPosition = ImGui::GetWindowPos();
				const ImVec2 contentMinimum = ImGui::GetWindowContentRegionMin();
				const ImVec2 contentMaximum = ImGui::GetWindowContentRegionMax();
				const ImRect dropRect(
					ImVec2(windowPosition.x + contentMinimum.x,
						windowPosition.y + contentMinimum.y),
					ImVec2(windowPosition.x + contentMaximum.x,
						windowPosition.y + contentMaximum.y));
				if (ImGui::BeginDragDropTargetCustom(dropRect,
					ImGui::GetID("##InspectorCSharpScriptDrop")))
				{
					AcceptCSharpScriptDrop(m_SelectionContext);
					ImGui::EndDragDropTarget();
				}
			}
			ImGui::End();
		}
		FinishModificationGesture();
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		if (m_SelectionContext != entity)
		{
			ClearColliderEditMode();
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
		}
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
		m_ForceOpenSceneRoot = true;
		if (m_Context)
		{
			Entity ancestor = m_Context->GetParent(entity);
			while (ancestor)
			{
				const uint64_t ancestorID = static_cast<uint64_t>(ancestor.GetUUID());
				if (!m_ForceOpenEntityNodes.insert(ancestorID).second)
					break;
				ancestor = m_Context->GetParent(ancestor);
			}
		}
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
		const bool cutPaste = m_ClipboardIsCut;
		if (cutPaste)
		{
			if (m_SceneModifiedCallback)
				m_SceneModifiedCallback(SceneModificationPhase::Begin);
			m_EntityToDelete = m_ClipboardEntity;
			ClearClipboard();
			m_CommitAfterPendingDeletion = true;
			if (m_SceneModifiedCallback)
				m_SceneModifiedCallback(SceneModificationPhase::Update);
		}
		else
			MarkModified(true);
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
			MarkModified(true);
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
		const bool canCreatePrefab = hasSelection && m_PrefabCreationAllowed
			&& static_cast<bool>(m_PrefabCreateCallback);
		if (ImGui::MenuItem("Create Prefab From Selection", nullptr, false,
			canCreatePrefab))
			m_PrefabCreateCallback(m_SelectionContext);
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
			MarkModified(true);
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
			const bool acceptedAsset = AcceptCSharpScriptDrop(entity)
				|| AcceptPrefabDrop(entity);
			if (!acceptedAsset)
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
				ImGui::SetScrollHereY(0.5f);
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
	template<> static bool* GetComponentEnabledFlag<SpriteAnimator>(SpriteAnimator& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<LineRenderer>(LineRenderer& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<Rigidbody2D>(Rigidbody2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<BoxCollider2D>(BoxCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<CircleCollider2D>(CircleCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<DistanceJoint2D>(DistanceJoint2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<AudioSource>(AudioSource& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<AudioListener>(AudioListener& component) { return &component.Enabled; }

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

	template<typename ModifiedFunction>
	static void DrawRegisteredComponent(const ComponentDescriptor& descriptor,
		Entity entity, ModifiedFunction onModified, bool editable)
	{
		if (!entity || !descriptor.InspectorVisible || !descriptor.Has(entity))
			return;

		ImGui::PushID(descriptor.StableName.c_str());
		ImGui::Separator();
		const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen
			| ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_FramePadding;
		const bool open = ImGui::TreeNodeEx("##RegisteredComponent", flags,
			"%s", descriptor.DisplayName.c_str());

		bool remove = false;
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 8.0f);
		ImGui::BeginDisabled(!editable);
		if (ImGui::SmallButton("..."))
			ImGui::OpenPopup("RegisteredComponentSettings");
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("RegisteredComponentSettings"))
		{
			ImGui::BeginDisabled(!editable);
			if (ImGui::MenuItem("Remove component"))
				remove = true;
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		if (open)
		{
			ImGui::BeginDisabled(!editable);
			for (const PropertyDescriptor& property : descriptor.Properties)
			{
				ImGui::PushID(property.StableName.c_str());
				PropertyValue value = property.Get(entity);
				bool changed = false;
				switch (property.Kind)
				{
					case PropertyKind::Bool:
					{
						bool item = std::get<bool>(value);
						changed = ImGui::Checkbox(property.DisplayName.c_str(), &item);
						value = item;
						break;
					}
					case PropertyKind::Int32:
					{
						int32_t item = std::get<int32_t>(value);
						changed = ImGui::DragInt(property.DisplayName.c_str(), &item, 1.0f);
						value = item;
						break;
					}
					case PropertyKind::Int64:
					{
						int64_t item = std::get<int64_t>(value);
						changed = ImGui::InputScalar(property.DisplayName.c_str(),
							ImGuiDataType_S64, &item);
						value = item;
						break;
					}
					case PropertyKind::UInt32:
					{
						uint32_t item = std::get<uint32_t>(value);
						changed = ImGui::InputScalar(property.DisplayName.c_str(),
							ImGuiDataType_U32, &item);
						value = item;
						break;
					}
					case PropertyKind::UInt64:
					{
						uint64_t item = std::get<uint64_t>(value);
						if (property.AssetReference)
						{
							AssetHandle handle(item);
							changed = DrawRegisteredAssetReference(property, handle);
							item = static_cast<uint64_t>(handle);
						}
						else
							changed = ImGui::InputScalar(property.DisplayName.c_str(),
								ImGuiDataType_U64, &item);
						value = item;
						break;
					}
					case PropertyKind::Float:
					{
						float item = std::get<float>(value);
						changed = ImGui::DragFloat(property.DisplayName.c_str(), &item, 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Double:
					{
						double item = std::get<double>(value);
						changed = ImGui::InputDouble(property.DisplayName.c_str(), &item);
						value = item;
						break;
					}
					case PropertyKind::String:
					{
						std::array<char, 1024> buffer{};
						const std::string& item = std::get<std::string>(value);
						const size_t count = std::min(item.size(), buffer.size() - 1);
						std::copy_n(item.data(), count, buffer.data());
						changed = ImGui::InputText(property.DisplayName.c_str(),
							buffer.data(), buffer.size());
						value = std::string(buffer.data());
						break;
					}
					case PropertyKind::Vector2:
					{
						auto item = std::get<glm::vec2>(value);
						changed = ImGui::DragFloat2(property.DisplayName.c_str(),
							glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Vector3:
					{
						auto item = std::get<glm::vec3>(value);
						changed = ImGui::DragFloat3(property.DisplayName.c_str(),
							glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Vector4:
					{
						auto item = std::get<glm::vec4>(value);
						changed = ImGui::DragFloat4(property.DisplayName.c_str(),
							glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
				}
				if (changed)
				{
					std::string error;
					if (property.Set(entity, value, error))
						onModified();
					else
						TC_Core_Warn("Could not set {0}.{1}: {2}", descriptor.StableName,
							property.StableName, error);
				}
				ImGui::PopID();
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		if (remove)
		{
			std::string error;
			if (descriptor.Remove(entity, error))
				onModified();
			else
				TC_Core_Warn("Could not remove {0}: {1}", descriptor.StableName, error);
		}
		ImGui::PopID();
	}

	void SceneHierarchyPanel::DrawSpriteAnimatorInspector(SpriteAnimator& animator,
		Entity entity)
	{
		using namespace SpriteAnimatorAuthoring;

		if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
			&& m_AnimatorRenameEntity != entity.GetUUID())
		{
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
		}

		std::function<void()> pendingMutation;
		bool requestRenamePopup = false;
		auto requestRename = [&](AnimatorRenameTarget target, size_t index,
			const std::string& currentName)
		{
			m_AnimatorRenameTarget = target;
			m_AnimatorRenameEntity = entity.GetUUID();
			m_AnimatorRenameIndex = index;
			strncpy_s(m_AnimatorRenameBuffer.data(), m_AnimatorRenameBuffer.size(),
				currentName.c_str(), _TRUNCATE);
			m_AnimatorRenameError.clear();
			requestRenamePopup = true;
		};

		if (ImGui::Checkbox("Play On Start", &animator.PlayOnStart))
			MarkModified();
		float animatorSpeed = animator.Speed;
		if (ImGui::DragFloat("Speed", &animatorSpeed, 0.05f, 0.0f, 100.0f,
			"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(animatorSpeed))
		{
			animatorSpeed = std::clamp(animatorSpeed, 0.0f, 100.0f);
			if (animatorSpeed != animator.Speed)
			{
				animator.Speed = animatorSpeed;
				MarkModified();
			}
		}

		const char* initialClip = animator.InitialClip.empty()
			? "First clip" : animator.InitialClip.c_str();
		if (ImGui::BeginCombo("Initial Clip", initialClip))
		{
			if (ImGui::Selectable("First clip", animator.InitialClip.empty()))
			{
				animator.InitialClip.clear();
				MarkModified(true);
			}
			for (const SpriteAnimationClip& clip : animator.Clips)
			{
				if (ImGui::Selectable(clip.Name.c_str(), animator.InitialClip == clip.Name))
				{
					animator.InitialClip = clip.Name;
					MarkModified(true);
				}
			}
			ImGui::EndCombo();
		}

		if (!animator.States.empty())
		{
			const char* initialState = animator.InitialState.empty()
				? "First state" : animator.InitialState.c_str();
			if (ImGui::BeginCombo("Initial State", initialState))
			{
				if (ImGui::Selectable("First state", animator.InitialState.empty()))
				{
					animator.InitialState.clear();
					MarkModified(true);
				}
				for (const AnimatorState& state : animator.States)
				{
					if (ImGui::Selectable(state.Name.c_str(),
						animator.InitialState == state.Name))
					{
						animator.InitialState = state.Name;
						MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
		}

		ImGui::TextDisabled("%zu clips, %zu parameters, %zu states, %zu transitions",
			animator.Clips.size(), animator.Parameters.size(), animator.States.size(),
			animator.Transitions.size());

		if (ImGui::TreeNodeEx("Clips", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (size_t clipIndex = 0; clipIndex < animator.Clips.size(); ++clipIndex)
			{
				SpriteAnimationClip& clip = animator.Clips[clipIndex];
				ImGui::PushID(static_cast<int>(clipIndex));
				const bool clipOpen = ImGui::TreeNodeEx("##Clip", 0, "%zu. %s",
					clipIndex + 1, clip.Name.c_str());
				if (clipOpen)
				{
					if (ImGui::Checkbox("Loop", &clip.Loop))
						MarkModified();
					if (ImGui::Button("Rename"))
						requestRename(AnimatorRenameTarget::Clip, clipIndex, clip.Name);
					ImGui::SameLine();
					ImGui::BeginDisabled(clipIndex == 0);
					if (ImGui::SmallButton("Up") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::swap(animator.Clips[clipIndex - 1],
								animator.Clips[clipIndex]);
							MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(clipIndex + 1 >= animator.Clips.size());
					if (ImGui::SmallButton("Down") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::swap(animator.Clips[clipIndex],
								animator.Clips[clipIndex + 1]);
							MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(animator.Clips.size() == 1);
					if (ImGui::SmallButton("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::string error;
							if (RemoveClip(animator, clipIndex, error))
								MarkModified(true);
							else if (!error.empty())
								TC_Core_Warn("Could not remove Animator clip: {0}", error);
						};
					}
					ImGui::EndDisabled();

					DrawAnimatorSectionLabel("Frames");
					for (size_t frameIndex = 0; frameIndex < clip.Frames.size(); ++frameIndex)
					{
						SpriteAnimationFrame& frame = clip.Frames[frameIndex];
						ImGui::PushID(static_cast<int>(frameIndex));
						ImGui::Text("Frame %zu", frameIndex + 1);
						ImGui::SameLine();
						ImGui::BeginDisabled(frameIndex == 0);
						if (ImGui::SmallButton("Up") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								std::swap(frames[frameIndex - 1], frames[frameIndex]);
								MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(frameIndex + 1 >= clip.Frames.size());
						if (ImGui::SmallButton("Down") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								std::swap(frames[frameIndex], frames[frameIndex + 1]);
								MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(clip.Frames.size() == 1);
						if (ImGui::SmallButton("Remove") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								frames.erase(frames.begin()
									+ static_cast<std::ptrdiff_t>(frameIndex));
								MarkModified(true);
							};
						}
						ImGui::EndDisabled();

						ImGui::TextDisabled("Sprite");
						if (DrawAnimatorSpriteField("Sprite", frame.SpriteHandle))
							MarkModified(true);
						float duration = frame.DurationSeconds;
						if (ImGui::DragFloat("Duration (s)", &duration, 0.005f,
							0.001f, 60.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)
							&& std::isfinite(duration))
						{
							duration = std::clamp(duration, 0.001f, 60.0f);
							if (duration != frame.DurationSeconds)
							{
								frame.DurationSeconds = duration;
								MarkModified();
							}
						}
						ImGui::Separator();
						ImGui::PopID();
					}
					if (ImGui::Button("Add Frame") && !pendingMutation)
					{
						AssetHandle sprite = AssetHandle(0);
						if (!clip.Frames.empty())
							sprite = clip.Frames.back().SpriteHandle;
						else if (entity.HasComponent<SpriteRenderer>())
							sprite = entity.GetComponent<SpriteRenderer>().SpriteHandle;
						pendingMutation = [this, &animator, clipIndex, sprite]()
						{
							animator.Clips[clipIndex].Frames.push_back(
								{ sprite, 1.0f / 12.0f });
							MarkModified(true);
						};
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (ImGui::Button("Add Clip") && !pendingMutation)
			{
				AssetHandle sprite = AssetHandle(0);
				if (entity.HasComponent<SpriteRenderer>())
					sprite = entity.GetComponent<SpriteRenderer>().SpriteHandle;
				pendingMutation = [this, &animator, sprite]()
				{
					SpriteAnimationClip clip;
					clip.Name = MakeUniqueName(animator.Clips, std::string("Clip"),
						[](const SpriteAnimationClip& item) { return item.Name; });
					clip.Frames.push_back({ sprite, 1.0f / 12.0f });
					animator.Clips.push_back(std::move(clip));
					MarkModified(true);
				};
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static constexpr const char* typeNames[] =
				{ "Bool", "Int", "Float", "Trigger" };
			for (size_t parameterIndex = 0;
				parameterIndex < animator.Parameters.size(); ++parameterIndex)
			{
				AnimatorParameter& parameter = animator.Parameters[parameterIndex];
				ImGui::PushID(static_cast<int>(parameterIndex));
				DrawAnimatorSectionLabel(parameter.Name.c_str());
				if (ImGui::SmallButton("Rename"))
					requestRename(AnimatorRenameTarget::Parameter, parameterIndex,
						parameter.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove") && !pendingMutation)
				{
					pendingMutation = [this, &animator, parameterIndex]()
					{
						std::string error;
						if (RemoveParameter(animator, parameterIndex, error))
							MarkModified(true);
						else if (!error.empty())
							TC_Core_Warn("Could not remove Animator parameter: {0}", error);
					};
				}
				int type = std::clamp(static_cast<int>(parameter.Type), 0, 3);
				if (ImGui::BeginCombo("Type", typeNames[type]))
				{
					for (int candidate = 0; candidate < 4; ++candidate)
					{
						if (ImGui::Selectable(typeNames[candidate], candidate == type)
							&& SetParameterType(animator, parameterIndex,
								static_cast<AnimatorParameterType>(candidate)))
							MarkModified(true);
					}
					ImGui::EndCombo();
				}
				switch (parameter.Type)
				{
					case AnimatorParameterType::Bool:
						if (ImGui::Checkbox("Default", &parameter.BoolValue))
							MarkModified();
						break;
					case AnimatorParameterType::Int:
						if (ImGui::InputInt("Default", &parameter.IntValue))
							MarkModified();
						break;
					case AnimatorParameterType::Float:
					{
						float value = parameter.FloatValue;
						if (ImGui::DragFloat("Default", &value, 0.05f)
							&& std::isfinite(value) && value != parameter.FloatValue)
						{
							parameter.FloatValue = value;
							MarkModified();
						}
						break;
					}
					case AnimatorParameterType::Trigger:
						ImGui::TextDisabled("Triggers always start reset.");
						break;
				}
				ImGui::PopID();
			}
			if (ImGui::Button("Add Parameter") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorParameter parameter;
					parameter.Name = MakeUniqueName(animator.Parameters,
						std::string("Parameter"),
						[](const AnimatorParameter& item) { return item.Name; });
					animator.Parameters.push_back(std::move(parameter));
					MarkModified(true);
				};
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
			{
				AnimatorState& state = animator.States[stateIndex];
				ImGui::PushID(static_cast<int>(stateIndex));
				DrawAnimatorSectionLabel(state.Name.c_str());
				if (ImGui::SmallButton("Rename"))
					requestRename(AnimatorRenameTarget::State, stateIndex, state.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove") && !pendingMutation)
				{
					pendingMutation = [this, &animator, stateIndex]()
					{
						std::string error;
						if (RemoveState(animator, stateIndex, error))
							MarkModified(true);
						else if (!error.empty())
							TC_Core_Warn("Could not remove Animator state: {0}", error);
					};
				}
				if (ImGui::BeginCombo("Clip", state.Clip.c_str()))
				{
					for (const SpriteAnimationClip& clip : animator.Clips)
					{
						if (ImGui::Selectable(clip.Name.c_str(), state.Clip == clip.Name))
						{
							state.Clip = clip.Name;
							MarkModified(true);
						}
					}
					ImGui::EndCombo();
				}
				float stateSpeed = state.Speed;
				if (ImGui::DragFloat("Speed", &stateSpeed, 0.05f, 0.01f, 100.0f,
					"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(stateSpeed))
				{
					stateSpeed = std::clamp(stateSpeed, 0.01f, 100.0f);
					if (stateSpeed != state.Speed)
					{
						state.Speed = stateSpeed;
						MarkModified();
					}
				}
				ImGui::PopID();
			}
			ImGui::BeginDisabled(animator.Clips.empty());
			if (ImGui::Button("Add State") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorState state;
					state.Name = MakeUniqueName(animator.States, std::string("State"),
						[](const AnimatorState& item) { return item.Name; });
					state.Clip = animator.Clips.front().Name;
					animator.States.push_back(std::move(state));
					MarkModified(true);
				};
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Evaluated top to bottom; first matching transition wins.");
			for (size_t transitionIndex = 0;
				transitionIndex < animator.Transitions.size(); ++transitionIndex)
			{
				AnimatorTransition& transition = animator.Transitions[transitionIndex];
				ImGui::PushID(static_cast<int>(transitionIndex));
				const char* source = transition.AnyState
					? "AnyState" : transition.FromState.c_str();
				const bool transitionOpen = ImGui::TreeNodeEx("##Transition", 0,
					"%zu. %s -> %s", transitionIndex + 1, source,
					transition.ToState.c_str());
				if (transitionOpen)
				{
					ImGui::BeginDisabled(transitionIndex == 0);
					if (ImGui::SmallButton("Up") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							std::swap(animator.Transitions[transitionIndex - 1],
								animator.Transitions[transitionIndex]);
							MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(transitionIndex + 1 >= animator.Transitions.size());
					if (ImGui::SmallButton("Down") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							std::swap(animator.Transitions[transitionIndex],
								animator.Transitions[transitionIndex + 1]);
							MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							animator.Transitions.erase(animator.Transitions.begin()
								+ static_cast<std::ptrdiff_t>(transitionIndex));
							MarkModified(true);
						};
					}

					bool anyState = transition.AnyState;
					if (ImGui::Checkbox("Any State", &anyState))
					{
						transition.AnyState = anyState;
						transition.FromState = anyState || animator.States.empty()
							? std::string{} : animator.States.front().Name;
						MarkModified(true);
					}
					if (!transition.AnyState)
					{
						if (ImGui::BeginCombo("From", transition.FromState.c_str()))
						{
							for (const AnimatorState& state : animator.States)
							{
								if (ImGui::Selectable(state.Name.c_str(),
									transition.FromState == state.Name))
								{
									transition.FromState = state.Name;
									MarkModified(true);
								}
							}
							ImGui::EndCombo();
						}
					}
					if (ImGui::BeginCombo("To", transition.ToState.c_str()))
					{
						for (const AnimatorState& state : animator.States)
						{
							if (ImGui::Selectable(state.Name.c_str(),
								transition.ToState == state.Name))
							{
								transition.ToState = state.Name;
								MarkModified(true);
							}
						}
						ImGui::EndCombo();
					}

					bool hasExitTime = transition.ExitTime >= 0.0f;
					ImGui::BeginDisabled(hasExitTime && transition.Conditions.empty());
					if (ImGui::Checkbox("Has Exit Time", &hasExitTime))
					{
						transition.ExitTime = hasExitTime ? 1.0f : -1.0f;
						MarkModified(true);
					}
					ImGui::EndDisabled();
					if (transition.ExitTime >= 0.0f)
					{
						float exitTime = transition.ExitTime;
						if (ImGui::SliderFloat("Exit Time", &exitTime, 0.0f, 1.0f,
							"%.3f", ImGuiSliderFlags_AlwaysClamp)
							&& std::isfinite(exitTime) && exitTime != transition.ExitTime)
						{
							transition.ExitTime = std::clamp(exitTime, 0.0f, 1.0f);
							MarkModified();
						}
					}

					DrawAnimatorSectionLabel("Conditions");
					for (size_t conditionIndex = 0;
						conditionIndex < transition.Conditions.size(); ++conditionIndex)
					{
						AnimatorCondition& condition = transition.Conditions[conditionIndex];
						ImGui::PushID(static_cast<int>(conditionIndex));
						const AnimatorParameter* selectedParameter = nullptr;
						for (const AnimatorParameter& parameter : animator.Parameters)
							if (parameter.Name == condition.Parameter)
								selectedParameter = &parameter;
						const char* parameterPreview = selectedParameter
							? selectedParameter->Name.c_str() : "<Missing>";
						if (ImGui::BeginCombo("Parameter", parameterPreview))
						{
							for (const AnimatorParameter& parameter : animator.Parameters)
							{
								if (ImGui::Selectable(parameter.Name.c_str(),
									condition.Parameter == parameter.Name))
								{
									condition.Parameter = parameter.Name;
									condition.Mode = DefaultConditionMode(parameter.Type);
									condition.Threshold = 0.0f;
									MarkModified(true);
								}
							}
							ImGui::EndCombo();
						}
						selectedParameter = nullptr;
						for (const AnimatorParameter& parameter : animator.Parameters)
							if (parameter.Name == condition.Parameter)
								selectedParameter = &parameter;
						if (selectedParameter)
						{
							if (IsBooleanParameter(selectedParameter->Type))
							{
								static constexpr AnimatorConditionMode modes[] =
									{ AnimatorConditionMode::If, AnimatorConditionMode::IfNot };
								static constexpr const char* names[] = { "If", "If Not" };
								const int current = condition.Mode == AnimatorConditionMode::IfNot
									? 1 : 0;
								if (ImGui::BeginCombo("Mode", names[current]))
								{
									for (int modeIndex = 0; modeIndex < 2; ++modeIndex)
										if (ImGui::Selectable(names[modeIndex], current == modeIndex))
										{
											condition.Mode = modes[modeIndex];
											MarkModified(true);
										}
									ImGui::EndCombo();
								}
							}
							else
							{
								static constexpr AnimatorConditionMode modes[] = {
									AnimatorConditionMode::Greater, AnimatorConditionMode::Less,
									AnimatorConditionMode::Equals, AnimatorConditionMode::NotEqual };
								static constexpr const char* names[] =
									{ "Greater", "Less", "Equals", "Not Equal" };
								int current = std::clamp(static_cast<int>(condition.Mode)
									- static_cast<int>(AnimatorConditionMode::Greater), 0, 3);
								if (ImGui::BeginCombo("Mode", names[current]))
								{
									for (int modeIndex = 0; modeIndex < 4; ++modeIndex)
										if (ImGui::Selectable(names[modeIndex], current == modeIndex))
										{
											condition.Mode = modes[modeIndex];
											MarkModified(true);
										}
									ImGui::EndCombo();
								}
								if (selectedParameter->Type == AnimatorParameterType::Int)
								{
									int threshold = static_cast<int>(condition.Threshold);
									if (ImGui::InputInt("Threshold", &threshold))
									{
										condition.Threshold = static_cast<float>(threshold);
										MarkModified();
									}
								}
								else
								{
									float threshold = condition.Threshold;
									if (ImGui::DragFloat("Threshold", &threshold, 0.05f)
										&& std::isfinite(threshold)
										&& threshold != condition.Threshold)
									{
										condition.Threshold = threshold;
										MarkModified();
									}
								}
							}
						}
						ImGui::BeginDisabled(transition.ExitTime < 0.0f
							&& transition.Conditions.size() == 1);
						if (ImGui::SmallButton("Remove Condition") && !pendingMutation)
						{
							pendingMutation = [this, &animator, transitionIndex,
								conditionIndex]()
							{
								auto& conditions = animator.Transitions[transitionIndex].Conditions;
								conditions.erase(conditions.begin()
									+ static_cast<std::ptrdiff_t>(conditionIndex));
								MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::Separator();
						ImGui::PopID();
					}
					ImGui::BeginDisabled(animator.Parameters.empty());
					if (ImGui::Button("Add Condition") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							const AnimatorParameter& parameter = animator.Parameters.front();
							animator.Transitions[transitionIndex].Conditions.push_back(
								{ parameter.Name, DefaultConditionMode(parameter.Type), 0.0f });
							MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::BeginDisabled(animator.States.empty());
			if (ImGui::Button("Add Transition") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorTransition transition;
					transition.FromState = animator.States.front().Name;
					transition.ToState = animator.States.front().Name;
					transition.ExitTime = 1.0f;
					animator.Transitions.push_back(std::move(transition));
					MarkModified(true);
				};
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		if (requestRenamePopup)
			ImGui::OpenPopup("Rename Animator Item");
		bool renamePopupOpen = true;
		if (ImGui::BeginPopupModal("Rename Animator Item", &renamePopupOpen,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			const bool submitted = ImGui::InputText("Name",
				m_AnimatorRenameBuffer.data(), m_AnimatorRenameBuffer.size(),
				ImGuiInputTextFlags_EnterReturnsTrue);

			const std::string candidate(m_AnimatorRenameBuffer.data());
			SpriteAnimator probe = animator;
			std::string validationError;
			bool validChange = false;
			switch (m_AnimatorRenameTarget)
			{
				case AnimatorRenameTarget::Clip:
					validChange = RenameClip(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::Parameter:
					validChange = RenameParameter(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::State:
					validChange = RenameState(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::None:
					validationError = "Nothing is selected for rename";
					break;
			}
			if (!validChange && validationError.empty())
				validationError = "Name is unchanged";
			m_AnimatorRenameError = validationError;
			if (!m_AnimatorRenameError.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "%s",
					m_AnimatorRenameError.c_str());

			ImGui::BeginDisabled(!validChange);
			const bool apply = ImGui::Button("Apply") || submitted;
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool cancel = ImGui::Button("Cancel");
			if (apply && validChange)
			{
				std::string error;
				bool renamed = false;
				switch (m_AnimatorRenameTarget)
				{
					case AnimatorRenameTarget::Clip:
						renamed = RenameClip(animator, m_AnimatorRenameIndex,
							candidate, error);
						break;
					case AnimatorRenameTarget::Parameter:
						renamed = RenameParameter(animator, m_AnimatorRenameIndex,
							candidate, error);
						break;
					case AnimatorRenameTarget::State:
						renamed = RenameState(animator, m_AnimatorRenameIndex,
							candidate, error);
						break;
					case AnimatorRenameTarget::None:
						break;
				}
				if (renamed)
					MarkModified(true);
				else if (!error.empty())
					TC_Core_Warn("Could not rename Animator item: {0}", error);
				m_AnimatorRenameTarget = AnimatorRenameTarget::None;
				m_AnimatorRenameEntity = UUID(0);
				m_AnimatorRenameError.clear();
				ImGui::CloseCurrentPopup();
			}
			else if (cancel || !renamePopupOpen)
			{
				m_AnimatorRenameTarget = AnimatorRenameTarget::None;
				m_AnimatorRenameEntity = UUID(0);
				m_AnimatorRenameError.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
			&& !requestRenamePopup && !ImGui::IsPopupOpen("Rename Animator Item"))
		{
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
		}

		if (pendingMutation)
			pendingMutation();
	}

	void SceneHierarchyPanel::DrawCSharpScripts(Entity entity)
	{
		if (!entity || !entity.HasComponent<CSharpScripts>())
			return;

		auto& scripts = entity.GetComponent<CSharpScripts>().Scripts;
		std::optional<size_t> removeIndex;
		for (size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex)
		{
			auto& entry = scripts[scriptIndex];
			const uint64_t attachmentID = static_cast<uint64_t>(entry.AttachmentID);
			ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(attachmentID)));

			const AssetMetadata* assetMetadata =
				AssetManager::Get().GetRegistry().GetMetadata(entry.ScriptAsset);
			const bool missing = static_cast<uint64_t>(entry.ScriptAsset) == 0 ||
				!assetMetadata || assetMetadata->IsMissing ||
				assetMetadata->Type != AssetType::CSharpScript;
			std::optional<EditorScriptMetadata> metadata;
			if (!missing && m_ScriptMetadataProvider)
				metadata = m_ScriptMetadataProvider(entry.ScriptAsset);

			if (metadata && !metadata->TypeName.empty() &&
				entry.LastKnownClassName != metadata->TypeName)
			{
				entry.LastKnownClassName = metadata->TypeName;
				MarkModified();
			}
			if (metadata && m_ColliderEditingAllowed &&
				ReconcileScriptEntryFields(entry, *metadata))
				MarkModified();
			std::string className = metadata && !metadata->TypeName.empty()
				? metadata->TypeName : entry.LastKnownClassName;
			if (className.empty() && assetMetadata)
				className = PathToUTF8(assetMetadata->FilePath.stem());
			if (className.empty())
				className = "Unknown Script";
			const std::string header = missing
				? "Missing Script: " + className
				: className + " (C# Script)";

			ImGui::Separator();
			if (missing)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.34f, 0.34f, 1.0f));
			const bool open = ImGui::TreeNodeEx("##CSharpScriptCard",
				ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
				ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding,
				"%s", header.c_str());
			if (missing)
				ImGui::PopStyleColor();

			if (ImGui::BeginPopupContextItem("ScriptSettings"))
			{
				ImGui::BeginDisabled(!m_ColliderEditingAllowed);
				if (ImGui::MenuItem("Remove script"))
					removeIndex = scriptIndex;
				ImGui::EndDisabled();
				ImGui::EndPopup();
			}

			if (open)
			{
				ImGui::BeginDisabled(!m_ColliderEditingAllowed);
				if (ImGui::Checkbox("Enabled", &entry.Enabled))
					MarkModified();
				if (assetMetadata)
					ImGui::TextDisabled("%s", PathToUTF8(assetMetadata->FilePath).c_str());
				if (missing)
					ImGui::TextWrapped("The script asset is missing or no longer resolves to a C# script. Stored values are preserved.");
				else if (!metadata)
					ImGui::TextDisabled("Managed metadata is unavailable; stored fields are shown as orphans.");

				std::vector<bool> consumed(entry.Fields.size(), false);
				if (metadata)
				{
					std::string lastHeader;
					for (const EditorScriptFieldMetadata& fieldMetadata : metadata->Fields)
					{
						size_t matchedIndex = entry.Fields.size();
						for (size_t fieldIndex = 0; fieldIndex < entry.Fields.size(); ++fieldIndex)
						{
							if (fieldIndex < consumed.size() && consumed[fieldIndex])
								continue;
							const ScriptField& stored = entry.Fields[fieldIndex];
							const bool idMatches = !fieldMetadata.FieldID.empty() &&
								stored.FieldID == fieldMetadata.FieldID;
							const bool nameMatches = stored.Name == fieldMetadata.Name ||
								std::find(fieldMetadata.FormerNames.begin(),
									fieldMetadata.FormerNames.end(), stored.Name) !=
									fieldMetadata.FormerNames.end();
							if ((idMatches || nameMatches) &&
								IsScriptFieldValueCompatible(fieldMetadata.Type, stored.Value))
							{
								matchedIndex = fieldIndex;
								break;
							}
						}

						if (matchedIndex != entry.Fields.size())
							consumed[matchedIndex] = true;
						if (fieldMetadata.Hidden)
							continue;
						if (!fieldMetadata.Header.empty() && fieldMetadata.Header != lastHeader)
						{
							ImGui::Spacing();
							ImGui::Separator();
							ImGui::TextDisabled("%s", fieldMetadata.Header.c_str());
							lastHeader = fieldMetadata.Header;
						}

						if (matchedIndex != entry.Fields.size())
						{
							if (DrawScriptFieldValue(entry.Fields[matchedIndex],
								&fieldMetadata, false))
								MarkModified();
						}
						else
						{
							ScriptField pending(fieldMetadata.FieldID, fieldMetadata.Name,
								fieldMetadata.Type,
								ScriptMetadataDefaultValue(fieldMetadata));
							if (DrawScriptFieldValue(pending, &fieldMetadata, false))
							{
								entry.Fields.push_back(std::move(pending));
								consumed.push_back(true);
								MarkModified();
							}
						}
					}
				}

				bool drewOrphanHeader = false;
				for (size_t fieldIndex = 0; fieldIndex < entry.Fields.size(); ++fieldIndex)
				{
					if (fieldIndex < consumed.size() && consumed[fieldIndex])
						continue;
					if (!drewOrphanHeader)
					{
						ImGui::Spacing();
						ImGui::Separator();
						ImGui::TextDisabled("Orphaned serialized fields");
						drewOrphanHeader = true;
					}
					if (DrawScriptFieldValue(entry.Fields[fieldIndex], nullptr, true))
						MarkModified();
				}
				if ((!metadata || metadata->Fields.empty()) && entry.Fields.empty())
					ImGui::TextDisabled("No serialized fields.");

				ImGui::Spacing();
				if (ImGui::Button("Remove Script"))
					removeIndex = scriptIndex;
				ImGui::EndDisabled();
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		if (removeIndex && *removeIndex < scripts.size())
		{
			scripts.erase(scripts.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
			MarkModified();
			if (scripts.empty())
				entity.RemoveComponent<CSharpScripts>();
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
		// Rich editor adapters stay in the Editor, while descriptor enumeration is
		// the sole dispatch source. Third-party descriptors need no adapter: their
		// property metadata is rendered by DrawRegisteredComponent below.
		std::unordered_map<uint64_t, std::function<void()>> richInspectors;
		richInspectors.emplace(ComponentIds::Transform, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::Camera, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::SpriteRenderer, [&]()
		{
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
				AssetRegistry& registry = AssetManager::Get().GetRegistry();
				const AssetMetadata* metadata = registry.GetMetadata(component.SpriteHandle);
				const AssetSubAsset* subSprite = nullptr;
				if (!metadata)
					metadata = registry.GetSubAssetOwner(component.SpriteHandle, &subSprite);
				if (metadata && IsSpriteAsset(*metadata))
				{
					spriteName = subSprite ? subSprite->Name
						: PathToUTF8(metadata->FilePath.stem());
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
						AssetRegistry& registry = AssetManager::Get().GetRegistry();
						const AssetMetadata* metadata = registry.GetMetadata(handle);
						const AssetSubAsset* subSprite = nullptr;
						if (!metadata)
							metadata = registry.GetSubAssetOwner(handle, &subSprite);
						if (metadata && IsSpriteAsset(*metadata) && !metadata->IsMissing
							&& (!subSprite || subSprite->Type == AssetType::Texture2D))
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
					if (IsSpriteAsset(metadata) && !metadata.IsMissing)
						sprites.push_back(&metadata);
				}
				std::sort(sprites.begin(), sprites.end(), [](const AssetMetadata* left,
					const AssetMetadata* right)
				{
					return LowerASCII(PathToUTF8(left->FilePath)) <
						LowerASCII(PathToUTF8(right->FilePath));
				});

				size_t spriteCount = 0;
				const std::string spriteQuery = LowerASCII(m_SpriteSearch.data());
				for (const AssetMetadata* metadata : sprites)
				{
					if (MatchesSpriteSearch(*metadata, m_SpriteSearch.data()))
						++spriteCount;
					for (const AssetSubAsset& child : metadata->SubAssets)
						if (child.Type == AssetType::Texture2D
							&& (spriteQuery.empty() || LowerASCII(child.Name).find(spriteQuery)
								!= std::string::npos))
							++spriteCount;
				}
				ImGui::TextDisabled("%zu Sprite%s", spriteCount, spriteCount == 1 ? "" : "s");
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
						{
							ImVec2 uvMin(0.0f, 1.0f);
							ImVec2 uvMax(1.0f, 0.0f);
							ResolvedSpriteAsset resolved;
							SpriteRenderGeometry geometry;
							if (AssetManager::Get().ResolveSpriteAsset(handle, resolved)
								&& resolved.IsSubAsset
								&& BuildSpriteRenderGeometry(resolved.Data,
									texture->GetWidth(), texture->GetHeight(), geometry))
							{
								uvMin = ImVec2(geometry.UMin, geometry.VMax);
								uvMax = ImVec2(geometry.UMax, geometry.VMin);
							}
							drawList->AddImage(ToImGuiTextureID(texture), previewMin,
								previewMax, uvMin, uvMax);
						}
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
					if (MatchesSpriteSearch(*metadata, m_SpriteSearch.data()))
					{
						const std::string label = PathToUTF8(metadata->FilePath.stem());
						drawSpriteTile(metadata->Handle, label.c_str(), &metadata->FilePath);
					}
					for (const AssetSubAsset& child : metadata->SubAssets)
					{
						if (child.Type != AssetType::Texture2D
							|| (!spriteQuery.empty() && LowerASCII(child.Name).find(spriteQuery)
								== std::string::npos))
							continue;
						drawSpriteTile(child.Handle, child.Name.c_str(), &metadata->FilePath);
					}
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
		});

		richInspectors.emplace(ComponentIds::SpriteAnimator, [&]()
		{
		DrawComponent<SpriteAnimator>("Sprite Animator", entity, m_Icons,
			EditorIcon::Sprite, [this, entity](auto& component)
		{
			DrawSpriteAnimatorInspector(component, entity);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::LineRenderer, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::AudioSource, [&]()
		{
		DrawComponent<AudioSource>("Audio Source", entity, m_Icons, EditorIcon::Count,
			[this](auto& component)
		{
			uint64_t clip = static_cast<uint64_t>(component.Clip);
			if (ImGui::InputScalar("Audio Clip", ImGuiDataType_U64, &clip))
			{
				const AssetMetadata* metadata = clip == 0 ? nullptr
					: AssetManager::Get().GetRegistry().GetMetadata(AssetHandle(clip));
				if (clip == 0 || (metadata && !metadata->IsMissing
					&& metadata->Type == AssetType::Audio))
				{
					component.Clip = AssetHandle(clip);
					MarkModified();
				}
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
							.GetMetadata(handle);
						if (metadata && !metadata->IsMissing
							&& metadata->Type == AssetType::Audio)
						{
							component.Clip = handle;
							MarkModified();
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::Checkbox("Play On Start", &component.PlayOnStart)) MarkModified();
			if (ImGui::Checkbox("Loop", &component.Loop)) MarkModified();
			if (ImGui::Checkbox("Streaming", &component.Streaming)) MarkModified();
			float volume = component.Volume;
			if (ImGui::DragFloat("Volume", &volume, 0.01f, 0.0f, 4.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(volume))
			{
				component.Volume = std::clamp(volume, 0.0f, 4.0f);
				MarkModified();
			}
			float pitch = component.Pitch;
			if (ImGui::DragFloat("Pitch", &pitch, 0.01f, 0.25f, 4.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(pitch))
			{
				component.Pitch = std::clamp(pitch, 0.25f, 4.0f);
				MarkModified();
			}
			const char* groups[] = { "Master", "Music", "SFX" };
			int group = component.MixerGroup;
			if (ImGui::Combo("Mixer Group", &group, groups, 3))
			{
				component.MixerGroup = static_cast<uint8_t>(group);
				MarkModified();
			}
			float blend = component.SpatialBlend;
			if (ImGui::SliderFloat("Spatial Blend", &blend, 0.0f, 1.0f, "%.2f")
				&& std::isfinite(blend))
			{
				component.SpatialBlend = std::clamp(blend, 0.0f, 1.0f);
				MarkModified();
			}
			float minimum = component.MinDistance;
			if (ImGui::DragFloat("Min Distance", &minimum, 0.05f, 0.0f,
				component.MaxDistance - 0.001f, "%.2f",
				ImGuiSliderFlags_AlwaysClamp) && std::isfinite(minimum))
			{
				component.MinDistance = std::max(0.0f,
					std::min(minimum, component.MaxDistance - 0.001f));
				MarkModified();
			}
			float maximum = component.MaxDistance;
			if (ImGui::DragFloat("Max Distance", &maximum, 0.05f,
				component.MinDistance + 0.001f, 100000.0f, "%.2f",
				ImGuiSliderFlags_AlwaysClamp) && std::isfinite(maximum))
			{
				component.MaxDistance = std::max(maximum,
					component.MinDistance + 0.001f);
				MarkModified();
			}
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::AudioListener, [&]()
		{
		DrawComponent<AudioListener>("Audio Listener", entity, m_Icons,
			EditorIcon::Count, [this](auto& component)
		{
			if (ImGui::Checkbox("Primary", &component.Primary))
				MarkModified();
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Rigidbody2D, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::BoxCollider2D, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::CircleCollider2D, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::DistanceJoint2D, [&]()
		{
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
		});

		richInspectors.emplace(ComponentIds::CSharpScripts, [&]()
		{
			DrawCSharpScripts(entity);
		});
		for (const ComponentDescriptor& descriptor :
			ComponentRegistry::Get().GetDescriptors())
		{
			if (!descriptor.InspectorVisible || !descriptor.Has(entity))
				continue;
			const auto rich = richInspectors.find(
				static_cast<uint64_t>(descriptor.TypeId));
			if (rich != richInspectors.end())
				rich->second();
			else if (descriptor.UseGenericInspector)
				DrawRegisteredComponent(descriptor, entity, onModified,
					m_ColliderEditingAllowed);
		}
		if (entity.HasComponent<OpaqueComponents>())
		{
			for (const OpaqueComponentRecord& missing :
				entity.GetComponent<OpaqueComponents>().Records)
			{
				ImGui::PushID(static_cast<int>(static_cast<uint64_t>(missing.TypeId)));
				ImGui::Separator();
				if (ImGui::TreeNodeEx("##MissingComponent",
					ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth,
					"Missing Component: %s", missing.StableName.c_str()))
				{
					ImGui::Text("Type UUID: %llu",
						static_cast<unsigned long long>(missing.TypeId));
					ImGui::Text("Schema: %u", missing.SchemaVersion);
					ImGui::TextDisabled("Payload is preserved until the component provider is available.");
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
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
			ImGui::MenuItem("C# Script (drag asset into Inspector)", nullptr, false, false);
			ImGui::Separator();
			for (const ComponentDescriptor& descriptor :
				ComponentRegistry::Get().GetDescriptors())
			{
				if (!descriptor.InspectorVisible || !descriptor.AddableInInspector
					|| descriptor.Has(entity))
					continue;
				if (ImGui::MenuItem(descriptor.DisplayName.c_str()))
				{
					std::string error;
					if (descriptor.Add(entity, error))
						MarkModified();
					else
						TC_Core_Warn("Could not add {0}: {1}", descriptor.StableName, error);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

	}

}
