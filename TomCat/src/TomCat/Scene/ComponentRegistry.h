#pragma once

#include "TomCat/Asset/Asset.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Scene/Entity.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace YAML {
	class Emitter;
	class Node;
}

namespace TomCat {

	namespace ComponentIds {
		// Explicit, persisted 64-bit UUID values. These constants are deliberately
		// assigned and must never be replaced by enum ordinals, hashes or typeid.
		inline constexpr uint64_t Health = 0x8d0df196efd946a1ULL;
		namespace HealthProperties {
			inline constexpr uint64_t Maximum = 0x91bc0a20e4f64ed1ULL;
			inline constexpr uint64_t Current = 0xa43fdaf1b97042e7ULL;
			inline constexpr uint64_t Invulnerable = 0xcb63837a8a7a4c12ULL;
		}
		inline constexpr uint64_t TextRenderer = 0x9f01000000000001ULL;
		inline constexpr uint64_t Canvas = 0x9f01000000000002ULL;
		inline constexpr uint64_t RectTransform = 0x9f01000000000003ULL;
		inline constexpr uint64_t UIImage = 0x9f01000000000004ULL;
		inline constexpr uint64_t UIText = 0x9f01000000000005ULL;
		inline constexpr uint64_t UIButton = 0x9f01000000000006ULL;
		inline constexpr uint64_t UIEventSystem = 0x9f01000000000007ULL;
		inline constexpr uint64_t UILayoutGroup = 0x9f01000000000008ULL;

			namespace TextRendererProperties {
			inline constexpr uint64_t Enabled = 0x9f01100000000001ULL;
			inline constexpr uint64_t Font = 0x9f01100000000002ULL;
			inline constexpr uint64_t Text = 0x9f01100000000003ULL;
			inline constexpr uint64_t FontSize = 0x9f01100000000004ULL;
			inline constexpr uint64_t Color = 0x9f01100000000005ULL;
			inline constexpr uint64_t Alignment = 0x9f01100000000006ULL;
			inline constexpr uint64_t MaxWidth = 0x9f01100000000007ULL;
			inline constexpr uint64_t LineSpacing = 0x9f01100000000008ULL;
			inline constexpr uint64_t FallbackFont = 0x9f01100000000009ULL;
			inline constexpr uint64_t EmojiFont = 0x9f0110000000000aULL;
		}
		namespace CanvasProperties {
			inline constexpr uint64_t Enabled = 0x9f01200000000001ULL;
			inline constexpr uint64_t ScaleMode = 0x9f01200000000002ULL;
			inline constexpr uint64_t ReferenceResolution = 0x9f01200000000003ULL;
			inline constexpr uint64_t MatchWidthOrHeight = 0x9f01200000000004ULL;
			inline constexpr uint64_t ScaleFactor = 0x9f01200000000005ULL;
			inline constexpr uint64_t ReferenceDPI = 0x9f01200000000006ULL;
			inline constexpr uint64_t SortingOrder = 0x9f01200000000007ULL;
		}
		namespace RectTransformProperties {
			inline constexpr uint64_t AnchorMin = 0x9f01300000000001ULL;
			inline constexpr uint64_t AnchorMax = 0x9f01300000000002ULL;
			inline constexpr uint64_t Pivot = 0x9f01300000000003ULL;
			inline constexpr uint64_t AnchoredPosition = 0x9f01300000000004ULL;
			inline constexpr uint64_t SizeDelta = 0x9f01300000000005ULL;
			inline constexpr uint64_t ClipChildren = 0x9f01300000000006ULL;
		}
		namespace UIImageProperties {
			inline constexpr uint64_t Enabled = 0x9f01400000000001ULL;
			inline constexpr uint64_t Image = 0x9f01400000000002ULL;
			inline constexpr uint64_t Color = 0x9f01400000000003ULL;
			inline constexpr uint64_t RaycastTarget = 0x9f01400000000004ULL;
			inline constexpr uint64_t PreserveAspect = 0x9f01400000000005ULL;
		}
		namespace UITextProperties {
			inline constexpr uint64_t Enabled = 0x9f01500000000001ULL;
			inline constexpr uint64_t Font = 0x9f01500000000002ULL;
			inline constexpr uint64_t Text = 0x9f01500000000003ULL;
			inline constexpr uint64_t FontSize = 0x9f01500000000004ULL;
			inline constexpr uint64_t Color = 0x9f01500000000005ULL;
			inline constexpr uint64_t Alignment = 0x9f01500000000006ULL;
			inline constexpr uint64_t Wrap = 0x9f01500000000007ULL;
			inline constexpr uint64_t LineSpacing = 0x9f01500000000008ULL;
			inline constexpr uint64_t RaycastTarget = 0x9f01500000000009ULL;
			inline constexpr uint64_t FallbackFont = 0x9f0150000000000aULL;
			inline constexpr uint64_t EmojiFont = 0x9f0150000000000bULL;
		}
		namespace UIButtonProperties {
			inline constexpr uint64_t Enabled = 0x9f01600000000001ULL;
			inline constexpr uint64_t Interactable = 0x9f01600000000002ULL;
			inline constexpr uint64_t NormalColor = 0x9f01600000000003ULL;
			inline constexpr uint64_t HoverColor = 0x9f01600000000004ULL;
			inline constexpr uint64_t PressedColor = 0x9f01600000000005ULL;
			inline constexpr uint64_t SelectedColor = 0x9f01600000000006ULL;
		}
		namespace UIEventSystemProperties {
			inline constexpr uint64_t Enabled = 0x9f01700000000001ULL;
			inline constexpr uint64_t ConsumeGameplayInput = 0x9f01700000000002ULL;
			inline constexpr uint64_t WrapNavigation = 0x9f01700000000003ULL;
		}
		namespace UILayoutGroupProperties {
			inline constexpr uint64_t Enabled = 0x9f01800000000001ULL;
			inline constexpr uint64_t Direction = 0x9f01800000000002ULL;
			inline constexpr uint64_t Spacing = 0x9f01800000000003ULL;
			inline constexpr uint64_t Padding = 0x9f01800000000004ULL;
			inline constexpr uint64_t ControlChildSize = 0x9f01800000000005ULL;
			inline constexpr uint64_t ChildSize = 0x9f01800000000006ULL;
		}
	}

	enum class PropertyKind : uint32_t
	{
		Bool = 1,
		Int32,
		Int64,
		UInt32,
		UInt64,
		Float,
		Double,
		String,
		Vector2,
		Vector3,
		Vector4
	};

	using PropertyValue = std::variant<bool, int32_t, int64_t, uint32_t, uint64_t,
		float, double, std::string, glm::vec2, glm::vec3, glm::vec4>;

	// Optional editor semantics layered over the persisted PropertyKind. Asset
	// references deliberately remain UInt64 values in Scene 11 and the managed
	// ABI; this metadata only constrains typed authoring controls.
	struct AssetPropertyMetadata
	{
		std::vector<AssetType> AcceptedTypes;
		bool AllowSubAssets = false;

		bool Accepts(AssetType type, bool isSubAsset = false) const
		{
			return (!isSubAsset || AllowSubAssets)
				&& std::find(AcceptedTypes.begin(), AcceptedTypes.end(), type)
					!= AcceptedTypes.end();
		}
	};

	struct PropertyDescriptor
	{
		UUID PropertyId = UUID(0);
		std::string StableName;
		std::string DisplayName;
		PropertyKind Kind = PropertyKind::Int32;
		std::function<PropertyValue(Entity)> Get;
		// Every editor, loader and scripting write goes through this setter so a
		// component can validate values and issue subsystem dirty notifications.
		std::function<bool(Entity, const PropertyValue&, std::string&)> Set;
		std::optional<AssetPropertyMetadata> AssetReference;
	};

	struct ComponentDescriptor
	{
		using HasFn = std::function<bool(Entity)>;
		using AddFn = std::function<bool(Entity, std::string&)>;
		using RemoveFn = std::function<bool(Entity, std::string&)>;
		using CopyFn = std::function<bool(Entity, Entity, std::string&)>;
		using EncodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			YAML::Emitter&, std::string&)>;
		using DecodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			const YAML::Node&, std::string&)>;

		UUID TypeId = UUID(0);
		std::string StableName;
		std::string DisplayName;
		uint32_t SchemaVersion = 1;
		std::vector<PropertyDescriptor> Properties;

		HasFn Has;
		AddFn Add;
		RemoveFn Remove;
		CopyFn Copy;
		EncodeFn Encode;
		DecodeFn Decode;
		bool InspectorVisible = true;
		// Only components whose structural/property mutations are safe at managed
		// callback boundaries opt into the ComponentApi capability.
		bool ScriptAccessible = false;
	};

	// Unknown plugin components are retained as validated YAML records. A scene
	// may therefore be opened and saved on a machine without the plugin without
	// losing the component or any nested property payload.
	struct OpaqueComponentRecord
	{
		UUID TypeId = UUID(0);
		std::string StableName;
		uint32_t SchemaVersion = 0;
		std::string SerializedRecord;
	};

	struct OpaqueComponents
	{
		std::vector<OpaqueComponentRecord> Records;
	};

	class ComponentRegistry final
	{
	public:
		static ComponentRegistry& Get();

		bool Register(ComponentDescriptor descriptor, std::string& error);
		const ComponentDescriptor* Find(UUID typeId) const;
		std::span<const ComponentDescriptor> GetDescriptors() const
		{
			return m_Descriptors;
		}

		bool Has(Entity entity, UUID typeId) const;
		bool Add(Entity entity, UUID typeId, std::string& error) const;
		bool Remove(Entity entity, UUID typeId, std::string& error) const;
		bool CopyRegisteredComponents(Entity source, Entity destination,
			std::string& error) const;

		// Emits/reads the Schema 11 entity field named Components.
		bool EncodeComponents(Entity entity, YAML::Emitter& output,
			std::string& error) const;
		bool DecodeComponents(Entity entity, const YAML::Node& components,
			std::string& error) const;

	private:
		ComponentRegistry();
		std::vector<ComponentDescriptor> m_Descriptors;
	};

}
