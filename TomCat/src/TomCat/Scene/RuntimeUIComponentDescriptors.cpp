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
#include <unordered_set>

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

		void ResetTransient(::TomCat::UIButton& value)
		{
			value.RuntimeHovered = false;
			value.RuntimePressed = false;
			value.RuntimeFocused = false;
			value.RuntimeClickedThisFrame = false;
			value.RuntimeClickSerial = 0;
		}

		void ResetTransient(::TomCat::UISlider& value)
		{
			value.RuntimeDragging = value.RuntimeFocused = false;
			value.RuntimeChangeSerial = 0;
		}

		void ResetTransient(::TomCat::UIInputField& value)
		{
			value.RuntimeFocused = false;
			value.RuntimeCaret = value.RuntimeSelectionAnchor = 0;
			value.RuntimeChangeSerial = 0;
			value.RuntimeLastInputFrame = 0;
		}

		void ResetTransient(::TomCat::UILocalization& value)
		{
			value.RuntimeTableSource.clear();
			value.RuntimeTranslations.clear();
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

		auto NonnegativeVector2()
		{
			return [](const glm::vec2& value, std::string& error)
			{
				if (!Finite(value) || glm::any(glm::lessThan(value, glm::vec2(0.0f))))
				{ error = "Vector2 must be finite and nonnegative"; return false; }
				return true;
			};
		}

		auto ValidLocalizationTable()
		{
			return [](const std::string& value, std::string& error)
			{
				try
				{
					bool valid = false;
					FontAtlasBuilder::DecodeUTF8(value, &valid);
					if (!valid || value.size() > 65536)
						throw std::runtime_error("localization table must be valid UTF-8, at most 65536 bytes");
					const auto table = YAML::Load(value);
					if (!table.IsMap()) throw std::runtime_error("localization table must map locales to translation maps");
					std::unordered_set<std::string> locales;
					for (const auto& locale : table)
					{
						if (!locale.first.IsScalar() || !locale.second.IsMap()
							|| !locales.emplace(locale.first.as<std::string>()).second)
							throw std::runtime_error("locales must be unique scalar names with translation maps");
						std::unordered_set<std::string> keys;
						for (const auto& translation : locale.second)
							if (!translation.first.IsScalar() || !translation.second.IsScalar()
								|| !keys.emplace(translation.first.as<std::string>()).second)
								throw std::runtime_error("translation keys must be unique and values must be text");
					}
					return true;
				}
				catch (const std::exception& exception)
				{ error = exception.what(); return false; }
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
					"Font", "Font", &::TomCat::TextRenderer::Font, AssetType::Font),
				AssetMemberProperty(::TomCat::ComponentIds::TextRendererProperties::FallbackFont,
					"FallbackFont", "Fallback Font", &::TomCat::TextRenderer::FallbackFont, AssetType::Font),
				AssetMemberProperty(::TomCat::ComponentIds::TextRendererProperties::EmojiFont,
					"EmojiFont", "Emoji Font", &::TomCat::TextRenderer::EmojiFont, AssetType::Font),
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
			AssetMemberProperty(UIImageProperties::Image, "Image", "Source Image",
				&::TomCat::UIImage::Image, AssetType::Texture2D, true),
			MemberProperty(UIImageProperties::Color, "Color", "Color", &::TomCat::UIImage::Color, FiniteColor()),
			MemberProperty(UIImageProperties::RaycastTarget, "RaycastTarget", "Raycast Target", &::TomCat::UIImage::RaycastTarget),
			MemberProperty(UIImageProperties::PreserveAspect, "PreserveAspect", "Preserve Aspect", &::TomCat::UIImage::PreserveAspect)
		}));

		result.push_back(MakeDescriptor<::TomCat::UIText>(
			::TomCat::ComponentIds::UIText, "TomCat.UIText", "UI Text", {
			MemberProperty(UITextProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIText::Enabled),
			AssetMemberProperty(UITextProperties::Font, "Font", "Font",
				&::TomCat::UIText::Font, AssetType::Font),
			AssetMemberProperty(UITextProperties::FallbackFont, "FallbackFont",
				"Fallback Font", &::TomCat::UIText::FallbackFont, AssetType::Font),
			AssetMemberProperty(UITextProperties::EmojiFont, "EmojiFont",
				"Emoji Font", &::TomCat::UIText::EmojiFont, AssetType::Font),
			MemberProperty(UITextProperties::Text, "Text", "Text", &::TomCat::UIText::Text, ValidText()),
			MemberProperty(UITextProperties::FontSize, "FontSize", "Font Size", &::TomCat::UIText::FontSize, FiniteFloat(1.0f, 10000.0f)),
			MemberProperty(UITextProperties::Color, "Color", "Color", &::TomCat::UIText::Color, FiniteColor()),
			MemberProperty(UITextProperties::Alignment, "Alignment", "Alignment", &::TomCat::UIText::Alignment,
				[](const TextAlignment& value, std::string& error) { if (value < TextAlignment::Left || value > TextAlignment::Right) { error = "invalid text alignment"; return false; } return true; }),
			MemberProperty(UITextProperties::Wrap, "Wrap", "Wrap", &::TomCat::UIText::Wrap),
			MemberProperty(UITextProperties::LineSpacing, "LineSpacing", "Line Spacing", &::TomCat::UIText::LineSpacing, FiniteFloat(0.1f, 10.0f)),
			MemberProperty(UITextProperties::RaycastTarget, "RaycastTarget", "Raycast Target", &::TomCat::UIText::RaycastTarget)
		}));

		auto buttonDescriptor = MakeDescriptor<::TomCat::UIButton>(
			::TomCat::ComponentIds::UIButton, "TomCat.UIButton", "UI Button", {
			MemberProperty(UIButtonProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIButton::Enabled),
			MemberProperty(UIButtonProperties::Interactable, "Interactable", "Interactable", &::TomCat::UIButton::Interactable),
			MemberProperty(UIButtonProperties::NormalColor, "NormalColor", "Normal Color", &::TomCat::UIButton::NormalColor, FiniteColor()),
			MemberProperty(UIButtonProperties::HoverColor, "HoverColor", "Hover Color", &::TomCat::UIButton::HoverColor, FiniteColor()),
			MemberProperty(UIButtonProperties::PressedColor, "PressedColor", "Pressed Color", &::TomCat::UIButton::PressedColor, FiniteColor()),
			MemberProperty(UIButtonProperties::SelectedColor, "SelectedColor", "Selected Color", &::TomCat::UIButton::SelectedColor, FiniteColor()),
			MemberProperty(UIButtonProperties::DisabledColor, "DisabledColor", "Disabled Color", &::TomCat::UIButton::DisabledColor, FiniteColor()),
			MemberProperty(UIButtonProperties::ColorMultiplier, "ColorMultiplier", "Color Multiplier", &::TomCat::UIButton::ColorMultiplier, FiniteFloat(0.0f, 5.0f))
		});
		const ComponentDescriptor::EncodeFn encodeButtonProperties =
			buttonDescriptor.Encode;
		const ComponentDescriptor::DecodeFn decodeButtonProperties =
			buttonDescriptor.Decode;
		buttonDescriptor.SchemaVersion = 3;
		buttonDescriptor.Migrations.push_back({ 1, 2,
			[](YAML::Node& record, std::string& error)
			{
				const YAML::Node oldProperties = record["Properties"];
				if (!oldProperties || !oldProperties.IsSequence())
				{
					error = "TomCat.UIButton v1 Properties must be a sequence";
					return false;
				}
				YAML::Node payload(YAML::NodeType::Map);
				payload["Fields"] = oldProperties;
				payload["OnClick"] = YAML::Node(YAML::NodeType::Sequence);
				record["Properties"] = payload;
				return true;
			} });
		buttonDescriptor.Migrations.push_back({ 2, 3,
			[](YAML::Node& record, std::string& error)
			{
				YAML::Node fields = record["Properties"]["Fields"];
				if (!fields || !fields.IsSequence())
				{
					error = "TomCat.UIButton v2 Fields must be a sequence";
					return false;
				}
				auto appendIfMissing = [&fields, &error](uint64_t id,
					const char* stableName, const YAML::Node& value)
				{
					for (const YAML::Node& existing : fields)
					{
						if (!existing.IsMap() || !existing["PropertyId"]
							|| !existing["StableName"])
							continue;
						const uint64_t existingID = existing["PropertyId"].as<uint64_t>();
						const std::string existingName =
							existing["StableName"].as<std::string>();
						if (existingID == id || existingName == stableName)
						{
							if (existingID == id && existingName == stableName)
								return true;
							error = "TomCat.UIButton v2 property identity conflicts with "
								+ std::string(stableName);
							return false;
						}
					}
					YAML::Node property(YAML::NodeType::Map);
					property["PropertyId"] = id;
					property["StableName"] = stableName;
					property["Value"] = value;
					fields.push_back(property);
					return true;
				};
				YAML::Node disabled(YAML::NodeType::Sequence);
				disabled.push_back(0.52f);
				disabled.push_back(0.52f);
				disabled.push_back(0.52f);
				disabled.push_back(0.5f);
				if (!appendIfMissing(UIButtonProperties::DisabledColor,
					"DisabledColor", disabled)
					|| !appendIfMissing(UIButtonProperties::ColorMultiplier,
						"ColorMultiplier", YAML::Node(1.0f)))
					return false;
				record["Properties"]["Fields"] = fields;
				return true;
			} });
		buttonDescriptor.Encode = [encodeButtonProperties](
			const ComponentDescriptor& descriptor, Entity entity,
			YAML::Emitter& output, std::string& error)
		{
			output << YAML::BeginMap;
			output << YAML::Key << "Fields" << YAML::Value;
			if (!encodeButtonProperties(descriptor, entity, output, error))
				return false;
			output << YAML::Key << "OnClick" << YAML::Value << YAML::BeginSeq;
			for (const UIButtonOnClickListener& listener :
				entity.GetComponent<::TomCat::UIButton>().OnClick)
			{
				output << YAML::BeginMap
					<< YAML::Key << "Enabled" << YAML::Value << listener.Enabled
					<< YAML::Key << "TargetEntity" << YAML::Value
					<< static_cast<uint64_t>(listener.TargetEntity)
					<< YAML::Key << "TargetAttachmentID" << YAML::Value
					<< static_cast<uint64_t>(listener.TargetAttachmentID)
					<< YAML::Key << "ScriptAsset" << YAML::Value
					<< static_cast<uint64_t>(listener.ScriptAsset)
					<< YAML::Key << "MethodName" << YAML::Value << listener.MethodName
					<< YAML::EndMap;
			}
			output << YAML::EndSeq << YAML::EndMap;
			return output.good();
		};
		buttonDescriptor.Decode = [decodeButtonProperties](
			const ComponentDescriptor& descriptor, Entity entity,
			const YAML::Node& payload, std::string& error)
		{
			const ::TomCat::UIButton original =
				entity.GetComponent<::TomCat::UIButton>();
			try
			{
				if (!payload.IsMap() || payload.size() != 2 || !payload["Fields"]
					|| !payload["OnClick"] || !payload["OnClick"].IsSequence())
					throw std::runtime_error(
						"Properties must contain exactly Fields and OnClick");
				std::unordered_set<std::string> keys;
				for (const auto& pair : payload)
				{
					const std::string key = pair.first.as<std::string>();
					if ((key != "Fields" && key != "OnClick")
						|| !keys.emplace(key).second)
						throw std::runtime_error("Properties contains an unknown or duplicate key");
				}
				if (!decodeButtonProperties(descriptor, entity, payload["Fields"], error))
					throw std::runtime_error(error.empty()
						? "button field validation failed" : error);
				const YAML::Node calls = payload["OnClick"];
				if (calls.size() > 1024)
					throw std::runtime_error("OnClick exceeds 1024 persistent listeners");
				std::vector<UIButtonOnClickListener> listeners;
				listeners.reserve(calls.size());
				for (size_t index = 0; index < calls.size(); ++index)
				{
					const YAML::Node call = calls[index];
					if (!call.IsMap() || call.size() != 5 || !call["Enabled"]
						|| !call["TargetEntity"] || !call["TargetAttachmentID"]
						|| !call["ScriptAsset"]
						|| !call["MethodName"])
						throw std::runtime_error("OnClick listener has an incompatible shape");
					std::unordered_set<std::string> callKeys;
					for (const auto& pair : call)
					{
						const std::string key = pair.first.as<std::string>();
						if ((key != "Enabled" && key != "TargetEntity"
							&& key != "TargetAttachmentID"
							&& key != "ScriptAsset" && key != "MethodName")
							|| !callKeys.emplace(key).second)
							throw std::runtime_error(
								"OnClick listener contains an unknown or duplicate key");
					}
					UIButtonOnClickListener listener;
					listener.Enabled = call["Enabled"].as<bool>();
					listener.TargetEntity = UUID(call["TargetEntity"].as<uint64_t>());
					listener.TargetAttachmentID = UUID(
						call["TargetAttachmentID"].as<uint64_t>());
					listener.ScriptAsset = AssetHandle(call["ScriptAsset"].as<uint64_t>());
					listener.MethodName = call["MethodName"].as<std::string>();
					const bool unassigned = static_cast<uint64_t>(listener.TargetEntity) == 0
						&& static_cast<uint64_t>(listener.TargetAttachmentID) == 0
						&& static_cast<uint64_t>(listener.ScriptAsset) == 0
						&& listener.MethodName.empty();
					const bool assigned = static_cast<uint64_t>(listener.TargetEntity) != 0
						&& static_cast<uint64_t>(listener.TargetAttachmentID) != 0
						&& static_cast<uint64_t>(listener.ScriptAsset) != 0
						&& !listener.MethodName.empty();
					const bool targetOnly =
						static_cast<uint64_t>(listener.TargetEntity) != 0
						&& static_cast<uint64_t>(listener.TargetAttachmentID) == 0
						&& static_cast<uint64_t>(listener.ScriptAsset) == 0
						&& listener.MethodName.empty();
					if ((!unassigned && !targetOnly && !assigned)
						|| listener.MethodName.size() > 512
						|| listener.MethodName.find('\0') != std::string::npos)
						throw std::runtime_error(
							"OnClick listener must be empty, target-only, or fully assigned");
					listeners.push_back(std::move(listener));
				}
				entity.GetComponent<::TomCat::UIButton>().OnClick = std::move(listeners);
				return true;
			}
			catch (const std::exception& exception)
			{
				entity.GetComponent<::TomCat::UIButton>() = original;
				if (error.empty())
					error = descriptor.StableName + ": " + exception.what();
				return false;
			}
		};
		buttonDescriptor.RemapEntityReferences = [](Entity entity,
			const EntityReferenceMapper& mapper, std::string& error)
		{
			auto& listeners = entity.GetComponent<::TomCat::UIButton>().OnClick;
			for (size_t index = 0; index < listeners.size(); ++index)
			{
				uint64_t target = static_cast<uint64_t>(listeners[index].TargetEntity);
				if (!mapper(target, "TomCat.UIButton.OnClick["
					+ std::to_string(index) + "].TargetEntity", error))
					return false;
				listeners[index].TargetEntity = UUID(target);
			}
			return true;
		};
		result.push_back(std::move(buttonDescriptor));

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
		result.push_back(MakeDescriptor<::TomCat::UISlider>(ComponentIds::UISlider, "TomCat.UISlider", "UISlider", {
			MemberProperty(UISliderProperties::Enabled, "Enabled", "Enabled", &::TomCat::UISlider::Enabled),
			MemberProperty(UISliderProperties::Interactable, "Interactable", "Interactable", &::TomCat::UISlider::Interactable),
			MemberProperty(UISliderProperties::Minimum, "Minimum", "Minimum", &::TomCat::UISlider::Minimum, FiniteFloat(-1000000.0f, 1000000.0f)),
			MemberProperty(UISliderProperties::Maximum, "Maximum", "Maximum", &::TomCat::UISlider::Maximum, FiniteFloat(-1000000.0f, 1000000.0f)),
			MemberProperty(UISliderProperties::Value, "Value", "Value", &::TomCat::UISlider::Value, FiniteFloat(-1000000.0f, 1000000.0f)),
			MemberProperty(UISliderProperties::Step, "Step", "Step", &::TomCat::UISlider::Step, FiniteFloat(0.0f, 1000000.0f)),
			MemberProperty(UISliderProperties::WholeNumbers, "WholeNumbers", "WholeNumbers", &::TomCat::UISlider::WholeNumbers),
			MemberProperty(UISliderProperties::Vertical, "Vertical", "Vertical", &::TomCat::UISlider::Vertical),
			MemberProperty(UISliderProperties::TrackColor, "TrackColor", "TrackColor", &::TomCat::UISlider::TrackColor, FiniteColor()),
			MemberProperty(UISliderProperties::FillColor, "FillColor", "FillColor", &::TomCat::UISlider::FillColor, FiniteColor())
		}));
		result.push_back(MakeDescriptor<::TomCat::UIScrollView>(ComponentIds::UIScrollView, "TomCat.UIScrollView", "UIScrollView", {
			MemberProperty(UIScrollViewProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIScrollView::Enabled),
			MemberProperty(UIScrollViewProperties::Horizontal, "Horizontal", "Horizontal", &::TomCat::UIScrollView::Horizontal),
			MemberProperty(UIScrollViewProperties::Vertical, "Vertical", "Vertical", &::TomCat::UIScrollView::Vertical),
			MemberProperty(UIScrollViewProperties::ContentSize, "ContentSize", "ContentSize", &::TomCat::UIScrollView::ContentSize, NonnegativeVector2()),
			MemberProperty(UIScrollViewProperties::Offset, "Offset", "Offset", &::TomCat::UIScrollView::Offset, NonnegativeVector2()),
			MemberProperty(UIScrollViewProperties::ScrollSpeed, "ScrollSpeed", "ScrollSpeed", &::TomCat::UIScrollView::ScrollSpeed, FiniteFloat(0.0f, 1000000.0f))
		}));
		result.push_back(MakeDescriptor<::TomCat::UIInputField>(ComponentIds::UIInputField, "TomCat.UIInputField", "UIInputField", {
			MemberProperty(UIInputFieldProperties::Enabled, "Enabled", "Enabled", &::TomCat::UIInputField::Enabled),
			MemberProperty(UIInputFieldProperties::Interactable, "Interactable", "Interactable", &::TomCat::UIInputField::Interactable),
			MemberProperty(UIInputFieldProperties::Text, "Text", "Text", &::TomCat::UIInputField::Text, ValidText()),
			MemberProperty(UIInputFieldProperties::Placeholder, "Placeholder", "Placeholder", &::TomCat::UIInputField::Placeholder, ValidText()),
			MemberProperty(UIInputFieldProperties::CharacterLimit, "CharacterLimit", "CharacterLimit", &::TomCat::UIInputField::CharacterLimit, [](const uint32_t& value, std::string& error) { if (value > 65536) { error = "character limit must not exceed 65536"; return false; } return true; }),
			MemberProperty(UIInputFieldProperties::Password, "Password", "Password", &::TomCat::UIInputField::Password),
			MemberProperty(UIInputFieldProperties::ReadOnly, "ReadOnly", "ReadOnly", &::TomCat::UIInputField::ReadOnly)
		}));
		result.push_back(MakeDescriptor<::TomCat::UITheme>(ComponentIds::UITheme, "TomCat.UITheme", "UITheme", {
			MemberProperty(UIThemeProperties::Enabled, "Enabled", "Enabled", &::TomCat::UITheme::Enabled),
			MemberProperty(UIThemeProperties::TextColor, "TextColor", "TextColor", &::TomCat::UITheme::TextColor, FiniteColor()),
			MemberProperty(UIThemeProperties::ImageColor, "ImageColor", "ImageColor", &::TomCat::UITheme::ImageColor, FiniteColor()),
			MemberProperty(UIThemeProperties::AccentColor, "AccentColor", "AccentColor", &::TomCat::UITheme::AccentColor, FiniteColor()),
			AssetMemberProperty(UIThemeProperties::Font, "Font", "Font", &::TomCat::UITheme::Font, AssetType::Font),
			MemberProperty(UIThemeProperties::FontScale, "FontScale", "FontScale", &::TomCat::UITheme::FontScale, FiniteFloat(0.01f, 100.0f))
		}));
		result.push_back(MakeDescriptor<::TomCat::UILocalization>(ComponentIds::UILocalization, "TomCat.UILocalization", "UILocalization", {
			MemberProperty(UILocalizationProperties::Enabled, "Enabled", "Enabled", &::TomCat::UILocalization::Enabled),
			MemberProperty(UILocalizationProperties::Locale, "Locale", "Locale", &::TomCat::UILocalization::Locale, ValidText()),
			MemberProperty(UILocalizationProperties::FallbackLocale, "FallbackLocale", "FallbackLocale", &::TomCat::UILocalization::FallbackLocale, ValidText()),
			MemberProperty(UILocalizationProperties::Table, "Table", "Table", &::TomCat::UILocalization::Table, ValidLocalizationTable())
		}));
		result.push_back(MakeDescriptor<::TomCat::UILocalizedText>(ComponentIds::UILocalizedText, "TomCat.UILocalizedText", "UILocalizedText", {
			MemberProperty(UILocalizedTextProperties::Enabled, "Enabled", "Enabled", &::TomCat::UILocalizedText::Enabled),
			MemberProperty(UILocalizedTextProperties::Key, "Key", "Key", &::TomCat::UILocalizedText::Key, ValidText())
		}));
		return result;
	}

}
