#pragma once

#include "TomCat/Runtime/RuntimeCompatibility.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
	#define TC_SCRIPT_CALL __cdecl
#else
	#define TC_SCRIPT_CALL
#endif

namespace TomCat::Scripting {

	inline constexpr uint32_t NativeApiVersion = RuntimeCompatibility::NativeApiVersion;
	inline constexpr uint32_t ManagedApiVersion = RuntimeCompatibility::ManagedApiVersion;
	inline constexpr uint32_t ScriptManifestVersion =
		RuntimeCompatibility::ScriptManifestVersion;

	enum class ScriptStatus : int32_t
	{
		Success = 0,
		InvalidArgument = -1,
		InvalidState = -2,
		NotFound = -3,
		VersionMismatch = -4,
		ManagedException = -5,
		BufferTooSmall = -6,
		WrongThread = -7,
		Unavailable = -8
	};

	struct NativeByteView
	{
		const uint8_t* Data = nullptr;
		uint64_t Length = 0;
	};

	using NativeUtf8View = NativeByteView;

	struct EntityHandleV1
	{
		uint64_t SceneSessionId = 0;
		uint64_t EntityId = 0;
		uint64_t RuntimeGeneration = 0;
	};

	struct ScriptInstanceHandleV1
	{
		uint64_t Value = 0;
	};

	struct NativeVector2
	{
		float X = 0.0f;
		float Y = 0.0f;
	};

	struct NativeVector3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
	};

	struct NativeVector4
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;
	};

	struct NativeMatrix4
	{
		float Values[16]{};
	};

	struct NativeDiagnosticV1
	{
		int32_t Severity = 0;
		int32_t Reserved = 0;
		NativeUtf8View Message;
		NativeUtf8View File;
		int32_t Line = 0;
		int32_t Column = 0;
	};

	struct NativeRaycastHit2D
	{
		EntityHandleV1 Entity;
		NativeVector2 Point;
		NativeVector2 Normal;
		float Fraction = 0.0f;
		int32_t IsTrigger = 0;
		uint32_t CollisionLayer = 0;
	};

	struct NativePhysicsQueryHit2D
	{
		EntityHandleV1 Entity;
		int32_t IsTrigger = 0;
		uint32_t CollisionLayer = 0;
	};

	enum class NativeComponentType : int32_t
	{
		Transform = 1,
		Rigidbody2D = 2,
		BoxCollider2D = 3,
		CircleCollider2D = 4,
		DistanceJoint2D = 5,
		SpriteRenderer = 6,
		Camera = 7,
		SpriteAnimator = 8
	};

	struct NativeScriptAttachmentV1
	{
		EntityHandleV1 Entity;
		uint64_t AttachmentId = 0;
		uint64_t ScriptAsset = 0;
		int32_t Enabled = 0;
		int32_t Reserved = 0;
	};

	enum class NativePhysicsEventKind : uint32_t
	{
		CollisionEnter = 0,
		CollisionExit = 1,
		TriggerEnter = 2,
		TriggerExit = 3
	};

	struct NativePhysicsEventV1
	{
		uint32_t Kind = 0;
		uint32_t Reserved = 0;
		EntityHandleV1 EntityA;
		EntityHandleV1 EntityB;
	};

	using MetadataReceiverV1 = int32_t(TC_SCRIPT_CALL*)(
		NativeByteView bytes, uint64_t receiverToken);

	struct NativeApiV1
	{
		uint32_t Version = NativeApiVersion;
		uint32_t Size = sizeof(NativeApiV1);

		int32_t(TC_SCRIPT_CALL* Log)(int32_t level, NativeUtf8View message) = nullptr;
		int32_t(TC_SCRIPT_CALL* EmitDiagnostic)(const NativeDiagnosticV1* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* IsMainThread)() = nullptr;
		int32_t(TC_SCRIPT_CALL* EntityIsAlive)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntityGetName)(EntityHandleV1 entity, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntitySetName)(EntityHandleV1 entity, NativeUtf8View value) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntityGetTag)(EntityHandleV1 entity, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntitySetTag)(EntityHandleV1 entity, NativeUtf8View value) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntityGetLayer)(EntityHandleV1 entity, uint32_t* layer) = nullptr;
		int32_t(TC_SCRIPT_CALL* EntitySetLayer)(EntityHandleV1 entity, uint32_t layer) = nullptr;
		int32_t(TC_SCRIPT_CALL* DestroyEntityDeferred)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* HasComponent)(EntityHandleV1 entity, int32_t componentType) = nullptr;
		int32_t(TC_SCRIPT_CALL* AddComponentDeferred)(EntityHandleV1 entity, int32_t componentType) = nullptr;
		int32_t(TC_SCRIPT_CALL* RemoveComponentDeferred)(EntityHandleV1 entity, int32_t componentType) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetPosition)(EntityHandleV1 entity, NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetPosition)(EntityHandleV1 entity, NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetRotationEuler)(EntityHandleV1 entity, NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetRotationEuler)(EntityHandleV1 entity, NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetScale)(EntityHandleV1 entity, NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetScale)(EntityHandleV1 entity, NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetWorldMatrix)(EntityHandleV1 entity, NativeMatrix4* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputIsKeyHeld)(uint32_t key) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputWasKeyPressed)(uint32_t key) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputWasKeyReleased)(uint32_t key) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputGetMousePosition)(NativeVector2* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputGetMouseDelta)(NativeVector2* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* InputGetModifiers)(uint32_t* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* RigidbodyGetLinearVelocity)(EntityHandleV1 entity, NativeVector2* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* RigidbodySetLinearVelocity)(EntityHandleV1 entity, NativeVector2 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* RigidbodyApplyForce)(EntityHandleV1 entity, NativeVector2 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* RigidbodyApplyLinearImpulse)(EntityHandleV1 entity, NativeVector2 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* PhysicsRaycast)(EntityHandleV1 context, NativeVector2 start,
			NativeVector2 end, uint32_t layerMask, int32_t includeTriggers,
			NativeRaycastHit2D* result) = nullptr;
		int32_t(TC_SCRIPT_CALL* PhysicsQueryAabb)(EntityHandleV1 context, NativeVector2 minimum,
			NativeVector2 maximum, uint32_t layerMask, int32_t includeTriggers,
			NativePhysicsQueryHit2D* hits, uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* AssetIsValid)(uint64_t assetHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* AssetGetType)(uint64_t assetHandle, int32_t* type) = nullptr;
		int32_t(TC_SCRIPT_CALL* BehaviourGetEnabled)(uint64_t instanceHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* BehaviourSetEnabledDeferred)(uint64_t instanceHandle,
			int32_t enabled) = nullptr;
		int32_t(TC_SCRIPT_CALL* BehaviourRemoveDeferred)(uint64_t instanceHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SceneGetActiveHandle)(uint64_t* sceneHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SceneGetActiveBuildIndex)(int32_t* buildIndex) = nullptr;
		int32_t(TC_SCRIPT_CALL* SceneRequestLoadHandle)(uint64_t sceneHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SceneRequestLoadIndex)(int32_t buildIndex) = nullptr;
		int32_t(TC_SCRIPT_CALL* SceneRequestReload)() = nullptr;
		int32_t(TC_SCRIPT_CALL* PrefabInstantiateDeferred)(EntityHandleV1 context,
			uint64_t prefabHandle, NativeVector3 worldPosition,
			EntityHandleV1 parent) = nullptr;
	};

	// NativeApiV1 is a frozen prefix. Optional services are discovered through
	// this V2 envelope, so adding input/audio/render services never shifts V1.
	using QueryCapabilityFnV2 = int32_t(TC_SCRIPT_CALL*)(NativeUtf8View name,
		uint32_t minimumVersion, void* output, uint32_t capacity, uint32_t* required);

	struct NativeApiV2
	{
		NativeApiV1 V1;
		QueryCapabilityFnV2 QueryCapability = nullptr;
	};

	inline constexpr std::string_view InputCapabilityName = "TomCat.InputApiV1";
	inline constexpr std::string_view SceneCapabilityName = "TomCat.SceneApiV1";
	struct NativeSceneApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeSceneApiV1);
		int32_t(TC_SCRIPT_CALL* RequestLoad)(uint64_t handle, int32_t buildIndex, uint32_t mode, int32_t asynchronous) = nullptr;
		int32_t(TC_SCRIPT_CALL* RequestUnload)(uint64_t handle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetActive)(uint64_t handle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetPersistent)(EntityHandleV1 entity, int32_t persistent) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetLoadStatus)(uint32_t* state, float* progress, int32_t* allowActivation) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetAllowActivation)(int32_t allow) = nullptr;
		int32_t(TC_SCRIPT_CALL* CancelLoad)() = nullptr;
		int32_t(TC_SCRIPT_CALL* GetLoadedScenes)(uint64_t* handles, uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetLastError)(uint8_t* buffer, uint32_t capacity, uint32_t* required) = nullptr;
	};
	inline constexpr std::string_view InputEventsCapabilityName =
		"TomCat.InputEventsApiV1";
	inline constexpr std::string_view ApplicationPathsCapabilityName =
		"TomCat.ApplicationPathsApiV1";

	struct NativeInputApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeInputApiV1);
		uint32_t MaximumGamepads = 16;
		uint32_t GamepadButtonCount = 15;
		uint32_t GamepadAxisCount = 6;
		uint32_t Reserved = 0;

		int32_t(TC_SCRIPT_CALL* IsMouseButtonHeld)(uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasMouseButtonPressed)(uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasMouseButtonReleased)(uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetScrollDelta)(NativeVector2* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* IsWindowFocused)() = nullptr;
		int32_t(TC_SCRIPT_CALL* IsGamepadConnected)(uint32_t gamepad) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasGamepadConnected)(uint32_t gamepad) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasGamepadDisconnected)(uint32_t gamepad) = nullptr;
		int32_t(TC_SCRIPT_CALL* IsGamepadButtonHeld)(uint32_t gamepad,
			uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasGamepadButtonPressed)(uint32_t gamepad,
			uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasGamepadButtonReleased)(uint32_t gamepad,
			uint32_t button) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetGamepadAxis)(uint32_t gamepad, uint32_t axis,
			float* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetGamepadName)(uint32_t gamepad, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
	};

	enum class NativeInputDeviceV1 : uint32_t
	{
		Keyboard = 1,
		MouseButton = 2,
		GamepadConnection = 3,
		GamepadButton = 4
	};

	enum class NativeInputActionV1 : uint32_t
	{
		Pressed = 1,
		Released = 2,
		Repeated = 3
	};

	struct NativeInputEventV1
	{
		uint64_t Sequence = 0;
		double TimestampSeconds = 0.0;
		uint64_t FrameNumber = 0;
		NativeInputDeviceV1 Device = NativeInputDeviceV1::Keyboard;
		NativeInputActionV1 Action = NativeInputActionV1::Pressed;
		uint32_t Code = 0;
		uint32_t DeviceIndex = 0;
	};

	struct NativeInputEventBatchInfoV1
	{
		uint64_t FirstFrameNumber = 0;
		uint64_t LastFrameNumber = 0;
		uint64_t FirstSequence = 0;
		uint64_t LastSequence = 0;
		uint64_t DroppedEventCount = 0;
		uint32_t EventCount = 0;
		uint32_t Reserved = 0;
	};

	struct NativeInputEventsApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeInputEventsApiV1);
		int32_t(TC_SCRIPT_CALL* GetBatchInfo)(
			NativeInputEventBatchInfoV1* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* CopyEvents)(NativeInputEventV1* events,
			uint32_t capacity, uint32_t* required) = nullptr;
	};

	struct NativeApplicationPathsApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeApplicationPathsApiV1);
		int32_t(TC_SCRIPT_CALL* GetSaveDirectory)(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetLogDirectory)(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetCrashDirectory)(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
	};

	inline constexpr std::string_view ComponentCapabilityName =
		"TomCat.ComponentApiV1";
	inline constexpr std::string_view ComponentStringCapabilityName =
		"TomCat.ComponentStringApiV1";
	inline constexpr uint32_t ComponentStringMaximumBytesV1 = 16u * 1024u * 1024u;
	inline constexpr std::string_view ComponentSchemaCapabilityName =
		"TomCat.ComponentSchemaApiV1";
	inline constexpr std::string_view DeferredCommandsCapabilityName =
		"TomCat.DeferredCommandsApiV1";
	inline constexpr std::string_view DeferredCallbackTransactionsCapabilityName =
		"TomCat.DeferredCallbackTransactionsApiV1";
	inline constexpr uint32_t DeferredCommandFailureMaximumBytesV1 = 4096;
	inline constexpr std::string_view GameplayCapabilityName =
		"TomCat.GameplayApiV1";

	inline constexpr std::string_view AudioCapabilityName = "TomCat.AudioApiV1";
	inline constexpr std::string_view AudioSpatialCapabilityName =
		"TomCat.AudioSpatialApiV1";
	inline constexpr std::string_view RuntimeUICapabilityName =
		"TomCat.RuntimeUIApiV1";

	// Optional audio service discovered through NativeApiV2::QueryCapability.
	// The frozen NativeApiV1 prefix remains byte-for-byte unchanged.
	struct NativeAudioApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeAudioApiV1);

		int32_t(TC_SCRIPT_CALL* IsHardwareAvailable)() = nullptr;
		int32_t(TC_SCRIPT_CALL* GetBackendName)(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* HasSource)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* AddSource)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* RemoveSource)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetClip)(EntityHandleV1 entity,
			uint64_t* clipHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetClip)(EntityHandleV1 entity,
			uint64_t clipHandle) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetEnabled)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetEnabled)(EntityHandleV1 entity,
			int32_t enabled) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetPlayOnStart)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetPlayOnStart)(EntityHandleV1 entity,
			int32_t playOnStart) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetLoop)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetLoop)(EntityHandleV1 entity, int32_t loop) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetVolume)(EntityHandleV1 entity, float* volume) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetVolume)(EntityHandleV1 entity, float volume) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetPitch)(EntityHandleV1 entity, float* pitch) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetPitch)(EntityHandleV1 entity, float pitch) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetMixerGroup)(EntityHandleV1 entity,
			int32_t* mixerGroup) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetMixerGroup)(EntityHandleV1 entity,
			int32_t mixerGroup) = nullptr;
		int32_t(TC_SCRIPT_CALL* Play)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* Pause)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* Stop)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetPlaybackState)(EntityHandleV1 entity,
			int32_t* state) = nullptr;
		int32_t(TC_SCRIPT_CALL* HasListener)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* AddListener)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* RemoveListener)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetListenerEnabled)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetListenerEnabled)(EntityHandleV1 entity,
			int32_t enabled) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetListenerPrimary)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetListenerPrimary)(EntityHandleV1 entity,
			int32_t primary) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetMixerVolume)(int32_t mixerGroup,
			float* volume) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetMixerVolume)(int32_t mixerGroup,
			float volume) = nullptr;
	};

	// Optional additive audio authoring/runtime fields. Kept separate so the
	// deployed AudioApiV1 layout remains a frozen binary contract.
	struct NativeAudioSpatialApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeAudioSpatialApiV1);
		int32_t(TC_SCRIPT_CALL* GetStreaming)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetStreaming)(EntityHandleV1 entity,
			int32_t streaming) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetSpatialBlend)(EntityHandleV1 entity,
			float* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetSpatialBlend)(EntityHandleV1 entity,
			float value) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetMinDistance)(EntityHandleV1 entity,
			float* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetMinDistance)(EntityHandleV1 entity,
			float value) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetMaxDistance)(EntityHandleV1 entity,
			float* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetMaxDistance)(EntityHandleV1 entity,
			float value) = nullptr;
	};

	// String properties and transient interaction state cannot be represented by
	// NativeComponentApiV1. Keep those operations in an additive capability so
	// the frozen component/property ABI remains unchanged.
	struct NativeRuntimeUIApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeRuntimeUIApiV1);

		int32_t(TC_SCRIPT_CALL* GetText)(EntityHandleV1 entity,
			uint64_t componentTypeId, uint8_t* buffer, uint32_t capacity,
			uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetText)(EntityHandleV1 entity,
			uint64_t componentTypeId, NativeUtf8View value) = nullptr;
		int32_t(TC_SCRIPT_CALL* WasButtonClicked)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetButtonClickSerial)(EntityHandleV1 entity,
			uint64_t* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* FocusButton)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetRect)(EntityHandleV1 entity,
			NativeVector4* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* IsGameplayInputCaptured)() = nullptr;
	};

	enum class NativePropertyKindV1 : uint32_t
	{
		Bool = 1,
		Int32,
		Int64,
		UInt32,
		UInt64,
		Float,
		Double,
		Vector2,
		Vector3,
		Vector4,
		// Metadata discovery can describe registry string properties even though
		// NativeComponentApiV1 intentionally has no string value transport.
		String
	};

	struct NativePropertyValueV1
	{
		uint32_t Kind = 0;
		uint32_t Reserved = 0;
		int64_t Integer = 0;
		double Number = 0.0;
		NativeVector4 Vector;
	};

	struct NativeComponentApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeComponentApiV1);
		int32_t(TC_SCRIPT_CALL* Has)(EntityHandleV1 entity,
			uint64_t componentTypeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* Add)(EntityHandleV1 entity,
			uint64_t componentTypeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* Remove)(EntityHandleV1 entity,
			uint64_t componentTypeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetProperty)(EntityHandleV1 entity,
			uint64_t componentTypeId, uint64_t propertyId,
			NativePropertyValueV1* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetProperty)(EntityHandleV1 entity,
			uint64_t componentTypeId, uint64_t propertyId,
			NativePropertyValueV1 value) = nullptr;
	};

	// Additive UTF-8 transport for ComponentRegistry string properties. The
	// deployed NativeComponentApiV1 layout stays frozen; callers discover this
	// table independently and copy returned bytes into owned storage.
	struct NativeComponentStringApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeComponentStringApiV1);
		int32_t(TC_SCRIPT_CALL* GetProperty)(EntityHandleV1 entity,
			uint64_t componentTypeId, uint64_t propertyId, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetProperty)(EntityHandleV1 entity,
			uint64_t componentTypeId, uint64_t propertyId,
			NativeUtf8View value) = nullptr;
	};

	// Lets the managed host poison the current Scene transaction when a callback
	// faults before a failing mutation reaches a native setter.
	struct NativeDeferredCommandsApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeDeferredCommandsApiV1);
		int32_t(TC_SCRIPT_CALL* AbortBatch)(EntityHandleV1 context,
			NativeUtf8View reason) = nullptr;
	};

	// Opens and synchronously completes one native command transaction for the
	// outermost managed lifecycle callback. CompleteCallback may trigger managed
	// lifecycle publication recursively; nested completions are sealed and drained
	// by the outermost native call.
	struct NativeDeferredCallbackTransactionsApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeDeferredCallbackTransactionsApiV1);
		int32_t(TC_SCRIPT_CALL* BeginCallback)(EntityHandleV1 context,
			uint64_t* token) = nullptr;
		int32_t(TC_SCRIPT_CALL* CompleteCallback)(uint64_t token) = nullptr;
	};

	enum class NativeComponentSchemaFlagsV1 : uint32_t
	{
		None = 0,
		ScriptAccessible = 1u << 0,
		InspectorVisible = 1u << 1
	};

	enum class NativeComponentPropertyFlagsV1 : uint32_t
	{
		None = 0,
		AssetReference = 1u << 0,
		EntityReference = 1u << 1
	};

	// Returned UTF-8 views borrow ComponentRegistry storage. They remain valid
	// until the next main-thread registry mutation; callers should copy them into
	// owned memory before triggering a module lifecycle operation.
	struct NativeComponentSchemaInfoV1
	{
		uint64_t TypeId = 0;
		uint64_t ProviderId = 0;
		uint32_t SchemaVersion = 0;
		uint32_t PropertyCount = 0;
		uint32_t Flags = 0;
		uint32_t Reserved = 0;
		NativeUtf8View StableName;
		NativeUtf8View DisplayName;
	};

	struct NativeComponentPropertySchemaInfoV1
	{
		uint64_t ComponentTypeId = 0;
		uint64_t PropertyId = 0;
		uint32_t Kind = 0;
		uint32_t Flags = 0;
		NativeUtf8View StableName;
		NativeUtf8View DisplayName;
	};

	// Additive discovery surface. NativeComponentApiV1 is a frozen deployed
	// contract and must not grow when schema metadata gains new fields.
	struct NativeComponentSchemaApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeComponentSchemaApiV1);
		int32_t(TC_SCRIPT_CALL* GetComponentCount)(uint32_t* count) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetComponent)(uint32_t index,
			NativeComponentSchemaInfoV1* component) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetPropertyCount)(uint64_t componentTypeId,
			uint32_t* count) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetProperty)(uint64_t componentTypeId,
			uint32_t index, NativeComponentPropertySchemaInfoV1* property) = nullptr;
	};

	// Scene/world services intentionally live outside the frozen NativeApiV1.
	// Structural mutations are deferred by ScriptEngine and the returned entity
	// handle is a stable reservation that becomes live at the callback safe point.
	struct NativeGameplayApiV1
	{
		uint32_t Version = 1;
		uint32_t Size = sizeof(NativeGameplayApiV1);

		int32_t(TC_SCRIPT_CALL* CreateEntityDeferred)(EntityHandleV1 context,
			NativeUtf8View name, NativeVector3 worldPosition, EntityHandleV1 parent,
			EntityHandleV1* reservedEntity) = nullptr;
		int32_t(TC_SCRIPT_CALL* FindEntityByName)(EntityHandleV1 context,
			NativeUtf8View name, EntityHandleV1* entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* QueryEntities)(EntityHandleV1 context,
			int32_t componentType, uint64_t registeredTypeId, EntityHandleV1* entities,
			uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetParent)(EntityHandleV1 entity,
			EntityHandleV1* parent) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetParentDeferred)(EntityHandleV1 entity,
			EntityHandleV1 parent) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetChildren)(EntityHandleV1 entity,
			EntityHandleV1* children, uint32_t capacity, uint32_t* required) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetActiveSelf)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetActiveSelf)(EntityHandleV1 entity,
			int32_t active) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetActiveInHierarchy)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetLocalPosition)(EntityHandleV1 entity,
			NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetLocalPosition)(EntityHandleV1 entity,
			NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetLocalRotationEuler)(EntityHandleV1 entity,
			NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetLocalRotationEuler)(EntityHandleV1 entity,
			NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformGetLocalScale)(EntityHandleV1 entity,
			NativeVector3* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* TransformSetLocalScale)(EntityHandleV1 entity,
			NativeVector3 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* GetComponentProperty)(EntityHandleV1 entity,
			int32_t componentType, uint32_t propertyId,
			NativePropertyValueV1* value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetComponentProperty)(EntityHandleV1 entity,
			int32_t componentType, uint32_t propertyId,
			NativePropertyValueV1 value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorPlay)(EntityHandleV1 entity,
			NativeUtf8View clipName, int32_t restart) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorStop)(EntityHandleV1 entity) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorSetBool)(EntityHandleV1 entity,
			NativeUtf8View parameter, int32_t value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorSetInt)(EntityHandleV1 entity,
			NativeUtf8View parameter, int32_t value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorSetFloat)(EntityHandleV1 entity,
			NativeUtf8View parameter, float value) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorSetTrigger)(EntityHandleV1 entity,
			NativeUtf8View parameter) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorResetTrigger)(EntityHandleV1 entity,
			NativeUtf8View parameter) = nullptr;
		int32_t(TC_SCRIPT_CALL* SpriteAnimatorGetCurrentState)(EntityHandleV1 entity,
			uint8_t* buffer, uint32_t capacity, uint32_t* required) = nullptr;
	};

	// Capability-local IDs. These values are explicit ABI constants and may only
	// be appended; they are not persisted in Scene files.
	namespace GameplayPropertyIds {
		inline constexpr uint32_t RigidbodyEnabled = 100;
		inline constexpr uint32_t RigidbodyBodyType = 101;
		inline constexpr uint32_t RigidbodyFixedRotation = 102;
		inline constexpr uint32_t SpriteEnabled = 200;
		inline constexpr uint32_t SpriteColor = 201;
		inline constexpr uint32_t SpriteAsset = 202;
		inline constexpr uint32_t SpriteTilingFactor = 203;
		inline constexpr uint32_t SpriteSortingLayer = 204;
		inline constexpr uint32_t SpriteOrderInLayer = 205;
		inline constexpr uint32_t CameraPrimary = 300;
		inline constexpr uint32_t CameraFixedAspectRatio = 301;
		inline constexpr uint32_t CameraBackgroundColor = 302;
		inline constexpr uint32_t CameraProjectionType = 303;
		inline constexpr uint32_t CameraOrthographicSize = 304;
		inline constexpr uint32_t CameraOrthographicNear = 305;
		inline constexpr uint32_t CameraOrthographicFar = 306;
		inline constexpr uint32_t CameraPerspectiveFov = 307;
		inline constexpr uint32_t CameraPerspectiveNear = 308;
		inline constexpr uint32_t CameraPerspectiveFar = 309;
		inline constexpr uint32_t BoxEnabled = 400;
		inline constexpr uint32_t BoxIsTrigger = 401;
		inline constexpr uint32_t BoxCollisionLayer = 402;
		inline constexpr uint32_t BoxCollisionMask = 403;
		inline constexpr uint32_t BoxOffset = 404;
		inline constexpr uint32_t BoxSize = 405;
		inline constexpr uint32_t BoxDensity = 406;
		inline constexpr uint32_t BoxFriction = 407;
		inline constexpr uint32_t BoxRestitution = 408;
		inline constexpr uint32_t BoxRestitutionThreshold = 409;
		inline constexpr uint32_t CircleEnabled = 500;
		inline constexpr uint32_t CircleIsTrigger = 501;
		inline constexpr uint32_t CircleCollisionLayer = 502;
		inline constexpr uint32_t CircleCollisionMask = 503;
		inline constexpr uint32_t CircleOffset = 504;
		inline constexpr uint32_t CircleRadius = 505;
		inline constexpr uint32_t CircleDensity = 506;
		inline constexpr uint32_t CircleFriction = 507;
		inline constexpr uint32_t CircleRestitution = 508;
		inline constexpr uint32_t JointEnabled = 600;
		inline constexpr uint32_t JointConnectedEntity = 601;
		inline constexpr uint32_t JointAnchor = 602;
		inline constexpr uint32_t JointConnectedAnchor = 603;
		inline constexpr uint32_t JointDistance = 604;
		inline constexpr uint32_t JointFrequency = 605;
		inline constexpr uint32_t JointDamping = 606;
		inline constexpr uint32_t JointCollideConnected = 607;
		inline constexpr uint32_t AnimatorEnabled = 700;
		inline constexpr uint32_t AnimatorSpeed = 701;
		inline constexpr uint32_t AnimatorIsPlaying = 702;
		inline constexpr uint32_t AnimatorCurrentFrame = 703;
	}

	struct ManagedApiV1
	{
		uint32_t Version = ManagedApiVersion;
		uint32_t Size = sizeof(ManagedApiV1);

		int32_t(TC_SCRIPT_CALL* CreateDomain)(int32_t domainKind, uint64_t* domainId) = nullptr;
		int32_t(TC_SCRIPT_CALL* LoadProjectAssembly)(uint64_t domainId,
			NativeByteView assembly, NativeByteView pdb) = nullptr;
		int32_t(TC_SCRIPT_CALL* ReadScriptMetadata)(uint64_t domainId,
			MetadataReceiverV1 receiver, uint64_t receiverToken) = nullptr;
		int32_t(TC_SCRIPT_CALL* CreateSceneRuntime)(uint64_t domainId, uint64_t sceneSessionId,
			uint64_t runtimeGeneration, uint64_t* sceneRuntimeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* InstantiateAll)(uint64_t sceneRuntimeId,
			const NativeScriptAttachmentV1* items, uint32_t count) = nullptr;
		int32_t(TC_SCRIPT_CALL* ApplySerializedFields)(uint64_t sceneRuntimeId,
			NativeByteView fieldsJson) = nullptr;
		int32_t(TC_SCRIPT_CALL* InvokeCreateAll)(uint64_t sceneRuntimeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* SetEnabled)(uint64_t sceneRuntimeId,
			uint64_t attachmentId, int32_t enabled) = nullptr;
		int32_t(TC_SCRIPT_CALL* UpdateAll)(uint64_t sceneRuntimeId, float deltaTime) = nullptr;
		int32_t(TC_SCRIPT_CALL* FixedUpdateAll)(uint64_t sceneRuntimeId,
			float fixedDeltaTime) = nullptr;
		int32_t(TC_SCRIPT_CALL* DispatchPhysicsEvents)(uint64_t sceneRuntimeId,
			const NativePhysicsEventV1* events, uint32_t count) = nullptr;
		int32_t(TC_SCRIPT_CALL* DestroyAll)(uint64_t sceneRuntimeId) = nullptr;
		int32_t(TC_SCRIPT_CALL* BeginUnloadDomain)(uint64_t domainId) = nullptr;
		int32_t(TC_SCRIPT_CALL* PollUnload)(uint64_t domainId, int32_t* unloaded) = nullptr;
		int32_t(TC_SCRIPT_CALL* DestroyAttachments)(uint64_t sceneRuntimeId,
			const uint64_t* attachmentIds, uint32_t count) = nullptr;
		int32_t(TC_SCRIPT_CALL* InstantiateAttachments)(uint64_t sceneRuntimeId,
			const NativeScriptAttachmentV1* items, uint32_t count,
			NativeByteView fieldsJson) = nullptr;
		// Added in ManagedApi version 2. Structural command projections are
		// resolved explicitly so a version-1 host is rejected during bootstrap.
		int32_t(TC_SCRIPT_CALL* ResolveDeferredCommandBatch)(
			uint64_t sceneRuntimeId, int32_t committed) = nullptr;
		// Added in ManagedApi version 3 for persistent UI/event callbacks.
		int32_t(TC_SCRIPT_CALL* InvokeMethod)(uint64_t sceneRuntimeId,
			uint64_t attachmentId, NativeUtf8View methodName) = nullptr;
	};

	using GetManagedApiFn = int32_t(TC_SCRIPT_CALL*)(
		const NativeApiV1* nativeApi, ManagedApiV1* managedApi);

	static_assert(std::is_standard_layout_v<NativeApiV1>);
	static_assert(std::is_standard_layout_v<NativeApiV2>);
	static_assert(std::is_standard_layout_v<NativeInputApiV1>);
	static_assert(std::is_standard_layout_v<NativeInputEventV1>);
	static_assert(std::is_standard_layout_v<NativeInputEventBatchInfoV1>);
	static_assert(std::is_standard_layout_v<NativeInputEventsApiV1>);
	static_assert(std::is_standard_layout_v<NativeApplicationPathsApiV1>);
	static_assert(std::is_standard_layout_v<NativeAudioApiV1>);
	static_assert(std::is_standard_layout_v<NativeAudioSpatialApiV1>);
	static_assert(std::is_standard_layout_v<NativeRuntimeUIApiV1>);
	static_assert(std::is_standard_layout_v<NativePropertyValueV1>);
	static_assert(std::is_standard_layout_v<NativeComponentApiV1>);
	static_assert(std::is_standard_layout_v<NativeComponentStringApiV1>);
	static_assert(std::is_standard_layout_v<NativeDeferredCommandsApiV1>);
	static_assert(std::is_standard_layout_v<NativeDeferredCallbackTransactionsApiV1>);
	static_assert(std::is_standard_layout_v<NativeComponentSchemaInfoV1>);
	static_assert(std::is_standard_layout_v<NativeComponentPropertySchemaInfoV1>);
	static_assert(std::is_standard_layout_v<NativeComponentSchemaApiV1>);
	static_assert(offsetof(NativeApiV2, V1) == 0);
	static_assert(std::is_standard_layout_v<ManagedApiV1>);
	static_assert(sizeof(NativeByteView) == 16);
	static_assert(sizeof(EntityHandleV1) == 24);
	static_assert(sizeof(NativeScriptAttachmentV1) == 48);
	static_assert(sizeof(NativePhysicsEventV1) == 56);
	static_assert(sizeof(NativeInputEventV1) == 40);
	static_assert(sizeof(NativeInputEventBatchInfoV1) == 48);
	static_assert(offsetof(NativeComponentStringApiV1, Version) == 0);
	static_assert(offsetof(NativeComponentStringApiV1, Size) == 4);
	static_assert(offsetof(NativeComponentStringApiV1, GetProperty) == 8);
	static_assert(offsetof(NativeComponentStringApiV1, SetProperty) == 8 + sizeof(void*));
	static_assert(sizeof(NativeComponentStringApiV1) == 8 + 2 * sizeof(void*));
	static_assert(offsetof(NativeDeferredCommandsApiV1, Version) == 0);
	static_assert(offsetof(NativeDeferredCommandsApiV1, Size) == 4);
	static_assert(offsetof(NativeDeferredCommandsApiV1, AbortBatch) == 8);
	static_assert(sizeof(NativeDeferredCommandsApiV1) == 8 + sizeof(void*));
	static_assert(offsetof(NativeDeferredCallbackTransactionsApiV1, Version) == 0);
	static_assert(offsetof(NativeDeferredCallbackTransactionsApiV1, Size) == 4);
	static_assert(offsetof(NativeDeferredCallbackTransactionsApiV1, BeginCallback) == 8);
	static_assert(offsetof(NativeDeferredCallbackTransactionsApiV1, CompleteCallback) == 8 + sizeof(void*));
	static_assert(sizeof(NativeDeferredCallbackTransactionsApiV1) == 8 + 2 * sizeof(void*));

}

#undef TC_SCRIPT_CALL
