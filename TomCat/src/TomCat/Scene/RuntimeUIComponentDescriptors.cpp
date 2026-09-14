#include "tcpch.h"
#include "RuntimeUIComponentDescriptors.h"

#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <map>
#include <type_traits>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		bool Finite(float value) { return std::isfinite(value); }
		bool Finite(const glm::vec2& value) { return Finite(value.x) && Finite(value.y); }
		bool Finite(const glm::vec4& value)
		{
			return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
		}

		void EmitValue(YAML::Emitter& output, PropertyKind kind,
			const PropertyValue& value)
		{
			switch (kind)
			{
				case PropertyKind::Bool: output << std::get<bool>(value); break;
				case PropertyKind::Int32: output << std::get<int32_t>(value); break;
				case PropertyKind::Int64: output << std::get<int64_t>(value); break;
				case PropertyKind::UInt32: output << std::get<uint32_t>(value); break;
				case PropertyKind::UInt64: output << std::get<uint64_t>(value); break;
				case PropertyKind::Float: output << std::get<float>(value); break;
				case PropertyKind::Double: output << std::get<double>(value); break;
				case PropertyKind::String: output << std::get<std::string>(value); break;
				case PropertyKind::Vector2:
				{
					const auto& item = std::get<glm::vec2>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << YAML::EndSeq;
					break;
				}
				case PropertyKind::Vector3:
				{
					const auto& item = std::get<glm::vec3>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << item.z
						<< YAML::EndSeq;
					break;
				}
				case PropertyKind::Vector4:
				{
					const auto& item = std::get<glm::vec4>(value);
					output << YAML::Flow << YAML::BeginSeq << item.x << item.y << item.z
						<< item.w << YAML::EndSeq;
					break;
				}
			}
		}

		PropertyValue ReadValue(const YAML::Node& node, PropertyKind kind)
		{
			switch (kind)
			{
				case PropertyKind::Bool: return node.as<bool>();
				case PropertyKind::Int32: return node.as<int32_t>();
				case PropertyKind::Int64: return node.as<int64_t>();
				case PropertyKind::UInt32: return node.as<uint32_t>();
				case PropertyKind::UInt64: return node.as<uint64_t>();
				case PropertyKind::Float: return node.as<float>();
				case PropertyKind::Double: return node.as<double>();
				case PropertyKind::String: return node.as<std::string>();
				case PropertyKind::Vector2:
					if (!node.IsSequence() || node.size() != 2)
						throw std::runtime_error("Vector2 requires two values");
					return glm::vec2(node[0].as<float>(), node[1].as<float>());
				case PropertyKind::Vector3:
					if (!node.IsSequence() || node.size() != 3)
						throw std::runtime_error("Vector3 requires three values");
					return glm::vec3(node[0].as<float>(), node[1].as<float>(),
						node[2].as<float>());
				case PropertyKind::Vector4:
					if (!node.IsSequence() || node.size() != 4)
						throw std::runtime_error("Vector4 requires four values");
					return glm::vec4(node[0].as<float>(), node[1].as<float>(),
						node[2].as<float>(), node[3].as<float>());
			}
			throw std::runtime_error("Unsupported registered property kind");
		}

		bool EncodeProperties(const ComponentDescriptor& descriptor, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			try
			{
				output << YAML::BeginSeq;
				for (const PropertyDescriptor& property : descriptor.Properties)
				{
					output << YAML::BeginMap;
					output << YAML::Key << "PropertyId" << YAML::Value
						<< static_cast<uint64_t>(property.PropertyId);
					output << YAML::Key << "StableName" << YAML::Value
						<< property.StableName;
					output << YAML::Key << "Value" << YAML::Value;
					EmitValue(output, property.Kind, property.Get(entity));
					output << YAML::EndMap;
				}
				output << YAML::EndSeq;
				return output.good();
			}
			catch (const std::exception& exception)
			{
				error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		}

		template<typename Component>
		void ResetTransient(Component&) {}

		void ResetTransient(RectTransform& value)
		{
			value.RuntimeRect = glm::vec4(0.0f);
			value.RuntimeClipRect = glm::vec4(0.0f);
		}

		void ResetTransient(UIButton& value)
		{
			value.RuntimeHovered = false;
			value.RuntimePressed = false;
			value.RuntimeFocused = false;
			value.RuntimeClickedThisFrame = false;
			value.RuntimeClickSerial = 0;
		}

		template<typename Field>
		PropertyKind PropertyKindFor()
		{
			if constexpr (std::is_same_v<Field, bool>) return PropertyKind::Bool;
			else if constexpr (std::is_same_v<Field, int32_t>) return PropertyKind::Int32;
			else if constexpr (std::is_same_v<Field, uint32_t>) return PropertyKind::UInt32;
			else if constexpr (std::is_same_v<Field, uint64_t>
				|| std::is_same_v<Field, AssetHandle>) return PropertyKind::UInt64;
			else if constexpr (std::is_same_v<Field, float>) return PropertyKind::Float;
			else if constexpr (std::is_same_v<Field, std::string>) return PropertyKind::String;
			else if constexpr (std::is_same_v<Field, glm::vec2>) return PropertyKind::Vector2;
			else if constexpr (std::is_same_v<Field, glm::vec4>) return PropertyKind::Vector4;
			else if constexpr (std::is_enum_v<Field>) return PropertyKind::Int32;
			else static_assert(sizeof(Field) == 0, "Unsupported registered member type");
		}

		template<typename Field>
		PropertyValue ToPropertyValue(const Field& value)
		{
			if constexpr (std::is_same_v<Field, AssetHandle>)
				return static_cast<uint64_t>(value);
			else if constexpr (std::is_enum_v<Field>)
				return static_cast<int32_t>(value);
			else
				return value;
		}

		template<typename Field>
		Field FromPropertyValue(const PropertyValue& value)
		{
			if constexpr (std::is_same_v<Field, AssetHandle>)
				return AssetHandle(std::get<uint64_t>(value));
			else if constexpr (std::is_enum_v<Field>)
				return static_cast<Field>(std::get<int32_t>(value));
			else
				return std::get<Field>(value);
		}

		template<typename Component, typename Field>
		PropertyDescriptor MemberPropertyImpl(uint64_t id, const char* stableName,
			const char* displayName, Field Component::* member,
			std::function<bool(const Field&, std::string&)> validate)
		{
			PropertyDescriptor property;
			property.PropertyId = UUID(id);
			property.StableName = stableName;
			property.DisplayName = displayName;
			property.Kind = PropertyKindFor<Field>();
			property.Get = [member](Entity entity) -> PropertyValue
			{
				return ToPropertyValue(entity.GetComponent<Component>().*member);
			};
			property.Set = [member, validate = std::move(validate)](Entity entity,
				const PropertyValue& value, std::string& error)
			{
				const Field converted = FromPropertyValue<Field>(value);
				if (validate && !validate(converted, error))
					return false;
				entity.GetComponent<Component>().*member = converted;
				return true;
			};
			property.DefaultValue = ToPropertyValue(Component{}.*member);
			return property;
		}

		template<typename Component, typename Field>
		PropertyDescriptor MemberProperty(uint64_t id, const char* stableName,
			const char* displayName, Field Component::* member)
		{
			return MemberPropertyImpl(id, stableName, displayName, member, {});
		}

		template<typename Component, typename Field, typename Validator>
		PropertyDescriptor MemberProperty(uint64_t id, const char* stableName,
			const char* displayName, Field Component::* member, Validator validate)
		{
			std::function<bool(const Field&, std::string&)> erased =
				std::move(validate);
			return MemberPropertyImpl(id, stableName, displayName, member,
				std::move(erased));
		}

		template<typename Component>
		PropertyDescriptor AssetMemberProperty(uint64_t id, const char* stableName,
			const char* displayName, AssetHandle Component::* member,
			AssetType acceptedType, bool allowSubAssets = false)
		{
			PropertyDescriptor property = MemberProperty(id, stableName,
				displayName, member);
			property.AssetReference = AssetPropertyMetadata{
				{ acceptedType }, allowSubAssets };
			return property;
		}

		template<typename Component>
		ComponentDescriptor MakeDescriptor(uint64_t typeId, const char* stableName,
			const char* displayName, std::vector<PropertyDescriptor> properties,
			std::function<bool(const Component&, std::string&)> validateWhole = {})
		{
			ComponentDescriptor descriptor;
			descriptor.TypeId = UUID(typeId);
			descriptor.StableName = stableName;
			descriptor.DisplayName = displayName;
			descriptor.SchemaVersion = 1;
			descriptor.ScriptAccessible = true;
			descriptor.Has = [](Entity entity)
			{
				return entity && entity.HasComponent<Component>();
			};
			descriptor.Add = [](Entity entity, std::string& error)
			{
				if (!entity)
				{
					error = "Cannot add component to an invalid entity";
					return false;
				}
				if (!entity.HasComponent<Component>())
					entity.AddComponent<Component>();
				return true;
			};
			descriptor.Remove = [](Entity entity, std::string& error)
			{
				if (!entity)
				{
					error = "Cannot remove component from an invalid entity";
					return false;
				}
				entity.RemoveComponent<Component>();
				return true;
			};
			descriptor.Copy = [](Entity source, Entity destination, std::string& error)
			{
				if (!source || !destination || !source.HasComponent<Component>())
				{
					error = "Cannot copy component from invalid entities";
					return false;
				}
				Component copied = source.GetComponent<Component>();
				ResetTransient(copied);
				destination.AddOrReplaceComponent<Component>(std::move(copied));
				return true;
			};
			descriptor.Encode = &EncodeProperties;
			descriptor.Decode = [validateWhole = std::move(validateWhole)](
				const ComponentDescriptor& current, Entity entity,
				const YAML::Node& propertiesNode, std::string& error)
			{
				if (!propertiesNode || !propertiesNode.IsSequence()
					|| propertiesNode.size() != current.Properties.size())
				{
					error = current.StableName + ".Properties has an incompatible shape";
					return false;
				}
				const Component original = entity.GetComponent<Component>();
				try
				{
					std::map<uint64_t, YAML::Node> values;
					for (const YAML::Node& node : propertiesNode)
					{
						if (!node.IsMap() || node.size() != 3 || !node["PropertyId"]
							|| !node["StableName"] || !node["Value"])
							throw std::runtime_error("property record must contain PropertyId, StableName and Value");
						const uint64_t id = node["PropertyId"].as<uint64_t>();
						if (id == 0 || !values.emplace(id, node).second)
							throw std::runtime_error("property IDs must be nonzero and unique");
					}
					for (const PropertyDescriptor& property : current.Properties)
					{
						const uint64_t id = static_cast<uint64_t>(property.PropertyId);
						const auto found = values.find(id);
						if (found == values.end()
							|| found->second["StableName"].as<std::string>() != property.StableName)
							throw std::runtime_error("property identity mismatch");
						if (!property.Set(entity,
							ReadValue(found->second["Value"], property.Kind), error))
							throw std::runtime_error(error.empty() ? "property validation failed" : error);
					}
					if (validateWhole && !validateWhole(entity.GetComponent<Component>(), error))
						throw std::runtime_error(error.empty() ? "component validation failed" : error);
					ResetTransient(entity.GetComponent<Component>());
					return true;
				}
				catch (const std::exception& exception)
				{
					entity.GetComponent<Component>() = original;
					if (error.empty())
						error = current.StableName + ": " + exception.what();
					return false;
				}
			};
			descriptor.Properties = std::move(properties);
			return descriptor;
		}

		auto FiniteFloat(float minimum, float maximum)
		{
			return [minimum, maximum](const float& value, std::string& error)
			{
				if (!Finite(value) || value < minimum || value > maximum)
				{
					error = "value is outside its finite range";
					return false;
				}
				return true;
			};
		}

		auto FiniteVector2()
		{
			return [](const glm::vec2& value, std::string& error)
			{
				if (!Finite(value)) { error = "Vector2 must be finite"; return false; }
				return true;
			};
		}

		auto FiniteColor()
		{
			return [](const glm::vec4& value, std::string& error)
			{
				if (!Finite(value)) { error = "Color must be finite"; return false; }
				return true;
			};
		}

		auto ValidText()
		{
			return [](const std::string& value, std::string& error)
			{
				bool valid = false;
				if (value.size() > 65536)
				{
					error = "Text exceeds 65536 UTF-8 bytes";
					return false;
				}
				(void)FontAtlasBuilder::DecodeUTF8(value, &valid);
				if (!valid) error = "Text is not valid UTF-8";
				return valid;
			};
		}

	}

	std::vector<ComponentDescriptor> MakeRuntimeUIComponentDescriptors()
	{
		using namespace ComponentIds;
		std::vector<ComponentDescriptor> result;
		result.push_back(MakeDescriptor<::TomCat::TextRenderer>(
			::TomCat::ComponentIds::TextRenderer,
			"TomCat.TextRenderer", "Text Renderer", {
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::Enabled,
					"Enabled", "Enabled", &::TomCat::TextRenderer::Enabled),
				AssetMemberProperty(::TomCat::ComponentIds::TextRendererProperties::Font,
					"Font", "Font Asset", &::TomCat::TextRenderer::Font, AssetType::Font),
				AssetMemberProperty(::TomCat::ComponentIds::TextRendererProperties::FallbackFont,
					"FallbackFont", "Fallback Font Asset", &::TomCat::TextRenderer::FallbackFont, AssetType::Font),
				AssetMemberProperty(::TomCat::ComponentIds::TextRendererProperties::EmojiFont,
					"EmojiFont", "Emoji Font Asset", &::TomCat::TextRenderer::EmojiFont, AssetType::Font),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::Text,
					"Text", "Text", &::TomCat::TextRenderer::Text, ValidText()),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::FontSize,
					"FontSize", "Font Size", &::TomCat::TextRenderer::FontSize,
					FiniteFloat(0.001f, 10000.0f)),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::Color,
					"Color", "Color", &::TomCat::TextRenderer::Color, FiniteColor()),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::Alignment,
					"Alignment", "Alignment", &::TomCat::TextRenderer::Alignment,
					[](const TextAlignment& value, std::string& error) {
						if (value < TextAlignment::Left || value > TextAlignment::Right)
						{ error = "invalid text alignment"; return false; } return true; }),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::MaxWidth,
					"MaxWidth", "Max Width", &::TomCat::TextRenderer::MaxWidth,
					FiniteFloat(0.0f, 1000000.0f)),
				MemberProperty(::TomCat::ComponentIds::TextRendererProperties::LineSpacing,
					"LineSpacing", "Line Spacing", &::TomCat::TextRenderer::LineSpacing,
					FiniteFloat(0.1f, 10.0f))
			}));

		result.push_back(MakeDescriptor<::TomCat::Canvas>(
			::TomCat::ComponentIds::Canvas, "TomCat.Canvas", "Canvas", {
			MemberProperty(CanvasProperties::Enabled, "Enabled", "Enabled", &::TomCat::Canvas::Enabled),
			MemberProperty(CanvasProperties::ScaleMode, "ScaleMode", "Scale Mode", &::TomCat::Canvas::ScaleMode,
				[](const CanvasScaleMode& value, std::string& error) { if (value < CanvasScaleMode::ConstantPixelSize || value > CanvasScaleMode::ScaleWithScreenSize) { error = "invalid Canvas scale mode"; return false; } return true; }),
			MemberProperty(CanvasProperties::ReferenceResolution, "ReferenceResolution", "Reference Resolution", &::TomCat::Canvas::ReferenceResolution, FiniteVector2()),
			MemberProperty(CanvasProperties::MatchWidthOrHeight, "MatchWidthOrHeight", "Match Width Or Height", &::TomCat::Canvas::MatchWidthOrHeight, FiniteFloat(0.0f, 1.0f)),
			MemberProperty(CanvasProperties::ScaleFactor, "ScaleFactor", "Scale Factor", &::TomCat::Canvas::ScaleFactor, FiniteFloat(0.01f, 100.0f)),
			MemberProperty(CanvasProperties::ReferenceDPI, "ReferenceDPI", "Reference DPI", &::TomCat::Canvas::ReferenceDPI, FiniteFloat(1.0f, 1000.0f)),
			MemberProperty(CanvasProperties::SortingOrder, "SortingOrder", "Sorting Order", &::TomCat::Canvas::SortingOrder)
		}, [](const ::TomCat::Canvas& value, std::string& error) { if (value.ReferenceResolution.x <= 0.0f || value.ReferenceResolution.y <= 0.0f) { error = "Canvas reference resolution must be positive"; return false; } return true; }));

		result.push_back(MakeDescriptor<::TomCat::RectTransform>(
			::TomCat::ComponentIds::RectTransform,
			"TomCat.RectTransform", "Rect Transform", {
			MemberProperty(RectTransformProperties::AnchorMin, "AnchorMin", "Anchor Min", &::TomCat::RectTransform::AnchorMin, FiniteVector2()),
			MemberProperty(RectTransformProperties::AnchorMax, "AnchorMax", "Anchor Max", &::TomCat::RectTransform::AnchorMax, FiniteVector2()),
			MemberProperty(RectTransformProperties::Pivot, "Pivot", "Pivot", &::TomCat::RectTransform::Pivot, FiniteVector2()),
			MemberProperty(RectTransformProperties::AnchoredPosition, "AnchoredPosition", "Anchored Position", &::TomCat::RectTransform::AnchoredPosition, FiniteVector2()),
			MemberProperty(RectTransformProperties::SizeDelta, "SizeDelta", "Size Delta", &::TomCat::RectTransform::SizeDelta, FiniteVector2()),
			MemberProperty(RectTransformProperties::ClipChildren, "ClipChildren", "Clip Children", &::TomCat::RectTransform::ClipChildren)
		}, [](const ::TomCat::RectTransform& value, std::string& error) {
			if (glm::any(glm::lessThan(value.AnchorMin, glm::vec2(0.0f))) || glm::any(glm::greaterThan(value.AnchorMax, glm::vec2(1.0f))) || glm::any(glm::greaterThan(value.AnchorMin, value.AnchorMax)) || glm::any(glm::lessThan(value.Pivot, glm::vec2(0.0f))) || glm::any(glm::greaterThan(value.Pivot, glm::vec2(1.0f)))) { error = "RectTransform anchors/pivot must be ordered in [0, 1]"; return false; } return true; }));

		result.push_back(MakeDescriptor<::TomCat::UIImage>(
			::TomCat::ComponentIds::UIImage, "TomCat.UIImage", "UI Image", {
			MemberProperty(UIImageProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIImage::Enabled),
			AssetMemberProperty(UIImageProperties::Image, "Image", "Image Asset",
				&::TomCat::UIImage::Image, AssetType::Texture2D, true),
			MemberProperty(UIImageProperties::Color, "Color", "Color", &::TomCat::UIImage::Color, FiniteColor()),
			MemberProperty(UIImageProperties::RaycastTarget, "RaycastTarget", "Raycast Target", &::TomCat::UIImage::RaycastTarget),
			MemberProperty(UIImageProperties::PreserveAspect, "PreserveAspect", "Preserve Aspect", &::TomCat::UIImage::PreserveAspect)
		}));

		result.push_back(MakeDescriptor<::TomCat::UIText>(
			::TomCat::ComponentIds::UIText, "TomCat.UIText", "UI Text", {
			MemberProperty(UITextProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIText::Enabled),
			AssetMemberProperty(UITextProperties::Font, "Font", "Font Asset",
				&::TomCat::UIText::Font, AssetType::Font),
			AssetMemberProperty(UITextProperties::FallbackFont, "FallbackFont",
				"Fallback Font Asset", &::TomCat::UIText::FallbackFont, AssetType::Font),
			AssetMemberProperty(UITextProperties::EmojiFont, "EmojiFont",
				"Emoji Font Asset", &::TomCat::UIText::EmojiFont, AssetType::Font),
			MemberProperty(UITextProperties::Text, "Text", "Text", &::TomCat::UIText::Text, ValidText()),
			MemberProperty(UITextProperties::FontSize, "FontSize", "Font Size", &::TomCat::UIText::FontSize, FiniteFloat(1.0f, 10000.0f)),
			MemberProperty(UITextProperties::Color, "Color", "Color", &::TomCat::UIText::Color, FiniteColor()),
			MemberProperty(UITextProperties::Alignment, "Alignment", "Alignment", &::TomCat::UIText::Alignment,
				[](const TextAlignment& value, std::string& error) { if (value < TextAlignment::Left || value > TextAlignment::Right) { error = "invalid text alignment"; return false; } return true; }),
			MemberProperty(UITextProperties::Wrap, "Wrap", "Wrap", &::TomCat::UIText::Wrap),
			MemberProperty(UITextProperties::LineSpacing, "LineSpacing", "Line Spacing", &::TomCat::UIText::LineSpacing, FiniteFloat(0.1f, 10.0f)),
			MemberProperty(UITextProperties::RaycastTarget, "RaycastTarget", "Raycast Target", &::TomCat::UIText::RaycastTarget)
		}));

		result.push_back(MakeDescriptor<::TomCat::UIButton>(
			::TomCat::ComponentIds::UIButton, "TomCat.UIButton", "UI Button", {
			MemberProperty(UIButtonProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIButton::Enabled),
			MemberProperty(UIButtonProperties::Interactable, "Interactable", "Interactable", &::TomCat::UIButton::Interactable),
			MemberProperty(UIButtonProperties::NormalColor, "NormalColor", "Normal Color", &::TomCat::UIButton::NormalColor, FiniteColor()),
			MemberProperty(UIButtonProperties::HoverColor, "HoverColor", "Hover Color", &::TomCat::UIButton::HoverColor, FiniteColor()),
			MemberProperty(UIButtonProperties::PressedColor, "PressedColor", "Pressed Color", &::TomCat::UIButton::PressedColor, FiniteColor()),
			MemberProperty(UIButtonProperties::SelectedColor, "SelectedColor", "Selected Color", &::TomCat::UIButton::SelectedColor, FiniteColor())
		}));

		result.push_back(MakeDescriptor<::TomCat::UIEventSystem>(
			::TomCat::ComponentIds::UIEventSystem,
			"TomCat.UIEventSystem", "UI Event System", {
			MemberProperty(UIEventSystemProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIEventSystem::Enabled),
			MemberProperty(UIEventSystemProperties::ConsumeGameplayInput, "ConsumeGameplayInput", "Consume Gameplay Input", &::TomCat::UIEventSystem::ConsumeGameplayInput),
			MemberProperty(UIEventSystemProperties::WrapNavigation, "WrapNavigation", "Wrap Navigation", &::TomCat::UIEventSystem::WrapNavigation)
		}));

		result.push_back(MakeDescriptor<::TomCat::UILayoutGroup>(
			::TomCat::ComponentIds::UILayoutGroup,
			"TomCat.UILayoutGroup", "UI Layout Group", {
			MemberProperty(UILayoutGroupProperties::Enabled, "Enabled", "Enabled", &::TomCat::UILayoutGroup::Enabled),
			MemberProperty(UILayoutGroupProperties::Direction, "Direction", "Direction", &::TomCat::UILayoutGroup::Direction,
				[](const UILayoutDirection& value, std::string& error) { if (value < UILayoutDirection::Horizontal || value > UILayoutDirection::Vertical) { error = "invalid UI layout direction"; return false; } return true; }),
			MemberProperty(UILayoutGroupProperties::Spacing, "Spacing", "Spacing", &::TomCat::UILayoutGroup::Spacing, FiniteFloat(0.0f, 100000.0f)),
			MemberProperty(UILayoutGroupProperties::Padding, "Padding", "Padding L/B/R/T", &::TomCat::UILayoutGroup::Padding,
				[](const glm::vec4& value, std::string& error) { if (!Finite(value) || glm::any(glm::lessThan(value, glm::vec4(0.0f)))) { error = "layout padding must be finite and nonnegative"; return false; } return true; }),
			MemberProperty(UILayoutGroupProperties::ControlChildSize, "ControlChildSize", "Control Child Size", &::TomCat::UILayoutGroup::ControlChildSize),
			MemberProperty(UILayoutGroupProperties::ChildSize, "ChildSize", "Child Size", &::TomCat::UILayoutGroup::ChildSize,
				[](const glm::vec2& value, std::string& error) { if (!Finite(value) || glm::any(glm::lessThan(value, glm::vec2(0.0f)))) { error = "child size must be finite and nonnegative"; return false; } return true; })
		}));
		return result;
	}

}
