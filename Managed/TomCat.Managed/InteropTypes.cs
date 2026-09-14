using System.Runtime.InteropServices;

namespace TomCat.Interop;

public static class ManagedAbi
{
    public const uint NativeApiVersion = 1;
    public const uint ManagedApiVersion = 2;
    public const uint ScriptManifestVersion = 1;
}

[StructLayout(LayoutKind.Sequential)]
public readonly unsafe struct NativeByteView(byte* data, ulong length)
{
    public readonly byte* Data = data;
    public readonly ulong Length = length;
}

[StructLayout(LayoutKind.Sequential)]
public readonly unsafe struct NativeUtf8View(byte* data, ulong length)
{
    public readonly byte* Data = data;
    public readonly ulong Length = length;
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeEntityHandleV1(ulong sceneSessionId, ulong entityId, ulong runtimeGeneration)
{
    public readonly ulong SceneSessionId = sceneSessionId;
    public readonly ulong EntityId = entityId;
    public readonly ulong RuntimeGeneration = runtimeGeneration;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector2
{
    public float X;
    public float Y;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector3
{
    public float X;
    public float Y;
    public float Z;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeVector4
{
	public float X;
	public float Y;
	public float Z;
	public float W;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeMatrix4
{
    public float M11, M12, M13, M14;
    public float M21, M22, M23, M24;
    public float M31, M32, M33, M34;
    public float M41, M42, M43, M44;
}

public enum NativeComponentTypeV1 : int
{
    Transform = 1,
    Rigidbody2D = 2,
    BoxCollider2D = 3,
    CircleCollider2D = 4,
    DistanceJoint2D = 5,
    SpriteRenderer = 6,
	Camera = 7,
	SpriteAnimator = 8
}

public enum NativePhysicsEventKindV1 : uint
{
    CollisionEnter = 0,
    CollisionExit = 1,
    TriggerEnter = 2,
    TriggerExit = 3
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct NativeScriptAttachmentV1
{
    public readonly NativeEntityHandleV1 Entity;
    public readonly ulong AttachmentId;
    public readonly ulong ScriptAsset;
    public readonly int Enabled;
    public readonly int Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public readonly struct NativePhysicsEventV1
{
    public readonly NativePhysicsEventKindV1 Kind;
    public readonly uint Reserved;
    public readonly NativeEntityHandleV1 EntityA;
    public readonly NativeEntityHandleV1 EntityB;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeRaycastHit2D
{
    public NativeEntityHandleV1 Entity;
    public NativeVector2 Point;
    public NativeVector2 Normal;
    public float Fraction;
    public int IsTrigger;
    public uint CollisionLayer;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativePhysicsQueryHit2D
{
    public NativeEntityHandleV1 Entity;
    public int IsTrigger;
    public uint CollisionLayer;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeDiagnosticV1
{
    public int Severity;
    public int Reserved;
    public NativeUtf8View Message;
    public NativeUtf8View File;
    public int Line;
    public int Column;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiV1
{
    public uint Version;
    public uint Size;
    public delegate* unmanaged[Cdecl]<int, NativeUtf8View, int> Log;
    public delegate* unmanaged[Cdecl]<NativeDiagnosticV1*, int> EmitDiagnostic;
    public delegate* unmanaged[Cdecl]<int> IsMainThread;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> EntityIsAlive;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, byte*, uint, uint*, int> EntityGetName;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View, int> EntitySetName;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, byte*, uint, uint*, int> EntityGetTag;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View, int> EntitySetTag;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, uint*, int> EntityGetLayer;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, uint, int> EntitySetLayer;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> DestroyEntityDeferred;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> HasComponent;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AddComponentDeferred;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> RemoveComponentDeferred;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetPosition;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetPosition;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetRotationEuler;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetRotationEuler;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetScale;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetScale;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeMatrix4*, int> TransformGetWorldMatrix;
    public delegate* unmanaged[Cdecl]<uint, int> InputIsKeyHeld;
    public delegate* unmanaged[Cdecl]<uint, int> InputWasKeyPressed;
    public delegate* unmanaged[Cdecl]<uint, int> InputWasKeyReleased;
    public delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMousePosition;
    public delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMouseDelta;
    public delegate* unmanaged[Cdecl]<uint*, int> InputGetModifiers;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2*, int> RigidbodyGetLinearVelocity;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodySetLinearVelocity;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyForce;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyLinearImpulse;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, NativeVector2, uint, int, NativeRaycastHit2D*, int> PhysicsRaycast;
    public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, NativeVector2, uint, int, NativePhysicsQueryHit2D*, uint, uint*, int> PhysicsQueryAabb;
    public delegate* unmanaged[Cdecl]<ulong, int> AssetIsValid;
    public delegate* unmanaged[Cdecl]<ulong, int*, int> AssetGetType;
	public delegate* unmanaged[Cdecl]<ulong, int> BehaviourGetEnabled;
	public delegate* unmanaged[Cdecl]<ulong, int, int> BehaviourSetEnabledDeferred;
	public delegate* unmanaged[Cdecl]<ulong, int> BehaviourRemoveDeferred;
	public delegate* unmanaged[Cdecl]<ulong*, int> SceneGetActiveHandle;
	public delegate* unmanaged[Cdecl]<int*, int> SceneGetActiveBuildIndex;
	public delegate* unmanaged[Cdecl]<ulong, int> SceneRequestLoadHandle;
	public delegate* unmanaged[Cdecl]<int, int> SceneRequestLoadIndex;
	public delegate* unmanaged[Cdecl]<int> SceneRequestReload;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, NativeVector3,
		NativeEntityHandleV1, int> PrefabInstantiateDeferred;
}

// The V1 table above is a frozen binary prefix. New native services are copied
// out through this optional envelope and independently versioned capability tables.
[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApiV2
{
	public NativeApiV1 V1;
	public delegate* unmanaged[Cdecl]<NativeUtf8View, uint, void*, uint, uint*, int> QueryCapability;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeInputApiV1
{
	public uint Version;
	public uint Size;
	public uint MaximumGamepads;
	public uint GamepadButtonCount;
	public uint GamepadAxisCount;
	public uint Reserved;
	public delegate* unmanaged[Cdecl]<uint, int> IsMouseButtonHeld;
	public delegate* unmanaged[Cdecl]<uint, int> WasMouseButtonPressed;
	public delegate* unmanaged[Cdecl]<uint, int> WasMouseButtonReleased;
	public delegate* unmanaged[Cdecl]<NativeVector2*, int> GetScrollDelta;
	public delegate* unmanaged[Cdecl]<int> IsWindowFocused;
	public delegate* unmanaged[Cdecl]<uint, int> IsGamepadConnected;
	public delegate* unmanaged[Cdecl]<uint, int> WasGamepadConnected;
	public delegate* unmanaged[Cdecl]<uint, int> WasGamepadDisconnected;
	public delegate* unmanaged[Cdecl]<uint, uint, int> IsGamepadButtonHeld;
	public delegate* unmanaged[Cdecl]<uint, uint, int> WasGamepadButtonPressed;
	public delegate* unmanaged[Cdecl]<uint, uint, int> WasGamepadButtonReleased;
	public delegate* unmanaged[Cdecl]<uint, uint, float*, int> GetGamepadAxis;
	public delegate* unmanaged[Cdecl]<uint, byte*, uint, uint*, int> GetGamepadName;
}

public enum NativeInputDeviceV1 : uint
{
	Keyboard = 1,
	MouseButton = 2,
	GamepadConnection = 3,
	GamepadButton = 4
}

public enum NativeInputActionV1 : uint
{
	Pressed = 1,
	Released = 2,
	Repeated = 3
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeInputEventV1
{
	public ulong Sequence;
	public double TimestampSeconds;
	public ulong FrameNumber;
	public NativeInputDeviceV1 Device;
	public NativeInputActionV1 Action;
	public uint Code;
	public uint DeviceIndex;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeInputEventBatchInfoV1
{
	public ulong FirstFrameNumber;
	public ulong LastFrameNumber;
	public ulong FirstSequence;
	public ulong LastSequence;
	public ulong DroppedEventCount;
	public uint EventCount;
	public uint Reserved;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeInputEventsApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeInputEventBatchInfoV1*, int> GetBatchInfo;
	public delegate* unmanaged[Cdecl]<NativeInputEventV1*, uint, uint*, int> CopyEvents;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeApplicationPathsApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> GetSaveDirectory;
	public delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> GetLogDirectory;
	public delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> GetCrashDirectory;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeGameplayApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		NativeVector3, NativeEntityHandleV1, NativeEntityHandleV1*, int> CreateEntityDeferred;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		NativeEntityHandleV1*, int> FindEntityByName;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, ulong,
		NativeEntityHandleV1*, uint, uint*, int> QueryEntities;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeEntityHandleV1*, int> GetParent;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeEntityHandleV1, int> SetParentDeferred;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeEntityHandleV1*, uint, uint*, int> GetChildren;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetActiveSelf;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetActiveSelf;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetActiveInHierarchy;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3*, int> TransformGetLocalPosition;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3, int> TransformSetLocalPosition;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3*, int> TransformGetLocalRotationEuler;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3, int> TransformSetLocalRotationEuler;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3*, int> TransformGetLocalScale;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1,
		NativeVector3, int> TransformSetLocalScale;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, uint,
		NativePropertyValueV1*, int> GetComponentProperty;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, uint,
		NativePropertyValueV1, int> SetComponentProperty;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		int, int> SpriteAnimatorPlay;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> SpriteAnimatorStop;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		int, int> SpriteAnimatorSetBool;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		int, int> SpriteAnimatorSetInt;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		float, int> SpriteAnimatorSetFloat;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		int> SpriteAnimatorSetTrigger;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View,
		int> SpriteAnimatorResetTrigger;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, byte*, uint, uint*,
		int> SpriteAnimatorGetCurrentState;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeAudioApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<int> IsHardwareAvailable;
	public delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> GetBackendName;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> HasSource;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AddSource;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> RemoveSource;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong*, int> GetClip;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, int> SetClip;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetEnabled;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetEnabled;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetPlayOnStart;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetPlayOnStart;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetLoop;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetLoop;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> GetVolume;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> SetVolume;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> GetPitch;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> SetPitch;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int*, int> GetMixerGroup;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetMixerGroup;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> Play;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> Pause;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> Stop;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int*, int> GetPlaybackState;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> HasListener;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AddListener;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> RemoveListener;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetListenerEnabled;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetListenerEnabled;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetListenerPrimary;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetListenerPrimary;
	public delegate* unmanaged[Cdecl]<int, float*, int> GetMixerVolume;
	public delegate* unmanaged[Cdecl]<int, float, int> SetMixerVolume;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeAudioSpatialApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> GetStreaming;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> SetStreaming;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> GetSpatialBlend;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> SetSpatialBlend;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> GetMinDistance;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> SetMinDistance;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> GetMaxDistance;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> SetMaxDistance;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeRuntimeUIApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, byte*, uint,
		uint*, int> GetText;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong,
		NativeUtf8View, int> SetText;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> WasButtonClicked;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong*, int>
		GetButtonClickSerial;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> FocusButton;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector4*, int>
		GetRect;
	public delegate* unmanaged[Cdecl]<int> IsGameplayInputCaptured;
}

public enum NativePropertyKindV1 : uint
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
	String
}

[StructLayout(LayoutKind.Sequential)]
public struct NativePropertyValueV1
{
	public NativePropertyKindV1 Kind;
	public uint Reserved;
	public long Integer;
	public double Number;
	public float X;
	public float Y;
	public float Z;
	public float W;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeComponentApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, int> Has;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, int> Add;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, int> Remove;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, ulong,
		NativePropertyValueV1*, int> GetProperty;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, ulong,
		NativePropertyValueV1, int> SetProperty;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeComponentStringApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, ulong, byte*,
		uint, uint*, int> GetProperty;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong, ulong,
		NativeUtf8View, int> SetProperty;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeDeferredCommandsApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View, int>
		AbortBatch;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeDeferredCallbackTransactionsApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<NativeEntityHandleV1, ulong*, int> BeginCallback;
	public delegate* unmanaged[Cdecl]<ulong, int> CompleteCallback;
}

[Flags]
public enum NativeComponentSchemaFlagsV1 : uint
{
	None = 0,
	ScriptAccessible = 1 << 0,
	InspectorVisible = 1 << 1
}

[Flags]
public enum NativeComponentPropertyFlagsV1 : uint
{
	None = 0,
	AssetReference = 1 << 0,
	EntityReference = 1 << 1
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeComponentSchemaInfoV1
{
	public ulong TypeId;
	public ulong ProviderId;
	public uint SchemaVersion;
	public uint PropertyCount;
	public NativeComponentSchemaFlagsV1 Flags;
	public uint Reserved;
	public NativeUtf8View StableName;
	public NativeUtf8View DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeComponentPropertySchemaInfoV1
{
	public ulong ComponentTypeId;
	public ulong PropertyId;
	public NativePropertyKindV1 Kind;
	public NativeComponentPropertyFlagsV1 Flags;
	public NativeUtf8View StableName;
	public NativeUtf8View DisplayName;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeComponentSchemaApiV1
{
	public uint Version;
	public uint Size;
	public delegate* unmanaged[Cdecl]<uint*, int> GetComponentCount;
	public delegate* unmanaged[Cdecl]<uint, NativeComponentSchemaInfoV1*, int>
		GetComponent;
	public delegate* unmanaged[Cdecl]<ulong, uint*, int> GetPropertyCount;
	public delegate* unmanaged[Cdecl]<ulong, uint,
		NativeComponentPropertySchemaInfoV1*, int> GetProperty;
}
