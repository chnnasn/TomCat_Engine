#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(_WIN32)
	#define TC_SCRIPT_CALL __cdecl
#else
	#define TC_SCRIPT_CALL
#endif

namespace TomCat::Scripting {

	inline constexpr uint32_t NativeApiVersion = 1;
	inline constexpr uint32_t ManagedApiVersion = 1;
	inline constexpr uint32_t ScriptManifestVersion = 1;

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
		SpriteRenderer = 6
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
	};

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
	};

	using GetManagedApiFn = int32_t(TC_SCRIPT_CALL*)(
		const NativeApiV1* nativeApi, ManagedApiV1* managedApi);

	static_assert(std::is_standard_layout_v<NativeApiV1>);
	static_assert(std::is_standard_layout_v<ManagedApiV1>);
	static_assert(sizeof(NativeByteView) == 16);
	static_assert(sizeof(EntityHandleV1) == 24);
	static_assert(sizeof(NativeScriptAttachmentV1) == 48);
	static_assert(sizeof(NativePhysicsEventV1) == 56);

}

#undef TC_SCRIPT_CALL
