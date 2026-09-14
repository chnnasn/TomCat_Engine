# TomCat managed scripting V1

This directory contains the independently buildable .NET 10 portion of the TomCat C# scripting
system:

- `TomCat.Managed`: the user-facing `using TomCat;` API and blittable native ABI definitions.
- `TomCat.ScriptHost`: the permanent default-ALC host, collectible project domains, instance
  lifecycle dispatch, field restoration, fault isolation, and unmanaged exports.
- `TomCat.ScriptGenerator`: the compile-time script recognizer, diagnostics, and embedded manifest
  generator. It references the Roslyn binaries shipped with the .NET SDK and has no NuGet dependency.
- `TomCat.Managed.Regression`: a no-test-framework executable regression suite.

Build and test with the .NET 10 SDK:

```powershell
dotnet build Managed/TomCat.Managed.slnx -c Release
dotnet run --project Managed/TomCat.Managed.Regression/TomCat.Managed.Regression.csproj -c Release
```

`TomCat.ScriptHost.runtimeconfig.json`, `TomCat.ScriptHost.dll`, and `TomCat.Managed.dll` are emitted
under `Managed/TomCat.ScriptHost/bin/<Configuration>/net10.0/`.

## Project input and generated manifest

The generated `Assembly-CSharp.csproj` references `TomCat.Managed` normally and
`TomCat.ScriptGenerator` as an analyzer, then supplies exactly one `ScriptAssets.json` as an
`AdditionalFiles` item:

```json
{
  "version": 1,
  "assets": {
    "Assets/Scripts/PlayerController.cs": 8172638172638
  }
}
```

The generator embeds a public `TomCat.Generated.ScriptManifest.Json` property. Manifest V1 records
the asset handle, full type name, execution order, disallow-multiple flag, lifecycle bit mask, and
serializable field metadata. Field IDs are deterministic, lower-case 128-bit hex strings derived
from the script asset handle and field identity.

The host accepts serialized fields as UTF-8 with this exact casing and shape:

```json
{
  "attachments": [
    {
      "attachmentId": 123,
      "fields": [
        {
          "fieldId": "14a71699a9c84a6c9e21872758c1a401",
          "name": "_moveForce",
          "type": "Float",
          "value": 12.0
        }
      ]
    }
  ]
}
```

The exact type tokens are `Bool`, `Int32`, `Int64`, `Float`, `Double`, `String`, `Vector2`,
`Vector3`, `Vector4`, `Color`, `Enum`, `Entity`, and `AssetRef`. Vectors and colors are numeric
arrays; Entity and AssetRef values are unsigned 64-bit JSON numbers; enums are signed 64-bit JSON
numbers. An unknown or type-mismatched field remains native-side orphan data and is not applied.

`AssetRef<T>` accepts the built-in markers `Texture2DAsset`, `ShaderAsset`, `AudioAsset`,
`FontAsset`, `MeshAsset`, `MaterialAsset`, `SceneAsset`, and `PrefabAsset`. Unsupported markers
produce compiler error `TCG010`. Both `IsValid` and Cook check the asset's actual type; marker
support describes asset identity and does not imply that a runtime loader exists for every type.

## Native bootstrap contract

Initialize CoreCLR once with `TomCat.ScriptHost.runtimeconfig.json`, then use hostfxr's
`load_assembly_and_get_function_pointer` with:

```text
assembly path: TomCat.ScriptHost.dll
type name:     TomCat.ScriptHost.EntryPoint, TomCat.ScriptHost
method name:   GetManagedApi
delegate type: UNMANAGEDCALLERSONLY_METHOD
```

The entry point is exactly:

```csharp
[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
public static int GetManagedApi(NativeApiV1* nativeApi, ManagedApiV1* managedApi)
```

All function pointers use Cdecl and return an `int32` status. Both tables begin with `uint32
Version; uint32 Size;`. Native must pass Version 1, at least the V1 table size, and every V1
callback. Bootstrap validates the complete table before publishing it; a missing callback returns
unavailable (-8) without replacing an earlier valid binding.

### ManagedApiV1 layout (Managed ABI version 2)

The historical struct name is retained because version 2 appends one callback to the stable
version-1 prefix. After `Version` and `Size`, the function pointers are laid out in this exact
order:

```text
int CreateDomain(int32 domainKind, uint64* domainId)
int LoadProjectAssembly(uint64 domainId, NativeByteView assembly, NativeByteView pdb)
int ReadScriptMetadata(uint64 domainId, MetadataReceiver receiver, uint64 receiverToken)
int CreateSceneRuntime(uint64 domainId, uint64 sceneSessionId,
                       uint64 runtimeGeneration, uint64* sceneRuntimeId)
int InstantiateAll(uint64 sceneRuntimeId, NativeScriptAttachmentV1* items, uint32 count)
int ApplySerializedFields(uint64 sceneRuntimeId, NativeByteView fieldsJson)
int InvokeCreateAll(uint64 sceneRuntimeId)
int SetEnabled(uint64 sceneRuntimeId, uint64 attachmentId, int32 enabled)
int UpdateAll(uint64 sceneRuntimeId, float deltaTime)
int FixedUpdateAll(uint64 sceneRuntimeId, float fixedDeltaTime)
int DispatchPhysicsEvents(uint64 sceneRuntimeId, NativePhysicsEventV1* events, uint32 count)
int DestroyAll(uint64 sceneRuntimeId)
int BeginUnloadDomain(uint64 domainId)
int PollUnload(uint64 domainId, int32* unloaded)
int DestroyAttachments(uint64 sceneRuntimeId, uint64* attachmentIds, uint32 count)
int InstantiateAttachments(uint64 sceneRuntimeId, NativeScriptAttachmentV1* items,
                           uint32 count, NativeByteView fieldsJson)
int ResolveDeferredCommandBatch(uint64 sceneRuntimeId, int32 committed)
```

`ResolveDeferredCommandBatch` acknowledges the atomic native command batch after staged
validation and live replay. A zero value discards managed projected lifecycle state; one publishes
it after the native Scene commit succeeds.

`MetadataReceiver` is `int(NativeByteView json, uint64 receiverToken)`. The token is opaque to
managed code and resolves only to a short-lived, thread-safe native receive context. Domain kinds are Metadata = 0 and
Play = 1. Status values are success = 0, invalid argument = -1, invalid state = -2, not found = -3,
version mismatch = -4, contained managed exception = -5, buffer too small = -6, wrong thread = -7,
and unavailable = -8.

### NativeApiV1 order

After `Version` and `Size`, the callback pointers are laid out in this exact order:

```text
Log, EmitDiagnostic, IsMainThread,
EntityIsAlive, EntityGetName, EntitySetName, EntityGetTag, EntitySetTag,
EntityGetLayer, EntitySetLayer, DestroyEntityDeferred,
HasComponent, AddComponentDeferred, RemoveComponentDeferred,
TransformGetPosition, TransformSetPosition,
TransformGetRotationEuler, TransformSetRotationEuler,
TransformGetScale, TransformSetScale, TransformGetWorldMatrix,
InputIsKeyHeld, InputWasKeyPressed, InputWasKeyReleased,
InputGetMousePosition, InputGetMouseDelta, InputGetModifiers,
RigidbodyGetLinearVelocity, RigidbodySetLinearVelocity,
RigidbodyApplyForce, RigidbodyApplyLinearImpulse,
PhysicsRaycast, PhysicsQueryAabb,
AssetIsValid, AssetGetType,
BehaviourGetEnabled, BehaviourSetEnabledDeferred, BehaviourRemoveDeferred,
SceneGetActiveHandle, SceneGetActiveBuildIndex,
SceneRequestLoadHandle, SceneRequestLoadIndex, SceneRequestReload,
PrefabInstantiateDeferred
```

### Deferred callback transactions

The current native host queries and supplies both
`TomCat.DeferredCommandsApiV1` and
`TomCat.DeferredCallbackTransactionsApiV1`. The latter has the exact Cdecl
layout below after `Version` and `Size`:

```text
int BeginCallback(NativeEntityHandleV1 context, uint64* token)
int CompleteCallback(uint64 token)
```

Every top-level lifecycle, update, physics, and display InputAction user callback
opens one transaction. Commands are visible to later reads in that callback,
validated against a staged Scene, and then committed together or discarded
together. `AbortBatch` marks the current transaction for rollback after a
contained user exception or a caught mutation validation failure. `CompleteCallback`
seals the command suffix, and native resolves sealed callbacks in FIFO order.
Native calls `ResolveDeferredCommandBatch` exactly once for every completed
callback, including an empty callback, so managed lifecycle projections stay
aligned with native state. The transaction covers operations routed through the
deferred command buffer: Entity creation/destruction and hierarchy state,
component and behaviour add/remove or enabled state, built-in and registered
component property setters, and Prefab instantiation. Immediate runtime controls
such as physics force/velocity, audio transport or mixer operations, and Animator
playback take effect synchronously and are outside `AbortBatch` rollback.

For a display InputAction multicast, each top-level subscriber owns a separate
transaction. Subscribers run in subscription order; a user exception aborts only
that subscriber, later subscribers still run, and the map is then disabled with one
diagnostic. Synchronous nested events such as Disable -> Canceled share the outer
subscriber transaction.

Nested lifecycle callbacks created by commit effects are appended to the same
non-recursive FIFO drain. A protocol error from Begin, Complete, or the managed
resolver is fatal to that Play Scene; native stops it instead of continuing with
an open transaction or a partially synchronized projection. Scene-wide phase
batching remains compatibility behavior for older hosts that do not publish the
callback transaction capability.

The authoritative field signatures and blittable layouts are in
`TomCat.Managed/InteropTypes.cs`; `ManagedApiV1` is in
`TomCat.ScriptHost/ManagedApiV1.cs`. `NativeByteView`/`NativeUtf8View` are a pointer followed by a
`uint64` byte length. `NativeEntityHandleV1` is `{ sceneSessionId, entityId, runtimeGeneration }`.
`NativeScriptAttachmentV1` is `{ Entity, attachmentId, scriptAsset, int32 enabled, int32 reserved }`.
`NativePhysicsEventV1` is `{ uint32 kind, uint32 reserved, Entity A, Entity B }`, where kinds 0..3
are collision enter, collision exit, trigger enter, and trigger exit.

Attachment IDs must be globally random, nonzero, and unique in a scene runtime. The managed ABI uses the
Attachment ID itself as `ScriptInstanceHandle.Value`; duplicate IDs are rejected. Native supplies
attachments in scene-entity order and then mount order. The host performs a stable sort by
`DefaultExecutionOrder`, using that input sequence as the tie-breaker, and destroys in reverse.
Prefab-created script attachments enter through `InstantiateAttachments` only after the current
managed callback returns. Their serialized fields are restored before the new batch receives
`OnCreate` and `OnEnable`; existing instances never receive those callbacks again.

## Scene and snapshot-Prefab APIs

`SceneManager` exposes the active `SceneAsset` and build index plus synchronous replacement
requests by scene handle/index and reload. Requests made during a lifecycle or physics callback are
committed by the native runtime at the frame-end safe point.

Scripts can serialize strongly typed `SceneAsset` and `PrefabAsset` fields. A behavior queues a
snapshot Prefab with `Instantiate(prefab, worldPosition, optionalParent)`. Prefab instances receive
fresh Scene UUIDs and AttachmentIDs; hierarchy, `DistanceJoint2D`, and C# `Entity` fields are
remapped before the batch becomes visible to managed lifecycle dispatch.

## Load and unload ownership

`TomCat.ScriptHost` and `TomCat.Managed` stay in the default ALC. Each game project DLL/PDB is loaded
from bytes into a collectible ALC. The custom loader always resolves `TomCat.Managed` to the default
ALC copy, preserving `TomCatBehaviour` type identity. `BeginUnloadDomain` stops dispatch, destroys
instances, clears reflection caches, calls `AssemblyLoadContext.Unload`, and retains only a weak
reference. Native must poll `PollUnload`; a domain that cannot unload must be reported and must not
be silently accumulated.
