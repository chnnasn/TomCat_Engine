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
	static bool DrawColorField(const char* label, float* color,
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

	static constexpr size_t MaximumInspectorTextBytes = 65536;

	static bool DrawBoundedMultilineText(const char* label, std::string& value,
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
	static ImTextureID ToImGuiTextureID(const Ref<Texture2D>& texture)
	{
		return texture
			? reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(texture->GetRendererID()))
			: nullptr;
	}

	static void DrawIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& minimum, const ImVec2& maximum, ImU32 tint = IM_COL32_WHITE)
	{
        DrawEditorGlyph(ImGui::GetWindowDrawList(),icons,icon,minimum,maximum,tint);
	}

	static std::string LowerASCII(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		return value;
	}

	static const char* GetAddComponentCategory(const ComponentDescriptor& descriptor)
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

	static bool MatchesAddComponentSearch(const ComponentDescriptor& descriptor,
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
		if (FindBuiltInSpriteAsset(handle))
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

	static bool ReadAuthoringAssetDocument(const std::filesystem::path& path,
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

	static bool DrawTypedAuthoringAssetField(const char* id, AssetType expectedType,
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

	static bool DrawSpritePaletteTile(AssetHandle handle, const char* label,
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

	static std::string RegisteredAssetReferenceLabel(
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

	static bool DrawRegisteredAssetReference(const PropertyDescriptor& property,
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

	static const PropertyDescriptor* FindRegisteredProperty(uint64_t componentType,
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

	static float DrawTreeRowIcon(const Ref<EditorIconSet>& icons, EditorIcon icon,
		const ImVec2& itemMin, const ImVec2& itemMax, ImU32 tint = IM_COL32_WHITE)
	{
		const float iconSize = std::min(std::round(ImGui::GetFontSize() * 0.95f),
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
		if (entity.HasComponent<SpriteRenderer>() || entity.HasComponent<Tilemap2D>()
			|| entity.HasComponent<ParticleSystem2D>())
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
		if (!m_ColliderEditingAllowed || !m_ColliderGizmosEnabled || m_ColliderEditMode == ColliderEditMode::None || !m_SelectionContext
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
        if (contextChanged || clearSelection)
        {
            m_MultiSelection.clear();
            m_HierarchyVisibleOrder.clear();
            m_PreviousHierarchyOrder.clear();
            m_SelectionAnchor = UUID(0);
        }
		if (contextChanged && m_Context)
		{
			for (const auto& [entityID, timeline] : m_AnimationTimelineStates)
			{
				(void)timeline;
				Entity previewed = m_Context->FindEntityByUUID(UUID(entityID));
				if (!previewed || !previewed.HasComponent<SpriteRenderer>())
					continue;
				auto& renderer = previewed.GetComponent<SpriteRenderer>();
				renderer.RuntimeSpriteOverrideActive = false;
				renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
			}
		}
		m_Context = context;
		if (m_Context && m_Project)
			m_Context->SetPhysics2DSettings(m_Project->GetSettings().Physics2D);
		m_ForceExpandParent = {};
		m_ForceOpenEntityNodes.clear();
		m_ForceOpenSceneRoot = false;
		m_EntityToDelete = {};
		m_PendingPrefabRoot = UUID(0);
		m_PendingPrefabAction = 0;
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
		m_AnimatorRenamePopupRequested = false;
		if (contextChanged)
		{
            m_InspectorAssetPath.clear(); m_LockedInspectorAssetPath.clear();
            m_InspectorLocked = false;
            m_InspectedEntity = UUID(0);
			m_AnimatorGraphStates.clear();
			m_TilemapBrushStates.clear();
			m_AnimationTimelineStates.clear();
			m_TilePaletteStates.clear();
		}
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

	void SceneHierarchyPanel::ResetForSceneReplacement(const Ref<Scene>& scene, UUID selectedEntity)
	{
		// Clear stale handles before SetContext, which can otherwise attempt to
		// derive the selected UUID from an entity index reused by the new registry.
		m_SelectionContext = {};
		// The caller owns the replacement/history transaction. Any Inspector
		// gesture belongs to the discarded registry and must not cancel that new
		// transaction through SetContext's usual context-switch notification.
		m_ModificationGestureActive = false;
		m_FrameEntityRequest = UUID(0);
		ClearClipboard();
		m_AnimatorGraphStates.clear();
		m_TilemapBrushStates.clear();
		m_AnimationTimelineStates.clear();
		m_TilePaletteStates.clear();
		SetContext(scene, true, false);
		if (scene && static_cast<uint64_t>(selectedEntity) != 0)
			SetSelectedEntity(scene->FindEntityByUUID(selectedEntity));
	}

	void SceneHierarchyPanel::ClearClipboard()
	{
		m_ClipboardEntity = {};
		m_ClipboardScene = nullptr;
		m_ClipboardIsCut = false;
	}

	void SceneHierarchyPanel::MarkModified(bool instant)
	{
        m_PrefabOverridesDirty = true;
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
		if (!entity || !m_Context || !m_ColliderEditingAllowed || !m_ScriptEditingEnabled ||
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
		if (!entity || !m_ColliderEditingAllowed || !m_ScriptEditingEnabled)
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

	bool SceneHierarchyPanel::FlushPendingCommands()
	{
		if (static_cast<uint64_t>(m_PendingPrefabRoot) != 0)
		{
			const UUID root = m_PendingPrefabRoot;
			m_PendingPrefabRoot = UUID(0);
			if (m_Context && m_PrefabActionCallback && m_PrefabCreationAllowed)
				m_PrefabActionCallback(m_Context->FindEntityByUUID(root), m_PendingPrefabAction, m_PendingPrefabEntity, m_PendingPrefabComponent, m_PendingPrefabProperty);
            m_PrefabOverridesDirty = true;
		}
		return FlushPendingDeletion();
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

	void SceneHierarchyPanel::OnImGuiRender(bool* hierarchyOpen, bool* inspectorOpen,
		bool sceneDirty)
	{
		if (m_ColliderEditMode != ColliderEditMode::None
			&& GetColliderEditMode() == ColliderEditMode::None)
			ClearColliderEditMode();
		// Keyboard commands can originate from the Scene viewport while Hierarchy
		// is hidden or covered by another dock tab. Drain deletion before any tree
		// traversal so it never remains queued until the panel becomes visible.
        FlushPendingDeletion();
        if (!m_Context || !m_SelectionContext) m_MultiSelection.clear();
        else
        {
            std::erase_if(m_MultiSelection, [&](UUID id) { return !m_Context->FindEntityByUUID(id); });
            if (!m_MultiSelection.empty() && std::find(m_MultiSelection.begin(),m_MultiSelection.end(),m_SelectionContext.GetUUID())==m_MultiSelection.end()) m_MultiSelection.clear();
        }
        m_HierarchyFocused = false;
		m_InspectorFocused = false;
		if (!hierarchyOpen || *hierarchyOpen)
		{
		const bool hierarchyVisible = BeginEditorWindow("Hierarchy", hierarchyOpen);
		m_HierarchyDocked = ImGui::IsWindowDocked();
        if (hierarchyVisible && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            m_InspectorAssetPath.clear();
		m_HierarchyFocused = hierarchyVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		if (hierarchyVisible && m_Context)
        {
            m_PreviousHierarchyOrder.swap(m_HierarchyVisibleOrder);
            m_HierarchyVisibleOrder.clear();
            ImGui::SetNextItemWidth(-1.0f);
            EditorSearchField("##HierarchySearch", "Search objects...", m_HierarchySearch.data(), m_HierarchySearch.size());
            FlushPendingDeletion();
            std::string sceneName = m_Context->GetSceneName();
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
			bool rootOpen = ImGui::TreeNodeEx((void*)m_Context.get(), rootFlags, "");
			ImGui::PopStyleColor(3);
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
					const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(handle);
					const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
					if (builtIn && m_SpriteCreateCallback)
						m_SpriteCreateCallback(handle);
					else if (metadata && metadata->Type == AssetType::Scene && m_SceneLoadCallback)
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
			const bool inspectorVisible = BeginEditorWindow("Inspector", inspectorOpen);
			m_InspectorDocked = ImGui::IsWindowDocked();
			m_InspectorFocused = inspectorVisible &&
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (inspectorVisible)
            {
                auto* inspectorWindow = ImGui::GetCurrentWindow();
                auto* node = inspectorWindow->DockNode;
                bool clicked = false;
                const auto drawLock = [&]() {
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    const float size = ImGui::GetFontSize() * 0.8f;
                    const ImVec2 start((a.x+b.x-size)*0.5f,(a.y+b.y-size)*0.5f);
                    DrawEditorGlyph(ImGui::GetWindowDrawList(),m_Icons,EditorIcon::Lock,start,
                        ImVec2(start.x+size,start.y+size),ImGui::GetColorU32(m_InspectorLocked ? ImGuiCol_CheckMark : ImGuiCol_Text));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",m_InspectorLocked ? "Unlock Inspector" : "Lock Inspector");
                };
                if (node && ImGui::DockNodeBeginAmendTabBar(node))
                {
                    // This ImGui fork packs trailing tabs after labels; explicitly align this title action.
                    if (auto* lockTab=ImGui::TabBarFindTabByID(node->TabBar,ImGui::GetID("   ##InspectorLockTab")))
                        lockTab->Offset=std::max(0.0f,node->Pos.x+node->Size.x-node->TabBar->BarRect.Min.x-lockTab->Width);
                    clicked = ImGui::TabItemButton("   ##InspectorLockTab",ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip);
                    drawLock();
                    ImGui::DockNodeEndAmendTabBar();
                }
                else if (!node)
                {
                    // A floating Inspector uses the same title-bar alignment.
                    const float size = inspectorWindow->TitleBarHeight();
                    const ImRect rect(ImVec2(inspectorWindow->Pos.x+inspectorWindow->Size.x-size*2,inspectorWindow->Pos.y),
                        ImVec2(inspectorWindow->Pos.x+inspectorWindow->Size.x-size,inspectorWindow->Pos.y+size));
                    const ImRect clip = inspectorWindow->ClipRect;
                    ImGui::PushClipRect(rect.Min,rect.Max,false);
                    bool hovered=false, held=false;
                    const ImGuiID id = inspectorWindow->GetID("InspectorTitleLock");
                    ImGui::ItemAdd(rect,id);
                    clicked = ImGui::ButtonBehavior(rect,id,&hovered,&held);
                    drawLock();
                    ImGui::PopClipRect();
                    inspectorWindow->ClipRect=clip;
                }
                if (clicked)
                {
                    m_InspectorLocked = !m_InspectorLocked;
                    m_LockedInspectorAssetPath = m_InspectorLocked ? m_InspectorAssetPath : std::filesystem::path{};
                    m_InspectedEntity = m_InspectorLocked && m_SelectionContext ? m_SelectionContext.GetUUID() : UUID(0);
                }
            }
            const auto& inspectedAsset = m_InspectorLocked ? m_LockedInspectorAssetPath : m_InspectorAssetPath;
            if (inspectorVisible && !inspectedAsset.empty() && m_AssetInspectorRenderer)
                m_AssetInspectorRenderer(inspectedAsset);
            else if (inspectorVisible && m_Context && (m_SelectionContext || (m_InspectorLocked && m_Context->FindEntityByUUID(m_InspectedEntity))))
			{
                Entity inspected = m_InspectorLocked ? m_Context->FindEntityByUUID(m_InspectedEntity) : m_SelectionContext;
                if (!inspected) { m_InspectorLocked = false; inspected = m_SelectionContext; }
                if (!m_InspectorLocked && m_MultiSelection.size()>1) DrawMultiSelectionInspector();
                else DrawComponents(inspected);
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
					AcceptCSharpScriptDrop(inspected);
					ImGui::EndDragDropTarget();
				}
			}
			if (inspectorVisible && !m_AnimatorRenamePopupGraphOwner
				&& (m_AnimatorRenameTarget == AnimatorRenameTarget::None
					|| !m_SelectionContext
					|| !m_SelectionContext.HasComponent<SpriteAnimator>()
					|| m_AnimatorRenameEntity != m_SelectionContext.GetUUID()))
				DismissAnimatorRenamePopup(false);
			ImGui::End();
		}
		FinishModificationGesture();
	}

	bool SceneHierarchyPanel::OpenAuthoringAsset(AssetHandle handle, AssetType type)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return false;
		const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
		if (path.empty())
			return false;

		auto rejectDirtyReplacement = [](AssetHandle current, bool loaded,
			bool dirty, std::string& message)
		{
			(void)current;
			if (loaded && dirty)
			{
				message = "Save or revert the current asset before opening another one.";
				return true;
			}
			return false;
		};
		std::string document;
		std::string error;
		if (type == AssetType::AnimationClip)
		{
			auto& editor = m_AnimationClipAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			AnimationClipAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& AnimationClipAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftSprite = AssetHandle(0);
			}
			else editor.Message = error;
			m_AnimationOpenRequested = true;
			return true;
		}
		if (type == AssetType::AnimatorController)
		{
			auto& editor = m_AnimatorControllerAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			AnimatorControllerAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& AnimatorControllerAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftClip = AssetHandle(0);
			}
			else editor.Message = error;
			m_AnimatorGraphOpenRequested = true;
			return true;
		}
		if (type == AssetType::TilePalette)
		{
			auto& editor = m_TilePaletteAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			TilePaletteAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& TilePaletteAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftSprite = AssetHandle(0);
			}
			else editor.Message = error;
			m_TilePaletteOpenRequested = true;
			return true;
		}
		return false;
	}

	bool SceneHierarchyPanel::SaveAnimationClipAsset()
	{
		auto& editor = m_AnimationClipAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::AnimationClip || currentPath.empty())
		{
			editor.Message = "The Animation Clip no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !AnimationClipAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Animation Clip is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	bool SceneHierarchyPanel::SaveAnimatorControllerAsset()
	{
		auto& editor = m_AnimatorControllerAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::AnimatorController || currentPath.empty())
		{
			editor.Message = "The Animator Controller no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !AnimatorControllerAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Animator Controller is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	bool SceneHierarchyPanel::SaveTilePaletteAsset()
	{
		auto& editor = m_TilePaletteAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::TilePalette || currentPath.empty())
		{
			editor.Message = "The Tile Palette no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !TilePaletteAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Tile Palette is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		if (m_ActiveTilePaletteHandle == imported)
			m_ActiveTilePalette = editor.Asset;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	void SceneHierarchyPanel::DrawAnimationClipAssetEditor()
	{
		auto& editor = m_AnimationClipAssetEditor;
		ImGui::Text("Animation Clip: %s%s", PathToUTF8(editor.Path.filename()).c_str(),
			editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveAnimationClipAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::AnimationClip);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		ImGui::Separator();
		char name[128]{};
		std::snprintf(name, sizeof(name), "%s", editor.Asset.Clip.Name.c_str());
		if (ImGui::InputText("Name", name, sizeof(name)))
		{
			editor.Asset.Clip.Name = name;
			editor.Dirty = true;
		}
		if (ImGui::Checkbox("Loop", &editor.Asset.Clip.Loop)) editor.Dirty = true;
		if (ImGui::DragFloat("Sample Rate", &editor.Asset.SampleRate, 0.25f,
			0.01f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
        if (!editor.Asset.Clip.Frames.empty())
        {
            editor.SelectedFrame = std::min(editor.SelectedFrame, editor.Asset.Clip.Frames.size()-1);
            if (ImGui::Button(editor.PreviewPlaying ? "Pause preview" : "Play preview")) editor.PreviewPlaying = !editor.PreviewPlaying;
            ImGui::SameLine();
            int selected = static_cast<int>(editor.SelectedFrame);
            ImGui::SetNextItemWidth(220);
            if (ImGui::SliderInt("Frame", &selected, 0, static_cast<int>(editor.Asset.Clip.Frames.size())-1))
            { editor.SelectedFrame = selected; editor.PreviewPlaying = false; editor.PreviewTime = 0; }
            if (editor.PreviewPlaying)
            {
                editor.PreviewTime += ImGui::GetIO().DeltaTime;
                size_t advances = 0;
                while (editor.PreviewTime >= editor.Asset.Clip.Frames[editor.SelectedFrame].DurationSeconds && advances++ < editor.Asset.Clip.Frames.size())
                {
                    editor.PreviewTime -= std::max(0.001f, editor.Asset.Clip.Frames[editor.SelectedFrame].DurationSeconds);
                    if (editor.SelectedFrame+1 < editor.Asset.Clip.Frames.size()) ++editor.SelectedFrame;
                    else if(editor.Asset.Clip.Loop) editor.SelectedFrame=0;
                    else { editor.PreviewPlaying=false; editor.PreviewTime=0; break; }
                }
            }
            DrawSpritePaletteTile(editor.Asset.Clip.Frames[editor.SelectedFrame].SpriteHandle, "Preview", false, 120);
            ImGui::BeginChild("ClipFrameStrip", ImVec2(0, 105), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t index=0; index<editor.Asset.Clip.Frames.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                if(index) ImGui::SameLine();
                if (DrawSpritePaletteTile(editor.Asset.Clip.Frames[index].SpriteHandle, std::to_string(index).c_str(), index==editor.SelectedFrame, 64))
                { editor.SelectedFrame=index; editor.PreviewPlaying=false; }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        DrawAnimatorSectionLabel("Selected frame");
		std::optional<size_t> remove;
		for (size_t index = 0; index < editor.Asset.Clip.Frames.size(); ++index)
		{
            if (index != editor.SelectedFrame) continue;
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("Frame %zu", index);
			AssetHandle sprite = editor.Asset.Clip.Frames[index].SpriteHandle;
			if (DrawAnimatorSpriteField("ExternalClipFrame", sprite))
			{
				editor.Asset.Clip.Frames[index].SpriteHandle = sprite;
				editor.Dirty = true;
			}
			if (ImGui::DragFloat("Duration", &editor.Asset.Clip.Frames[index].DurationSeconds,
				0.005f, 0.001f, 3600.0f, "%.3f s", ImGuiSliderFlags_AlwaysClamp))
				editor.Dirty = true;
			if (ImGui::SmallButton("Remove")) remove = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (remove)
		{
			editor.Asset.Clip.Frames.erase(editor.Asset.Clip.Frames.begin()
				+ static_cast<std::ptrdiff_t>(*remove));
			editor.Dirty = true;
		}
		ImGui::TextUnformatted("New Frame Sprite");
		DrawAnimatorSpriteField("ExternalClipNewFrame", editor.DraftSprite);
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftSprite) == 0);
		if (ImGui::Button("Add Frame"))
		{
			editor.Asset.Clip.Frames.push_back({ editor.DraftSprite,
				1.0f / std::max(editor.Asset.SampleRate, 0.01f) });
			editor.DraftSprite = AssetHandle(0);
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
	}

	void SceneHierarchyPanel::DrawAnimatorControllerAssetEditor()
	{
		auto& editor = m_AnimatorControllerAssetEditor;
		auto& asset = editor.Asset;
		ImGui::Text("Animator Controller: %s%s",
			PathToUTF8(editor.Path.filename()).c_str(), editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveAnimatorControllerAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::AnimatorController);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		DrawAnimatorSectionLabel("Parameters");
		std::optional<size_t> removeParameter;
		for (size_t index = 0; index < asset.Parameters.size(); ++index)
		{
			AnimatorParameter& parameter = asset.Parameters[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::TextUnformatted(parameter.Name.c_str());
			ImGui::SameLine();
			const char* types[] = { "Bool", "Int", "Float", "Trigger" };
			int type = static_cast<int>(parameter.Type);
			const bool used = std::any_of(asset.Transitions.begin(), asset.Transitions.end(),
				[&](const AnimatorTransition& transition)
				{
					return std::any_of(transition.Conditions.begin(),
						transition.Conditions.end(), [&](const AnimatorCondition& condition)
						{ return condition.Parameter == parameter.Name; });
				});
			ImGui::BeginDisabled(used);
			if (ImGui::Combo("Type", &type, types, 4))
			{
				parameter.Type = static_cast<AnimatorParameterType>(type);
				editor.Dirty = true;
			}
			ImGui::EndDisabled();
			switch (parameter.Type)
			{
				case AnimatorParameterType::Bool:
				case AnimatorParameterType::Trigger:
					if (ImGui::Checkbox("Default", &parameter.BoolValue)) editor.Dirty = true;
					break;
				case AnimatorParameterType::Int:
					if (ImGui::InputInt("Default", &parameter.IntValue)) editor.Dirty = true;
					break;
				case AnimatorParameterType::Float:
					if (ImGui::DragFloat("Default", &parameter.FloatValue, 0.05f)) editor.Dirty = true;
					break;
			}
			if (ImGui::SmallButton("Remove Parameter")) removeParameter = index;
			ImGui::PopID();
		}
		if (removeParameter)
		{
			const std::string removed = asset.Parameters[*removeParameter].Name;
			asset.Parameters.erase(asset.Parameters.begin()
				+ static_cast<std::ptrdiff_t>(*removeParameter));
			for (AnimatorTransition& transition : asset.Transitions)
				std::erase_if(transition.Conditions,
					[&](const AnimatorCondition& condition)
					{ return condition.Parameter == removed; });
			editor.Dirty = true;
		}
		if (ImGui::Button("Add Parameter"))
		{
			AnimatorParameter parameter;
			parameter.Name = SpriteAnimatorAuthoring::MakeUniqueName(asset.Parameters,
				std::string("Parameter"), [](const AnimatorParameter& value)
				{ return value.Name; });
			asset.Parameters.push_back(std::move(parameter));
			editor.Dirty = true;
		}

		DrawAnimatorSectionLabel("States");
		if (!asset.States.empty())
		{
			const char* initial = asset.InitialState.c_str();
			if (ImGui::BeginCombo("Initial State", initial))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == asset.InitialState))
					{
						asset.InitialState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
		}
		std::optional<size_t> removeState;
		for (size_t index = 0; index < asset.States.size(); ++index)
		{
			auto& state = asset.States[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("%s", state.Name.c_str());
			if (DrawTypedAuthoringAssetField("StateClip", AssetType::AnimationClip,
				state.ClipHandle)) editor.Dirty = true;
			if (ImGui::DragFloat("Speed", &state.Speed, 0.05f, 0.001f, 100.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
			if (ImGui::SmallButton("Open Clip"))
				OpenAuthoringAsset(state.ClipHandle, AssetType::AnimationClip);
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove State")) removeState = index;
			ImGui::PopID();
		}
		if (removeState)
		{
			const std::string removed = asset.States[*removeState].Name;
			asset.States.erase(asset.States.begin()
				+ static_cast<std::ptrdiff_t>(*removeState));
			std::erase_if(asset.Transitions, [&](const AnimatorTransition& transition)
			{
				return transition.ToState == removed
					|| (!transition.AnyState && transition.FromState == removed);
			});
			asset.InitialState = asset.States.empty() ? std::string{}
				: asset.States.front().Name;
			editor.Dirty = true;
		}
		ImGui::TextUnformatted("New State Animation Clip");
		DrawTypedAuthoringAssetField("NewStateClip", AssetType::AnimationClip,
			editor.DraftClip);
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftClip) == 0);
		if (ImGui::Button("Add State"))
		{
			AnimatorControllerAsset::State state;
			state.Name = SpriteAnimatorAuthoring::MakeUniqueName(asset.States,
				std::string("State"), [](const AnimatorControllerAsset::State& value)
				{ return value.Name; });
			state.ClipHandle = editor.DraftClip;
			asset.States.push_back(std::move(state));
			if (asset.InitialState.empty()) asset.InitialState = asset.States.back().Name;
			editor.DraftClip = AssetHandle(0);
			editor.Dirty = true;
		}
		ImGui::EndDisabled();

		DrawAnimatorSectionLabel("Transitions");
		std::optional<size_t> removeTransition;
		for (size_t index = 0; index < asset.Transitions.size(); ++index)
		{
			AnimatorTransition& transition = asset.Transitions[index];
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::Checkbox("Any State", &transition.AnyState))
			{
				if (!transition.AnyState && transition.FromState.empty()
					&& !asset.States.empty()) transition.FromState = asset.States.front().Name;
				editor.Dirty = true;
			}
			if (!transition.AnyState && ImGui::BeginCombo("From", transition.FromState.c_str()))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == transition.FromState))
					{
						transition.FromState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("To", transition.ToState.c_str()))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == transition.ToState))
					{
						transition.ToState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
			if (ImGui::DragFloat("Exit Time", &transition.ExitTime, 0.01f, -1.0f,
				1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
			ImGui::TextDisabled("Conditions: %zu", transition.Conditions.size());
			std::optional<size_t> removeCondition;
			for (size_t conditionIndex = 0;
				conditionIndex < transition.Conditions.size(); ++conditionIndex)
			{
				AnimatorCondition& condition = transition.Conditions[conditionIndex];
				ImGui::PushID(static_cast<int>(conditionIndex));
				if (ImGui::BeginCombo("Parameter", condition.Parameter.c_str()))
				{
					for (const AnimatorParameter& parameter : asset.Parameters)
					{
						if (ImGui::Selectable(parameter.Name.c_str(),
							condition.Parameter == parameter.Name))
						{
							condition.Parameter = parameter.Name;
							condition.Mode = (parameter.Type == AnimatorParameterType::Bool
								|| parameter.Type == AnimatorParameterType::Trigger)
								? AnimatorConditionMode::If
								: parameter.Type == AnimatorParameterType::Float
									? AnimatorConditionMode::Greater
									: AnimatorConditionMode::Equals;
							editor.Dirty = true;
						}
					}
					ImGui::EndCombo();
				}
				const auto parameter = std::find_if(asset.Parameters.begin(),
					asset.Parameters.end(), [&](const AnimatorParameter& value)
					{ return value.Name == condition.Parameter; });
				if (parameter != asset.Parameters.end())
				{
					struct ModeOption { AnimatorConditionMode Mode; const char* Label; };
					std::vector<ModeOption> modes;
					if (parameter->Type == AnimatorParameterType::Bool
						|| parameter->Type == AnimatorParameterType::Trigger)
						modes = { { AnimatorConditionMode::If, "If" },
							{ AnimatorConditionMode::IfNot, "If Not" } };
					else if (parameter->Type == AnimatorParameterType::Float)
						modes = { { AnimatorConditionMode::Greater, "Greater" },
							{ AnimatorConditionMode::Less, "Less" } };
					else
						modes = { { AnimatorConditionMode::Greater, "Greater" },
							{ AnimatorConditionMode::Less, "Less" },
							{ AnimatorConditionMode::Equals, "Equals" },
							{ AnimatorConditionMode::NotEqual, "Not Equal" } };
					const auto currentMode = std::find_if(modes.begin(), modes.end(),
						[&](const ModeOption& option)
						{ return option.Mode == condition.Mode; });
					const char* modeLabel = currentMode == modes.end()
						? "Choose" : currentMode->Label;
					if (ImGui::BeginCombo("Mode", modeLabel))
					{
						for (const ModeOption& option : modes)
							if (ImGui::Selectable(option.Label,
								condition.Mode == option.Mode))
							{
								condition.Mode = option.Mode;
								editor.Dirty = true;
							}
						ImGui::EndCombo();
					}
					if (parameter->Type == AnimatorParameterType::Float)
					{
						if (ImGui::DragFloat("Threshold", &condition.Threshold, 0.05f))
							editor.Dirty = true;
					}
					else if (parameter->Type == AnimatorParameterType::Int)
					{
						int threshold = static_cast<int>(condition.Threshold);
						if (ImGui::InputInt("Threshold", &threshold))
						{
							condition.Threshold = static_cast<float>(threshold);
							editor.Dirty = true;
						}
					}
				}
				if (ImGui::SmallButton("Delete Condition"))
					removeCondition = conditionIndex;
				ImGui::PopID();
			}
			if (removeCondition)
			{
				transition.Conditions.erase(transition.Conditions.begin()
					+ static_cast<std::ptrdiff_t>(*removeCondition));
				editor.Dirty = true;
			}
			ImGui::BeginDisabled(asset.Parameters.empty());
			if (ImGui::SmallButton("Add Condition"))
			{
				AnimatorCondition condition;
				condition.Parameter = asset.Parameters.front().Name;
				condition.Mode = (asset.Parameters.front().Type == AnimatorParameterType::Bool
					|| asset.Parameters.front().Type == AnimatorParameterType::Trigger)
					? AnimatorConditionMode::If
					: asset.Parameters.front().Type == AnimatorParameterType::Float
						? AnimatorConditionMode::Greater
						: AnimatorConditionMode::Equals;
				transition.Conditions.push_back(std::move(condition));
				editor.Dirty = true;
			}
			ImGui::EndDisabled();
			if (ImGui::SmallButton("Remove Transition")) removeTransition = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeTransition)
		{
			asset.Transitions.erase(asset.Transitions.begin()
				+ static_cast<std::ptrdiff_t>(*removeTransition));
			editor.Dirty = true;
		}
		ImGui::BeginDisabled(asset.States.empty());
		if (ImGui::Button("Add Transition"))
		{
			AnimatorTransition transition;
			transition.FromState = asset.States.front().Name;
			transition.ToState = asset.States.front().Name;
			transition.ExitTime = 1.0f;
			asset.Transitions.push_back(std::move(transition));
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
	}

	void SceneHierarchyPanel::DrawTilePaletteAssetEditor()
	{
		auto& editor = m_TilePaletteAssetEditor;
		auto& asset = editor.Asset;
		ImGui::Text("Tile Palette: %s%s", PathToUTF8(editor.Path.filename()).c_str(),
			editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveTilePaletteAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::TilePalette);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		ImGui::SameLine();
		if (ImGui::Button("Use for Painting"))
		{
			m_ActiveTilePaletteHandle = editor.Handle;
			m_ActiveTilePalette = editor.Asset;
			if (m_SelectionContext && m_SelectionContext.HasComponent<Tilemap2D>()
				&& !editor.Asset.Tiles.empty())
			{
				m_TilePaletteStates[static_cast<uint64_t>(m_SelectionContext.GetUUID())]
					.SpriteHandle = editor.Asset.Tiles.front().SpriteHandle;
			}
			editor.Message = "Active painting palette selected.";
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(asset.CellSize), 0.05f,
			0.001f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(asset.CellGap), 0.05f,
			-1000000.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
		DrawAnimatorSectionLabel("Palette Tiles");
		std::optional<size_t> remove;
		for (size_t index = 0; index < asset.Tiles.size(); ++index)
		{
			TilePaletteEntry& tile = asset.Tiles[index];
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::InputInt2("Coordinate", &tile.Coordinate.x)) editor.Dirty = true;
			if (DrawAnimatorSpriteField("PaletteEntrySprite", tile.SpriteHandle))
				editor.Dirty = true;
			if (ImGui::SmallButton("Remove Tile")) remove = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (remove)
		{
			asset.Tiles.erase(asset.Tiles.begin() + static_cast<std::ptrdiff_t>(*remove));
			editor.Dirty = true;
		}
		ImGui::InputInt2("New Coordinate", &editor.DraftCoordinate.x);
		ImGui::TextUnformatted("New Tile Sprite");
		DrawAnimatorSpriteField("PaletteNewSprite", editor.DraftSprite);
		const bool duplicate = std::any_of(asset.Tiles.begin(), asset.Tiles.end(),
			[&](const TilePaletteEntry& tile)
			{ return tile.Coordinate == editor.DraftCoordinate; });
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftSprite) == 0 || duplicate);
		if (ImGui::Button("Add Tile"))
		{
			asset.Tiles.push_back({ editor.DraftCoordinate, editor.DraftSprite });
			editor.DraftSprite = AssetHandle(0);
			editor.DraftCoordinate.x++;
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
		if (duplicate) ImGui::TextDisabled("That palette coordinate is already occupied.");
	}

	void SceneHierarchyPanel::OnAnimatorGraphImGuiRender(bool* open)
	{
		m_AnimatorGraphFocused = false;
		if (open && !*open)
			return;

		PrepareEditorToolWindow(ImVec2(1040,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Animator", open);
		m_AnimatorGraphDocked = ImGui::IsWindowDocked();
		m_AnimatorGraphFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		bool drewAnimator = false;
		if (visible)
		{
			if (static_cast<uint64_t>(m_AnimatorControllerAssetEditor.Handle) != 0)
			{
				DrawAnimatorControllerAssetEditor();
				drewAnimator = true;
			}
			else if (!m_SelectionContext)
			{
				ImGui::TextDisabled("Select an entity with a Sprite Animator component.");
			}
			else if (!m_SelectionContext.HasComponent<SpriteAnimator>())
			{
				ImGui::Text("%s", m_SelectionContext.GetName().c_str());
				ImGui::Separator();
				ImGui::TextDisabled("The selected entity does not have a Sprite Animator component.");
			}
			else
			{
				Entity entity = m_SelectionContext;
				SpriteAnimator& animator = entity.GetComponent<SpriteAnimator>();
				if (static_cast<uint64_t>(animator.ControllerHandle) != 0)
				{
					ImGui::Text("Selected: %s", entity.GetName().c_str());
					ImGui::Separator();
					ImGui::TextWrapped("This component uses an external Animator Controller. "
						"Its embedded snapshot is ignored at Play start and is read-only here.");
					if (ImGui::Button("Open Controller"))
						OpenAuthoringAsset(animator.ControllerHandle,
							AssetType::AnimatorController);
					drewAnimator = true;
				}
				else
				{
				ImGui::Text("Selected: %s", entity.GetName().c_str());
				ImGui::Separator();
				std::function<void()> pendingMutation;
				const float sidebarWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.24f,
					190.0f, 300.0f);
				if (ImGui::BeginChild("AnimatorSidebar", ImVec2(sidebarWidth, 0.0f), true))
				{
					if (ImGui::BeginTabBar("AnimatorSidebarTabs"))
					{
						if (ImGui::BeginTabItem("Layers"))
						{
							ImGui::Selectable("Base Layer", true);
							ImGui::TextDisabled("States: %zu", animator.States.size());
							ImGui::EndTabItem();
						}
						if (ImGui::BeginTabItem("Parameters"))
						{
							if (ImGui::SmallButton("+##AnimatorParameter"))
							{
								AnimatorParameter parameter;
								parameter.Name = SpriteAnimatorAuthoring::MakeUniqueName(
									animator.Parameters, std::string("Parameter"),
									[](const AnimatorParameter& value) { return value.Name; });
								animator.Parameters.push_back(std::move(parameter));
								MarkModified(true);
							}
							ImGui::Separator();
							for (AnimatorParameter& parameter : animator.Parameters)
							{
								ImGui::PushID(&parameter);
								ImGui::TextUnformatted(parameter.Name.c_str());
								ImGui::SameLine(ImGui::GetContentRegionAvail().x - 36.0f);
								switch (parameter.Type)
								{
									case AnimatorParameterType::Bool:
									case AnimatorParameterType::Trigger:
										if (ImGui::Checkbox("##Value", &parameter.BoolValue)) MarkModified();
										break;
									case AnimatorParameterType::Int:
										if (ImGui::InputInt("##Value", &parameter.IntValue)) MarkModified();
										break;
									case AnimatorParameterType::Float:
										if (ImGui::DragFloat("##Value", &parameter.FloatValue, 0.05f)) MarkModified();
										break;
								}
								ImGui::PopID();
							}
							if (animator.Parameters.empty())
								ImGui::TextDisabled("List is Empty");
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
				ImGui::EndChild();
				ImGui::SameLine();
				if (ImGui::BeginChild("AnimatorStateMachine", ImVec2(0.0f, 0.0f), false))
					DrawSpriteAnimatorGraph(animator, entity, pendingMutation, true);
				ImGui::EndChild();
				if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
					&& m_AnimatorRenameEntity != entity.GetUUID())
					DismissAnimatorRenamePopup(true);
				else
					DrawAnimatorRenamePopup(animator, entity, true);
				ApplyAnimatorPendingMutation(entity, pendingMutation);
				drewAnimator = true;
				}
			}
		}
		if (!drewAnimator && m_AnimatorRenamePopupGraphOwner)
			DismissAnimatorRenamePopup(true);
		ImGui::End();
		FinishModificationGesture();
	}

	void SceneHierarchyPanel::OnAnimationImGuiRender(bool* open)
	{
		using namespace SpriteAnimatorAuthoring;
		m_AnimationFocused = false;
		if (open && !*open)
			return;

		auto restorePreview = [this](uint64_t entityID, AnimationTimelineState& state)
		{
			if (!m_Context)
				return;
			Entity entity = m_Context->FindEntityByUUID(UUID(entityID));
			if (entity && entity.HasComponent<SpriteRenderer>())
			{
				auto& renderer = entity.GetComponent<SpriteRenderer>();
				renderer.RuntimeSpriteOverrideActive = false;
				renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				renderer.Sprite = static_cast<uint64_t>(renderer.SpriteHandle) != 0
					? AssetManager::Get().LoadTexture(renderer.SpriteHandle) : Ref<Texture2D>{};
			}
			state.Playing = false;
		};

		PrepareEditorToolWindow(ImVec2(1040,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Animation", open);
		m_AnimationDocked = ImGui::IsWindowDocked();
		m_AnimationFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			if (open && !*open)
				for (auto& [entityID, state] : m_AnimationTimelineStates)
					restorePreview(entityID, state);
			return;
		}
		if (static_cast<uint64_t>(m_AnimationClipAssetEditor.Handle) != 0)
		{
			DrawAnimationClipAssetEditor();
			ImGui::End();
			return;
		}

		if (!m_SelectionContext || !m_SelectionContext.HasComponent<SpriteAnimator>())
		{
			for (auto& [entityID, state] : m_AnimationTimelineStates)
				restorePreview(entityID, state);
			ImGui::TextDisabled("Select an entity with a Sprite Animator component.");
			ImGui::End();
			return;
		}

		Entity entity = m_SelectionContext;
		SpriteAnimator& animator = entity.GetComponent<SpriteAnimator>();
		if (static_cast<uint64_t>(animator.ControllerHandle) != 0)
		{
			for (auto& [entityID, state] : m_AnimationTimelineStates)
				restorePreview(entityID, state);
			ImGui::TextWrapped("This component uses an external Animator Controller. "
				"Open an Animation Clip asset from the Project panel, or open the "
				"Controller and choose a state's clip.");
			if (ImGui::Button("Open Controller"))
				OpenAuthoringAsset(animator.ControllerHandle,
					AssetType::AnimatorController);
			ImGui::End();
			return;
		}
		const uint64_t entityID = static_cast<uint64_t>(entity.GetUUID());
		for (auto& [otherID, otherState] : m_AnimationTimelineStates)
			if (otherID != entityID)
				restorePreview(otherID, otherState);
		AnimationTimelineState& timeline = m_AnimationTimelineStates[entityID];

		ImGui::Text("%s", entity.GetName().c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(210.0f);
		const char* clipPreview = timeline.SelectedClip.empty()
			? "Select Clip" : timeline.SelectedClip.c_str();
		if (ImGui::BeginCombo("##AnimationClip", clipPreview))
		{
			for (const SpriteAnimationClip& clip : animator.Clips)
			{
				if (ImGui::Selectable(clip.Name.c_str(), timeline.SelectedClip == clip.Name))
				{
					timeline.SelectedClip = clip.Name;
					timeline.SelectedFrame = 0;
					timeline.Time = 0.0;
					timeline.Playing = false;
				}
			}
			ImGui::EndCombo();
		}
		if (animator.Clips.empty())
		{
			ImGui::SameLine();
			if (ImGui::Button("Create Clip"))
			{
				SpriteAnimationClip clip;
				clip.Name = "Default";
				clip.Frames.push_back({ entity.HasComponent<SpriteRenderer>()
					? entity.GetComponent<SpriteRenderer>().SpriteHandle
					: AssetHandle(0), 1.0f / 12.0f });
				animator.Clips.push_back(std::move(clip));
				timeline.SelectedClip = animator.Clips.front().Name;
				MarkModified(true);
			}
		}
		if (timeline.SelectedClip.empty() && !animator.Clips.empty())
			timeline.SelectedClip = animator.Clips.front().Name;
		const std::optional<size_t> clipIndex = FindClipIndex(animator,
			timeline.SelectedClip);

		ImGui::Separator();
		if (ImGui::Checkbox("Preview", &timeline.Preview))
		{
			if (!timeline.Preview)
				restorePreview(entityID, timeline);
		}
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.2f, 0.18f, 1.0f));
		timeline.Recording = false;
		ImGui::BeginDisabled();
		ImGui::Checkbox("Record", &timeline.Recording);
		ImGui::EndDisabled();
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Property recording support pending");
		ImGui::SameLine();
		ImGui::BeginDisabled(!clipIndex || animator.Clips[*clipIndex].Frames.empty());
		if (ImGui::Button("|<"))
		{
			timeline.SelectedFrame = 0;
			timeline.Time = 0.0;
		}
		ImGui::SameLine();
		if (ImGui::Button("<"))
		{
			timeline.SelectedFrame = timeline.SelectedFrame > 0
				? timeline.SelectedFrame - 1 : 0;
			timeline.Time = 0.0;
			for (size_t i = 0; clipIndex && i < timeline.SelectedFrame; ++i)
				timeline.Time += animator.Clips[*clipIndex].Frames[i].DurationSeconds;
		}
		ImGui::SameLine();
		if (ImGui::Button(timeline.Playing ? "||" : ">"))
		{
			timeline.Playing = !timeline.Playing;
			if (timeline.Playing)
				timeline.Preview = true;
		}
		ImGui::SameLine();
		if (ImGui::Button(">##NextFrame"))
		{
			const size_t count = animator.Clips[*clipIndex].Frames.size();
			timeline.SelectedFrame = std::min(timeline.SelectedFrame + 1, count - 1);
			timeline.Time = 0.0;
			for (size_t i = 0; i < timeline.SelectedFrame; ++i)
				timeline.Time += animator.Clips[*clipIndex].Frames[i].DurationSeconds;
		}
		ImGui::SameLine();
		if (ImGui::Button(">|"))
		{
			timeline.SelectedFrame = animator.Clips[*clipIndex].Frames.size() - 1;
			timeline.Time = std::nextafter(ClipDuration(animator.Clips[*clipIndex]), 0.0);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(86.0f);
		if (ImGui::DragFloat("FPS", &timeline.FrameRate, 0.25f, 1.0f, 240.0f, "%.1f"))
			timeline.FrameRate = std::clamp(timeline.FrameRate, 1.0f, 240.0f);

		if (clipIndex)
		{
			SpriteAnimationClip& clip = animator.Clips[*clipIndex];
			const double duration = ClipDuration(clip);
			if (timeline.Playing && duration > 0.0)
			{
				timeline.Time += static_cast<double>(ImGui::GetIO().DeltaTime);
				if (clip.Loop)
					timeline.Time = std::fmod(timeline.Time, duration);
				else if (timeline.Time >= duration)
				{
					timeline.Time = std::nextafter(duration, 0.0);
					timeline.Playing = false;
				}
				timeline.SelectedFrame = FrameAtTime(clip, timeline.Time);
			}
			double scrubTime = timeline.Time;
			const double minimumTime = 0.0;
			ImGui::SetNextItemWidth(-1.0f);
			if (duration > 0.0 && ImGui::SliderScalar("##AnimationTime",
				ImGuiDataType_Double, &scrubTime, &minimumTime,
				&duration, "%.3f s"))
			{
				timeline.Time = std::clamp(scrubTime, 0.0,
					std::nextafter(duration, 0.0));
				timeline.SelectedFrame = FrameAtTime(clip, timeline.Time);
				timeline.Playing = false;
			}

			if (timeline.Preview && entity.HasComponent<SpriteRenderer>())
			{
				auto& renderer = entity.GetComponent<SpriteRenderer>();
				if (clip.Frames.empty())
				{
					renderer.RuntimeSpriteOverrideActive = false;
					renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				}
				else
				{
					timeline.SelectedFrame = std::min(timeline.SelectedFrame,
						clip.Frames.size() - 1);
					const AssetHandle frameSprite =
						clip.Frames[timeline.SelectedFrame].SpriteHandle;
					renderer.RuntimeSpriteOverrideActive = true;
					renderer.RuntimeSpriteOverrideHandle = frameSprite;
					renderer.Sprite = AssetManager::Get().LoadTexture(frameSprite);
				}
			}

			ImGui::Separator();
			ImGui::TextDisabled("DOPESHEET  •  Drop Sprite assets to append frames");
			std::optional<size_t> removeFrame;
			std::optional<std::pair<size_t, size_t>> moveFrame;
			if (ImGui::BeginChild("AnimationDopesheet", ImVec2(0.0f, 210.0f), true,
				ImGuiWindowFlags_HorizontalScrollbar))
			{
				for (size_t frameIndex = 0; frameIndex < clip.Frames.size(); ++frameIndex)
				{
					ImGui::PushID(static_cast<int>(frameIndex));
					const float width = std::clamp(clip.Frames[frameIndex].DurationSeconds
						* timeline.FrameRate * 34.0f, 54.0f, 220.0f);
					const std::string label = std::to_string(frameIndex) + "\n"
						+ AnimatorSpriteLabel(clip.Frames[frameIndex].SpriteHandle);
					if (ImGui::Selectable(label.c_str(), timeline.SelectedFrame == frameIndex,
						0, ImVec2(width, 92.0f)))
					{
						timeline.SelectedFrame = frameIndex;
						timeline.Time = 0.0;
						for (size_t i = 0; i < frameIndex; ++i)
							timeline.Time += clip.Frames[i].DurationSeconds;
						timeline.Playing = false;
					}
					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("TC_ANIMATION_FRAME", &frameIndex,
							sizeof(frameIndex));
						ImGui::Text("Move Frame %zu", frameIndex);
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							"TC_ANIMATION_FRAME"))
							if (payload->DataSize == sizeof(size_t))
								moveFrame = { *static_cast<const size_t*>(payload->Data), frameIndex };
						ImGui::EndDragDropTarget();
					}
					if (frameIndex + 1 < clip.Frames.size()) ImGui::SameLine();
					ImGui::PopID();
				}
				const ImVec2 dropMin = ImGui::GetWindowPos();
				const ImVec2 dropMax(dropMin.x + ImGui::GetWindowSize().x,
					dropMin.y + ImGui::GetWindowSize().y);
				if (ImGui::BeginDragDropTargetCustom(ImRect(dropMin, dropMax),
					ImGui::GetID("##AnimationDropTarget")))
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
						AssetDragDropPayloadID))
					{
						if (payload->DataSize == sizeof(uint64_t))
						{
							const AssetHandle sprite(*static_cast<const uint64_t*>(payload->Data));
							const AssetMetadata* metadata = nullptr;
							const AssetSubAsset* subAsset = nullptr;
							if (ResolveSelectableSprite(sprite, metadata, subAsset))
							{
								std::string error;
								if (InsertFrame(clip, clip.Frames.size(),
									{ sprite, 1.0f / timeline.FrameRate }, error))
								{
									timeline.SelectedFrame = clip.Frames.size() - 1;
									MarkModified(true);
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			ImGui::EndChild();
			if (moveFrame)
			{
				std::string error;
				if (MoveFrame(clip, moveFrame->first, moveFrame->second, error))
				{
					timeline.SelectedFrame = moveFrame->second;
					MarkModified(true);
				}
			}

			if (!clip.Frames.empty())
			{
				timeline.SelectedFrame = std::min(timeline.SelectedFrame,
					clip.Frames.size() - 1);
				SpriteAnimationFrame& frame = clip.Frames[timeline.SelectedFrame];
				AssetHandle editedSprite = frame.SpriteHandle;
				if (DrawAnimatorSpriteField("AnimationFrameSprite", editedSprite)
					&& editedSprite != frame.SpriteHandle)
				{
					frame.SpriteHandle = editedSprite;
					MarkModified(true);
				}
				float durationSeconds = frame.DurationSeconds;
				if (ImGui::DragFloat("Frame Duration", &durationSeconds, 0.005f,
					0.001f, 3600.0f, "%.3f s"))
				{
					std::string error;
					if (SetFrameDuration(clip, timeline.SelectedFrame,
						durationSeconds, error)) MarkModified();
				}
				if (ImGui::Button("Delete Frame"))
					removeFrame = timeline.SelectedFrame;
			}
			AssetHandle newFrameSprite = AssetHandle(0);
			ImGui::SetNextItemWidth(240.0f);
			if (DrawAnimatorSpriteField("AnimationNewFrameSprite", newFrameSprite)
				&& static_cast<uint64_t>(newFrameSprite) != 0)
			{
				std::string error;
				if (InsertFrame(clip, clip.Frames.size(),
					{ newFrameSprite, 1.0f / timeline.FrameRate }, error))
				{
					timeline.SelectedFrame = clip.Frames.size() - 1;
					MarkModified(true);
				}
			}
			if (removeFrame)
			{
				std::string error;
				if (RemoveFrame(clip, *removeFrame, error))
				{
					timeline.SelectedFrame = clip.Frames.empty() ? 0
						: std::min(timeline.SelectedFrame, clip.Frames.size() - 1);
					MarkModified(true);
				}
			}
		}
		ImGui::End();
		if (open && !*open)
			for (auto& [previewEntityID, previewState] : m_AnimationTimelineStates)
				restorePreview(previewEntityID, previewState);
		FinishModificationGesture();
	}

	void SceneHierarchyPanel::OnTilePaletteImGuiRender(bool* open)
	{
		m_TilePaletteFocused = false;
		if (open && !*open)
			return;
		PrepareEditorToolWindow(ImVec2(860,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Tile Palette", open);
		m_TilePaletteDocked = ImGui::IsWindowDocked();
		m_TilePaletteFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			return;
		}
		if (static_cast<uint64_t>(m_TilePaletteAssetEditor.Handle) != 0)
		{
			DrawTilePaletteAssetEditor();
			ImGui::End();
			return;
		}
		if (!m_SelectionContext || !m_SelectionContext.HasComponent<Tilemap2D>())
		{
			ImGui::TextDisabled("Select a Tilemap 2D entity to paint.");
			ImGui::End();
			return;
		}

		Entity entity = m_SelectionContext;
		Tilemap2D& tilemap = entity.GetComponent<Tilemap2D>();
		TilePaletteState& palette = m_TilePaletteStates[
			static_cast<uint64_t>(entity.GetUUID())];
		static constexpr const char* toolLabels[] = {
			"Select", "Move", "Paint", "Box", "Eyedropper", "Erase", "Fill"
		};
		for (int tool = 0; tool < 7; ++tool)
		{
			if (tool > 0 && ImGui::GetItemRectMax().x + 90.0f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
			if (ImGui::Selectable(toolLabels[tool],
				static_cast<int>(palette.Tool) == tool, 0, ImVec2(82.0f, 0.0f)))
				palette.Tool = static_cast<TilePaletteTool>(tool);
		}
		ImGui::Separator();
		ImGui::Text("Active Tilemap: %s", entity.GetName().c_str());
		ImGui::InputInt2("Cell", &palette.Coordinate.x);
		if (palette.Tool == TilePaletteTool::Box)
			ImGui::InputInt2("Box End", &palette.BoxEnd.x);
		if (palette.Tool == TilePaletteTool::Move)
			ImGui::InputInt2("Move To", &palette.MoveDestination.x);
		DrawColorField("Tint", glm::value_ptr(palette.Tint));

		std::vector<std::pair<AssetHandle, std::string>> sprites;
		if (static_cast<uint64_t>(m_ActiveTilePaletteHandle) != 0)
		{
			const AssetMetadata* paletteMetadata = AssetManager::Get().GetRegistry()
				.GetMetadata(m_ActiveTilePaletteHandle);
			ImGui::TextDisabled("Palette: %s", paletteMetadata
				? PathToUTF8(paletteMetadata->FilePath.filename()).c_str() : "Missing");
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear Palette"))
			{
				m_ActiveTilePaletteHandle = AssetHandle(0);
				m_ActiveTilePalette = {};
			}
			for (const TilePaletteEntry& tile : m_ActiveTilePalette.Tiles)
			{
				const std::string label = AnimatorSpriteLabel(tile.SpriteHandle)
					+ " [" + std::to_string(tile.Coordinate.x) + ", "
					+ std::to_string(tile.Coordinate.y) + "]";
				if (std::none_of(sprites.begin(), sprites.end(),
					[&](const auto& item) { return item.first == tile.SpriteHandle; }))
					sprites.emplace_back(tile.SpriteHandle, label);
			}
		}
		else
		{
			for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				sprites.emplace_back(builtIn.Handle, std::string(builtIn.Name));
			for (const auto& [handle, metadata] :
				AssetManager::Get().GetRegistry().GetAssets())
			{
				if (!IsSpriteAsset(metadata) || metadata.IsMissing)
					continue;
				sprites.emplace_back(handle, PathToUTF8(metadata.FilePath.filename()));
				for (const AssetSubAsset& child : metadata.SubAssets)
					if (child.Type == AssetType::Texture2D)
						sprites.emplace_back(child.Handle, child.Name);
			}
		}
		std::sort(sprites.begin(), sprites.end(), [](const auto& left, const auto& right)
		{
			return LowerASCII(left.second) < LowerASCII(right.second);
		});
		ImGui::TextDisabled("PALETTE SPRITES");
		if (ImGui::BeginChild("TilePaletteSprites", ImVec2(0.0f, 260.0f), true))
		{
			const float tileWidth = 72.0f;
			const int columns = std::max(1, static_cast<int>(
				ImGui::GetContentRegionAvail().x / (tileWidth + ImGui::GetStyle().ItemSpacing.x)));
			int column = 0;
			for (const auto& [handle, label] : sprites)
			{
				const std::string spriteID = "TilePaletteSprite##"
					+ std::to_string(static_cast<uint64_t>(handle));
				ImGui::PushID(spriteID.c_str());
				if (DrawSpritePaletteTile(handle, label.c_str(),
					palette.SpriteHandle == handle, tileWidth))
					palette.SpriteHandle = handle;
				ImGui::PopID();
				if (++column % columns != 0) ImGui::SameLine();
			}
		}
		ImGui::EndChild();

		const TilemapCell* selectedCell = Tilemap2DRuntime::FindCell(tilemap,
			palette.Coordinate);
		const int64_t boxMinX = std::min<int64_t>(palette.Coordinate.x,
			palette.BoxEnd.x);
		const int64_t boxMaxX = std::max<int64_t>(palette.Coordinate.x,
			palette.BoxEnd.x);
		const int64_t boxMinY = std::min<int64_t>(palette.Coordinate.y,
			palette.BoxEnd.y);
		const int64_t boxMaxY = std::max<int64_t>(palette.Coordinate.y,
			palette.BoxEnd.y);
		const uint64_t boxWidth = static_cast<uint64_t>(boxMaxX - boxMinX + 1);
		const uint64_t boxHeight = static_cast<uint64_t>(boxMaxY - boxMinY + 1);
		const bool boxAreaValid = boxWidth <= 65536 && boxHeight <= 65536
			&& boxWidth * boxHeight <= 65536;
		const bool missingPaintSprite = (palette.Tool == TilePaletteTool::Paint
			|| palette.Tool == TilePaletteTool::Box
			|| palette.Tool == TilePaletteTool::Fill)
			&& static_cast<uint64_t>(palette.SpriteHandle) == 0;
		ImGui::BeginDisabled(missingPaintSprite
			|| (palette.Tool == TilePaletteTool::Box && !boxAreaValid));
		if (ImGui::Button("Apply Tool", ImVec2(-1.0f, 0.0f)))
		{
			bool changed = false;
			auto paintCell = [&](glm::ivec2 coordinate)
			{
				TilemapCell cell;
				cell.Coordinate = coordinate;
				cell.SpriteHandle = palette.SpriteHandle;
				cell.Tint = palette.Tint;
				changed |= Tilemap2DRuntime::SetCell(tilemap, std::move(cell));
			};
			switch (palette.Tool)
			{
				case TilePaletteTool::Select:
				case TilePaletteTool::Eyedropper:
					if (selectedCell)
					{
						palette.SpriteHandle = selectedCell->SpriteHandle;
						palette.Tint = selectedCell->Tint;
					}
					break;
				case TilePaletteTool::Move:
					if (selectedCell)
					{
						TilemapCell moved = *selectedCell;
						changed = Tilemap2DRuntime::EraseCell(tilemap,
							palette.Coordinate);
						moved.Coordinate = palette.MoveDestination;
						changed |= Tilemap2DRuntime::SetCell(tilemap, std::move(moved));
					}
					break;
				case TilePaletteTool::Paint:
					paintCell(palette.Coordinate);
					break;
				case TilePaletteTool::Box:
				{
					for (int64_t y = boxMinY; y <= boxMaxY; ++y)
						for (int64_t x = boxMinX; x <= boxMaxX; ++x)
							paintCell({ static_cast<int>(x), static_cast<int>(y) });
					break;
				}
				case TilePaletteTool::Erase:
					changed = Tilemap2DRuntime::EraseCell(tilemap, palette.Coordinate);
					break;
				case TilePaletteTool::Fill:
				{
					const AssetHandle replaced = selectedCell
						? selectedCell->SpriteHandle : AssetHandle(0);
					if (static_cast<uint64_t>(replaced) == 0)
					{
						paintCell(palette.Coordinate);
						break;
					}
					std::vector<glm::ivec2> queue{ palette.Coordinate };
					std::unordered_set<int64_t> visited;
					auto key = [](glm::ivec2 value)
					{
						return static_cast<int64_t>((static_cast<uint64_t>(
							static_cast<uint32_t>(value.x)) << 32)
							^ static_cast<uint32_t>(value.y));
					};
					for (size_t cursor = 0; cursor < queue.size() && queue.size() < 65536; ++cursor)
					{
						const glm::ivec2 coordinate = queue[cursor];
						if (!visited.insert(key(coordinate)).second) continue;
						const TilemapCell* cell = Tilemap2DRuntime::FindCell(tilemap, coordinate);
						if (!cell || cell->SpriteHandle != replaced) continue;
						paintCell(coordinate);
						queue.push_back(coordinate + glm::ivec2(1, 0));
						queue.push_back(coordinate + glm::ivec2(-1, 0));
						queue.push_back(coordinate + glm::ivec2(0, 1));
						queue.push_back(coordinate + glm::ivec2(0, -1));
					}
					break;
				}
			}
			if (changed) MarkModified(true);
		}
		ImGui::EndDisabled();
		if (palette.Tool == TilePaletteTool::Box && !boxAreaValid)
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
				"Box is limited to 65,536 cells.");
		ImGui::TextDisabled("%zu occupied cells", tilemap.Cells.size());
		ImGui::End();
		FinishModificationGesture();
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
        m_InspectorAssetPath.clear();
        m_MultiSelection.clear();
		if (m_SelectionContext != entity)
		{
			if (m_SelectionContext && m_SelectionContext.HasComponent<SpriteRenderer>())
			{
				auto& renderer = m_SelectionContext.GetComponent<SpriteRenderer>();
				renderer.RuntimeSpriteOverrideActive = false;
				renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				const auto timeline = m_AnimationTimelineStates.find(
					static_cast<uint64_t>(m_SelectionContext.GetUUID()));
				if (timeline != m_AnimationTimelineStates.end())
					timeline->second.Playing = false;
			}
			ClearColliderEditMode();
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
			m_AnimatorRenamePopupRequested = false;
			m_AddComponentPopupRequested = false;
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
		std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", entity.GetName().c_str());
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
		if (ImGui::MenuItem(hasSelection && m_SelectionContext.HasComponent<PrefabLink>()
			? "Create Prefab Variant From Selection" : "Create Prefab From Selection", nullptr, false,
			canCreatePrefab))
			m_PrefabCreateCallback(m_SelectionContext);
		if (hasSelection && m_PrefabCreationAllowed && m_PrefabActionCallback
			&& m_SelectionContext.HasComponent<PrefabLink>())
		{
			const char* labels[] = { "Update Prefab (Keep Overrides)", "Revert All Prefab Overrides",
				"Apply All Overrides to Prefab", "Unpack Prefab", "Show Prefab Overrides in Console" };
			for (int action = 0; action < 5; ++action)
				if (ImGui::MenuItem(labels[action]))
				{
					m_PendingPrefabRoot = m_SelectionContext.GetUUID();
					m_PendingPrefabAction = action;
				}
		}
		if (ImGui::MenuItem("Delete", "Del", false, hasSelection)) DeleteSelectedEntity();
		if (hasSelection && m_Context)
		{
			const bool hiddenSelf = m_Context->IsEditorHidden(m_SelectionContext);
			const bool hiddenByParent = !hiddenSelf
				&& !m_Context->IsVisibleInEditorHierarchy(m_SelectionContext);
			if (hiddenByParent)
				ImGui::MenuItem("Hidden in Scene by Parent", nullptr, false, false);
			else if (ImGui::MenuItem(hiddenSelf ? "Show in Scene" : "Hide in Scene"))
			{
				if (m_Context->SetEditorHidden(m_SelectionContext, !hiddenSelf))
					MarkModified();
			}
		}
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
			const bool alreadyHasPrimary = m_Context
				&& m_Context->HasAuthoredPrimaryCamera();
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

			Entity entity = m_Context->CreateEntity(primitiveName);
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
				entity = m_Context->GetParent(entity);
			}
			return Entity{};
		};
		auto FindReusableRootCanvas = [&]()
		{
			// Root order is the authored Hierarchy order, so multiple eligible Canvases
			// resolve predictably. Disabled, inactive, or editor-hidden roots are skipped
			// because placing a new control there would make it appear to be missing.
			for (UUID rootID : m_Context->GetRootEntityUUIDs())
			{
				Entity root = m_Context->FindEntityByUUID(rootID);
				if (root && root.HasComponent<Canvas>()
					&& root.GetComponent<Canvas>().Enabled
					&& m_Context->IsActiveInHierarchy(root)
					&& m_Context->IsVisibleInEditorHierarchy(root))
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
					Entity canvas = m_Context->CreateEntity("Canvas");
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

			Entity element = m_Context->CreateEntity(name);
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
			m_Context->SetParent(element, uiParent);
			// Match Unity's Button hierarchy: the selectable graphic lives on the
			// Button entity and the label is a separate, stretched child. This keeps
			// the label independently editable without making it intercept clicks.
			if (addButton)
			{
				Entity label = m_Context->CreateEntity("Text");
				auto& labelRect = label.AddComponent<RectTransform>();
				labelRect.AnchorMin = { 0.0f, 0.0f };
				labelRect.AnchorMax = { 1.0f, 1.0f };
				labelRect.SizeDelta = { 0.0f, 0.0f };
				auto& labelText = label.AddComponent<UIText>();
				labelText.Text = "Button";
				labelText.Alignment = TextAlignment::Center;
				labelText.Color = { 0.1f, 0.1f, 0.1f, 1.0f };
				m_Context->SetParent(label, element);
				m_ForceOpenEntityNodes.emplace(
					static_cast<uint64_t>(element.GetUUID()));
			}
			m_ForceExpandParent = uiParent;
			m_SelectionContext = element;
			BeginRename(element);
			MarkModified(true);
            return element;
		};

		if (ImGui::BeginMenu("2D Object"))
		{
			if (ImGui::MenuItem("World Text"))
			{
				Entity text = m_Context->CreateEntity("World Text");
				text.AddComponent<TextRenderer>();
				// World Text participates in the camera/world transform pass. Keep it
				// outside a Canvas hierarchy even when a UI element is selected.
				if (FindCanvasAncestor(parentForNewObject))
				{
					m_ForceOpenSceneRoot = true;
					m_SelectionContext = text;
					BeginRename(text);
					MarkModified(true);
				}
				else
					CreateAsSelectedChild(text);
			}
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
					ImGui::SetTooltip("Open a project to create Sprites.");
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Tilemap"))
			{
				if (ImGui::MenuItem("Rectangular"))
				{
					Entity grid = m_Context->CreateEntity("Grid");
					grid.AddComponent<Grid2D>();
					if (parentForNewObject)
						m_Context->SetParent(grid, parentForNewObject);
					else
						m_ForceOpenSceneRoot = true;
					Entity tilemap = m_Context->CreateEntity("Tilemap");
					tilemap.AddComponent<Tilemap2D>();
					tilemap.AddComponent<TilemapRenderer2D>();
					m_Context->SetParent(tilemap, grid);
					m_ForceExpandParent = grid;
					m_SelectionContext = tilemap;
					BeginRename(tilemap);
					MarkModified(true);
					m_TilePaletteOpenRequested = true;
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
			if (ImGui::MenuItem("Particle System 2D"))
			{
				Entity particles = m_Context->CreateEntity("Particle System 2D");
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
					Entity light = m_Context->CreateEntity("Global Light 2D");
					auto& component = light.AddComponent<Light2D>();
					component.Type = Light2DType::Global;
					CreateAsSelectedChild(light);
				}
				if (ImGui::MenuItem("Point Light 2D"))
				{
					Entity light = m_Context->CreateEntity("Point Light 2D");
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
				Entity source = m_Context->CreateEntity("Audio Source");
				source.AddComponent<AudioSource>();
				CreateAsSelectedChild(source);
			}
			if (ImGui::MenuItem("Audio Listener"))
			{
				Entity listener = m_Context->CreateEntity("Audio Listener");
				listener.AddComponent<AudioListener>();
				CreateAsSelectedChild(listener);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("UI (Canvas)"))
		{
			if (ImGui::MenuItem("Canvas"))
			{
				Entity canvas = m_Context->CreateEntity("Canvas");
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

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		if (!entity)
			return;

        if (m_HierarchySearch[0])
        {
            const std::string query = LowerASCII(m_HierarchySearch.data());
            std::function<bool(Entity)> matches = [&](Entity candidate) {
                if (LowerASCII(candidate.GetName()).find(query) != std::string::npos) return true;
                for (UUID child : m_Context->GetChildrenUUIDs(candidate))
                    if (Entity item = m_Context->FindEntityByUUID(child); item && matches(item)) return true;
                return false;
            };
            if (!matches(entity)) return;
            ImGui::SetNextItemOpen(true);
        }
        m_HierarchyVisibleOrder.push_back(entity.GetUUID());
        auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		const bool activeInHierarchy = m_Context->IsActiveInHierarchy(entity);
		const bool hiddenSelf = m_Context->IsEditorHidden(entity);
		const bool visibleInEditor = m_Context->IsVisibleInEditorHierarchy(entity);
		const bool hiddenByParent = !hiddenSelf && !visibleInEditor;
		const bool dimmed = !activeInHierarchy || !visibleInEditor;
		if (dimmed)
		{
			const float alpha = visibleInEditor ? 1.0f : 0.68f;
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.45f, 0.45f, 0.45f, alpha));
		}

		const bool isSelected = (m_SelectionContext == entity) || std::find(m_MultiSelection.begin(),m_MultiSelection.end(),entity.GetUUID()) != m_MultiSelection.end();
		const auto children = m_Context->GetChildrenUUIDs(entity);
		const bool hasChildren = !children.empty();

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_FramePadding;
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
		DrawIcon(m_Icons, visibleInEditor ? EditorIcon::EyeOn : EditorIcon::EyeOff,
			visibilityMinimum, visibilityMaximum,
			hiddenByParent ? IM_COL32(120, 120, 120, 115)
				: hiddenSelf ? IM_COL32(155, 155, 155, 190)
				: IM_COL32(175, 175, 175, 210));
		const float entityIconX = visibilityMaximum.x + GetHierarchyIconTextGap();
		DrawIcon(m_Icons, ResolveEntityEditorIcon(entity),
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
			if (visibilityClicked && m_Context->SetEditorHidden(entity, !hiddenSelf))
				MarkModified();
		}

		if (isSelected)
			ImGui::PopStyleColor(3);

        if (!visibilityClicked && ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            if (ImGui::GetIO().KeyShift && static_cast<uint64_t>(m_SelectionAnchor) != 0)
            {
                auto first = std::find(m_PreviousHierarchyOrder.begin(), m_PreviousHierarchyOrder.end(), m_SelectionAnchor);
                auto last = std::find(m_PreviousHierarchyOrder.begin(), m_PreviousHierarchyOrder.end(), entity.GetUUID());
                if (first != m_PreviousHierarchyOrder.end() && last != m_PreviousHierarchyOrder.end())
                {
                    if (first > last) std::swap(first, last);
                    if (!ImGui::GetIO().KeyCtrl) m_MultiSelection.clear();
                    for (auto current = first; current <= last; ++current)
                        if (std::find(m_MultiSelection.begin(), m_MultiSelection.end(), *current) == m_MultiSelection.end())
                            m_MultiSelection.push_back(*current);
                }
                else { m_MultiSelection.clear(); m_SelectionAnchor = entity.GetUUID(); }
                m_SelectionContext = entity;
            }
            else if (ImGui::GetIO().KeyCtrl)
            {
                m_SelectionAnchor = entity.GetUUID();
                if(m_MultiSelection.empty() && m_SelectionContext) m_MultiSelection.push_back(m_SelectionContext.GetUUID());
                auto found=std::find(m_MultiSelection.begin(),m_MultiSelection.end(),entity.GetUUID());
                if(found==m_MultiSelection.end()) { m_MultiSelection.push_back(entity.GetUUID()); m_SelectionContext=entity; }
                else { m_MultiSelection.erase(found); m_SelectionContext=m_MultiSelection.empty()?Entity{}:m_Context->FindEntityByUUID(m_MultiSelection.back()); }
            }
            else { m_MultiSelection.clear(); m_SelectionContext=entity; m_SelectionAnchor=entity.GetUUID(); }
        }
		if (!renameActive && !visibilityHovered && rowHovered
			&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			m_SelectionContext = entity;
			m_FrameEntityRequest = entity.GetUUID();
		}
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

		if (dimmed)
			ImGui::PopStyleColor();
	}

static bool DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
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
	static void DrawProperty(const std::string& label, float columnWidth)
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

	template<typename T> static bool* GetComponentEnabledFlag(T&) { return nullptr; }
	template<> static bool* GetComponentEnabledFlag<C_Camera>(C_Camera& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<SpriteRenderer>(SpriteRenderer& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<SpriteAnimator>(SpriteAnimator& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<LineRenderer>(LineRenderer& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<Tilemap2D>(Tilemap2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<TilemapRenderer2D>(TilemapRenderer2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<ParticleSystem2D>(ParticleSystem2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<Light2D>(Light2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<Rigidbody2D>(Rigidbody2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<BoxCollider2D>(BoxCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<CircleCollider2D>(CircleCollider2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<DistanceJoint2D>(DistanceJoint2D& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<AudioSource>(AudioSource& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<AudioListener>(AudioListener& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<UIButton>(UIButton& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<UIImage>(UIImage& component) { return &component.Enabled; }
	template<> static bool* GetComponentEnabledFlag<UIText>(UIText& component) { return &component.Enabled; }

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
	static void DrawRegisteredComponent(const ComponentDescriptor& descriptor,
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

	void SceneHierarchyPanel::DrawSpriteAnimatorGraph(SpriteAnimator& animator,
		Entity entity,
		std::function<void()>& pendingMutation, bool standaloneWindow)
	{
		using namespace SpriteAnimatorAuthoring;
		constexpr size_t noTransition = static_cast<size_t>(-1);
		constexpr float nodeWidth = 160.0f;
		constexpr float nodeHeight = 62.0f;
		const uint64_t entityID = static_cast<uint64_t>(entity.GetUUID());
		AnimatorGraphEditorState& graph = m_AnimatorGraphStates[entityID];
		SynchronizeGraphLayout(animator, graph.Layout);
		auto requestRename = [this, entity, standaloneWindow](AnimatorRenameTarget target,
			size_t index, const std::string& currentName)
		{
			RequestAnimatorRename(target, entity, index, currentName,
				standaloneWindow);
		};

		if (!graph.SelectedState.empty()
			&& !FindStateIndex(animator, graph.SelectedState))
			graph.SelectedState.clear();
		if (!graph.TransitionSource.empty()
			&& !FindStateIndex(animator, graph.TransitionSource))
			graph.TransitionSource.clear();
		if (graph.SelectedTransition >= animator.Transitions.size())
			graph.SelectedTransition = noTransition;

		ImGui::BeginDisabled(animator.Clips.empty());
		if (ImGui::Button("Add State##Graph") && !pendingMutation)
		{
			AnimatorState state;
			state.Name = MakeUniqueName(animator.States, std::string("State"),
				[](const AnimatorState& item) { return item.Name; });
			state.Clip = animator.Clips.front().Name;
			animator.States.push_back(std::move(state));
			graph.SelectedState = animator.States.back().Name;
			graph.SelectedAnyState = false;
			graph.SelectedTransition = noTransition;
			SynchronizeGraphLayout(animator, graph.Layout);
			MarkModified(true);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Reset Layout"))
		{
			ResetGraphLayout(animator, graph.Layout);
			graph.PanX = 0.0f;
			graph.PanY = 0.0f;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Drag nodes | middle-drag pans | right-click connects");

		if (ImGui::BeginChild("AnimatorGraphCanvas", ImVec2(0.0f, 360.0f), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImVec2 canvasSize = ImGui::GetContentRegionAvail();
			canvasSize.x = std::max(canvasSize.x, 260.0f);
			canvasSize.y = std::max(canvasSize.y, 120.0f);
			ImGui::InvisibleButton("##AnimatorGraphBackground", canvasSize,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
			const bool canvasHovered = ImGui::IsItemHovered();
			if (ImGui::BeginPopupContextItem("Animator Canvas Menu"))
			{
				ImGui::BeginDisabled(animator.Clips.empty());
				if (ImGui::MenuItem("Create State"))
				{
					AnimatorState state;
					state.Name = MakeUniqueName(animator.States, std::string("State"),
						[](const AnimatorState& item) { return item.Name; });
					state.Clip = animator.Clips.front().Name;
					animator.States.push_back(std::move(state));
					graph.SelectedState = animator.States.back().Name;
					SynchronizeGraphLayout(animator, graph.Layout);
					MarkModified(true);
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(animator.States.empty());
				if (ImGui::MenuItem("Create Any State Transition"))
				{
					graph.TransitionSource.clear();
					graph.TransitionSourceAnyState = true;
				}
				ImGui::EndDisabled();
				ImGui::Separator();
				if (ImGui::MenuItem("Reset Layout"))
				{
					ResetGraphLayout(animator, graph.Layout);
					graph.PanX = 0.0f;
					graph.PanY = 0.0f;
				}
				ImGui::EndPopup();
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				graph.SelectedState.clear();
				graph.SelectedAnyState = false;
				graph.SelectedTransition = noTransition;
			}
			ImGui::SetItemAllowOverlap();
			if (ImGui::IsWindowHovered()
				&& ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
			{
				graph.PanX += ImGui::GetIO().MouseDelta.x;
				graph.PanY += ImGui::GetIO().MouseDelta.y;
			}

			const auto screenPosition = [&](const AnimatorGraphPosition& position)
			{
				return ImVec2(origin.x + graph.PanX + position.X,
					origin.y + graph.PanY + position.Y);
			};
			const auto add = [](const ImVec2& left, const ImVec2& right)
			{
				return ImVec2(left.x + right.x, left.y + right.y);
			};
			const auto subtract = [](const ImVec2& left, const ImVec2& right)
			{
				return ImVec2(left.x - right.x, left.y - right.y);
			};
			const auto multiply = [](const ImVec2& value, float scalar)
			{
				return ImVec2(value.x * scalar, value.y * scalar);
			};
			const auto bezierPoint = [&](const ImVec2& p0, const ImVec2& p1,
				const ImVec2& p2, const ImVec2& p3, float t)
			{
				const float inverse = 1.0f - t;
				return add(add(multiply(p0, inverse * inverse * inverse),
					multiply(p1, 3.0f * inverse * inverse * t)),
					add(multiply(p2, 3.0f * inverse * t * t),
						multiply(p3, t * t * t)));
			};

			drawList->PushClipRect(origin,
				ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);
			const ImU32 gridColor = ImGui::GetColorU32(ImVec4(0.38f, 0.40f, 0.44f, 0.22f));
			constexpr float gridSize = 32.0f;
			float gridX = std::fmod(graph.PanX, gridSize);
			float gridY = std::fmod(graph.PanY, gridSize);
			if (gridX < 0.0f) gridX += gridSize;
			if (gridY < 0.0f) gridY += gridSize;
			for (float x = gridX; x < canvasSize.x; x += gridSize)
				drawList->AddLine(ImVec2(origin.x + x, origin.y),
					ImVec2(origin.x + x, origin.y + canvasSize.y), gridColor);
			for (float y = gridY; y < canvasSize.y; y += gridSize)
				drawList->AddLine(ImVec2(origin.x, origin.y + y),
					ImVec2(origin.x + canvasSize.x, origin.y + y), gridColor);

			const AnimatorGraphPosition anyStatePosition{ 24.0f, 32.0f };
			const auto nodeTopLeft = [&](std::string_view name)
			{
				const auto found = graph.Layout.StatePositions.find(std::string(name));
				return found == graph.Layout.StatePositions.end()
					? screenPosition(AnimatorGraphPosition{})
					: screenPosition(found->second);
			};
			const auto nodeCenter = [&](std::string_view name)
			{
				const ImVec2 topLeft = nodeTopLeft(name);
				return ImVec2(topLeft.x + nodeWidth * 0.5f,
					topLeft.y + nodeHeight * 0.5f);
			};
			const ImVec2 anyCenter = add(screenPosition(anyStatePosition),
				ImVec2(nodeWidth * 0.5f, nodeHeight * 0.5f));

			// Edges are drawn before nodes. Their center handles provide a large,
			// unambiguous selection target even where multiple curves overlap.
			for (size_t transitionIndex = 0;
				transitionIndex < animator.Transitions.size(); ++transitionIndex)
			{
				const AnimatorTransition& transition = animator.Transitions[transitionIndex];
				if (!FindStateIndex(animator, transition.ToState)
					|| (!transition.AnyState
						&& !FindStateIndex(animator, transition.FromState)))
					continue;
				const ImVec2 sourceCenter = transition.AnyState ? anyCenter
					: nodeCenter(transition.FromState);
				const ImVec2 targetCenter = nodeCenter(transition.ToState);
				ImVec2 p0;
				ImVec2 p1;
				ImVec2 p2;
				ImVec2 p3;
				if (!transition.AnyState && transition.FromState == transition.ToState)
				{
					p0 = ImVec2(sourceCenter.x - 34.0f,
						sourceCenter.y - nodeHeight * 0.5f);
					p1 = ImVec2(sourceCenter.x - 70.0f, p0.y - 58.0f);
					p2 = ImVec2(sourceCenter.x + 70.0f, p0.y - 58.0f);
					p3 = ImVec2(sourceCenter.x + 34.0f, p0.y);
				}
				else
				{
					const bool forward = sourceCenter.x <= targetCenter.x;
					p0 = ImVec2(sourceCenter.x + (forward ? nodeWidth : -nodeWidth) * 0.5f,
						sourceCenter.y);
					p3 = ImVec2(targetCenter.x + (forward ? -nodeWidth : nodeWidth) * 0.5f,
						targetCenter.y);
					const float control = std::max(55.0f,
						std::abs(p3.x - p0.x) * 0.45f);
					p1 = ImVec2(p0.x + (forward ? control : -control), p0.y);
					p2 = ImVec2(p3.x + (forward ? -control : control), p3.y);
				}
				const bool selected = graph.SelectedTransition == transitionIndex;
				const ImU32 edgeColor = selected
					? ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 1.0f))
					: ImGui::GetColorU32(ImVec4(0.63f, 0.68f, 0.78f, 0.92f));
				drawList->AddBezierCubic(p0, p1, p2, p3, edgeColor,
					selected ? 3.0f : 2.0f);
				const ImVec2 marker = bezierPoint(p0, p1, p2, p3, 0.5f);
				const ImVec2 arrow = bezierPoint(p0, p1, p2, p3, 0.92f);
				const ImVec2 beforeArrow = bezierPoint(p0, p1, p2, p3, 0.87f);
				const ImVec2 direction = subtract(arrow, beforeArrow);
				const float length = std::sqrt(direction.x * direction.x
					+ direction.y * direction.y);
				if (length > 0.001f)
				{
					const ImVec2 unit(direction.x / length, direction.y / length);
					const ImVec2 side(-unit.y, unit.x);
					drawList->AddTriangleFilled(arrow,
						add(subtract(arrow, multiply(unit, 10.0f)), multiply(side, 5.0f)),
						subtract(subtract(arrow, multiply(unit, 10.0f)), multiply(side, 5.0f)),
						edgeColor);
				}
				drawList->AddCircleFilled(marker, selected ? 6.0f : 5.0f, edgeColor);

				ImGui::PushID(static_cast<int>(transitionIndex));
				ImGui::SetCursorScreenPos(ImVec2(marker.x - 10.0f, marker.y - 10.0f));
				ImGui::InvisibleButton("##TransitionEdge", ImVec2(20.0f, 20.0f),
					ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					graph.SelectedTransition = transitionIndex;
					graph.SelectedState.clear();
					graph.SelectedAnyState = false;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("Transition Edge Menu");
				if (ImGui::BeginPopup("Transition Edge Menu"))
				{
					ImGui::TextUnformatted(transition.AnyState ? "Any State" : transition.FromState.c_str());
					ImGui::SameLine();
					ImGui::Text("-> %s", transition.ToState.c_str());
					if (ImGui::MenuItem("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, entityID, transitionIndex]()
						{
							std::string error;
							if (SpriteAnimatorAuthoring::RemoveTransition(animator,
								transitionIndex, error))
							{
								auto found = m_AnimatorGraphStates.find(entityID);
								if (found != m_AnimatorGraphStates.end())
									found->second.SelectedTransition = noTransition;
								MarkModified(true);
							}
						};
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}

			if (canvasHovered && (!graph.TransitionSource.empty()
				|| graph.TransitionSourceAnyState))
			{
				const ImVec2 source = graph.TransitionSourceAnyState ? anyCenter
					: nodeCenter(graph.TransitionSource);
				drawList->AddLine(source, ImGui::GetMousePos(),
					ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 0.95f)), 2.0f);
			}

			// Any State participates as a transition source but is not runtime data.
			const ImVec2 anyTopLeft = screenPosition(anyStatePosition);
			ImGui::SetCursorScreenPos(anyTopLeft);
			ImGui::InvisibleButton("##AnyStateNode", ImVec2(nodeWidth, nodeHeight),
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			ImGui::SetItemAllowOverlap();
			const bool anyHovered = ImGui::IsItemHovered();
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				graph.SelectedAnyState = true;
				graph.SelectedState.clear();
				graph.SelectedTransition = noTransition;
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("Any State Menu");
			if (ImGui::BeginPopup("Any State Menu"))
			{
				if (ImGui::MenuItem("Make Transition"))
				{
					graph.TransitionSourceAnyState = true;
					graph.TransitionSource.clear();
				}
				ImGui::EndPopup();
			}
			const ImU32 anyFill = graph.SelectedAnyState
				? ImGui::GetColorU32(ImVec4(0.48f, 0.27f, 0.60f, 1.0f))
				: ImGui::GetColorU32(ImVec4(0.30f, 0.19f, 0.38f, 1.0f));
			drawList->AddRectFilled(anyTopLeft,
				ImVec2(anyTopLeft.x + nodeWidth, anyTopLeft.y + nodeHeight),
				anyFill, 7.0f);
			drawList->AddRect(anyTopLeft,
				ImVec2(anyTopLeft.x + nodeWidth, anyTopLeft.y + nodeHeight),
				anyHovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
					: ImGui::GetColorU32(ImGuiCol_Border), 7.0f, 0, anyHovered ? 2.0f : 1.0f);
			drawList->AddText(ImVec2(anyTopLeft.x + 12.0f, anyTopLeft.y + 12.0f),
				ImGui::GetColorU32(ImGuiCol_Text), "Any State");
			drawList->AddText(ImVec2(anyTopLeft.x + 12.0f, anyTopLeft.y + 34.0f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "global source");

			for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
			{
				const AnimatorState& state = animator.States[stateIndex];
				auto position = graph.Layout.StatePositions.find(state.Name);
				if (position == graph.Layout.StatePositions.end())
					continue;
				ImVec2 topLeft = screenPosition(position->second);
				ImGui::PushID(static_cast<int>(stateIndex));
				ImGui::SetCursorScreenPos(topLeft);
				ImGui::InvisibleButton("##StateNode", ImVec2(nodeWidth, nodeHeight),
					ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();
				const bool hovered = ImGui::IsItemHovered();
				if (ImGui::IsItemActive()
					&& ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
				{
					position->second.X += ImGui::GetIO().MouseDelta.x;
					position->second.Y += ImGui::GetIO().MouseDelta.y;
					topLeft = screenPosition(position->second);
					graph.SelectedState = state.Name;
					graph.SelectedAnyState = false;
					graph.SelectedTransition = noTransition;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					if (!graph.TransitionSource.empty()
						|| graph.TransitionSourceAnyState)
					{
						std::optional<size_t> sourceIndex;
						if (!graph.TransitionSourceAnyState)
							sourceIndex = FindStateIndex(animator,
								graph.TransitionSource);
						std::string error;
						size_t addedIndex = noTransition;
						if ((graph.TransitionSourceAnyState || sourceIndex)
							&& AddTransition(animator, sourceIndex, stateIndex,
								&addedIndex, error))
						{
							graph.SelectedTransition = addedIndex;
							MarkModified(true);
						}
						else if (!error.empty())
							TC_Core_Warn("Could not create Animator transition: {0}", error);
						graph.TransitionSource.clear();
						graph.TransitionSourceAnyState = false;
					}
					else
					{
						graph.SelectedState = state.Name;
						graph.SelectedAnyState = false;
						graph.SelectedTransition = noTransition;
					}
				}
				if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					requestRename(AnimatorRenameTarget::State, stateIndex, state.Name);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("State Node Menu");
				if (ImGui::BeginPopup("State Node Menu"))
				{
					if (ImGui::MenuItem("Make Transition"))
					{
						graph.TransitionSource = state.Name;
						graph.TransitionSourceAnyState = false;
					}
					if (ImGui::MenuItem("Set as Layer Default State", nullptr,
						animator.InitialState == state.Name))
					{
						animator.InitialState = state.Name;
						MarkModified(true);
					}
					if (ImGui::MenuItem("Rename"))
						requestRename(AnimatorRenameTarget::State, stateIndex, state.Name);
					if (ImGui::MenuItem("Delete") && !pendingMutation)
					{
						const std::string stateName = state.Name;
						pendingMutation = [this, &animator, entityID, stateName]()
						{
							const auto stateIndex = SpriteAnimatorAuthoring::FindStateIndex(
								animator, stateName);
							std::string error;
							if (stateIndex && SpriteAnimatorAuthoring::RemoveState(animator,
								*stateIndex, error))
							{
								auto found = m_AnimatorGraphStates.find(entityID);
								if (found != m_AnimatorGraphStates.end())
								{
									found->second.SelectedState.clear();
									found->second.SelectedTransition = noTransition;
								}
								MarkModified(true);
							}
						};
					}
					ImGui::EndPopup();
				}

				const bool selected = graph.SelectedState == state.Name;
				const bool initial = animator.InitialState == state.Name
					|| (animator.InitialState.empty() && stateIndex == 0);
				const ImU32 fill = initial
					? ImGui::GetColorU32(ImVec4(0.16f, 0.42f, 0.25f, 1.0f))
					: ImGui::GetColorU32(ImVec4(0.16f, 0.25f, 0.36f, 1.0f));
				const ImU32 outline = selected
					? ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 1.0f))
					: hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
						: ImGui::GetColorU32(ImGuiCol_Border);
				drawList->AddRectFilled(topLeft,
					ImVec2(topLeft.x + nodeWidth, topLeft.y + nodeHeight), fill, 7.0f);
				drawList->AddRect(topLeft,
					ImVec2(topLeft.x + nodeWidth, topLeft.y + nodeHeight), outline,
					7.0f, 0, selected || hovered ? 2.0f : 1.0f);
				drawList->AddText(ImVec2(topLeft.x + 12.0f, topLeft.y + 10.0f),
					ImGui::GetColorU32(ImGuiCol_Text), state.Name.c_str());
				const std::string clipLabel = std::string("Clip: ") + state.Clip;
				drawList->AddText(ImVec2(topLeft.x + 12.0f, topLeft.y + 34.0f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), clipLabel.c_str());
				if (initial)
					drawList->AddCircleFilled(ImVec2(topLeft.x + nodeWidth - 12.0f,
						topLeft.y + 12.0f), 4.0f,
						ImGui::GetColorU32(ImVec4(0.48f, 1.0f, 0.58f, 1.0f)));
				ImGui::PopID();
			}
			drawList->PopClipRect();
		}
		ImGui::EndChild();

		if (!graph.TransitionSource.empty() || graph.TransitionSourceAnyState)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.18f, 1.0f),
				"Select a target state for %s",
				graph.TransitionSourceAnyState ? "Any State"
					: graph.TransitionSource.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Cancel Link"))
			{
				graph.TransitionSource.clear();
				graph.TransitionSourceAnyState = false;
			}
		}

		if (!graph.SelectedState.empty())
		{
			const std::optional<size_t> selectedIndex = FindStateIndex(animator,
				graph.SelectedState);
			if (selectedIndex)
			{
				AnimatorState& selected = animator.States[*selectedIndex];
				DrawAnimatorSectionLabel(selected.Name.c_str());
				if (ImGui::SmallButton("Rename Selected"))
					requestRename(AnimatorRenameTarget::State, *selectedIndex, selected.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Make Transition"))
				{
					graph.TransitionSource = selected.Name;
					graph.TransitionSourceAnyState = false;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Set Initial"))
				{
					animator.InitialState = selected.Name;
					MarkModified(true);
				}
				if (ImGui::BeginCombo("Selected Clip", selected.Clip.c_str()))
				{
					for (const SpriteAnimationClip& clip : animator.Clips)
						if (ImGui::Selectable(clip.Name.c_str(),
							selected.Clip == clip.Name))
						{
							selected.Clip = clip.Name;
							MarkModified(true);
						}
					ImGui::EndCombo();
				}
			}
		}
		else if (graph.SelectedTransition < animator.Transitions.size())
		{
			const size_t transitionIndex = graph.SelectedTransition;
			AnimatorTransition& transition = animator.Transitions[transitionIndex];
			DrawAnimatorSectionLabel("Selected Transition");
			const char* sourcePreview = transition.AnyState
				? "Any State" : transition.FromState.c_str();
			if (ImGui::BeginCombo("Graph Source", sourcePreview))
			{
				const auto target = FindStateIndex(animator, transition.ToState);
				if (target && ImGui::Selectable("Any State", transition.AnyState))
				{
					std::string error;
					if (SetTransitionEndpoints(animator, transitionIndex, std::nullopt,
						*target, error))
						MarkModified(true);
				}
				for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
				{
					const AnimatorState& state = animator.States[stateIndex];
					if (target && ImGui::Selectable(state.Name.c_str(),
						!transition.AnyState && transition.FromState == state.Name))
					{
						std::string error;
						if (SetTransitionEndpoints(animator, transitionIndex, stateIndex,
							*target, error))
							MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("Graph Target", transition.ToState.c_str()))
			{
				std::optional<size_t> source;
				if (!transition.AnyState)
					source = FindStateIndex(animator, transition.FromState);
				for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
				{
					const AnimatorState& state = animator.States[stateIndex];
					if ((transition.AnyState || source)
						&& ImGui::Selectable(state.Name.c_str(),
							transition.ToState == state.Name))
					{
						std::string error;
						if (SetTransitionEndpoints(animator, transitionIndex, source,
							stateIndex, error))
							MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
			bool hasExitTime = transition.ExitTime >= 0.0f;
			ImGui::BeginDisabled(hasExitTime && transition.Conditions.empty());
			if (ImGui::Checkbox("Graph Has Exit Time", &hasExitTime))
			{
				transition.ExitTime = hasExitTime ? 1.0f : -1.0f;
				MarkModified(true);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove Selected Transition") && !pendingMutation)
			{
				pendingMutation = [this, &animator, entityID, transitionIndex]()
				{
					std::string error;
					if (SpriteAnimatorAuthoring::RemoveTransition(animator,
						transitionIndex, error))
					{
						auto found = m_AnimatorGraphStates.find(entityID);
						if (found != m_AnimatorGraphStates.end())
							found->second.SelectedTransition = noTransition;
						MarkModified(true);
					}
				};
			}
			ImGui::TextDisabled("Conditions and priority remain editable in Transitions below.");
		}
	}

	void SceneHierarchyPanel::RequestAnimatorRename(AnimatorRenameTarget target,
		Entity entity, size_t index, const std::string& currentName,
		bool graphWindow)
	{
		m_AnimatorRenameTarget = target;
		m_AnimatorRenameEntity = entity.GetUUID();
		m_AnimatorRenameIndex = index;
		std::snprintf(m_AnimatorRenameBuffer.data(),
			m_AnimatorRenameBuffer.size(), "%s", currentName.c_str());
		m_AnimatorRenameError.clear();
		m_AnimatorRenamePopupGraphOwner = graphWindow;
		m_AnimatorRenamePopupRequested = true;
	}

	void SceneHierarchyPanel::DrawAnimatorRenamePopup(SpriteAnimator& animator,
		Entity entity, bool graphWindow)
	{
		using namespace SpriteAnimatorAuthoring;
		if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
			&& m_AnimatorRenamePopupGraphOwner != graphWindow)
			return;
		const bool requestPopup = m_AnimatorRenamePopupRequested;
		if (requestPopup)
		{
			ImGui::OpenPopup("Rename Animator Item");
			m_AnimatorRenamePopupRequested = false;
		}
		bool renamePopupOpen = true;
		PrepareEditorPopup("Rename Animator Item",520,true);
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
					{
						const std::string oldName = m_AnimatorRenameIndex
							< animator.States.size()
							? animator.States[m_AnimatorRenameIndex].Name : std::string{};
						renamed = RenameState(animator, m_AnimatorRenameIndex,
							candidate, error);
						if (renamed)
						{
							auto graph = m_AnimatorGraphStates.find(
								static_cast<uint64_t>(entity.GetUUID()));
							if (graph != m_AnimatorGraphStates.end())
							{
								RenameGraphState(graph->second.Layout, oldName, candidate);
								if (graph->second.SelectedState == oldName)
									graph->second.SelectedState = candidate;
								if (graph->second.TransitionSource == oldName)
									graph->second.TransitionSource = candidate;
							}
						}
						break;
					}
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
			&& !requestPopup && !ImGui::IsPopupOpen("Rename Animator Item"))
		{
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
			m_AnimatorRenamePopupRequested = false;
		}
	}

	void SceneHierarchyPanel::DismissAnimatorRenamePopup(bool graphWindow)
	{
		if (m_AnimatorRenamePopupGraphOwner != graphWindow)
			return;
		m_AnimatorRenamePopupRequested = false;
		PrepareEditorPopup("Rename Animator Item",520,true);
        if (ImGui::BeginPopupModal("Rename Animator Item", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		m_AnimatorRenameEntity = UUID(0);
		m_AnimatorRenameError.clear();
	}

	void SceneHierarchyPanel::ApplyAnimatorPendingMutation(Entity entity,
		std::function<void()>& pendingMutation)
	{
		if (!pendingMutation)
			return;
		pendingMutation();
		// Deferred list edits may reorder or erase transitions. Clear the graph's
		// index-based edge selection so the next click cannot edit a different
		// transition that moved into the old slot.
		auto graph = m_AnimatorGraphStates.find(
			static_cast<uint64_t>(entity.GetUUID()));
		if (graph != m_AnimatorGraphStates.end())
			graph->second.SelectedTransition = static_cast<size_t>(-1);
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
		auto requestRename = [this, entity](AnimatorRenameTarget target, size_t index,
			const std::string& currentName)
		{
			RequestAnimatorRename(target, entity, index, currentName, false);
		};

		const ComponentDescriptor* animatorDescriptor =
			ComponentRegistry::Get().Find(UUID(ComponentIds::SpriteAnimator));
		const PropertyDescriptor* controllerProperty = nullptr;
		if (animatorDescriptor)
		{
			const auto found = std::find_if(animatorDescriptor->Properties.begin(),
				animatorDescriptor->Properties.end(), [](const PropertyDescriptor& property)
				{
					return property.PropertyId
						== UUID(ComponentIds::SpriteAnimatorProperties::Controller);
				});
			if (found != animatorDescriptor->Properties.end())
				controllerProperty = &*found;
		}
		if (controllerProperty && controllerProperty->AssetReference)
		{
			AssetHandle controller = animator.ControllerHandle;
			if (DrawRegisteredAssetReference(*controllerProperty, controller,
				&m_AssetRevealCallback)
				&& controller != animator.ControllerHandle)
			{
				std::string error;
				if (controllerProperty->Set(entity,
					static_cast<uint64_t>(controller), error))
					MarkModified(true);
				else
					TC_Core_Warn("Could not assign Animator Controller: {0}", error);
			}
		}
		const bool usesExternalController =
			static_cast<uint64_t>(animator.ControllerHandle) != 0;
		if (!usesExternalController)
			ImGui::TextDisabled("Embedded controller: graph and clips are stored with this entity.");
		else
			ImGui::TextDisabled("External controller and Animation Clips are loaded when Play starts.");
		if (usesExternalController)
		{
			if (ImGui::Button("Open Controller"))
				OpenAuthoringAsset(animator.ControllerHandle,
					AssetType::AnimatorController);
		}
		else
		{
			if (ImGui::Button("Open Animation")) m_AnimationOpenRequested = true;
			ImGui::SameLine();
			if (ImGui::Button("Open Animator")) m_AnimatorGraphOpenRequested = true;
		}
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
		if (usesExternalController)
		{
			ImGui::TextWrapped("Controller parameters, states, transitions, and clips "
				"are edited in the external asset. The embedded snapshot is read-only.");
			return;
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
		if (ImGui::TreeNodeEx("Animator Graph", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSpriteAnimatorGraph(animator, entity, pendingMutation);
			ImGui::TreePop();
		}

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

		DrawAnimatorRenamePopup(animator, entity, false);

		ApplyAnimatorPendingMutation(entity, pendingMutation);
	}

	void SceneHierarchyPanel::DrawGrid2DInspector(Grid2D& grid)
	{
		bool changed = false;
		glm::vec2 cellSize = grid.CellSize;
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(cellSize), 0.05f,
			0.0001f, 0.0f, "%.3f"))
		{
			cellSize.x = std::max(0.0001f, std::isfinite(cellSize.x)
				? cellSize.x : grid.CellSize.x);
			cellSize.y = std::max(0.0001f, std::isfinite(cellSize.y)
				? cellSize.y : grid.CellSize.y);
			grid.CellSize = cellSize;
			changed = true;
		}
		glm::vec2 cellGap = grid.CellGap;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(cellGap), 0.01f)
			&& std::isfinite(cellGap.x) && std::isfinite(cellGap.y))
		{
			grid.CellGap = cellGap;
			changed = true;
		}
		const char* layouts[] = {
			"Rectangle", "Isometric", "Isometric Z As Y", "Hexagon"
		};
		int layoutIndex = std::clamp(static_cast<int>(grid.Layout), 0, 3);
		if (ImGui::Combo("Cell Layout", &layoutIndex, layouts,
			static_cast<int>(std::size(layouts))))
		{
			grid.Layout = static_cast<GridCellLayout2D>(layoutIndex);
			changed = true;
		}
		const char* swizzles[] = { "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX" };
		int swizzle = std::clamp(static_cast<int>(grid.Swizzle), 0, 5);
		if (ImGui::Combo("Cell Swizzle", &swizzle, swizzles,
			static_cast<int>(std::size(swizzles))))
		{
			grid.Swizzle = static_cast<GridCellSwizzle2D>(swizzle);
			changed = true;
		}
		if (changed)
			MarkModified();
	}

	void SceneHierarchyPanel::DrawTilemap2DInspector(Tilemap2D& tilemap,
		Entity entity)
	{
		if (ImGui::Button("Open Tile Palette", ImVec2(-1.0f, 0.0f)))
			m_TilePaletteOpenRequested = true;
		bool gridChanged = false;
		glm::vec2 cellSize = tilemap.CellSize;
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(cellSize), 0.05f,
			0.0001f, 0.0f, "%.3f"))
		{
			if (!std::isfinite(cellSize.x) || !std::isfinite(cellSize.y))
				cellSize = tilemap.CellSize;
			cellSize.x = std::max(cellSize.x, 0.0001f);
			cellSize.y = std::max(cellSize.y, 0.0001f);
			if (cellSize != tilemap.CellSize)
			{
				tilemap.CellSize = cellSize;
				gridChanged = true;
			}
		}
		glm::vec2 cellGap = tilemap.CellGap;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(cellGap), 0.01f))
		{
			if (std::isfinite(cellGap.x) && std::isfinite(cellGap.y)
				&& cellGap != tilemap.CellGap)
			{
				tilemap.CellGap = cellGap;
				gridChanged = true;
			}
		}
		if (entity.HasComponent<TilemapRenderer2D>())
			ImGui::TextDisabled("Sorting is configured by Tilemap Renderer 2D.");
		else
		{
			gridChanged |= ImGui::InputInt("Sorting Layer", &tilemap.SortingLayer);
			gridChanged |= ImGui::InputInt("Order In Layer", &tilemap.OrderInLayer);
		}
		if (gridChanged)
			MarkModified();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("PAINT BRUSH");
		TilemapBrushState& brush = m_TilemapBrushStates[
			static_cast<uint64_t>(entity.GetUUID())];
		ImGui::InputInt2("Coordinate", &brush.Coordinate.x);
		ImGui::TextUnformatted("Palette Sprite");
		ImGui::SetNextItemWidth(-1.0f);
		DrawAnimatorSpriteField("TilemapBrushSprite", brush.SpriteHandle);
		DrawColorField("Tint", glm::value_ptr(brush.Tint));
		ImGui::Checkbox("Flip X", &brush.FlipX);
		ImGui::SameLine();
		ImGui::Checkbox("Flip Y", &brush.FlipY);
		const char* rotations[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
		int brushRotation = std::clamp(brush.RotationQuarterTurns, 0, 3);
		if (ImGui::Combo("Rotation", &brushRotation, rotations, 4))
			brush.RotationQuarterTurns = brushRotation;

		const TilemapCell* occupied = Tilemap2DRuntime::FindCell(tilemap,
			brush.Coordinate);
		ImGui::BeginDisabled(static_cast<uint64_t>(brush.SpriteHandle) == 0);
		if (ImGui::Button(occupied ? "Update Cell" : "Paint Cell"))
		{
			TilemapCell cell;
			cell.Coordinate = brush.Coordinate;
			cell.SpriteHandle = brush.SpriteHandle;
			cell.Tint = brush.Tint;
			cell.FlipX = brush.FlipX;
			cell.FlipY = brush.FlipY;
			cell.RotationQuarterTurns = brush.RotationQuarterTurns;
			if (Tilemap2DRuntime::SetCell(tilemap, std::move(cell)))
				MarkModified(true);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!occupied);
		if (ImGui::Button("Erase"))
		{
			if (Tilemap2DRuntime::EraseCell(tilemap, brush.Coordinate))
				MarkModified(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Pick Cell") && occupied)
		{
			brush.SpriteHandle = occupied->SpriteHandle;
			brush.Tint = occupied->Tint;
			brush.FlipX = occupied->FlipX;
			brush.FlipY = occupied->FlipY;
			brush.RotationQuarterTurns = occupied->RotationQuarterTurns;
		}
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("CELLS (%zu)", tilemap.Cells.size());
		std::optional<TilemapCell> editedCell;
		std::optional<glm::ivec2> removedCoordinate;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH
			| ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg
			| ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
		if (ImGui::BeginTable("##TilemapCells", 7, tableFlags,
			ImVec2(0.0f, std::min(260.0f,
				74.0f + static_cast<float>(tilemap.Cells.size()) * 27.0f))))
		{
			ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("Sprite", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Tint", ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Rot", ImGuiTableColumnFlags_WidthFixed, 66.0f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableHeadersRow();
			for (size_t cellIndex = 0; cellIndex < tilemap.Cells.size(); ++cellIndex)
			{
				const TilemapCell& source = tilemap.Cells[cellIndex];
				TilemapCell candidate = source;
				bool changed = false;
				ImGui::PushID(static_cast<int>(cellIndex));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%d, %d", source.Coordinate.x, source.Coordinate.y);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= DrawAnimatorSpriteField("CellSprite", candidate.SpriteHandle);
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= DrawColorField("##CellTint", glm::value_ptr(candidate.Tint),
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
				ImGui::TableSetColumnIndex(3);
				changed |= ImGui::Checkbox("##FlipX", &candidate.FlipX);
				ImGui::TableSetColumnIndex(4);
				changed |= ImGui::Checkbox("##FlipY", &candidate.FlipY);
				ImGui::TableSetColumnIndex(5);
				int rotation = std::clamp(candidate.RotationQuarterTurns, 0, 3);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##Rotation", &rotation, rotations, 4))
				{
					candidate.RotationQuarterTurns = rotation;
					changed = true;
				}
				ImGui::TableSetColumnIndex(6);
				if (ImGui::SmallButton("x"))
					removedCoordinate = source.Coordinate;
				if (changed)
					editedCell = std::move(candidate);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (editedCell && Tilemap2DRuntime::SetCell(tilemap, std::move(*editedCell)))
			MarkModified(true);
		if (removedCoordinate
			&& Tilemap2DRuntime::EraseCell(tilemap, *removedCoordinate))
			MarkModified(true);
		ImGui::BeginDisabled(tilemap.Cells.empty());
		if (ImGui::Button("Clear All Cells", ImVec2(-1.0f, 0.0f)))
		{
			tilemap.Cells.clear();
			MarkModified(true);
		}
		ImGui::EndDisabled();
	}

	void SceneHierarchyPanel::DrawTilemapRenderer2DInspector(
		TilemapRenderer2D& renderer)
	{
		bool changed = false;
		const char* sortOrders[] = {
			"Bottom Left", "Bottom Right", "Top Left", "Top Right"
		};
		int sortOrder = std::clamp(static_cast<int>(renderer.SortOrder), 0, 3);
		if (ImGui::Combo("Sort Order", &sortOrder, sortOrders,
			static_cast<int>(std::size(sortOrders))))
		{
			renderer.SortOrder = static_cast<TilemapSortOrder2D>(sortOrder);
			changed = true;
		}
		changed |= ImGui::InputInt("Sorting Layer", &renderer.SortingLayer);
		changed |= ImGui::InputInt("Order In Layer", &renderer.OrderInLayer);

		ImGui::BeginDisabled();
		int mode = static_cast<int>(renderer.Mode);
		const char* modes[] = { "Chunk", "Individual" };
		ImGui::Combo("Mode", &mode, modes, static_cast<int>(std::size(modes)));
		int culling = static_cast<int>(renderer.DetectChunkCulling);
		const char* cullingModes[] = { "Auto", "Manual" };
		ImGui::Combo("Detect Chunk Culling", &culling, cullingModes,
			static_cast<int>(std::size(cullingModes)));
		const std::string material = static_cast<uint64_t>(renderer.MaterialHandle) == 0
			? "None" : "Material #" + std::to_string(
				static_cast<uint64_t>(renderer.MaterialHandle));
		ImGui::Button((material + "###TilemapMaterial").c_str(), ImVec2(-1.0f, 0.0f));
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("renderer backend support pending");
		ImGui::TextDisabled("Mode, chunk culling, and Material: renderer backend support pending");
		if (changed)
			MarkModified();
	}

	void SceneHierarchyPanel::DrawParticleSystem2DInspector(
		ParticleSystem2D& system)
	{
		ImGui::TextDisabled("PREVIEW");
		if (ImGui::Button(system.RuntimePlaying ? "Restart" : "Play"))
			ParticleSystem2DRuntime::Play(system, true);
		ImGui::SameLine();
		ImGui::BeginDisabled(!system.RuntimePlaying);
		if (ImGui::Button("Stop"))
			ParticleSystem2DRuntime::Stop(system, false);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(system.RuntimeParticles.empty());
		if (ImGui::Button("Clear"))
			ParticleSystem2DRuntime::Stop(system, true);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu / %d", system.RuntimeParticles.size(),
			std::max(system.MaxParticles, 0));

		bool changed = false;
		changed |= ImGui::Checkbox("Play On Start", &system.PlayOnStart);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Loop", &system.Loop);
		float duration = system.Duration;
		if (ImGui::DragFloat("Duration", &duration, 0.05f, 0.0f, 0.0f, "%.2f s"))
		{
			if (std::isfinite(duration))
			{
				system.Duration = std::max(duration, 0.0f);
				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("EMISSION");
		float emissionRate = system.EmissionRate;
		if (ImGui::DragFloat("Rate", &emissionRate, 0.1f, 0.0f, 0.0f, "%.1f / s"))
		{
			if (std::isfinite(emissionRate))
			{
				system.EmissionRate = std::max(emissionRate, 0.0f);
				changed = true;
			}
		}
		int maxParticles = system.MaxParticles;
		if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 0, 100000))
		{
			system.MaxParticles = std::clamp(maxParticles, 0, 100000);
			if (system.RuntimeParticles.size()
				> static_cast<size_t>(system.MaxParticles))
				system.RuntimeParticles.resize(static_cast<size_t>(system.MaxParticles));
			changed = true;
		}
		float lifetime = system.StartLifetime;
		if (ImGui::DragFloat("Lifetime", &lifetime, 0.02f, 0.0001f, 0.0f, "%.2f s"))
		{
			if (std::isfinite(lifetime))
			{
				system.StartLifetime = std::max(lifetime, 0.0001f);
				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("SHAPE AND MOTION");
		float speed = system.StartSpeed;
		if (ImGui::DragFloat("Speed", &speed, 0.02f, 0.0f, 0.0f))
		{
			if (std::isfinite(speed))
			{
				system.StartSpeed = std::max(speed, 0.0f);
				changed = true;
			}
		}
		glm::vec2 direction = system.Direction;
		if (ImGui::DragFloat2("Direction", glm::value_ptr(direction), 0.02f)
			&& std::isfinite(direction.x) && std::isfinite(direction.y))
		{
			system.Direction = direction;
			changed = true;
		}
		float spread = system.SpreadDegrees;
		if (ImGui::SliderFloat("Spread", &spread, 0.0f, 360.0f, "%.0f deg"))
		{
			system.SpreadDegrees = spread;
			changed = true;
		}
		float gravity = system.GravityScale;
		if (ImGui::DragFloat("Gravity Scale", &gravity, 0.02f)
			&& std::isfinite(gravity))
		{
			system.GravityScale = gravity;
			changed = true;
		}

		ImGui::Separator();
		ImGui::TextDisabled("APPEARANCE");
		float startSize = system.StartSize;
		if (ImGui::DragFloat("Start Size", &startSize, 0.01f, 0.0f, 0.0f)
			&& std::isfinite(startSize))
		{
			system.StartSize = std::max(startSize, 0.0f);
			changed = true;
		}
		float endSize = system.EndSize;
		if (ImGui::DragFloat("End Size", &endSize, 0.01f, 0.0f, 0.0f)
			&& std::isfinite(endSize))
		{
			system.EndSize = std::max(endSize, 0.0f);
			changed = true;
		}
		changed |= DrawColorField("Start Color", glm::value_ptr(system.StartColor));
		changed |= DrawColorField("End Color", glm::value_ptr(system.EndColor));
		ImGui::TextUnformatted("Sprite");
		ImGui::SetNextItemWidth(-1.0f);
		changed |= DrawAnimatorSpriteField("ParticleSprite", system.SpriteHandle);
		changed |= ImGui::InputInt("Sorting Layer", &system.SortingLayer);
		changed |= ImGui::InputInt("Order In Layer", &system.OrderInLayer);
		uint32_t seed = system.Seed;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &seed))
		{
			system.Seed = seed;
			changed = true;
		}
		if (changed)
		{
			ParticleSystem2DRuntime::Reset(system);
			MarkModified();
		}
	}

	void SceneHierarchyPanel::DrawLight2DInspector(Light2D& light)
	{
		bool changed = false;
		const char* lightTypes[] = { "Global", "Point" };
		int lightType = static_cast<int>(light.Type);
		if (ImGui::Combo("Type", &lightType, lightTypes, 2))
		{
			light.Type = static_cast<Light2DType>(lightType);
			changed = true;
		}
		changed |= DrawColorField("Color", glm::value_ptr(light.Color));
		float intensity = light.Intensity;
		if (ImGui::DragFloat("Intensity", &intensity, 0.02f, 0.0f, 0.0f)
			&& std::isfinite(intensity))
		{
			light.Intensity = std::max(intensity, 0.0f);
			changed = true;
		}
		ImGui::BeginDisabled(light.Type == Light2DType::Global);
		float radius = light.Radius;
		if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.0001f, 0.0f)
			&& std::isfinite(radius))
		{
			light.Radius = std::max(radius, 0.0001f);
			changed = true;
		}
		float falloff = light.Falloff;
		if (ImGui::DragFloat("Falloff", &falloff, 0.02f, 0.0001f, 0.0f)
			&& std::isfinite(falloff))
		{
			light.Falloff = std::max(falloff, 0.0001f);
			changed = true;
		}
		ImGui::EndDisabled();
		if (light.Type == Light2DType::Global)
			ImGui::TextDisabled("Radius and falloff apply to Point lights.");
		if (changed)
			MarkModified();
	}

	void SceneHierarchyPanel::DrawUIButtonInspector(UIButton& button, Entity entity)
	{
		bool changed = ImGui::Checkbox("Interactable", &button.Interactable);
		int transition = 0;
		const char* transitions[] = { "Color Tint" };
		ImGui::BeginDisabled();
		ImGui::Combo("Transition", &transition, transitions,
			static_cast<int>(std::size(transitions)));
		const std::string targetGraphic = entity.HasComponent<UIImage>()
			? entity.GetName() + " (Image)" : "None (Image)";
		std::array<char, 256> targetGraphicBuffer{};
		std::copy_n(targetGraphic.data(),
			std::min(targetGraphic.size(), targetGraphicBuffer.size() - 1),
			targetGraphicBuffer.data());
		ImGui::InputText("Target Graphic", targetGraphicBuffer.data(),
			targetGraphicBuffer.size(), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
		changed |= DrawColorField("Normal Color", glm::value_ptr(button.NormalColor));
		changed |= DrawColorField("Highlighted Color", glm::value_ptr(button.HoverColor));
		changed |= DrawColorField("Pressed Color", glm::value_ptr(button.PressedColor));
		changed |= DrawColorField("Selected Color", glm::value_ptr(button.SelectedColor));
		changed |= DrawColorField("Disabled Color", glm::value_ptr(button.DisabledColor));
		changed |= ImGui::SliderFloat("Color Multiplier", &button.ColorMultiplier,
			0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (changed) MarkModified();
        if(ImGui::TreeNode("Preview states"))
        {
            const char* names[]={"Normal","Hover","Pressed","Selected","Disabled"};
            const glm::vec4 colors[]={button.NormalColor,button.HoverColor,button.PressedColor,button.SelectedColor,button.DisabledColor};
            for(int i=0;i<5;++i)
            {
                const auto color=colors[i]*button.ColorMultiplier;
                ImGui::ColorButton(names[i],ImVec4(color.x,color.y,color.z,colors[i].a),0,ImVec2(48,24));
                ImGui::SameLine(); ImGui::TextUnformatted(names[i]);
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("On Click ()");
		std::optional<size_t> removeIndex;
		for (size_t index = 0; index < button.OnClick.size(); ++index)
		{
			auto& listener = button.OnClick[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::BeginGroup();
			if (ImGui::Checkbox("##Enabled", &listener.Enabled))
				MarkModified();
			ImGui::SameLine();

			Entity target;
			if (static_cast<uint64_t>(listener.TargetEntity) != 0 && m_Context)
				target = m_Context->FindEntityByUUID(listener.TargetEntity);
			const std::string targetLabel = target
				? target.GetName() + " (Entity)"
				: (static_cast<uint64_t>(listener.TargetEntity) == 0
					? "None (Entity)" : "Missing Entity");
			ImGui::SetNextItemWidth(-32.0f);
			if (ImGui::Button(targetLabel.c_str(), ImVec2(-32.0f, 0.0f)))
				ImGui::OpenPopup("OnClickTargetPicker");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID))
				{
					Entity dropped = GetDraggedSceneEntity(payload, m_Context);
					if (dropped)
					{
						listener.TargetEntity = dropped.GetUUID();
						listener.TargetAttachmentID = UUID(0);
						listener.ScriptAsset = AssetHandle(0);
						listener.MethodName.clear();
						MarkModified();
					}
				}
				ImGui::EndDragDropTarget();
			}
			PrepareEditorPopup("OnClickTargetPicker",440);
        if (ImGui::BeginPopup("OnClickTargetPicker"))
			{
				if (ImGui::MenuItem("This Entity"))
				{
					listener.TargetEntity = entity.GetUUID();
					listener.TargetAttachmentID = UUID(0);
					listener.ScriptAsset = AssetHandle(0);
					listener.MethodName.clear();
					MarkModified();
				}
				ImGui::TextDisabled("Or drag an entity from Hierarchy");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				listener.TargetEntity = UUID(0);
				listener.TargetAttachmentID = UUID(0);
				listener.ScriptAsset = AssetHandle(0);
				listener.MethodName.clear();
				MarkModified();
				target = {};
			}

			std::string selectedMethod = "No Function";
			if (!listener.MethodName.empty())
			{
				selectedMethod = "Missing: " + listener.MethodName;
				if (target && target.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						target.GetComponent<CSharpScripts>().Scripts)
					{
						if (script.AttachmentID != listener.TargetAttachmentID)
							continue;
						std::string className = script.LastKnownClassName;
						if (m_ScriptMetadataProvider)
						{
							const auto metadata = m_ScriptMetadataProvider(script.ScriptAsset);
							if (metadata && !metadata->TypeName.empty())
								className = metadata->TypeName;
						}
						selectedMethod = (className.empty() ? "Script" : className)
							+ "." + listener.MethodName;
						break;
					}
				}
			}

			ImGui::BeginDisabled(!target);
			if (ImGui::BeginCombo("##OnClickMethod", selectedMethod.c_str()))
			{
				if (ImGui::Selectable("No Function", listener.MethodName.empty()))
				{
					listener.TargetAttachmentID = UUID(0);
					listener.ScriptAsset = AssetHandle(0);
					listener.MethodName.clear();
					MarkModified();
				}
				if (target && target.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						target.GetComponent<CSharpScripts>().Scripts)
					{
						const auto metadata = m_ScriptMetadataProvider
							? m_ScriptMetadataProvider(script.ScriptAsset)
							: std::optional<EditorScriptMetadata>{};
						if (!metadata || metadata->EventMethods.empty())
							continue;
						const std::string className = metadata->TypeName.empty()
							? script.LastKnownClassName : metadata->TypeName;
						for (const std::string& method : metadata->EventMethods)
						{
							const std::string label = (className.empty() ? "Script" : className)
								+ "/" + method;
							const bool selected = listener.TargetAttachmentID
								== script.AttachmentID && listener.MethodName == method;
							if (ImGui::Selectable(label.c_str(), selected))
							{
								listener.TargetAttachmentID = script.AttachmentID;
								listener.ScriptAsset = script.ScriptAsset;
								listener.MethodName = method;
								MarkModified();
							}
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
				removeIndex = index;
			ImGui::EndGroup();
			ImGui::PopID();
		}
		if (removeIndex)
		{
			button.OnClick.erase(button.OnClick.begin()
				+ static_cast<std::ptrdiff_t>(*removeIndex));
			MarkModified();
		}
		if (ImGui::Button("+", ImVec2(-1.0f, 0.0f)))
		{
			button.OnClick.emplace_back();
			MarkModified();
		}
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


    void SceneHierarchyPanel::DrawMultiSelectionInspector()
    {
        std::vector<Entity> entities;
        for(UUID id:m_MultiSelection) if(Entity e=m_Context->FindEntityByUUID(id)) entities.push_back(e);
        if(entities.empty()) return;
        ImGui::Text("%zu objects selected",entities.size());
        ImGui::TextWrapped("Ctrl-click to change selection. Editing a common property replaces that value on every selected object. Mixed values are marked with a dash.");
        for(const auto& descriptor:ComponentRegistry::Get().GetDescriptors())
        {
            if(!descriptor.InspectorVisible || descriptor.Properties.empty()) continue;
            bool common=true;
            for(Entity e:entities) if(!descriptor.Has(e)) { common=false; break; }
            if(common) DrawRegisteredComponent(descriptor,entities.front(),[this](){MarkModified();},m_ColliderEditingAllowed,&entities);
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
			std::snprintf(m_NameEditBuffer, sizeof(m_NameEditBuffer), "%s", tag.c_str());
		}

	    if (entity.HasComponent<PrefabLink>())
    {
        const auto& link = entity.GetComponent<PrefabLink>();
        const auto* metadata = AssetManager::Get().GetRegistry().GetMetadata(link.Source);
        ImGui::Separator();
        ImGui::TextWrapped("Prefab: %s", metadata ? PathToUTF8(metadata->FilePath).c_str() : "Missing source");
        if (metadata && ImGui::SmallButton("Select source") && m_AssetRevealCallback) m_AssetRevealCallback(link.Source);
        if (ImGui::TreeNode("Overrides"))
        {
            static UUID cachedRoot{0}; static Scene* cachedScene=nullptr; static double refreshed=0;
            static std::vector<std::string> paths;
            static std::vector<PrefabPropertyOverride> properties;
            static std::string error;
            if(cachedRoot!=entity.GetUUID() || cachedScene!=m_Context.get() || (m_PrefabOverridesDirty && ImGui::GetTime()-refreshed>0.5))
            {
                cachedRoot=entity.GetUUID(); cachedScene=m_Context.get(); refreshed=ImGui::GetTime(); m_PrefabOverridesDirty=false;
                PrefabLinkedInstance::GetOverridePaths(m_Context,entity.GetUUID(),paths,error);
                if(error.empty()) PrefabLinkedInstance::GetPropertyOverrides(m_Context,entity.GetUUID(),properties,error);
            }
            if(ImGui::SmallButton("Refresh differences")) m_PrefabOverridesDirty=true;
            if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());
            if(paths.empty()) ImGui::TextDisabled("No overrides. This instance matches its source.");
            else
            {
                ImGui::Text("%zu changed paths",paths.size());
                auto text=[](const PropertyValue& value) {
                    return std::visit([](const auto& item)->std::string {
                        using T=std::decay_t<decltype(item)>;
                        if constexpr(std::is_same_v<T,std::string>) return item;
                        else if constexpr(std::is_same_v<T,bool>) return item?"true":"false";
                        else if constexpr(std::is_arithmetic_v<T>) return std::to_string(item);
                        else { std::string result; for(int i=0;i<item.length();++i) { if(i) result+=", "; result+=std::to_string(item[i]); } return result; }
                    },value);
                };
                ImGui::BeginChild("PropertyOverrides",ImVec2(0,230),true);
                for(size_t i=0;i<properties.size();++i)
                {
                    const auto& item=properties[i]; ImGui::PushID(static_cast<int>(i));
                    ImGui::TextWrapped("%s",item.Path.c_str());
                    ImGui::TextWrapped("Source: %s",text(item.Before).c_str());
                    ImGui::TextWrapped("Instance: %s",text(item.After).c_str());
                    ImGui::BeginDisabled(!m_PrefabCreationAllowed || !m_PrefabActionCallback);
                    auto queue=[&](int action) { m_PendingPrefabRoot=entity.GetUUID(); m_PendingPrefabAction=action; m_PendingPrefabEntity=item.EntityID; m_PendingPrefabComponent=item.ComponentID; m_PendingPrefabProperty=item.PropertyID; };
                    if(ImGui::SmallButton("Apply property")) queue(5);
                    ImGui::SameLine(); if(ImGui::SmallButton("Revert property")) queue(6);
                    ImGui::EndDisabled(); ImGui::Separator(); ImGui::PopID();
                }
                if(ImGui::TreeNode("All changes (including structure)")) { for(const auto& path:paths) ImGui::TextWrapped("%s",path.c_str()); ImGui::TreePop(); }
                ImGui::EndChild();
            }
            ImGui::BeginDisabled(!m_PrefabCreationAllowed || !m_PrefabActionCallback);
            if (ImGui::Button("Update")) { m_PendingPrefabRoot = entity.GetUUID(); m_PendingPrefabAction = 0; }
            ImGui::SameLine();
            if (ImGui::Button("Apply All")) ImGui::OpenPopup("ApplyPrefabOverrides");
            ImGui::SameLine();
            if (ImGui::Button("Revert All")) ImGui::OpenPopup("RevertPrefabOverrides");
            PrepareEditorPopup("ApplyPrefabOverrides",480);
        if (ImGui::BeginPopup("ApplyPrefabOverrides"))
            {
                ImGui::TextWrapped("Write all overrides to the source prefab. Other linked instances can receive these changes.");
                if (ImGui::Button("Apply to source")) { m_PendingPrefabRoot = entity.GetUUID(); m_PendingPrefabAction = 2; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            PrepareEditorPopup("RevertPrefabOverrides",480);
        if (ImGui::BeginPopup("RevertPrefabOverrides"))
            {
                ImGui::TextWrapped("Replace this instance's overrides with its source values.");
                if (ImGui::Button("Revert instance")) { m_PendingPrefabRoot = entity.GetUUID(); m_PendingPrefabAction = 1; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }
	if (entity.HasComponent<EntityMetadata>())
		{
			if (DrawEntityIconSelector(m_Icons, entity, m_ColliderEditingAllowed))
				MarkModified();
			ImGui::SameLine(0.0f, 4.0f);
		}
		if (DrawCompactCheckbox("##ActiveSelf", &tagComponent.ActiveSelf))
			MarkModified();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Active Self controls gameplay scripts, rendering, audio, and physics for this Entity and its descendants.");
		ImGui::SameLine(0.0f, 5.0f);
		ImGui::SetNextItemWidth(-1.0f);
		const bool committed = ImGui::InputText("##Name", m_NameEditBuffer, sizeof(m_NameEditBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (committed || ImGui::IsItemDeactivatedAfterEdit())
		{
			if (m_Context->RenameEntity(entity, m_NameEditBuffer))
				MarkModified();
			std::snprintf(m_NameEditBuffer, sizeof(m_NameEditBuffer), "%s", tag.c_str());
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
				if (ImGui::DragFloat("##PerspectiveNear", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetPerspectiveNearClip(value)) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetPerspectiveFarClip();
				if (ImGui::DragFloat("##PerspectiveFar", &value, 0.1f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetPerspectiveFarClip(value)) MarkModified();
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
				if (ImGui::DragFloat("##OrthoNear", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetOrthographicNearClip(value)) MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetOrthographicFarClip();
				if (ImGui::DragFloat("##OrthoFar", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetOrthographicFarClip(value)) MarkModified();
				ImGui::Columns(1);
			}

			DrawProperty("Fixed Aspect Ratio", columnWidth);
			if (ImGui::Checkbox("##FixedAspectRatio", &component.FixedAspectRatio)) MarkModified();
			ImGui::Columns(1);
			DrawProperty("Background Color", columnWidth);
			if (DrawColorField("##BackgroundColor", glm::value_ptr(component.BackgroundColor))) MarkModified();
			ImGui::Columns(1);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::SpriteRenderer, [&]()
		{
		DrawComponent<SpriteRenderer>("Sprite Renderer", entity, m_Icons, EditorIcon::Sprite,
			[this, entity](auto& component)
		{
			const float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			if (DrawColorField("##Color", glm::value_ptr(component._Color))) MarkModified();
			ImGui::Columns(1);

			DrawProperty("Sprite", columnWidth);
			std::string spriteName = "None";
			EditorIcon spriteFieldIcon = EditorIcon::Sprite;
			Ref<Texture2D> spritePreview;
			const uint64_t rawSpriteHandle = static_cast<uint64_t>(component.SpriteHandle);
			if (rawSpriteHandle != 0)
			{
				if (const BuiltInSpriteAsset* builtIn =
					FindBuiltInSpriteAsset(component.SpriteHandle))
				{
					spriteName = std::string(builtIn->Name);
					component.Sprite = AssetManager::Get().LoadTexture(component.SpriteHandle);
					spritePreview = component.Sprite;
				}
				else
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
			}

			const float pickerButtonWidth = ImGui::GetFrameHeight();
			const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x -
				pickerButtonWidth - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Button("##SpriteAssetField", ImVec2(fieldWidth, 0.0f));
			const bool revealSprite = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			if (ImGui::IsItemHovered() && rawSpriteHandle != 0)
				ImGui::SetTooltip("Show in Project");
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
			if (revealSprite && rawSpriteHandle != 0 && m_AssetRevealCallback)
				m_AssetRevealCallback(component.SpriteHandle);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = nullptr;
						const AssetSubAsset* subSprite = nullptr;
						if (ResolveSelectableSprite(handle, metadata, subSprite))
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

			PrepareEditorToolWindow(ImVec2(760,520),ImVec2(420,300));
			if (ImGui::BeginPopupModal("Select Sprite##SpritePicker", &m_SpritePickerOpen,
				ImGuiWindowFlags_NoCollapse))
			{
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-1.0f);
				EditorSearchField("##SpriteSearch", "Search Sprites",
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
				for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				{
					if (spriteQuery.empty()
						|| LowerASCII(std::string(builtIn.Name)).find(spriteQuery)
							!= std::string::npos)
						++spriteCount;
				}
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
				for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				{
					if (!spriteQuery.empty()
						&& LowerASCII(std::string(builtIn.Name)).find(spriteQuery)
							== std::string::npos)
						continue;
					const std::filesystem::path packagePath =
						GetBuiltInSpriteAssetPath(builtIn.Handle);
					const std::string label(builtIn.Name);
					drawSpriteTile(builtIn.Handle, label.c_str(), &packagePath);
				}
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

		richInspectors.emplace(ComponentIds::RectTransform, [&]()
		{
		DrawComponent<RectTransform>("Rect Transform", entity, m_Icons,
			EditorIcon::Move, [this, entity](RectTransform& component)
		{
			glm::vec2 anchoredPosition = component.AnchoredPosition;
			glm::vec2 sizeDelta = component.SizeDelta;
			glm::vec2 anchorMin = component.AnchorMin;
			glm::vec2 anchorMax = component.AnchorMax;
			glm::vec2 pivot = component.Pivot;
			bool clipChildren = component.ClipChildren;

            bool presetEdited = false;
            if (ImGui::Button("Anchor presets")) ImGui::OpenPopup("AnchorPresets");
            PrepareEditorPopup("AnchorPresets",450);
        if (ImGui::BeginPopup("AnchorPresets"))
            {
                ImGui::TextDisabled("Anchor alignment / stretch");
                ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("Shift: also set pivot. Alt: also reset position and size."); ImGui::PopTextWrapPos();
                const char* names[] = { "Left", "Center", "Right", "Stretch" };
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                    {
                        ImGui::PushID(y*4+x);
                        if (x) ImGui::SameLine();
                        const char* vertical[] = { "Bottom", "Middle", "Top", "Stretch" };
                        std::string title = std::string(names[x]) + " / " + vertical[y];
                        const ImVec2 origin=ImGui::GetCursorScreenPos();
                        const bool clicked=ImGui::InvisibleButton("Preset",ImVec2(70,50));
                        auto* draw=ImGui::GetWindowDrawList();
                        draw->AddRectFilled(origin,ImVec2(origin.x+70,origin.y+50),ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_HeaderHovered:ImGuiCol_FrameBg),2);
                        const ImVec2 min(origin.x+16,origin.y+8),max(origin.x+54,origin.y+42);
                        draw->AddRect(min,max,IM_COL32(145,145,145,255));
                        const float ax=min.x+x*0.5f*(max.x-min.x),ay=max.y-y*0.5f*(max.y-min.y);
                        if(x==3) draw->AddLine(ImVec2(min.x,origin.y+25),ImVec2(max.x,origin.y+25),IM_COL32(80,170,240,255),2);
                        if(y==3) draw->AddLine(ImVec2(origin.x+35,min.y),ImVec2(origin.x+35,max.y),IM_COL32(80,170,240,255),2);
                        if(x!=3 && y!=3) draw->AddCircleFilled(ImVec2(ax,ay),3,IM_COL32(80,170,240,255));
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",title.c_str());
                        if (clicked)
                        {
                            anchorMin = { x == 3 ? 0.0f : x*0.5f, y == 3 ? 0.0f : y*0.5f };
                            anchorMax = { x == 3 ? 1.0f : x*0.5f, y == 3 ? 1.0f : y*0.5f };
                            if (ImGui::GetIO().KeyShift) pivot = (anchorMin + anchorMax)*0.5f;
                            if (ImGui::GetIO().KeyAlt) { anchoredPosition = {}; if(x==3) sizeDelta.x=0; if(y==3) sizeDelta.y=0; }
                            presetEdited = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                ImGui::EndPopup();
            }
            auto vectorField = [](const char* label, glm::vec2& value, float speed, bool normalized) {
                ImGui::PushID(label);
                DrawProperty(label);
                const bool edited = ImGui::DragFloat2("##Value", glm::value_ptr(value), speed,
                    0.0f, normalized ? 1.0f : 0.0f, "%.3f",
                    normalized ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
                ImGui::Columns(1);
                ImGui::PopID();
                return edited;
            };
            bool rectEdited = vectorField("Position", anchoredPosition, 0.1f, false);
            rectEdited |= vectorField("Size Delta", sizeDelta, 0.1f, false);
            ImGui::Spacing();
            ImGui::TextDisabled("ANCHORS");
            rectEdited |= vectorField("Min", anchorMin, 0.005f, true);
            rectEdited |= vectorField("Max", anchorMax, 0.005f, true);
            rectEdited |= vectorField("Pivot", pivot, 0.005f, true);
            DrawProperty("Clip Children");
            rectEdited |= ImGui::Checkbox("##ClipChildren", &clipChildren);
            ImGui::Columns(1);
			if (rectEdited || presetEdited)
			{
				const bool finite = std::isfinite(anchoredPosition.x)
					&& std::isfinite(anchoredPosition.y)
					&& std::isfinite(sizeDelta.x) && std::isfinite(sizeDelta.y)
					&& std::isfinite(anchorMin.x) && std::isfinite(anchorMin.y)
					&& std::isfinite(anchorMax.x) && std::isfinite(anchorMax.y)
					&& std::isfinite(pivot.x) && std::isfinite(pivot.y);
				if (finite)
				{
					anchorMin = glm::clamp(anchorMin, glm::vec2(0.0f),
						glm::vec2(1.0f));
					anchorMax = glm::clamp(anchorMax, anchorMin,
						glm::vec2(1.0f));
					pivot = glm::clamp(pivot, glm::vec2(0.0f), glm::vec2(1.0f));
					component.AnchoredPosition = anchoredPosition;
					component.SizeDelta = sizeDelta;
					component.AnchorMin = anchorMin;
					component.AnchorMax = anchorMax;
					component.Pivot = pivot;
					component.ClipChildren = clipChildren;
					MarkModified();
				}
			}

			// RectTransform replaces the ordinary Transform header in the Inspector,
			// while rotation and scale keep using the entity's real Transform data.
			if (!entity.HasComponent<Transform>() || !m_Context)
				return;
			auto& transformComponent = entity.GetComponent<Transform>();
			const bool hasParent = static_cast<bool>(m_Context->GetParent(entity));
			const glm::vec3 translation = hasParent
				? transformComponent._LocalTranslation : transformComponent._Translation;
			glm::vec3 rotation = hasParent
				? transformComponent._LocalRotation : transformComponent._Rotation;
			glm::vec3 scale = hasParent
				? transformComponent._LocalScale : transformComponent._Scale;
			float rotationZ = glm::degrees(rotation.z);
			glm::vec2 scaleXY(scale.x, scale.y);
			DrawProperty("Rotation Z");
            bool transformEdited = ImGui::DragFloat("##RotationZ", &rotationZ, 0.1f,
				0.0f, 0.0f, "%.2f deg");
            ImGui::Columns(1);
            transformEdited |= vectorField("Scale", scaleXY, 0.01f, false);
			if (transformEdited && std::isfinite(rotationZ)
				&& std::isfinite(scaleXY.x) && std::isfinite(scaleXY.y))
			{
				rotation.z = glm::radians(rotationZ);
				scale.x = scaleXY.x;
				scale.y = scaleXY.y;
				const glm::mat4 matrix = Math::ComposeTransform(translation,
					rotation, scale);
				const bool committed = hasParent
					? m_Context->SetLocalTransform(entity, matrix)
					: m_Context->SetWorldTransform(entity, matrix);
				if (committed)
					MarkModified();
			}
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Grid2D, [&]()
		{
		DrawComponent<Grid2D>("Grid 2D", entity, m_Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawGrid2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Tilemap2D, [&]()
		{
		DrawComponent<Tilemap2D>("Tilemap 2D", entity, m_Icons,
			EditorIcon::Sprite, [this, entity](auto& component)
		{
			DrawTilemap2DInspector(component, entity);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::TilemapRenderer2D, [&]()
		{
		DrawComponent<TilemapRenderer2D>("Tilemap Renderer 2D", entity, m_Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawTilemapRenderer2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::ParticleSystem2D, [&]()
		{
		DrawComponent<ParticleSystem2D>("Particle System 2D", entity, m_Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawParticleSystem2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Light2D, [&]()
		{
		DrawComponent<Light2D>("Light 2D", entity, m_Icons,
			EditorIcon::Count, [this](auto& component)
		{
			DrawLight2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::LineRenderer, [&]()
		{
		DrawComponent<LineRenderer>("Line Renderer", entity, m_Icons, EditorIcon::Count,
			[this](auto& component)
		{
			const float columnWidth = 100.0f;

			DrawProperty("Color", columnWidth);
			if (DrawColorField("##Color", glm::value_ptr(component._Color)))
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
		DrawComponent<AudioSource>("Audio Source", entity, m_Icons, EditorIcon::Audio,
			[this](auto& component)
		{
            const auto* clipMetadata = AssetManager::Get().GetRegistry().GetMetadata(component.Clip);
            const std::string clipLabel = clipMetadata ? PathToUTF8(clipMetadata->FilePath.filename()) : "None (Audio)";
            if (ImGui::BeginCombo("Audio Clip", clipLabel.c_str()))
            {
                if (ImGui::Selectable("None")) { component.Clip = AssetHandle(0); MarkModified(); }
                for (const auto& [handle, metadata] : AssetManager::Get().GetRegistry().GetAssets())
                    if (!metadata.IsMissing && metadata.Type == AssetType::Audio)
                    {
                        ImGui::PushID(std::to_string(static_cast<uint64_t>(handle)).c_str());
                        if (ImGui::Selectable(PathToUTF8(metadata.FilePath).c_str())) { component.Clip = handle; MarkModified(); }
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            uint64_t clip = static_cast<uint64_t>(component.Clip);
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

		richInspectors.emplace(ComponentIds::UIImage, [&]()
		{
			DrawComponent<UIImage>("Image", entity, m_Icons, EditorIcon::Sprite,
				[this](UIImage& component)
				{
					if (const PropertyDescriptor* imageProperty = FindRegisteredProperty(
						ComponentIds::UIImage, "Image"))
					{
						AssetHandle image = component.Image;
						if (DrawRegisteredAssetReference(*imageProperty, image))
						{
							component.Image = image;
							MarkModified();
						}
					}
					if (DrawColorField("Color", glm::value_ptr(component.Color)))
						MarkModified();
					if (ImGui::Checkbox("Raycast Target", &component.RaycastTarget))
						MarkModified();
					if (ImGui::Checkbox("Preserve Aspect", &component.PreserveAspect))
						MarkModified();
				}, onModified, m_ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::UIText, [&]()
		{
			DrawComponent<UIText>("Text", entity, m_Icons, EditorIcon::Font,
				[this](UIText& component)
				{
					if (DrawBoundedMultilineText("Text", component.Text,
						ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.5f)))
					{
						MarkModified();
					}

					ImGui::Spacing();
					ImGui::TextDisabled("CHARACTER");
					auto drawFont = [this](const char* stableName,
						AssetHandle& handle)
					{
						const PropertyDescriptor* property = FindRegisteredProperty(
							ComponentIds::UIText, stableName);
						if (!property)
							return;
						ImGui::PushID(stableName);
						AssetHandle candidate = handle;
						if (DrawRegisteredAssetReference(*property, candidate))
						{
							handle = candidate;
							MarkModified();
						}
						ImGui::PopID();
					};
					drawFont("Font", component.Font);
					drawFont("FallbackFont", component.FallbackFont);
					drawFont("EmojiFont", component.EmojiFont);
					float fontSize = component.FontSize;
					if (ImGui::DragFloat("Font Size", &fontSize, 0.25f, 1.0f,
						10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp)
						&& std::isfinite(fontSize))
					{
						component.FontSize = std::clamp(fontSize, 1.0f, 10000.0f);
						MarkModified();
					}
					float lineSpacing = component.LineSpacing;
					if (ImGui::DragFloat("Line Spacing", &lineSpacing, 0.01f,
						0.1f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)
						&& std::isfinite(lineSpacing))
					{
						component.LineSpacing = std::clamp(lineSpacing, 0.1f, 10.0f);
						MarkModified();
					}

					ImGui::Spacing();
					ImGui::TextDisabled("PARAGRAPH");
					const char* alignments[] = { "Left", "Center", "Right" };
					int alignment = static_cast<int>(component.Alignment);
					if (ImGui::Combo("Alignment", &alignment, alignments,
						static_cast<int>(std::size(alignments))))
					{
						component.Alignment = static_cast<TextAlignment>(alignment);
						MarkModified();
					}
					if (ImGui::Checkbox("Wrap", &component.Wrap))
						MarkModified();
					if (DrawColorField("Color", glm::value_ptr(component.Color)))
						MarkModified();
					if (ImGui::Checkbox("Raycast Target", &component.RaycastTarget))
						MarkModified();
				}, onModified, m_ColliderEditingAllowed);
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
		richInspectors.emplace(ComponentIds::UIButton, [&]()
		{
			DrawComponent<UIButton>("Button", entity, m_Icons,
				EditorIcon::Count, [&](UIButton& button)
				{
					DrawUIButtonInspector(button, entity);
				}, onModified, m_ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::BoxCollider2D, [&]()
		{
		DrawComponent<BoxCollider2D>("Box Collider 2D", entity, m_Icons, EditorIcon::BoxCollider2D,
			[this, entity](auto& component)
		{
			const bool editingCollider = GetColliderEditMode() == ColliderEditMode::Box;
			ImGui::BeginDisabled(!m_ColliderEditingAllowed || !m_ColliderGizmosEnabled);
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
			ImGui::BeginDisabled(!m_ColliderEditingAllowed || !m_ColliderGizmosEnabled);
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
				ImGui::SetTooltip("Butter circles cannot become ellipses. The largest absolute world X/Y scale keeps the fixture circular.");
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
				ImGui::SetTooltip("Butter damping ratio in the range 0 to 1.");

			if (ImGui::Checkbox("Collide Connected", &component.CollideConnected))
				changed = true;
			if (changed)
				MarkModified();
		}, onModified, m_ColliderEditingAllowed);
		});

        richInspectors.emplace(ComponentIds::UILocalization, [&]()
        {
            DrawComponent<UILocalization>("UI Localization", entity, m_Icons, EditorIcon::Font,
                [&](UILocalization& component)
                {
                    auto editString = [&](const char* label, std::string& value) {
                        std::array<char, 4096> buffer{};
                        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                        ImGui::BeginDisabled(value.size() >= buffer.size());
                        if (ImGui::InputText(label, buffer.data(), buffer.size())) { value=buffer.data(); MarkModified(); }
                        ImGui::EndDisabled();
                    };
                    editString("Locale", component.Locale);
                    editString("Fallback locale", component.FallbackLocale);
                    try
                    {
                        YAML::Node table = YAML::Load(component.Table);
                        if (!table.IsMap()) throw std::runtime_error("Translation table must be a language-to-key map.");
                        std::vector<std::string> locales; std::set<std::string> keys;
                        for(const auto& locale : table)
                        {
                            if (!locale.second.IsMap()) throw std::runtime_error("Each language must contain a key/value map.");
                            locales.push_back(locale.first.as<std::string>());
                            for(const auto& value : locale.second) { keys.insert(value.first.as<std::string>()); if(!value.second.IsScalar()) throw std::runtime_error("Translations must be text values."); }
                        }
                        bool changed=false;
                        if(ImGui::Button("Add current locale") && !component.Locale.empty() && !table[component.Locale])
                        { table[component.Locale]=YAML::Node(YAML::NodeType::Map); changed=true; }
                        ImGui::TextWrapped("Missing keys use the fallback locale. An explicitly empty translation displays empty text. Right-click a cell to restore fallback.");
                        static char newKey[128]{};
                        ImGui::InputTextWithHint("##TranslationKey", "New translation key", newKey, sizeof(newKey));
                        ImGui::SameLine();
                        if(ImGui::Button("Add key") && newKey[0] && !component.Locale.empty() && !keys.contains(newKey))
                        { table[component.Locale][newKey]=""; changed=true; newKey[0]=0; }
                        if(!locales.empty() && ImGui::BeginTable("Translations", static_cast<int>(std::min<size_t>(locales.size(), 30))+1,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit))
                        {
                            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130);
                            for(size_t i=0;i<std::min<size_t>(locales.size(),30);++i) ImGui::TableSetupColumn(locales[i].c_str(), ImGuiTableColumnFlags_WidthFixed,180);
                            ImGui::TableHeadersRow();
                            for(const auto& key : keys)
                            {
                                ImGui::PushID(key.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(key.c_str());
                                for(size_t i=0;i<std::min<size_t>(locales.size(),30);++i)
                                {
                                    ImGui::TableNextColumn(); ImGui::PushID(locales[i].c_str());
                                    const YAML::Node values=table[locales[i]];
                                    std::string value=values[key] ? values[key].as<std::string>() : "";
                                    std::array<char,4096> buffer{}; std::snprintf(buffer.data(),buffer.size(),"%s",value.c_str());
                                    ImGui::SetNextItemWidth(-1); ImGui::BeginDisabled(value.size()>=buffer.size());
                                    if(ImGui::InputTextWithHint("##Translation", values[key] ? "Empty" : "Missing (fallback)",buffer.data(),buffer.size())) { table[locales[i]][key]=buffer.data(); changed=true; }
                                    if (ImGui::BeginPopupContextItem("TranslationActions"))
                                    {
                                        if (ImGui::MenuItem("Use fallback (remove translation)", nullptr, false, static_cast<bool>(values[key])))
                                        { table[locales[i]].remove(key); changed=true; }
                                        ImGui::EndPopup();
                                    }
                                    ImGui::EndDisabled(); ImGui::PopID();
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if(locales.size()>30) ImGui::TextWrapped("Showing first 30 locales. All locales remain in the source table.");
                        if(changed) { YAML::Emitter output; output<<table; component.Table=output.c_str(); MarkModified(); }
                    }
                    catch(const std::exception& error) { ImGui::TextWrapped("Table error: %s",error.what()); }
                    if(ImGui::TreeNode("Source table / recovery"))
                    { if(DrawBoundedMultilineText("##LocalizationSource",component.Table,ImVec2(-1,120))) MarkModified(); ImGui::TreePop(); }
                }, onModified, m_ColliderEditingAllowed);
        });
        richInspectors.emplace(ComponentIds::UITheme, [&]()
        {
            DrawComponent<UITheme>("UI Theme", entity, m_Icons, EditorIcon::Material, [&](UITheme& theme) {
                bool changed=DrawColorField("Text",glm::value_ptr(theme.TextColor));
                changed|=DrawColorField("Image",glm::value_ptr(theme.ImageColor));
                changed|=DrawColorField("Accent",glm::value_ptr(theme.AccentColor));
                changed|=ImGui::DragFloat("Font scale",&theme.FontScale,0.01f,0.1f,10.0f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                const auto* descriptor=ComponentRegistry::Get().Find(UUID(ComponentIds::UITheme));
                if(descriptor) for(const auto& property:descriptor->Properties) if(property.StableName=="Font") changed|=DrawRegisteredAssetReference(property,theme.Font);
                ImGui::TextDisabled("Theme preview (inherited by descendants)");
                ImGui::ColorButton("Image tint",ImVec4(theme.ImageColor.x,theme.ImageColor.y,theme.ImageColor.z,theme.ImageColor.w),0,ImVec2(80,24));
                ImGui::SameLine(); ImGui::ColorButton("Accent tint",ImVec4(theme.AccentColor.x,theme.AccentColor.y,theme.AccentColor.z,theme.AccentColor.w),0,ImVec2(80,24));
                ImGui::TextColored(ImVec4(theme.TextColor.x,theme.TextColor.y,theme.TextColor.z,theme.TextColor.w),"Sample text");
                if(changed) MarkModified();
            },onModified,m_ColliderEditingAllowed);
        });
		richInspectors.emplace(ComponentIds::CSharpScripts, [&]()
		{
			ImGui::BeginDisabled(!m_ScriptEditingEnabled);
			DrawCSharpScripts(entity);
			ImGui::EndDisabled();
		});
        for (const ComponentDescriptor& descriptor :
            ComponentRegistry::Get().GetDescriptors())
        {
            if (!descriptor.InspectorVisible || !descriptor.Has(entity))
				continue;
			const uint64_t componentType = static_cast<uint64_t>(descriptor.TypeId);
			// RectTransform is the UI-facing Transform. Its rich inspector below also
			// edits the shared Transform rotation/scale, so drawing both headers would
			// expose two conflicting transform surfaces.
			if (componentType == ComponentIds::Transform
				&& entity.HasComponent<RectTransform>())
				continue;
			const auto rich = richInspectors.find(componentType);
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
        const auto addMin=ImGui::GetItemRectMin(),addMax=ImGui::GetItemRectMax();
        const float addSize=ImGui::GetFontSize()*0.85f;
        const ImVec2 addStart(addMin.x+ImGui::GetStyle().FramePadding.x,addMin.y+(addMax.y-addMin.y-addSize)*0.5f);
        DrawEditorGlyph(ImGui::GetWindowDrawList(),m_Icons,EditorIcon::Add,addStart,
            ImVec2(addStart.x+addSize,addStart.y+addSize),ImGui::GetColorU32(ImVec4(1,1,1,1)));
		ImGui::EndDisabled();
		if (addComponentPressed || m_AddComponentPopupRequested)
		{
			m_AddComponentSearch.fill('\0');
			m_AddComponentSearchFocusRequested = true;
			m_AddComponentPopupRequested = false;
			ImGui::OpenPopup("AddComponent");
		}

		PrepareEditorPopup("AddComponent",420);
        if (ImGui::BeginPopup("AddComponent"))
		{
			ImGui::BeginDisabled(!m_ColliderEditingAllowed);
			if (m_AddComponentSearchFocusRequested)
			{
				ImGui::SetKeyboardFocusHere();
				m_AddComponentSearchFocusRequested = false;
			}
			ImGui::SetNextItemWidth(-1.0f);
			EditorSearchField("##AddComponentSearch", "Search components...",
				m_AddComponentSearch.data(), m_AddComponentSearch.size());
			ImGui::Separator();

			const std::string query = LowerASCII(m_AddComponentSearch.data());
			constexpr std::array<const char*, 8> categoryOrder = {
				"Rendering", "Animation", "Tilemap", "Physics 2D",
				"Audio", "UI", "Scripting", "Gameplay"
			};
			bool drewResult = false;
			auto drawDescriptor = [&](const ComponentDescriptor& descriptor)
			{
				ImGui::PushID(descriptor.StableName.c_str());
				const bool selected = ImGui::MenuItem(descriptor.DisplayName.c_str());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
					ImGui::SetTooltip("%s", descriptor.StableName.c_str());
				ImGui::PopID();
				if (selected)
				{
					std::string error;
					const uint64_t componentType =
						static_cast<uint64_t>(descriptor.TypeId);
					if (componentType == ComponentIds::SpriteAnimator
						&& !entity.HasComponent<SpriteRenderer>())
						entity.AddComponent<SpriteRenderer>();
					if (componentType == ComponentIds::Tilemap2D
						&& !entity.HasComponent<TilemapRenderer2D>())
						entity.AddComponent<TilemapRenderer2D>();
					if ((componentType == ComponentIds::UIImage
						|| componentType == ComponentIds::UIText
						|| componentType == ComponentIds::UIButton
						|| componentType == ComponentIds::UISlider
						|| componentType == ComponentIds::UIScrollView
						|| componentType == ComponentIds::UIInputField
						|| componentType == ComponentIds::UILocalizedText)
						&& !entity.HasComponent<RectTransform>())
						entity.AddComponent<RectTransform>();
					if ((componentType == ComponentIds::UIInputField || componentType == ComponentIds::UILocalizedText)
						&& !entity.HasComponent<UIText>()) entity.AddComponent<UIText>();
					if (componentType == ComponentIds::UIButton
						&& !entity.HasComponent<UIImage>())
						entity.AddComponent<UIImage>();
					if (componentType == ComponentIds::Canvas
						&& !entity.HasComponent<UIEventSystem>())
						entity.AddComponent<UIEventSystem>();
					if (descriptor.Add(entity, error))
						MarkModified();
					else
						TC_Core_Warn("Could not add {0}: {1}", descriptor.StableName, error);
					ImGui::CloseCurrentPopup();
				}
			};

			auto descriptorAvailable = [&](const ComponentDescriptor& descriptor,
				const char* category)
			{
				return descriptor.InspectorVisible && descriptor.AddableInInspector
					&& !descriptor.Has(entity)
					&& std::string_view(GetAddComponentCategory(descriptor)) == category
					&& MatchesAddComponentSearch(descriptor, query);
			};

			if (query.empty())
			{
				for (const char* category : categoryOrder)
				{
					bool hasItems = std::string_view(category) == "Scripting";
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
						hasItems = hasItems || descriptorAvailable(descriptor, category);
					if (!hasItems)
						continue;
					drewResult = true;
					if (!ImGui::BeginMenu(category))
						continue;
					if (std::string_view(category) == "Scripting")
						ImGui::MenuItem("C# Script (drag asset into Inspector)",
							nullptr, false, false);
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
					{
						if (descriptorAvailable(descriptor, category))
							drawDescriptor(descriptor);
					}
					ImGui::EndMenu();
				}
			}
			else
			{
				for (const char* category : categoryOrder)
				{
					bool categoryDrawn = false;
					const std::string scriptSearch = LowerASCII(
						std::string("C# Script Scripting"));
					if (std::string_view(category) == "Scripting"
						&& scriptSearch.find(query) != std::string::npos)
					{
						ImGui::TextDisabled("Scripting");
						ImGui::MenuItem("C# Script (drag asset into Inspector)",
							nullptr, false, false);
						categoryDrawn = true;
					}
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
					{
						if (!descriptorAvailable(descriptor, category))
							continue;
						if (!categoryDrawn)
						{
							ImGui::TextDisabled("%s", category);
							categoryDrawn = true;
						}
						drawDescriptor(descriptor);
					}
					if (categoryDrawn)
					{
						drewResult = true;
						ImGui::Spacing();
					}
				}
			}
			if (!drewResult)
				ImGui::TextDisabled("No matching components");
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

	}

}
