using System.Runtime.InteropServices;

namespace TomCat.Interop;

public static class ManagedAbi
{
    public const uint NativeApiVersion = 1;
    public const uint ManagedApiVersion = 1;
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
    SpriteRenderer = 6
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
}
