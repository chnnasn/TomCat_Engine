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
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace YAML {
	class Emitter;
	class Node;
}

namespace TomCat {

	namespace ComponentIds {
		inline constexpr uint64_t Light3D = 0x9f00000000000017ULL;
		inline constexpr uint64_t Environment3D = 0x9f00000000000018ULL;
		inline constexpr uint64_t MeshRenderer = 0x9f00000000000016ULL;
		// Explicit, persisted 64-bit UUID values. These constants are deliberately
		// assigned and must never be replaced by enum ordinals, hashes or typeid.
		inline constexpr uint64_t ID = 0x9f00000000000001ULL;
		inline constexpr uint64_t Tag = 0x9f00000000000002ULL;
		inline constexpr uint64_t EntityMetadata = 0x9f00000000000003ULL;
		inline constexpr uint64_t Transform = 0x9f00000000000004ULL;
		inline constexpr uint64_t Camera = 0x9f00000000000005ULL;
		inline constexpr uint64_t SpriteRenderer = 0x9f00000000000006ULL;
		inline constexpr uint64_t SpriteAnimator = 0x9f00000000000007ULL;
		inline constexpr uint64_t LineRenderer = 0x9f00000000000008ULL;
		inline constexpr uint64_t CSharpScripts = 0x9f00000000000009ULL;
		inline constexpr uint64_t AudioSource = 0x9f0000000000000aULL;
		inline constexpr uint64_t AudioListener = 0x9f0000000000000bULL;
		inline constexpr uint64_t Rigidbody2D = 0x9f0000000000000cULL;
		inline constexpr uint64_t BoxCollider2D = 0x9f0000000000000dULL;
		inline constexpr uint64_t CircleCollider2D = 0x9f0000000000000eULL;
		inline constexpr uint64_t DistanceJoint2D = 0x9f0000000000000fULL;
		inline constexpr uint64_t EditorVisibility = 0x9f00000000000010ULL;
		inline constexpr uint64_t Tilemap2D = 0x9f00000000000011ULL;
		inline constexpr uint64_t ParticleSystem2D = 0x9f00000000000012ULL;
		inline constexpr uint64_t Light2D = 0x9f00000000000013ULL;
		inline constexpr uint64_t Grid2D = 0x9f00000000000014ULL;
		inline constexpr uint64_t TilemapRenderer2D = 0x9f00000000000015ULL;

		// Property IDs below preserve the numeric IDs exposed by the original
		// gameplay ABI. They are now persisted 64-bit identities owned by the
		// descriptor, so managed bindings and tools no longer duplicate a switch.
		namespace IDProperties { inline constexpr uint64_t Value = 1; }
		namespace TagProperties {
			inline constexpr uint64_t Name = 1;
			inline constexpr uint64_t ActiveSelf = 2;
			// Historical source/wire alias. Property ID 2 has always represented
			// gameplay activation, so its persisted StableName remains "Visible".
			inline constexpr uint64_t Visible = ActiveSelf;
		}
		namespace EditorVisibilityProperties {
			inline constexpr uint64_t Hidden = 1;
		}
		namespace EntityMetadataProperties {
			inline constexpr uint64_t GameplayTag = 1;
			inline constexpr uint64_t Layer = 2;
			inline constexpr uint64_t HierarchyIcon = 3;
		}
		namespace TransformProperties {
			inline constexpr uint64_t Translation = 1;
			inline constexpr uint64_t Rotation = 2;
			inline constexpr uint64_t Scale = 3;
			inline constexpr uint64_t LocalTranslation = 4;
			inline constexpr uint64_t LocalRotation = 5;
			inline constexpr uint64_t LocalScale = 6;
		}
		namespace Rigidbody2DProperties {
			inline constexpr uint64_t Enabled = 100;
			inline constexpr uint64_t BodyType = 101;
			inline constexpr uint64_t FixedRotation = 102;
		}
		namespace SpriteRendererProperties {
			inline constexpr uint64_t Enabled = 200;
			inline constexpr uint64_t Color = 201;
			inline constexpr uint64_t Sprite = 202;
			inline constexpr uint64_t TilingFactor = 203;
			inline constexpr uint64_t SortingLayer = 204;
			inline constexpr uint64_t OrderInLayer = 205;
		}
		namespace CameraProperties {
			inline constexpr uint64_t Primary = 300;
			inline constexpr uint64_t FixedAspectRatio = 301;
			inline constexpr uint64_t BackgroundColor = 302;
			inline constexpr uint64_t ProjectionType = 303;
			inline constexpr uint64_t OrthographicSize = 304;
			inline constexpr uint64_t OrthographicNear = 305;
			inline constexpr uint64_t OrthographicFar = 306;
			inline constexpr uint64_t PerspectiveFov = 307;
			inline constexpr uint64_t PerspectiveNear = 308;
			inline constexpr uint64_t PerspectiveFar = 309;
			inline constexpr uint64_t Enabled = 310;
		}
		namespace BoxCollider2DProperties {
			inline constexpr uint64_t Enabled = 400;
			inline constexpr uint64_t IsTrigger = 401;
			inline constexpr uint64_t CollisionLayer = 402;
			inline constexpr uint64_t CollisionMask = 403;
			inline constexpr uint64_t Offset = 404;
			inline constexpr uint64_t Size = 405;
			inline constexpr uint64_t Density = 406;
			inline constexpr uint64_t Friction = 407;
			inline constexpr uint64_t Restitution = 408;
			inline constexpr uint64_t RestitutionThreshold = 409;
		}
		namespace CircleCollider2DProperties {
			inline constexpr uint64_t Enabled = 500;
			inline constexpr uint64_t IsTrigger = 501;
			inline constexpr uint64_t CollisionLayer = 502;
			inline constexpr uint64_t CollisionMask = 503;
			inline constexpr uint64_t Offset = 504;
			inline constexpr uint64_t Radius = 505;
			inline constexpr uint64_t Density = 506;
			inline constexpr uint64_t Friction = 507;
			inline constexpr uint64_t Restitution = 508;
		}
		namespace DistanceJoint2DProperties {
			inline constexpr uint64_t Enabled = 600;
			inline constexpr uint64_t ConnectedEntity = 601;
			inline constexpr uint64_t Anchor = 602;
			inline constexpr uint64_t ConnectedAnchor = 603;
			inline constexpr uint64_t Distance = 604;
			inline constexpr uint64_t Frequency = 605;
			inline constexpr uint64_t Damping = 606;
			inline constexpr uint64_t CollideConnected = 607;
		}
		namespace SpriteAnimatorProperties {
			inline constexpr uint64_t Enabled = 700;
			inline constexpr uint64_t Speed = 701;
			inline constexpr uint64_t PlayOnStart = 704;
			inline constexpr uint64_t InitialClip = 705;
			inline constexpr uint64_t Controller = 706;
		}
		namespace LineRendererProperties {
			inline constexpr uint64_t Enabled = 800;
			inline constexpr uint64_t Color = 801;
			inline constexpr uint64_t Start = 802;
			inline constexpr uint64_t End = 803;
			inline constexpr uint64_t Width = 804;
		}
		namespace AudioSourceProperties {
			inline constexpr uint64_t Enabled = 900;
			inline constexpr uint64_t Clip = 901;
			inline constexpr uint64_t PlayOnStart = 902;
			inline constexpr uint64_t Loop = 903;
			inline constexpr uint64_t Streaming = 904;
			inline constexpr uint64_t Volume = 905;
			inline constexpr uint64_t Pitch = 906;
			inline constexpr uint64_t SpatialBlend = 907;
			inline constexpr uint64_t MinDistance = 908;
			inline constexpr uint64_t MaxDistance = 909;
			inline constexpr uint64_t MixerGroup = 910;
		}
		namespace AudioListenerProperties {
			inline constexpr uint64_t Enabled = 920;
			inline constexpr uint64_t Primary = 921;
		}
		namespace Tilemap2DProperties {
			inline constexpr uint64_t Enabled = 1000;
			inline constexpr uint64_t CellSize = 1001;
			inline constexpr uint64_t CellGap = 1002;
			inline constexpr uint64_t SortingLayer = 1003;
			inline constexpr uint64_t OrderInLayer = 1004;
		}
		namespace Grid2DProperties {
			inline constexpr uint64_t CellSize = 1300;
			inline constexpr uint64_t CellGap = 1301;
			inline constexpr uint64_t Layout = 1302;
			inline constexpr uint64_t Swizzle = 1303;
		}
		namespace TilemapRenderer2DProperties {
			inline constexpr uint64_t Enabled = 1400;
			inline constexpr uint64_t SortOrder = 1401;
			inline constexpr uint64_t Mode = 1402;
			inline constexpr uint64_t DetectChunkCulling = 1403;
			inline constexpr uint64_t SortingLayer = 1404;
			inline constexpr uint64_t OrderInLayer = 1405;
			inline constexpr uint64_t Material = 1406;
		}
		namespace ParticleSystem2DProperties {
			inline constexpr uint64_t Enabled = 1100;
			inline constexpr uint64_t PlayOnStart = 1101;
			inline constexpr uint64_t Loop = 1102;
			inline constexpr uint64_t Duration = 1103;
			inline constexpr uint64_t EmissionRate = 1104;
			inline constexpr uint64_t MaxParticles = 1105;
			inline constexpr uint64_t StartLifetime = 1106;
			inline constexpr uint64_t StartSpeed = 1107;
			inline constexpr uint64_t StartSize = 1108;
			inline constexpr uint64_t EndSize = 1109;
			inline constexpr uint64_t GravityScale = 1110;
			inline constexpr uint64_t Direction = 1111;
			inline constexpr uint64_t SpreadDegrees = 1112;
			inline constexpr uint64_t StartColor = 1113;
			inline constexpr uint64_t EndColor = 1114;
			inline constexpr uint64_t Sprite = 1115;
			inline constexpr uint64_t SortingLayer = 1116;
			inline constexpr uint64_t OrderInLayer = 1117;
			inline constexpr uint64_t Seed = 1118;
		}
		namespace Light2DProperties {
			inline constexpr uint64_t Enabled = 1200;
			inline constexpr uint64_t Type = 1201;
			inline constexpr uint64_t Color = 1202;
			inline constexpr uint64_t Intensity = 1203;
			inline constexpr uint64_t Radius = 1204;
			inline constexpr uint64_t Falloff = 1205;
		}
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
		inline constexpr uint64_t UISlider = 0x9f01000000000009ULL;
		namespace UISliderProperties {
			inline constexpr uint64_t Enabled = 0x9f01900000000001ULL;
			inline constexpr uint64_t Interactable = 0x9f01900000000002ULL;
			inline constexpr uint64_t Minimum = 0x9f01900000000003ULL;
			inline constexpr uint64_t Maximum = 0x9f01900000000004ULL;
			inline constexpr uint64_t Value = 0x9f01900000000005ULL;
			inline constexpr uint64_t Step = 0x9f01900000000006ULL;
			inline constexpr uint64_t WholeNumbers = 0x9f01900000000007ULL;
			inline constexpr uint64_t Vertical = 0x9f01900000000008ULL;
			inline constexpr uint64_t TrackColor = 0x9f01900000000009ULL;
			inline constexpr uint64_t FillColor = 0x9f0190000000000aULL;
		}
		inline constexpr uint64_t UIScrollView = 0x9f0100000000000aULL;
		namespace UIScrollViewProperties {
			inline constexpr uint64_t Enabled = 0x9f01a00000000001ULL;
			inline constexpr uint64_t Horizontal = 0x9f01a00000000002ULL;
			inline constexpr uint64_t Vertical = 0x9f01a00000000003ULL;
			inline constexpr uint64_t ContentSize = 0x9f01a00000000004ULL;
			inline constexpr uint64_t Offset = 0x9f01a00000000005ULL;
			inline constexpr uint64_t ScrollSpeed = 0x9f01a00000000006ULL;
		}
		inline constexpr uint64_t UIInputField = 0x9f0100000000000bULL;
		namespace UIInputFieldProperties {
			inline constexpr uint64_t Enabled = 0x9f01b00000000001ULL;
			inline constexpr uint64_t Interactable = 0x9f01b00000000002ULL;
			inline constexpr uint64_t Text = 0x9f01b00000000003ULL;
			inline constexpr uint64_t Placeholder = 0x9f01b00000000004ULL;
			inline constexpr uint64_t CharacterLimit = 0x9f01b00000000005ULL;
			inline constexpr uint64_t Password = 0x9f01b00000000006ULL;
			inline constexpr uint64_t ReadOnly = 0x9f01b00000000007ULL;
		}
		inline constexpr uint64_t UITheme = 0x9f0100000000000cULL;
		namespace UIThemeProperties {
			inline constexpr uint64_t Enabled = 0x9f01c00000000001ULL;
			inline constexpr uint64_t TextColor = 0x9f01c00000000002ULL;
			inline constexpr uint64_t ImageColor = 0x9f01c00000000003ULL;
			inline constexpr uint64_t AccentColor = 0x9f01c00000000004ULL;
			inline constexpr uint64_t Font = 0x9f01c00000000005ULL;
			inline constexpr uint64_t FontScale = 0x9f01c00000000006ULL;
		}
		inline constexpr uint64_t UILocalization = 0x9f0100000000000dULL;
		namespace UILocalizationProperties {
			inline constexpr uint64_t Enabled = 0x9f01d00000000001ULL;
			inline constexpr uint64_t Locale = 0x9f01d00000000002ULL;
			inline constexpr uint64_t FallbackLocale = 0x9f01d00000000003ULL;
			inline constexpr uint64_t Table = 0x9f01d00000000004ULL;
		}
		inline constexpr uint64_t UILocalizedText = 0x9f0100000000000eULL;
		namespace UILocalizedTextProperties {
			inline constexpr uint64_t Enabled = 0x9f01e00000000001ULL;
			inline constexpr uint64_t Key = 0x9f01e00000000002ULL;
		}


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
			inline constexpr uint64_t DisabledColor = 0x9f01600000000007ULL;
			inline constexpr uint64_t ColorMultiplier = 0x9f01600000000008ULL;
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

	enum class MissingEntityReferencePolicy : uint8_t
	{
		Preserve,
		Clear,
		Reject
	};

	// Descriptor callbacks use the registry-owned mapper so scalar and structured
	// entity references share the same missing-reference policy and diagnostics.
	using EntityReferenceMapper = std::function<bool(uint64_t&, std::string_view,
		std::string&)>;

	enum class ComponentMutationPhase : uint8_t
	{
		Commit,
		Validation
	};

	// Script transactions apply provider callbacks to an isolated Scene first.
	// Providers use this thread-local phase to suppress sidecars, notifications,
	// counters, and every other effect outside the supplied Entity.
	ComponentMutationPhase GetComponentMutationPhase() noexcept;

	class ComponentMutationPhaseScope final
	{
	public:
		explicit ComponentMutationPhaseScope(ComponentMutationPhase phase) noexcept;
		~ComponentMutationPhaseScope() noexcept;
		ComponentMutationPhaseScope(const ComponentMutationPhaseScope&) = delete;
		ComponentMutationPhaseScope& operator=(
			const ComponentMutationPhaseScope&) = delete;
	private:
		ComponentMutationPhase m_Previous;
	};

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
		// Script-accessible properties provide their value on a trivially fresh
		// component. Deferred scripting projections replay descriptor Add in the
		// Validation phase so contextual defaults and component dependencies match
		// commit; third-party providers must explicitly support that phase.
		std::optional<PropertyValue> DefaultValue;
		std::optional<AssetPropertyMetadata> AssetReference;
		// A persisted UUID that identifies another entity. Prefab/duplicate paths
		// remap these properties through the Registry instead of a component switch.
		bool EntityReference = false;
	};

	struct ComponentSchemaMigration
	{
		uint32_t FromVersion = 0;
		uint32_t ToVersion = 0;
		// Receives the complete persisted component record. The registry updates
		// SchemaVersion after a successful step and validates identity itself.
		std::function<bool(YAML::Node&, std::string&)> Migrate;
	};

	struct ComponentDescriptor
	{
		using HasFn = std::function<bool(Entity)>;
		using AddFn = std::function<bool(Entity, std::string&)>;
		using RemoveFn = std::function<bool(Entity, std::string&)>;
		using CopyFn = std::function<bool(Entity, Entity, std::string&)>;
		using EncodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			YAML::Emitter&, std::string&)>;
		using LegacyEncodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			YAML::Emitter&, std::string&)>;
		using LegacyDecodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			const YAML::Node&, std::string&)>;
		using DecodeFn = std::function<bool(const ComponentDescriptor&, Entity,
			const YAML::Node&, std::string&)>;
		using RemapEntityReferencesFn = std::function<bool(Entity,
			const EntityReferenceMapper&, std::string&)>;

		UUID TypeId = UUID(0);
		// Zero is reserved for engine-owned descriptors. Modules use a stable,
		// nonzero provider UUID so their live components can be detached safely.
		UUID ProviderId = UUID(0);
		std::string StableName;
		std::string DisplayName;
		uint32_t SchemaVersion = 1;
		std::vector<ComponentSchemaMigration> Migrations;
		std::vector<PropertyDescriptor> Properties;

		HasFn Has;
		AddFn Add;
		RemoveFn Remove;
		CopyFn Copy;
		EncodeFn Encode;
		DecodeFn Decode;
		// Handles structured or repeated references that cannot be represented by a
		// scalar PropertyDescriptor. Scalar EntityReference properties are remapped
		// automatically before this callback runs.
		RemapEntityReferencesFn RemapEntityReferences;
		// Scene schema 11 mirrors engine components into their historical top-level
		// keys so older readers can still consume newly saved scenes. The callback
		// belongs to the descriptor so this compatibility projection cannot drift
		// into a second centralized component switch in SceneSerializer.
		LegacyEncodeFn EncodeLegacyFields;
		LegacyDecodeFn DecodeLegacyFields;
		bool InspectorVisible = true;
		// Built-ins with rich Editor adapters remain visible in schema enumeration
		// but opt out of the generic property widget.
		bool UseGenericInspector = true;
		// Scene 11 retains its legacy top-level fields for backwards compatibility.
		// Mirrored descriptors are additionally emitted in Components and may apply
		// their values to a component already created by that compatibility reader.
		bool PersistInComponentSequence = true;
		bool DecodeIntoExisting = false;
		bool Removable = true;
		// Controls the Editor Add Component menu independently of InspectorVisible.
		// Intrinsic and authoring-only attachment containers opt out.
		bool AddableInInspector = true;
		// Only components whose structural/property mutations are safe at managed
		// callback boundaries opt into the ComponentApi capability.
		// Script-accessible provider callbacks run first in Validation phase on an
		// isolated Scene and once in Commit phase on the live Scene. Third-party
		// providers must explicitly acknowledge the phase contract and suppress all
		// effects outside the supplied validation Entity.
		bool ScriptAccessible = false;
		bool SupportsTransactionalValidation = false;
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
		bool RemapEntityReferences(Entity entity,
			const std::unordered_map<UUID, UUID>& entityMap,
			MissingEntityReferencePolicy missingPolicy, std::string& error) const;

		// Emits optional historical top-level fields through descriptor-owned
		// compatibility projections. Provider components normally leave this empty.
		bool EncodeLegacyComponents(Entity entity, YAML::Emitter& output,
			std::string& error) const;
		bool DecodeLegacyComponents(Entity entity, const YAML::Node& entityNode,
			std::string& error) const;
		// Emits/reads the Schema 11 entity field named Components.
		bool EncodeComponents(Entity entity, YAML::Emitter& output,
			std::string& error) const;
		bool DecodeComponents(Entity entity, const YAML::Node& components,
			std::string& error) const;

		// Registry mutation and provider callbacks are main-thread operations. On
		// unload, every live provider component is first serialized into an opaque
		// record; descriptors are removed only after all entities were converted.
		bool UnregisterProvider(UUID providerId, std::span<const Entity> liveEntities,
			std::string& error);
		// Converts compatible opaque records back to live components after their
		// provider is registered. Unsupported future/old schemas stay opaque.
		bool RehydrateOpaqueComponents(std::span<const Entity> liveEntities,
			UUID providerId, std::string& error) const;

	private:
		ComponentRegistry();
		std::vector<ComponentDescriptor> m_Descriptors;
	};

}
