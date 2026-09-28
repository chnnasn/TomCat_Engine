#pragma once

// Internal helpers shared by the SceneHierarchyPanel family of panels.
// Extracted verbatim from SceneHierarchyPanel.cpp when the panel was split
// into SceneHierarchyTreePanel / SceneInspectorPanel /
// SceneAuthoringEditorsPanel. Private to Editor/TomCatInut/src/panels.

#include "SceneHierarchyPanel.h"

#include <imgui/imgui.h>
#include <yaml-cpp/yaml.h>
#include <set>
#include <imgui/imgui_internal.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <functional>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/Advanced2DAuthoringAssets.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "../EditorDragDrop.h"
#include "../EditorPropertyTransaction.h"
#include "../EditorVisuals.h"

#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Asset/Asset.h"
#include "../EditorIcons.h"
#include "../Scripting/ScriptEditorMetadata.h"

namespace TomCat {
namespace HierarchyDetail {


	enum class HierarchyDropZone
	{
		Before,
		Child,
		After
	};

	inline Entity GetDraggedSceneEntity(const ImGuiPayload* payload, const Ref<Scene>& scene)
	{
		if (!payload || !scene || payload->DataSize != sizeof(uint64_t))
			return {};

		const uint64_t rawUUID = *static_cast<const uint64_t*>(payload->Data);
		if (rawUUID == 0)
			return {};
		return scene->FindEntityByUUID(UUID(rawUUID));
	}

	inline HierarchyDropZone GetHierarchyDropZone(const ImVec2& itemMin, const ImVec2& itemMax)
	{
		const float height = std::max(1.0f, itemMax.y - itemMin.y);
		const float relativeY = ImGui::GetMousePos().y - itemMin.y;
		if (relativeY < height / 3.0f)
			return HierarchyDropZone::Before;
		if (relativeY > height * 2.0f / 3.0f)
			return HierarchyDropZone::After;
		return HierarchyDropZone::Child;
	}

	inline void DrawHierarchyDropPreview(const ImVec2& itemMin, const ImVec2& itemMax,
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
	inline void DrawProperty(const std::string& label, float columnWidth = 100.0f);
	inline bool DrawColorField(const char* label, float* color,
		ImGuiColorEditFlags extraFlags = ImGuiColorEditFlags_None)
	{
		// Keep the first inspector level compact. Clicking the swatch opens the
		// full hue-wheel picker with RGB/HSV/hex and alpha controls in its popup.
		return ImGui::ColorEdit4(label, color, extraFlags
			| ImGuiColorEditFlags_NoInputs
			| ImGuiColorEditFlags_AlphaBar
			| ImGuiColorEditFlags_AlphaPreviewHalf
			| ImGuiColorEditFlags_PickerHueWheel);
	}

	inline constexpr size_t MaximumInspectorTextBytes = 65536;

	inline bool DrawBoundedMultilineText(const char* label, std::string& value,
		const ImVec2& size)
	{
		std::vector<char> buffer(MaximumInspectorTextBytes + 1, '\0');
		const size_t count = std::min(value.size(), MaximumInspectorTextBytes);
		std::copy_n(value.data(), count, buffer.data());
		if (!ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), size))
			return false;

		std::string edited(buffer.data());
		bool validUTF8 = false;
		(void)FontAtlasBuilder::DecodeUTF8(edited, &validUTF8);
		if (!validUTF8)
			return false;
		value = std::move(edited);
		return true;
	}
	inline ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
	{
		return texture
			? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
			: nullptr;
	}

	inline void DrawIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& minimum, const ImVec2& maximum, ImU32 tint = IM_COL32_WHITE)
	{
        DrawEditorGlyph(ImGui::GetWindowDrawList(),icons,icon,minimum,maximum,tint);
	}

	inline std::string LowerASCII(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	inline const char* GetAddComponentCategory(const ComponentDescriptor& descriptor)
	{
		switch (static_cast<uint64_t>(descriptor.TypeId))
		{
		case ComponentIds::Camera:
		case ComponentIds::SpriteRenderer:
		case ComponentIds::LineRenderer:
		case ComponentIds::ParticleSystem2D:
		case ComponentIds::Light2D:
		case ComponentIds::TextRenderer:
			return "Rendering";
		case ComponentIds::SpriteAnimator:
			return "Animation";
		case ComponentIds::Grid2D:
		case ComponentIds::Tilemap2D:
		case ComponentIds::TilemapRenderer2D:
			return "Tilemap";
		case ComponentIds::Rigidbody2D:
		case ComponentIds::BoxCollider2D:
		case ComponentIds::CircleCollider2D:
		case ComponentIds::DistanceJoint2D:
			return "Physics 2D";
		case ComponentIds::AudioSource:
		case ComponentIds::AudioListener:
			return "Audio";
		case ComponentIds::Canvas:
		case ComponentIds::RectTransform:
		case ComponentIds::UIImage:
		case ComponentIds::UIText:
		case ComponentIds::UIButton:
		case ComponentIds::UIEventSystem:
		case ComponentIds::UILayoutGroup:
		case ComponentIds::UISlider:
		case ComponentIds::UIScrollView:
		case ComponentIds::UIInputField:
		case ComponentIds::UITheme:
		case ComponentIds::UILocalization:
		case ComponentIds::UILocalizedText:
			return "UI";
		case ComponentIds::CSharpScripts:
			return "Scripting";
		default:
			return "Gameplay";
		}
	}

	inline bool MatchesAddComponentSearch(const ComponentDescriptor& descriptor,
		std::string_view lowercaseQuery)
	{
		if (lowercaseQuery.empty())
			return true;
		std::string searchable = descriptor.DisplayName + " "
			+ descriptor.StableName + " " + GetAddComponentCategory(descriptor);
		searchable = LowerASCII(std::move(searchable));
		return searchable.find(lowercaseQuery) != std::string::npos;
	}

	template<typename T>
	inline T ScriptRangeEndpoint(double value)
	{
		if (!std::isfinite(value))
			return T{};
		if (value <= static_cast<double>(std::numeric_limits<T>::lowest()))
			return std::numeric_limits<T>::lowest();
		if (value >= static_cast<double>(std::numeric_limits<T>::max()))
			return std::numeric_limits<T>::max();
		return static_cast<T>(value);
	}

	inline void DrawScriptFieldTooltip(const EditorScriptFieldMetadata* metadata)
	{
		if (metadata && !metadata->Tooltip.empty() &&
			ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", metadata->Tooltip.c_str());
	}

	inline bool DrawScriptFieldValue(ScriptField& field,
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
				changed = DrawColorField("##Value",
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

	inline bool MatchesSpriteSearch(const AssetMetadata& metadata, const char* search)
	{
		if (!search || search[0] == '\0')
			return true;
		const std::string query = LowerASCII(search);
		return LowerASCII(PathToUTF8(metadata.FilePath)).find(query) != std::string::npos;
	}

	inline bool ResolveSelectableSprite(AssetHandle handle,
		const AssetMetadata*& metadata, const AssetSubAsset*& subAsset)
	{
		metadata = nullptr;
		subAsset = nullptr;
		if (static_cast<uint64_t>(handle) == 0)
			return true;
		if (FindBuiltInSpriteAsset(handle))
			return true;
		AssetRegistry& registry = AssetManager::Get().GetRegistry();
		metadata = registry.GetMetadata(handle);
		if (!metadata)
			metadata = registry.GetSubAssetOwner(handle, &subAsset);
		return metadata && !metadata->IsMissing && IsSpriteAsset(*metadata)
			&& (!subAsset || subAsset->Type == AssetType::Texture2D);
	}

	inline std::string AnimatorSpriteLabel(AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return "None";
		if (const BuiltInFontAsset* builtIn = FindBuiltInFontAsset(handle))
			return std::string(builtIn->Name);
		if (const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(handle))
			return std::string(builtIn->Name);
		const AssetMetadata* metadata = nullptr;
		const AssetSubAsset* subAsset = nullptr;
		if (!ResolveSelectableSprite(handle, metadata, subAsset))
			return "Missing #" + std::to_string(static_cast<uint64_t>(handle));
		std::string label = PathToUTF8(metadata->FilePath.filename());
		if (subAsset)
			label += " / " + subAsset->Name;
		return label;
	}

	inline bool DrawAnimatorSpriteField(const char* id, AssetHandle& handle)
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

		PrepareEditorPopup("Select Sprite",440);
        if (ImGui::BeginPopup("Select Sprite"))
		{
			if (ImGui::Selectable("None", static_cast<uint64_t>(handle) == 0))
			{
				handle = AssetHandle(0);
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
			{
				const std::string item = std::string(builtIn.Name) + "###Sprite_"
					+ std::to_string(static_cast<uint64_t>(builtIn.Handle));
				if (ImGui::Selectable(item.c_str(), handle == builtIn.Handle))
				{
					handle = builtIn.Handle;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
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

	inline bool ReadAuthoringAssetDocument(const std::filesystem::path& path,
		std::string& document, std::string& error)
	{
		document.clear();
		error.clear();
		std::error_code fileError;
		const uintmax_t size = std::filesystem::file_size(path, fileError);
		constexpr uintmax_t maximumBytes = 16u * 1024u * 1024u;
		if (fileError || size == 0 || size > maximumBytes)
		{
			error = fileError ? fileError.message()
				: "authoring asset is empty or exceeds 16 MiB";
			return false;
		}
		std::ifstream input(path, std::ios::binary);
		if (!input)
		{
			error = "could not open the source file";
			return false;
		}
		document.resize(static_cast<size_t>(size));
		if (!input.read(document.data(), static_cast<std::streamsize>(size)))
		{
			document.clear();
			error = "could not read the complete source file";
			return false;
		}
		return true;
	}

	inline bool DrawTypedAuthoringAssetField(const char* id, AssetType expectedType,
		AssetHandle& handle)
	{
		bool changed = false;
		const AssetMetadata* current = static_cast<uint64_t>(handle) == 0 ? nullptr
			: AssetManager::Get().GetRegistry().GetMetadata(handle);
		std::string label = current && !current->IsMissing
			? PathToUTF8(current->FilePath.filename()) : "None";
		if (static_cast<uint64_t>(handle) != 0 && !current)
			label = "Missing #" + std::to_string(static_cast<uint64_t>(handle));
		label += "###";
		label += id;
		if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("Select Authoring Asset");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				AssetDragDropPayloadID, ImGuiDragDropFlags_AcceptBeforeDelivery))
			{
				if (payload->DataSize == sizeof(uint64_t))
				{
					const AssetHandle candidate(
						*static_cast<const uint64_t*>(payload->Data));
					const AssetMetadata* metadata =
						AssetManager::Get().GetRegistry().GetMetadata(candidate);
					if (metadata && !metadata->IsMissing && metadata->Type == expectedType
						&& payload->IsDelivery() && candidate != handle)
					{
						handle = candidate;
						changed = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("Authoring Asset Context"))
		{
			if (ImGui::MenuItem("Clear", nullptr, false,
				static_cast<uint64_t>(handle) != 0))
			{
				handle = AssetHandle(0);
				changed = true;
			}
			ImGui::EndPopup();
		}
		PrepareEditorPopup("Select Authoring Asset",480);
        if (ImGui::BeginPopup("Select Authoring Asset"))
		{
			if (ImGui::Selectable("None", static_cast<uint64_t>(handle) == 0))
			{
				handle = AssetHandle(0);
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			std::vector<const AssetMetadata*> candidates;
			for (const auto& [assetHandle, metadata] :
				AssetManager::Get().GetRegistry().GetAssets())
			{
				(void)assetHandle;
				if (!metadata.IsMissing && metadata.Type == expectedType)
					candidates.push_back(&metadata);
			}
			std::sort(candidates.begin(), candidates.end(),
				[](const AssetMetadata* left, const AssetMetadata* right)
				{
					return LowerASCII(PathToUTF8(left->FilePath))
						< LowerASCII(PathToUTF8(right->FilePath));
				});
			for (const AssetMetadata* metadata : candidates)
			{
				const std::string item = PathToUTF8(metadata->FilePath.filename())
					+ "###Asset_" + std::to_string(
						static_cast<uint64_t>(metadata->Handle));
				if (ImGui::Selectable(item.c_str(), handle == metadata->Handle))
				{
					handle = metadata->Handle;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
		return changed;
	}

	inline bool DrawSpritePaletteTile(AssetHandle handle, const char* label,
		bool selected, float extent = 72.0f)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(extent, extent + ImGui::GetTextLineHeight() + 8.0f);
		const std::string id = std::string("##PaletteSprite_")
			+ std::to_string(static_cast<uint64_t>(handle));
		ImGui::InvisibleButton(id.c_str(), size);
		const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 maximum(origin.x + size.x, origin.y + size.y);
		draw->AddRectFilled(origin, maximum, ImGui::GetColorU32(selected
			? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg),
			3.0f);
		const ImVec2 imageMin(origin.x + 5.0f, origin.y + 5.0f);
		const ImVec2 imageMax(origin.x + extent - 5.0f, origin.y + extent - 5.0f);
		if (static_cast<uint64_t>(handle) != 0)
		{
			ResolvedSpriteAsset resolved;
			const bool resolvedSprite = AssetManager::Get().ResolveSpriteAsset(handle,
				resolved);
			const AssetHandle textureHandle = resolvedSprite
				? resolved.TextureHandle : handle;
			const Ref<Texture2D> texture = AssetManager::Get().LoadTexture(textureHandle);
			if (texture)
			{
				ImVec2 uvMin(0.0f, 1.0f);
				ImVec2 uvMax(1.0f, 0.0f);
				SpriteRenderGeometry geometry;
				if (resolvedSprite && resolved.IsSubAsset
					&& BuildSpriteRenderGeometry(resolved.Data, texture->GetWidth(),
						texture->GetHeight(), geometry))
				{
					uvMin = ImVec2(geometry.UMin, geometry.VMax);
					uvMax = ImVec2(geometry.UMax, geometry.VMin);
				}
				draw->AddImage(ToImGuiTextureID(texture), imageMin, imageMax, uvMin, uvMax);
			}
		}
		const ImRect labelRect(ImVec2(origin.x + 3.0f, origin.y + extent),
			ImVec2(maximum.x - 3.0f, maximum.y - 2.0f));
		ImGui::RenderTextClipped(labelRect.Min, labelRect.Max, label, nullptr,
			nullptr, ImVec2(0.5f, 0.5f), &labelRect);
		if (hovered)
			ImGui::SetTooltip("%s", label);
		return clicked;
	}

	inline void DrawAnimatorSectionLabel(const char* label)
	{
		ImGui::Spacing();
		ImGui::TextDisabled("%s", label);
		ImGui::Separator();
	}

	inline bool ResolveRegisteredAssetReference(
		const AssetPropertyMetadata& semantics, AssetHandle handle,
		const AssetMetadata*& metadata, const AssetSubAsset*& subAsset)
	{
		metadata = nullptr;
		subAsset = nullptr;
		if (static_cast<uint64_t>(handle) == 0)
			return true;
		if (FindBuiltInFontAsset(handle))
			return semantics.Accepts(AssetType::Font, false);
		AssetRegistry& registry = AssetManager::Get().GetRegistry();
		metadata = registry.GetMetadata(handle);
		if (!metadata)
			metadata = registry.GetSubAssetOwner(handle, &subAsset);
		const AssetType type = subAsset ? subAsset->Type
			: metadata ? metadata->Type : AssetType::None;
		return metadata && !metadata->IsMissing
			&& semantics.Accepts(type, subAsset != nullptr);
	}

	inline std::string RegisteredAssetReferenceLabel(
		const AssetPropertyMetadata& semantics, AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return "None";
		if (const BuiltInFontAsset* builtIn = FindBuiltInFontAsset(handle))
		{
			std::string label(builtIn->Name);
			if (!semantics.Accepts(AssetType::Font, false))
				label += " (Incompatible)";
			else
				label += " (Built-in)";
			return label;
		}
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

	inline bool DrawRegisteredAssetReference(const PropertyDescriptor& property,
		AssetHandle& handle,
		const std::function<void(AssetHandle)>* revealAsset = nullptr)
	{
		const AssetPropertyMetadata& semantics = *property.AssetReference;
		ImGui::TextUnformatted(property.DisplayName.c_str());
		if (semantics.AllowSubAssets && semantics.AcceptedTypes.size() == 1
			&& semantics.AcceptedTypes.front() == AssetType::Texture2D)
			return DrawAnimatorSpriteField("RegisteredSpriteReference", handle);

		bool changed = false;
		const bool legacyDefaultFont = static_cast<uint64_t>(handle) == 0
			&& property.StableName == "Font"
			&& semantics.Accepts(AssetType::Font, false);
		const std::string label = (legacyDefaultFont
			? std::string("Legacy Runtime (Built-in default)")
			: RegisteredAssetReferenceLabel(semantics, handle))
			+ "###RegisteredAssetReference";
		const float pickerWidth = ImGui::GetFrameHeight();
		const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x
			- pickerWidth - ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::Button(label.c_str(), ImVec2(fieldWidth, 0.0f));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left)
			&& static_cast<uint64_t>(handle) != 0 && revealAsset && *revealAsset)
			(*revealAsset)(handle);
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
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		if (ImGui::Button("o##RegisteredAssetPicker", ImVec2(pickerWidth, 0.0f)))
			ImGui::OpenPopup("RegisteredAssetPicker");
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Select %s", property.DisplayName.c_str());
		PrepareEditorPopup("RegisteredAssetPicker",480);
        if (ImGui::BeginPopup("RegisteredAssetPicker"))
		{
			if (ImGui::Selectable("None", static_cast<uint64_t>(handle) == 0))
			{
				handle = AssetHandle(0);
				changed = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::Separator();
			bool foundCompatibleAsset = false;
			if (semantics.Accepts(AssetType::Font, false))
			{
				for (const BuiltInFontAsset& builtIn : GetBuiltInFontAssets())
				{
					foundCompatibleAsset = true;
					const std::string label = std::string(builtIn.Name) + " (Built-in)";
					if (ImGui::Selectable(label.c_str(), builtIn.Handle == handle))
					{
						handle = builtIn.Handle;
						changed = true;
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::Separator();
			}
			for (const auto& [candidate, metadata] :
				AssetManager::Get().GetRegistry().GetAssets())
			{
				if (metadata.IsMissing
					|| !semantics.Accepts(metadata.Type, false))
					continue;
				foundCompatibleAsset = true;
				const std::string path = PathToUTF8(metadata.FilePath);
				ImGui::PushID(path.c_str());
				if (ImGui::Selectable(path.c_str(), candidate == handle))
				{
					handle = candidate;
					changed = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			if (!foundCompatibleAsset)
				ImGui::TextDisabled("No compatible assets");
			ImGui::EndPopup();
		}
		return changed;
	}

	inline const PropertyDescriptor* FindRegisteredProperty(uint64_t componentType,
		std::string_view stableName)
	{
		const ComponentDescriptor* descriptor = ComponentRegistry::Get().Find(
			UUID(componentType));
		if (!descriptor)
			return nullptr;
		const auto found = std::find_if(descriptor->Properties.begin(),
			descriptor->Properties.end(), [stableName](const PropertyDescriptor& property)
			{
				return property.StableName == stableName;
			});
		return found == descriptor->Properties.end() ? nullptr : &*found;
	}

	inline float DrawTreeRowIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& itemMin, const ImVec2& itemMax, ImU32 tint = IM_COL32_WHITE)
	{
		const float iconSize = std::min(std::round(ImGui::GetFontSize() * 0.95f),
			std::max(1.0f, itemMax.y - itemMin.y - 4.0f));
		const float x = std::round(itemMin.x + ImGui::GetTreeNodeToLabelSpacing());
		const float y = std::round(itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
		DrawIcon(icons, icon, ImVec2(x, y), ImVec2(x + iconSize, y + iconSize), tint);
		return iconSize;
	}

	inline float GetHierarchyIconTextGap()
	{
		return std::max(3.0f, std::round(ImGui::GetFontSize() * 0.12f));
	}

	inline float GetCompactCheckboxWidth(float scale = 0.68f)
	{
		return std::max(10.0f, std::round(ImGui::GetFrameHeight() * scale));
	}

	inline bool DrawCompactCheckbox(const char* id, bool* v, float scale = 0.68f)
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

	inline EditorIcon ResolveAutomaticEntityEditorIcon(Entity entity)
	{
		if (entity.HasComponent<C_Camera>())
			return EditorIcon::Camera;
		if (entity.HasComponent<SpriteRenderer>() || entity.HasComponent<Tilemap2D>()
			|| entity.HasComponent<ParticleSystem2D>())
			return EditorIcon::Sprite;
		if (entity.HasComponent<Rigidbody2D>())
			return EditorIcon::Rigidbody2D;
		if (entity.HasComponent<BoxCollider2D>() || entity.HasComponent<CircleCollider2D>())
			return EditorIcon::BoxCollider2D;
		return EditorIcon::Entity;
	}

	inline EditorIcon ResolveEntityEditorIcon(Entity entity)
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

	inline EditorIcon ResolveEntityIconChoice(Entity entity, EntityIconMode mode)
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

	inline const char* GetEntityIconModeLabel(EntityIconMode mode)
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

	inline bool DrawEntityIconSelector(const Ref<EditorIconSet>& icons, Entity entity,
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
		PrepareEditorPopup("EntityIconPicker",380);
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
inline bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
	{
        ImGui::PushID(label.c_str());
        DrawProperty(label, columnWidth);
        bool changed = false;
        const float width = std::max(24.0f, (ImGui::GetContentRegionAvail().x - 44.0f) / 3.0f);
        for (int axis=0; axis<3; ++axis)
        {
            ImGui::PushID(axis);
            if(axis) ImGui::SameLine(0, 3);
            const char* labels[] = { "X", "Y", "Z" };
            ImGui::TextDisabled("%s", labels[axis]); ImGui::SameLine(0, 2);
            ImGui::SetNextItemWidth(width);
            changed |= ImGui::DragFloat("##Value", &values[axis], 0.1f, 0, 0, "%.2f");
            if(ImGui::BeginPopupContextItem("AxisValue"))
            {
                if(ImGui::MenuItem("Reset axis")) { values[axis]=resetValue; changed=true; }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::Columns(1);
        ImGui::PopID();
        return changed;
	}

	// 通用的两列布局绘制函数（标签在左侧，控件右对齐）
	inline void DrawProperty(const std::string& label, float columnWidth)
	{
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth + 40.0f);

        const float width = std::clamp(ImGui::GetWindowWidth() * 0.43f, 92.0f, columnWidth + 100.0f);
        ImGui::SetColumnWidth(0, width);
        ImGui::AlignTextToFramePadding();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(min.x + std::max(1.0f, width - 12.0f), min.y + ImGui::GetTextLineHeight());
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, max.x,
            label.c_str(), nullptr, nullptr);
        ImGui::Dummy(ImVec2(max.x - min.x, ImGui::GetTextLineHeight()));
        if (ImGui::IsItemHovered() && ImGui::CalcTextSize(label.c_str()).x > max.x-min.x)
            ImGui::SetTooltip("%s", label.c_str());
        ImGui::NextColumn();
        ImGui::SetNextItemWidth(-1.0f);
	}

	template<typename T> inline bool* GetComponentEnabledFlag(T&) { return nullptr; }
	template<> inline bool* GetComponentEnabledFlag<C_Camera>(C_Camera& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<SpriteRenderer>(SpriteRenderer& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<SpriteAnimator>(SpriteAnimator& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<LineRenderer>(LineRenderer& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<Tilemap2D>(Tilemap2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<TilemapRenderer2D>(TilemapRenderer2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<ParticleSystem2D>(ParticleSystem2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<Light2D>(Light2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<Rigidbody2D>(Rigidbody2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<BoxCollider2D>(BoxCollider2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<CircleCollider2D>(CircleCollider2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<DistanceJoint2D>(DistanceJoint2D& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<AudioSource>(AudioSource& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<AudioListener>(AudioListener& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<UIButton>(UIButton& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<UIImage>(UIImage& component) { return &component.Enabled; }
	template<> inline bool* GetComponentEnabledFlag<UIText>(UIText& component) { return &component.Enabled; }

	inline bool DrawColliderMaterialProperties(float& density, float& friction, float& restitution)
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

	inline bool DrawCollisionFilterProperties(bool& isTrigger,
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

    static void DrawPropertyActions(const ComponentDescriptor& descriptor, Entity entity, const std::function<void()>& onModified)
    {
        static uint64_t copiedType = 0;
        static std::map<std::string, PropertyValue> copied;
        if (descriptor.Properties.empty()) return;
        if (ImGui::MenuItem("Copy property values"))
        {
            copied.clear(); copiedType=static_cast<uint64_t>(descriptor.TypeId);
            for(const auto& property:descriptor.Properties)
                if(!property.EntityReference) copied.emplace(property.StableName,property.Get(entity));
        }
        const bool paste = ImGui::MenuItem("Paste property values",nullptr,false,copiedType==static_cast<uint64_t>(descriptor.TypeId) && !copied.empty());
        const bool reset = ImGui::MenuItem("Reset property values");
        if (paste || reset)
        {
            std::vector<std::pair<const PropertyDescriptor*,PropertyValue>> before;
            std::string error;
            bool accepted=true;
            for(const auto& property:descriptor.Properties)
            {
                const PropertyValue* value=nullptr;
                if(reset && property.DefaultValue) value=&*property.DefaultValue;
                if(paste) { auto found=copied.find(property.StableName); if(found!=copied.end()) value=&found->second; }
                if(!value) continue;
                before.emplace_back(&property,property.Get(entity));
                if(!property.Set(entity,*value,error)) { accepted=false; break; }
            }
            if(accepted && !before.empty()) onModified();
            else if(!accepted)
            {
                for(auto it=before.rbegin();it!=before.rend();++it) { std::string rollback; it->first->Set(entity,it->second,rollback); }
                TC_Core_Warn("Property operation rolled back: {0}",error);
            }
        }
        ImGui::Separator();
    }

template<typename T, typename UIFunction, typename ModifiedFunction>
inline void DrawComponent(const std::string& name, Entity entity,
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
            for(const auto& descriptor:ComponentRegistry::Get().GetDescriptors())
                if(descriptor.DisplayName==name && descriptor.Has(entity)) { DrawPropertyActions(descriptor,entity,onModified); break; }
            if(name != "Transform" && name != "Rect Transform")
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
	inline void DrawRegisteredComponent(const ComponentDescriptor& descriptor,
		Entity entity, ModifiedFunction onModified, bool editable, const std::vector<Entity>* targets = nullptr)
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
		ImGui::BeginDisabled(!editable || targets);
		if (ImGui::SmallButton("..."))
			ImGui::OpenPopup("RegisteredComponentSettings");
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("RegisteredComponentSettings"))
		{
			ImGui::BeginDisabled(!editable);
            DrawPropertyActions(descriptor,entity,onModified);
			if (ImGui::MenuItem("Remove component"))
				remove = true;
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

		if (open)
		{
			ImGui::BeginDisabled(!editable);
			const uint64_t componentType = static_cast<uint64_t>(descriptor.TypeId);
			if (componentType == ComponentIds::Canvas)
				ImGui::TextDisabled("Screen Space - Overlay");
			else if (componentType == ComponentIds::TextRenderer)
				ImGui::TextDisabled("World-space text rendered by the active camera");
			for (const PropertyDescriptor& property : descriptor.Properties)
			{
				ImGui::PushID(property.StableName.c_str());
                PropertyValue value = property.Get(entity);
                bool mixed=false;
                if(targets) for(Entity target:*targets) if(property.Get(target)!=value) { mixed=true; break; }
                if(mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,true);
                const bool inlineField = !property.AssetReference;
                if (inlineField) DrawProperty(property.DisplayName);
                bool changed = false;
				switch (property.Kind)
				{
					case PropertyKind::Bool:
					{
						bool item = std::get<bool>(value);
						changed = ImGui::Checkbox("##Value", &item);
						value = item;
						break;
					}
					case PropertyKind::Int32:
					{
						int32_t item = std::get<int32_t>(value);
						const char* const* labels = nullptr;
						int labelCount = 0;
						static const char* textAlignmentLabels[] = {
							"Left", "Center", "Right" };
						static const char* canvasScaleLabels[] = {
							"Constant Pixel Size", "Scale With Screen Size" };
						static const char* layoutDirectionLabels[] = {
							"Horizontal", "Vertical" };
						if ((componentType == ComponentIds::TextRenderer
							|| componentType == ComponentIds::UIText)
							&& property.StableName == "Alignment")
						{
							labels = textAlignmentLabels;
							labelCount = static_cast<int>(std::size(textAlignmentLabels));
						}
						else if (componentType == ComponentIds::Canvas
							&& property.StableName == "ScaleMode")
						{
							labels = canvasScaleLabels;
							labelCount = static_cast<int>(std::size(canvasScaleLabels));
						}
						else if (componentType == ComponentIds::UILayoutGroup
							&& property.StableName == "Direction")
						{
							labels = layoutDirectionLabels;
							labelCount = static_cast<int>(std::size(layoutDirectionLabels));
						}
						if (labels && item >= 0 && item < labelCount)
							changed = ImGui::Combo("##Value", &item,
								labels, labelCount);
						else
							changed = ImGui::DragInt("##Value", &item, 1.0f);
						value = item;
						break;
					}
					case PropertyKind::Int64:
					{
						int64_t item = std::get<int64_t>(value);
						changed = ImGui::InputScalar("##Value",
							ImGuiDataType_S64, &item);
						value = item;
						break;
					}
					case PropertyKind::UInt32:
					{
						uint32_t item = std::get<uint32_t>(value);
						changed = ImGui::InputScalar("##Value",
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
							changed = ImGui::InputScalar("##Value",
								ImGuiDataType_U64, &item);
						value = item;
						break;
					}
					case PropertyKind::Float:
					{
						float item = std::get<float>(value);
						if (componentType == ComponentIds::Canvas
							&& property.StableName == "MatchWidthOrHeight")
							changed = ImGui::SliderFloat("##Value",
								&item, 0.0f, 1.0f, "%.2f");
						else
							changed = ImGui::DragFloat("##Value", &item, 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Double:
					{
						double item = std::get<double>(value);
						changed = ImGui::InputDouble("##Value", &item);
						value = item;
						break;
					}
					case PropertyKind::String:
					{
						std::string item = std::get<std::string>(value);
						if (((componentType == ComponentIds::TextRenderer
							|| componentType == ComponentIds::UIText)
							&& property.StableName == "Text")
							|| (componentType == ComponentIds::UILocalization && property.StableName == "Table"))
							changed = DrawBoundedMultilineText("##Value",
								item, ImVec2(-1.0f,
									ImGui::GetTextLineHeight() * 3.5f));
						else
						{
							std::array<char, 4096> buffer{};
							const size_t count = std::min(item.size(), buffer.size() - 1);
							std::copy_n(item.data(), count, buffer.data());
							changed = ImGui::InputText("##Value",
								buffer.data(), buffer.size());
							item = std::string(buffer.data());
						}
						value = std::move(item);
						break;
					}
					case PropertyKind::Vector2:
					{
						auto item = std::get<glm::vec2>(value);
						changed = ImGui::DragFloat2("##Value",
							glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Vector3:
					{
						auto item = std::get<glm::vec3>(value);
						changed = ImGui::DragFloat3("##Value",
							glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
					case PropertyKind::Vector4:
					{
						auto item = std::get<glm::vec4>(value);
						const bool isColor = property.StableName.find("Color")
							!= std::string::npos;
						if (isColor)
							changed = DrawColorField("##Value",
								glm::value_ptr(item));
						else
							changed = ImGui::DragFloat4("##Value",
								glm::value_ptr(item), 0.1f);
						value = item;
						break;
					}
				}
                if (inlineField) ImGui::Columns(1);
                if(mixed) ImGui::PopItemFlag();
				if (changed)
				{
					std::string error;
                    const std::vector<Entity> single{entity};
                    const auto& edits=targets?*targets:single;
                    const bool accepted=EditorProperties::SetAll(property,edits,value,error);
                    if(accepted) onModified();
                    if(!accepted)
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


}
}
