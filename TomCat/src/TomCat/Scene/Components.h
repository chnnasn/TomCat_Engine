#pragma once

#include "TomCat/Asset/Asset.h"
#include "SceneCamera.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Renderer/Texture.h"
#include "TomCat/Scripting/ScriptField.h"

#include <ekit/component.hpp>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>



namespace TomCat {
	

	struct ID
	{
		EKIT_COMPONENT(ID);
		UUID id;

		ID() = default;
		ID(const ID&) = default;
		ID(const UUID& uuid)
			: id(uuid) {}
	};

	struct Tag
	{
		EKIT_COMPONENT(Tag);
		std::string _Tag;
		// Gameplay activation. The persisted property is still named "Visible" for
		// Scene 9-11 compatibility, but its runtime meaning has always matched
		// Unity-style ActiveSelf.
		bool ActiveSelf = true;

		Tag() = default;
		Tag(const Tag&) = default;
		Tag(const std::string& tag)
			: _Tag(tag) {
		}
	};


	struct Transform
	{
		EKIT_COMPONENT(Transform);
		// World-space transform.
		glm::vec3 _Translation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _Scale{ 1.0f, 1.0f, 1.0f };

		// Local transform relative to the direct parent.
		glm::vec3 _LocalTranslation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _LocalRotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 _LocalScale{ 1.0f, 1.0f, 1.0f };

		Transform() = default;
		Transform(const Transform&) = default;
		Transform(const glm::vec3& translation)
			: _Translation(translation), _LocalTranslation(translation) {
		}

		glm::mat4 GetTransform() const
		{
			return Math::ComposeTransform(_Translation, _Rotation, _Scale);
		}

		glm::mat4 GetLocalTransform() const
		{
			return Math::ComposeTransform(_LocalTranslation, _LocalRotation, _LocalScale);
		}

		bool SetTransform(const glm::mat4& transform)
		{
			return Math::DecomposeTransform(transform, _Translation, _Rotation, _Scale);
		}

		bool SetLocalTransform(const glm::mat4& transform)
		{
			return Math::DecomposeTransform(transform, _LocalTranslation, _LocalRotation, _LocalScale);
		}

	};

	struct SpriteRenderer
	{
		EKIT_COMPONENT(SpriteRenderer);
		bool Enabled = true;
		glm::vec4 _Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		AssetHandle SpriteHandle = AssetHandle(0);
		// Runtime-only resolved sprite. SpriteHandle is the serialized source of truth.
		Ref<Texture2D> Sprite;
		// Editor animation preview uses a runtime-only handle so scrubbing never
		// mutates the serialized SpriteHandle (and therefore cannot leak into Save).
		bool RuntimeSpriteOverrideActive = false;
		AssetHandle RuntimeSpriteOverrideHandle = AssetHandle(0);
		float TilingFactor = 1.0f;
		// Lower layers/orders are submitted first. Numeric layer identities stay
		// stable if an editor-facing display name is renamed later.
		int32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;

		SpriteRenderer() = default;
		SpriteRenderer(const SpriteRenderer&) = default;
		SpriteRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	// Authoring-only Scene visibility. This optional component is added only when
	// an Entity is hidden in the Editor; runtime systems deliberately ignore it.
	// Keeping this state separate from Tag::ActiveSelf lets authors hide a subtree
	// without disabling scripts, rendering in Game view, audio, or physics.
	struct EditorVisibility
	{
		EKIT_COMPONENT(EditorVisibility);
		bool Hidden = true;
	};

	struct SpriteAnimationFrame
	{
		EKIT_COMPONENT(SpriteAnimationFrame);
		// Stable source/sub-asset identity. Paths never enter animation data.
		AssetHandle SpriteHandle = AssetHandle(0);
		float DurationSeconds = 1.0f / 12.0f;
	};

	struct SpriteAnimationClip
	{
		EKIT_COMPONENT(SpriteAnimationClip);
		std::string Name = "Default";
		bool Loop = true;
		std::vector<SpriteAnimationFrame> Frames;
	};

	enum class AnimatorParameterType : uint8_t
	{
		Bool = 0,
		Int,
		Float,
		Trigger
	};

	struct AnimatorParameter
	{
		EKIT_COMPONENT(AnimatorParameter);
		std::string Name;
		AnimatorParameterType Type = AnimatorParameterType::Bool;
		bool BoolValue = false;
		int32_t IntValue = 0;
		float FloatValue = 0.0f;
	};

	enum class AnimatorConditionMode : uint8_t
	{
		If = 0,
		IfNot,
		Greater,
		Less,
		Equals,
		NotEqual
	};

	struct AnimatorCondition
	{
		EKIT_COMPONENT(AnimatorCondition);
		std::string Parameter;
		AnimatorConditionMode Mode = AnimatorConditionMode::If;
		float Threshold = 0.0f;
	};

	struct AnimatorState
	{
		EKIT_COMPONENT(AnimatorState);
		std::string Name;
		std::string Clip;
		float Speed = 1.0f;
	};

	struct AnimatorTransition
	{
		EKIT_COMPONENT(AnimatorTransition);
		// AnyState ignores FromState. Otherwise FromState must name one state.
		std::string FromState;
		std::string ToState;
		bool AnyState = false;
		// Negative disables exit time; otherwise this is normalized [0, 1].
		float ExitTime = -1.0f;
		std::vector<AnimatorCondition> Conditions;
	};

	struct SpriteAnimator
	{
		EKIT_COMPONENT(SpriteAnimator);
		static constexpr uint32_t InvalidClipIndex = 0xffffffffu;

		bool Enabled = true;
		// Optional external graph. At runtime the controller and its referenced
		// Animation Clip assets replace the embedded authoring snapshot below.
		AssetHandle ControllerHandle = AssetHandle(0);
		bool PlayOnStart = true;
		// Empty selects the first clip. Names are unique inside one Animator.
		std::string InitialClip;
		float Speed = 1.0f;
		std::vector<SpriteAnimationClip> Clips;
		// Empty States preserves V1 direct-clip playback exactly. A state machine
		// reuses Clips as its immutable frame sources.
		std::string InitialState;
		std::vector<AnimatorParameter> Parameters;
		std::vector<AnimatorState> States;
		std::vector<AnimatorTransition> Transitions;

		// Transient playback state; never serialized and reset by Scene/Prefab copy.
		uint32_t RuntimeClipIndex = InvalidClipIndex;
		uint32_t RuntimeFrameIndex = 0;
		double RuntimeFrameElapsed = 0.0;
		bool RuntimePlaying = false;
		bool RuntimeInitialized = false;
		uint32_t RuntimeStateIndex = InvalidClipIndex;
		double RuntimeStateElapsed = 0.0;
	};

	// Stable scene data used by the editor to choose an Entity icon. Entity is the
	// Unity-style default; Automatic may be selected explicitly to resolve from the
	// current components.
	enum class EntityIconMode : uint8_t
	{
		Automatic = 0,
		Entity,
		Camera,
		Sprite,
		Rigidbody2D,
		Collider2D
	};

	// Project-defined gameplay identity shared by every Entity, including entities
	// that currently have no renderer or collider. Layer is a stable 0-based slot;
	// the project's Physics2DSettings decides which entity layers may interact.
	struct EntityMetadata
	{
		EKIT_COMPONENT(EntityMetadata);
		std::string GameplayTag = "Untagged";
		uint8_t Layer = 0;
		EntityIconMode HierarchyIcon = EntityIconMode::Entity;

		EntityMetadata() = default;
		EntityMetadata(const EntityMetadata&) = default;
	};

	struct LineRenderer
	{
		EKIT_COMPONENT(LineRenderer);
		bool Enabled = true;
		glm::vec4 _Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec3 Start{ -0.5f, 0.0f, 0.0f };
		glm::vec3 End{ 0.5f, 0.0f, 0.0f };
		float Width = 1.0f;

		LineRenderer() = default;
		LineRenderer(const LineRenderer&) = default;
		LineRenderer(const glm::vec4& color)
			: _Color(color) {
		}
	};

	// A sparse authoring grid. Each occupied coordinate owns one stable Sprite
	// reference, so atlased sub-sprites work without introducing a second asset
	// identity system. Cells are kept in deterministic row-major order by the
	// Tilemap2D authoring helpers.
	struct TilemapCell
	{
		EKIT_COMPONENT(TilemapCell);
		glm::ivec2 Coordinate{ 0, 0 };
		AssetHandle SpriteHandle = AssetHandle(0);
		glm::vec4 Tint{ 1.0f };
		bool FlipX = false;
		bool FlipY = false;
		int32_t RotationQuarterTurns = 0;
	};

	enum class GridCellLayout2D : int32_t
	{
		Rectangle = 0,
		Isometric,
		IsometricZAsY,
		Hexagon
	};

	enum class GridCellSwizzle2D : int32_t
	{
		XYZ = 0,
		XZY,
		YXZ,
		YZX,
		ZXY,
		ZYX
	};

	// Unity-style layout owner. Tilemaps normally live below an Entity carrying
	// this component. Tilemap2D keeps its original CellSize/CellGap fields so old
	// scenes continue to load and render when no Grid2D is present.
	struct Grid2D
	{
		EKIT_COMPONENT(Grid2D);
		glm::vec2 CellSize{ 1.0f, 1.0f };
		glm::vec2 CellGap{ 0.0f, 0.0f };
		GridCellLayout2D Layout = GridCellLayout2D::Rectangle;
		GridCellSwizzle2D Swizzle = GridCellSwizzle2D::XYZ;
	};

	enum class TilemapSortOrder2D : int32_t
	{
		BottomLeft = 0,
		BottomRight,
		TopLeft,
		TopRight
	};

	enum class TilemapRendererMode2D : int32_t
	{
		Chunk = 0,
		Individual
	};

	enum class TilemapChunkCulling2D : int32_t
	{
		Auto = 0,
		Manual
	};

	// Rendering policy is deliberately separate from sparse tile data. The
	// legacy fields on Tilemap2D remain the fallback when this component is absent.
	struct TilemapRenderer2D
	{
		EKIT_COMPONENT(TilemapRenderer2D);
		bool Enabled = true;
		TilemapSortOrder2D SortOrder = TilemapSortOrder2D::BottomLeft;
		TilemapRendererMode2D Mode = TilemapRendererMode2D::Chunk;
		TilemapChunkCulling2D DetectChunkCulling = TilemapChunkCulling2D::Auto;
		int32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;
		AssetHandle MaterialHandle = AssetHandle(0);
	};

	struct Tilemap2D
	{
		EKIT_COMPONENT(Tilemap2D);
		bool Enabled = true;
		glm::vec2 CellSize{ 1.0f, 1.0f };
		glm::vec2 CellGap{ 0.0f, 0.0f };
		int32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;
		std::vector<TilemapCell> Cells;
	};

	struct Particle2D
	{
		EKIT_COMPONENT(Particle2D);
		glm::vec2 Position{ 0.0f };
		glm::vec2 Velocity{ 0.0f };
		float Age = 0.0f;
		float Lifetime = 1.0f;
		float StartSize = 1.0f;
		float EndSize = 0.0f;
	};

	// CPU simulated and deterministically seeded. Runtime particles are omitted
	// from Scene/Prefab persistence by the component descriptor.
	struct ParticleSystem2D
	{
		EKIT_COMPONENT(ParticleSystem2D);
		bool Enabled = true;
		bool PlayOnStart = true;
		bool Loop = true;
		float Duration = 5.0f;
		float EmissionRate = 10.0f;
		int32_t MaxParticles = 256;
		float StartLifetime = 1.0f;
		float StartSpeed = 1.0f;
		float StartSize = 0.2f;
		float EndSize = 0.0f;
		float GravityScale = 0.0f;
		// 2D authored forward is local +X.
		glm::vec2 Direction{ 1.0f, 0.0f };
		float SpreadDegrees = 25.0f;
		glm::vec4 StartColor{ 1.0f };
		glm::vec4 EndColor{ 1.0f, 1.0f, 1.0f, 0.0f };
		AssetHandle SpriteHandle = AssetHandle(0);
		int32_t SortingLayer = 0;
		int32_t OrderInLayer = 0;
		uint32_t Seed = 1;

		bool RuntimePlaying = false;
		bool RuntimeInitialized = false;
		float RuntimeTime = 0.0f;
		float RuntimeEmissionAccumulator = 0.0f;
		uint32_t RuntimeRandomState = 1;
		std::vector<Particle2D> RuntimeParticles;
	};

	enum class Light2DType : int32_t
	{
		Global = 0,
		Point = 1
	};

	struct Light2D
	{
		EKIT_COMPONENT(Light2D);
		bool Enabled = true;
		Light2DType Type = Light2DType::Point;
		glm::vec4 Color{ 1.0f };
		float Intensity = 1.0f;
		float Radius = 5.0f;
		float Falloff = 1.0f;
	};

	struct C_Camera
	{
		EKIT_COMPONENT(C_Camera);
		SceneCamera _Camera;
		bool Enabled = true;
		bool Primary = true; // TODO: think about moving to Scene
		bool FixedAspectRatio = false;
		glm::vec4 BackgroundColor = glm::vec4(0.53f, 0.81f, 0.92f, 1.0f);  // 默认天蓝色

		C_Camera() = default;
		C_Camera(const C_Camera&) = default;
	};

	// Authoring/serialization state only. CLR instances and handles are owned by
	// ScriptEngine and must never enter the ECS registry.
	struct CSharpScriptEntry
	{
		EKIT_COMPONENT(CSharpScriptEntry);
		// UUID's default constructor produces a nonzero stable attachment identity.
		// Scene::Copy preserves it; DuplicateEntity regenerates it for the copy.
		UUID AttachmentID;
		bool Enabled = true;
		AssetHandle ScriptAsset{ 0 };
		std::string LastKnownClassName;
		ScriptFieldMap Fields;
	};

	struct CSharpScripts
	{
		EKIT_COMPONENT(CSharpScripts);
		std::vector<CSharpScriptEntry> Scripts;
	};

	// First component implemented entirely through ComponentRegistry. It is kept
	// intentionally small so the registry's add/remove, property, copy and
	// persistence paths have an executable vertical-slice fixture.
	struct HealthComponent
	{
		EKIT_COMPONENT(HealthComponent);
		int32_t Maximum = 100;
		int32_t Current = 100;
		bool Invulnerable = false;
	};

	// Audio references are stable AssetHandles. RuntimeVoice and
	// RuntimeClipHandle are transient and are always cleared while copying or
	// deserializing scene/prefab authoring data.
	struct AudioSource
	{
		EKIT_COMPONENT(AudioSource);
		bool Enabled = true;
		AssetHandle Clip = AssetHandle(0);
		bool PlayOnStart = true;
		bool Loop = false;
		bool Streaming = false;
		float Volume = 1.0f;
		float Pitch = 1.0f;
		// 0 is transform-independent 2D playback; 1 applies full panning and
		// distance attenuation against the primary active AudioListener.
		float SpatialBlend = 0.0f;
		float MinDistance = 1.0f;
		float MaxDistance = 25.0f;
		// Matches AudioMixerGroup: 0 Master, 1 Music, 2 SFX. Kept as a byte in
		// the scene component to avoid coupling Components.h to the backend.
		uint8_t MixerGroup = 2;

		uint64_t RuntimeVoice = 0;
		AssetHandle RuntimeClipHandle = AssetHandle(0);
		bool RuntimeAutoPlayEvaluated = false;
		bool RuntimeStreaming = false;
	};

	struct AudioListener
	{
		EKIT_COMPONENT(AudioListener);
		bool Enabled = true;
		bool Primary = true;
	};

	// Runtime text/UI authoring data. These components are rendered by the
	// engine's Renderer2D path; ImGui remains Editor chrome only.
	enum class TextAlignment : int32_t
	{
		Left = 0,
		Center = 1,
		Right = 2
	};

	struct TextRenderer
	{
		EKIT_COMPONENT(TextRenderer);
		bool Enabled = true;
		AssetHandle Font = AssetHandle(BuiltInLegacyRuntimeFontHandleValue);
		AssetHandle FallbackFont = AssetHandle(0);
		AssetHandle EmojiFont = AssetHandle(0);
		std::string Text = "Text";
		float FontSize = 1.0f;
		glm::vec4 Color{ 1.0f };
		TextAlignment Alignment = TextAlignment::Left;
		float MaxWidth = 0.0f;
		float LineSpacing = 1.0f;
	};

	enum class CanvasScaleMode : int32_t
	{
		ConstantPixelSize = 0,
		ScaleWithScreenSize = 1
	};

	struct Canvas
	{
		EKIT_COMPONENT(Canvas);
		bool Enabled = true;
		CanvasScaleMode ScaleMode = CanvasScaleMode::ScaleWithScreenSize;
		glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
		float MatchWidthOrHeight = 0.5f;
		float ScaleFactor = 1.0f;
		float ReferenceDPI = 96.0f;
		int32_t SortingOrder = 0;
	};

	struct RectTransform
	{
		EKIT_COMPONENT(RectTransform);
		glm::vec2 AnchorMin{ 0.5f, 0.5f };
		glm::vec2 AnchorMax{ 0.5f, 0.5f };
		glm::vec2 Pivot{ 0.5f, 0.5f };
		glm::vec2 AnchoredPosition{ 0.0f };
		glm::vec2 SizeDelta{ 100.0f, 100.0f };
		bool ClipChildren = false;

		// Layout-space rectangles written by RuntimeUISystem. RuntimeClipRect is a
		// compatibility/diagnostic AABB; exact transformed clipping is retained in
		// RuntimeUILayoutSnapshot. They are transient and deliberately omitted from
		// ComponentRegistry persistence.
		glm::vec4 RuntimeRect{ 0.0f };
		glm::vec4 RuntimeClipRect{ 0.0f };
	};

	struct UIImage
	{
		EKIT_COMPONENT(UIImage);
		bool Enabled = true;
		AssetHandle Image = AssetHandle(0);
		glm::vec4 Color{ 1.0f };
		bool RaycastTarget = true;
		bool PreserveAspect = false;
	};

	struct UIText
	{
		EKIT_COMPONENT(UIText);
		bool Enabled = true;
		AssetHandle Font = AssetHandle(BuiltInLegacyRuntimeFontHandleValue);
		AssetHandle FallbackFont = AssetHandle(0);
		AssetHandle EmojiFont = AssetHandle(0);
		std::string Text = "Text";
		float FontSize = 24.0f;
		glm::vec4 Color{ 1.0f };
		TextAlignment Alignment = TextAlignment::Left;
		bool Wrap = true;
		float LineSpacing = 1.0f;
		bool RaycastTarget = false;
	};

	// Persistent, authoring-time listener for UIButton.OnClick. TargetAttachmentID
	// is the exact C# component identity; ScriptAsset is retained for authoring
	// metadata and for validating that a stale listener never calls another script.
	// An all-zero/empty target is retained as an unassigned UnityEvent-style slot.
	struct UIButtonOnClickListener
	{
		EKIT_COMPONENT(UIButtonOnClickListener);
		bool Enabled = true;
		UUID TargetEntity{ 0 };
		UUID TargetAttachmentID{ 0 };
		AssetHandle ScriptAsset{ 0 };
		std::string MethodName;
	};

	struct UIButton
	{
		EKIT_COMPONENT(UIButton);
		bool Enabled = true;
		bool Interactable = true;
		glm::vec4 NormalColor{ 1.0f };
		glm::vec4 HoverColor{ 0.9f, 0.9f, 0.9f, 1.0f };
		glm::vec4 PressedColor{ 0.72f, 0.72f, 0.72f, 1.0f };
		glm::vec4 SelectedColor{ 0.82f, 0.9f, 1.0f, 1.0f };
		glm::vec4 DisabledColor{ 0.52f, 0.52f, 0.52f, 0.5f };
		float ColorMultiplier = 1.0f;
		std::vector<UIButtonOnClickListener> OnClick;

		bool RuntimeHovered = false;
		bool RuntimePressed = false;
		bool RuntimeFocused = false;
		bool RuntimeClickedThisFrame = false;
		uint64_t RuntimeClickSerial = 0;
	};

	struct UIEventSystem
	{
		EKIT_COMPONENT(UIEventSystem);
		bool Enabled = true;
		bool ConsumeGameplayInput = true;
		bool WrapNavigation = true;
	};

	struct UISlider
	{
		EKIT_COMPONENT(UISlider);
		bool Enabled = true;
		bool Interactable = true;
		float Minimum = 0.0f;
		float Maximum = 1.0f;
		float Value = 0.5f;
		float Step = 0.01f;
		bool WholeNumbers = false;
		bool Vertical = false;
		glm::vec4 TrackColor{ 0.2f, 0.2f, 0.2f, 1.0f };
		glm::vec4 FillColor{ 0.3f, 0.6f, 1.0f, 1.0f };
		bool RuntimeDragging = false;
		bool RuntimeFocused = false;
		uint64_t RuntimeChangeSerial = 0;
	};

	// The viewport owns its children's clipping and scroll offset. Combine with
	// UILayoutGroup for lists; ContentSize is authored in Canvas reference pixels.
	struct UIScrollView
	{
		EKIT_COMPONENT(UIScrollView);
		bool Enabled = true;
		bool Horizontal = false;
		bool Vertical = true;
		glm::vec2 ContentSize{ 300.0f, 600.0f };
		glm::vec2 Offset{ 0.0f };
		float ScrollSpeed = 40.0f;
	};

	struct UIInputField
	{
		EKIT_COMPONENT(UIInputField);
		bool Enabled = true;
		bool Interactable = true;
		std::string Text;
		std::string Placeholder = "Enter text";
		uint32_t CharacterLimit = 1024;
		bool Password = false;
		bool ReadOnly = false;
		bool RuntimeFocused = false;
		// UTF-8 byte boundaries. Kept transient so duplication never retains focus.
		uint32_t RuntimeCaret = 0;
		uint32_t RuntimeSelectionAnchor = 0;
		uint64_t RuntimeChangeSerial = 0;
		uint64_t RuntimeLastInputFrame = 0;
	};

	// Inherited by descendants. Authored image/text colors remain multiplicative
	// tints, allowing a theme change without rewriting component authoring data.
	struct UITheme
	{
		EKIT_COMPONENT(UITheme);
		bool Enabled = true;
		glm::vec4 TextColor{ 1.0f };
		glm::vec4 ImageColor{ 1.0f };
		glm::vec4 AccentColor{ 0.3f, 0.6f, 1.0f, 1.0f };
		AssetHandle Font{ 0 };
		float FontScale = 1.0f;
	};

	struct UILocalization
	{
		EKIT_COMPONENT(UILocalization);
		bool Enabled = true;
		std::string Locale = "en";
		std::string FallbackLocale = "en";
		// YAML/JSON map: { en: { play: Play }, zh: { play: ... } }.
		std::string Table = "{}";
		std::string RuntimeTableSource;
		std::map<std::string, std::map<std::string, std::string>> RuntimeTranslations;
	};

	struct UILocalizedText
	{
		EKIT_COMPONENT(UILocalizedText);
		bool Enabled = true;
		std::string Key;
	};

	enum class UILayoutDirection : int32_t
	{
		Horizontal = 0,
		Vertical = 1
	};

	struct UILayoutGroup
	{
		EKIT_COMPONENT(UILayoutGroup);
		bool Enabled = true;
		UILayoutDirection Direction = UILayoutDirection::Vertical;
		float Spacing = 8.0f;
		// left, bottom, right, top
		glm::vec4 Padding{ 0.0f };
		bool ControlChildSize = false;
		glm::vec2 ChildSize{ 100.0f, 32.0f };
	};


	// Physics
	struct Rigidbody2D
	{
		EKIT_COMPONENT(Rigidbody2D);
		bool Enabled = true;
		enum class BodyType { Static = 0, Dynamic, Kinematic };
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		// Storage for runtime
		void* RuntimeBody = nullptr;

		Rigidbody2D() = default;
		Rigidbody2D(const Rigidbody2D&) = default;
	};

	struct BoxCollider2D
	{
		EKIT_COMPONENT(BoxCollider2D);
		bool Enabled = true;
		bool IsTrigger = false;
		// Box2D category/mask bits. CollisionLayer must contain at least one bit.
		uint16_t CollisionLayer = 0x0001;
		uint16_t CollisionMask = 0xFFFF;
		glm::vec2 Offset = { 0.0f, 0.0f };
		glm::vec2 Size = { 0.5f, 0.5f };

		// TODO(Yan): move into physics material in the future maybe
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float RestitutionThreshold = 0.5f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		BoxCollider2D() = default;
		BoxCollider2D(const BoxCollider2D&) = default;
	};

	struct CircleCollider2D
	{
		EKIT_COMPONENT(CircleCollider2D);
		bool Enabled = true;
		bool IsTrigger = false;
		// Box2D category/mask bits. CollisionLayer must contain at least one bit.
		uint16_t CollisionLayer = 0x0001;
		uint16_t CollisionMask = 0xFFFF;
		glm::vec2 Offset = { 0.0f, 0.0f };
		float Radius = 0.5f;

		// Keep these rules in sync with BoxCollider2D. A Box2D circle cannot become
		// an ellipse, so runtime/editor geometry uses Radius multiplied by the
		// largest absolute world X/Y scale component. Offset uses the same 2D
		// transform as Box2D: translation XY, rotation Z, and signed scale XY.
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		CircleCollider2D() = default;
		CircleCollider2D(const CircleCollider2D&) = default;
	};

	struct DistanceJoint2D
	{
		EKIT_COMPONENT(DistanceJoint2D);
		bool Enabled = true;
		UUID ConnectedEntity{ 0 };
		// Local anchors, in each body's unscaled local coordinate system.
		glm::vec2 Anchor{ 0.0f, 0.0f };
		glm::vec2 ConnectedAnchor{ 0.0f, 0.0f };
		float Distance = 1.0f;
		float Frequency = 0.0f;
		// Box2D damping ratio in the inclusive [0, 1] range.
		float Damping = 0.0f;
		bool CollideConnected = false;

		// Storage for runtime
		void* RuntimeJoint = nullptr;

		DistanceJoint2D() = default;
		DistanceJoint2D(const DistanceJoint2D&) = default;
	};

}
