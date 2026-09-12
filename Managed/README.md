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

### ManagedApiV1 order

After `Version` and `Size`, the function pointers are laid out in this exact order:

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
```

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
BehaviourGetEnabled, BehaviourSetEnabledDeferred
BehaviourRemoveDeferred
```

The authoritative field signatures and blittable layouts are in
`TomCat.Managed/InteropTypes.cs`; `ManagedApiV1` is in
`TomCat.ScriptHost/ManagedApiV1.cs`. `NativeByteView`/`NativeUtf8View` are a pointer followed by a
`uint64` byte length. `NativeEntityHandleV1` is `{ sceneSessionId, entityId, runtimeGeneration }`.
`NativeScriptAttachmentV1` is `{ Entity, attachmentId, scriptAsset, int32 enabled, int32 reserved }`.
`NativePhysicsEventV1` is `{ uint32 kind, uint32 reserved, Entity A, Entity B }`, where kinds 0..3
are collision enter, collision exit, trigger enter, and trigger exit.

Attachment IDs must be globally random, nonzero, and unique in a scene runtime. Managed V1 uses the
Attachment ID itself as `ScriptInstanceHandle.Value`; duplicate IDs are rejected. Native supplies
attachments in scene-entity order and then mount order. The host performs a stable sort by
`DefaultExecutionOrder`, using that input sequence as the tie-breaker, and destroys in reverse.

## Load and unload ownership

`TomCat.ScriptHost` and `TomCat.Managed` stay in the default ALC. Each game project DLL/PDB is loaded
from bytes into a collectible ALC. The custom loader always resolves `TomCat.Managed` to the default
ALC copy, preserving `TomCatBehaviour` type identity. `BeginUnloadDomain` stops dispatch, destroys
instances, clears reflection caches, calls `AssemblyLoadContext.Unload`, and retains only a weak
reference. Native must poll `PollUnload`; a domain that cannot unload must be reported and must not
be silently accumulated.
