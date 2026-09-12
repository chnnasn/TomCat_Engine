using System.Runtime.InteropServices;
using System.Text;
using TomCat.Interop;

namespace TomCat;

public sealed class TomCatException : InvalidOperationException
{
    public TomCatException(string message) : base(message) { }
}

internal static unsafe class NativeBridge
{
	private const int NativeInvalidArgument = -1;
	private const int NativeVersionMismatch = -4;
	private const int NativeNotFound = -3;
	private const int NativeBufferTooSmall = -6;
	private const int NativeUnavailable = -8;

	private static readonly object s_bindGate = new();
    private static NativeApiV1 s_api;
    private static bool s_bound;

    internal static bool IsBound => Volatile.Read(ref s_bound);

    internal static int Bind(NativeApiV1* api)
    {
		if (api is null)
			return NativeInvalidArgument;
		if (api->Version != ManagedAbi.NativeApiVersion
			|| api->Size < (uint)sizeof(NativeApiV1))
			return NativeVersionMismatch;

		NativeApiV1 candidate = *api;
		if (!HasRequiredCallbacks(candidate))
			return NativeUnavailable;

		// Publish only a fully validated table. A rejected rebind leaves the last
		// complete process-lifetime table and its bound state untouched.
		lock (s_bindGate)
		{
			if (!s_bound)
			{
				s_api = candidate;
				Volatile.Write(ref s_bound, true);
			}
		}
        return 0;
    }

	private static bool HasRequiredCallbacks(NativeApiV1 api) =>
		api.Log != null && api.EmitDiagnostic != null && api.IsMainThread != null &&
		api.EntityIsAlive != null && api.EntityGetName != null && api.EntitySetName != null &&
		api.EntityGetTag != null && api.EntitySetTag != null && api.EntityGetLayer != null &&
		api.EntitySetLayer != null && api.DestroyEntityDeferred != null &&
		api.HasComponent != null && api.AddComponentDeferred != null &&
		api.RemoveComponentDeferred != null && api.TransformGetPosition != null &&
		api.TransformSetPosition != null && api.TransformGetRotationEuler != null &&
		api.TransformSetRotationEuler != null && api.TransformGetScale != null &&
		api.TransformSetScale != null && api.TransformGetWorldMatrix != null &&
		api.InputIsKeyHeld != null && api.InputWasKeyPressed != null &&
		api.InputWasKeyReleased != null && api.InputGetMousePosition != null &&
		api.InputGetMouseDelta != null && api.InputGetModifiers != null &&
		api.RigidbodyGetLinearVelocity != null && api.RigidbodySetLinearVelocity != null &&
		api.RigidbodyApplyForce != null && api.RigidbodyApplyLinearImpulse != null &&
		api.PhysicsRaycast != null && api.PhysicsQueryAabb != null &&
		api.AssetIsValid != null && api.AssetGetType != null &&
		api.BehaviourGetEnabled != null && api.BehaviourSetEnabledDeferred != null &&
		api.BehaviourRemoveDeferred != null && api.SceneGetActiveHandle != null &&
		api.SceneGetActiveBuildIndex != null && api.SceneRequestLoadHandle != null &&
		api.SceneRequestLoadIndex != null && api.SceneRequestReload != null &&
		api.PrefabInstantiateDeferred != null;

	internal static void EnsureMainThread(bool requireLifecycleContext = true)
	{
		if (!Volatile.Read(ref s_bound))
			throw new TomCatException("The TomCat native API is not initialized.");
		if (s_api.IsMainThread == null || s_api.IsMainThread() == 0)
			throw new TomCatException(
				"WrongThread: TomCat engine APIs may only be used from the main thread.");
		if (requireLifecycleContext && !ScriptExecutionContext.IsActive)
			throw new TomCatException(
				"TomCat engine APIs cannot be used from a script constructor or field initializer. Use OnCreate or another lifecycle callback.");
    }

    internal static NativeEntityHandleV1 ToNative(Entity entity) =>
        new(entity.SceneSessionId, entity.Id, entity.RuntimeGeneration);

    internal static Entity FromNative(NativeEntityHandleV1 entity) =>
        new(entity.SceneSessionId, entity.EntityId, entity.RuntimeGeneration);

    internal static bool EntityIsAlive(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.EntityIsAlive != null, "Entity.IsAlive");
        return ReadBoolean(s_api.EntityIsAlive(ToNative(entity)), "Entity.IsAlive");
    }

    internal static string GetEntityName(Entity entity) => ReadEntityString(entity, s_api.EntityGetName, "Entity.Name");
    internal static string GetEntityTag(Entity entity) => ReadEntityString(entity, s_api.EntityGetTag, "Entity.Tag");

    private static string ReadEntityString(Entity entity,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, byte*, uint, uint*, int> callback,
        string operation)
    {
        EnsureMainThread();
		Require(callback != null, operation);
		uint required = 0;
		int probeStatus = callback(ToNative(entity), null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, operation);
        if (required == 0)
            return string.Empty;
        if (required > 16 * 1024 * 1024)
            throw new TomCatException($"{operation} returned an invalid UTF-8 length.");

        byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
        fixed (byte* buffer = bytes)
        {
            uint actual = required;
            Check(callback(ToNative(entity), buffer, required, &actual), operation);
            if (actual > required)
                throw new TomCatException($"{operation} changed length while being read.");
            return Encoding.UTF8.GetString(bytes, 0, (int)actual);
        }
    }

    internal static void SetEntityName(Entity entity, string value) =>
        WithUtf8(value, "Entity.Name", view => s_api.EntitySetName(ToNative(entity), view), s_api.EntitySetName != null);

    internal static void SetEntityTag(Entity entity, string value) =>
        WithUtf8(value, "Entity.Tag", view => s_api.EntitySetTag(ToNative(entity), view), s_api.EntitySetTag != null);

    internal static uint GetEntityLayer(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.EntityGetLayer != null, "Entity.Layer");
        uint value = 0;
        Check(s_api.EntityGetLayer(ToNative(entity), &value), "Entity.Layer");
        return value;
    }

    internal static void SetEntityLayer(Entity entity, uint value)
    {
        EnsureMainThread();
        Require(s_api.EntitySetLayer != null, "Entity.Layer");
        Check(s_api.EntitySetLayer(ToNative(entity), value), "Entity.Layer");
    }

    internal static void DestroyEntity(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.DestroyEntityDeferred != null, "Entity.Destroy");
        Check(s_api.DestroyEntityDeferred(ToNative(entity)), "Entity.Destroy");
    }

    internal static bool HasComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.HasComponent != null, "Entity.HasComponent");
        return ReadBoolean(s_api.HasComponent(ToNative(entity), (int)type), "Entity.HasComponent");
    }

    internal static void AddComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.AddComponentDeferred != null, "Entity.AddComponent");
        Check(s_api.AddComponentDeferred(ToNative(entity), (int)type), "Entity.AddComponent");
    }

    internal static void RemoveComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.RemoveComponentDeferred != null, "Entity.RemoveComponent");
        Check(s_api.RemoveComponentDeferred(ToNative(entity), (int)type), "Entity.RemoveComponent");
    }

    internal static Vector3 GetTransformVector(Entity entity,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        NativeVector3 value = default;
        Check(callback(ToNative(entity), &value), operation);
        return new(value.X, value.Y, value.Z);
    }

    internal static void SetTransformVector(Entity entity, Vector3 value,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        Check(callback(ToNative(entity), new NativeVector3 { X = value.X, Y = value.Y, Z = value.Z }), operation);
    }

    internal static Matrix4 GetWorldMatrix(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.TransformGetWorldMatrix != null, "Transform.WorldMatrix");
        NativeMatrix4 value = default;
        Check(s_api.TransformGetWorldMatrix(ToNative(entity), &value), "Transform.WorldMatrix");
        return new Matrix4
        {
            M11 = value.M11, M12 = value.M12, M13 = value.M13, M14 = value.M14,
            M21 = value.M21, M22 = value.M22, M23 = value.M23, M24 = value.M24,
            M31 = value.M31, M32 = value.M32, M33 = value.M33, M34 = value.M34,
            M41 = value.M41, M42 = value.M42, M43 = value.M43, M44 = value.M44
        };
    }

    internal static Vector2 GetRigidbodyVelocity(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.RigidbodyGetLinearVelocity != null, "Rigidbody2D.LinearVelocity");
        NativeVector2 value = default;
        Check(s_api.RigidbodyGetLinearVelocity(ToNative(entity), &value), "Rigidbody2D.LinearVelocity");
        return new(value.X, value.Y);
    }

    internal static void SetRigidbodyVector(Entity entity, Vector2 value,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        Check(callback(ToNative(entity), new NativeVector2 { X = value.X, Y = value.Y }), operation);
    }

    internal static bool InputBoolean(KeyCode key,
        delegate* unmanaged[Cdecl]<uint, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        return ReadBoolean(callback((uint)key), operation);
    }

    internal static Vector2 InputVector(delegate* unmanaged[Cdecl]<NativeVector2*, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        NativeVector2 value = default;
        Check(callback(&value), operation);
        return new(value.X, value.Y);
    }

    internal static KeyModifiers GetModifiers()
    {
        EnsureMainThread();
        Require(s_api.InputGetModifiers != null, "Input.Modifiers");
        uint value = 0;
        Check(s_api.InputGetModifiers(&value), "Input.Modifiers");
        return (KeyModifiers)value;
    }

    internal static RaycastHit2D? Raycast(Entity context, Vector2 start, Vector2 end,
        uint layerMask, bool includeTriggers)
    {
        EnsureMainThread();
        Require(s_api.PhysicsRaycast != null, "Physics2D.Raycast");
        NativeRaycastHit2D hit = default;
        int status = s_api.PhysicsRaycast(ToNative(context), ToNative(start), ToNative(end),
            layerMask, includeTriggers ? 1 : 0, &hit);
		if (status == 0)
			return new RaycastHit2D(FromNative(hit.Entity), new(hit.Point.X, hit.Point.Y),
				new(hit.Normal.X, hit.Normal.Y), hit.Fraction, hit.IsTrigger != 0, hit.CollisionLayer);
		if (status == NativeNotFound)
			return null;
        throw new TomCatException($"Physics2D.Raycast failed with status {status}.");
    }

    internal static PhysicsQueryHit2D[] QueryAabb(Entity context, Vector2 minimum, Vector2 maximum,
        uint layerMask, bool includeTriggers)
    {
        EnsureMainThread();
		Require(s_api.PhysicsQueryAabb != null, "Physics2D.QueryAABB");
		uint required = 0;
		int probeStatus = s_api.PhysicsQueryAabb(ToNative(context), ToNative(minimum), ToNative(maximum),
			layerMask, includeTriggers ? 1 : 0, null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, "Physics2D.QueryAABB");
        if (required == 0)
            return [];
        if (required > 1_000_000)
            throw new TomCatException("Physics2D.QueryAABB returned an invalid result count.");

        NativePhysicsQueryHit2D[] native = GC.AllocateUninitializedArray<NativePhysicsQueryHit2D>((int)required);
        fixed (NativePhysicsQueryHit2D* values = native)
        {
            uint actual = required;
            Check(s_api.PhysicsQueryAabb(ToNative(context), ToNative(minimum), ToNative(maximum),
                layerMask, includeTriggers ? 1 : 0, values, required, &actual), "Physics2D.QueryAABB");
            if (actual > required)
                throw new TomCatException("Physics2D.QueryAABB changed count while being read.");
            var result = new PhysicsQueryHit2D[actual];
            for (int index = 0; index < result.Length; ++index)
                result[index] = new(FromNative(native[index].Entity), native[index].IsTrigger != 0,
                    native[index].CollisionLayer);
            return result;
        }
    }

    internal static bool AssetIsValid(ulong handle)
    {
        EnsureMainThread();
        Require(s_api.AssetIsValid != null, "AssetRef.IsValid");
        return ReadBoolean(s_api.AssetIsValid(handle), "AssetRef.IsValid");
    }

    internal static AssetType GetAssetType(ulong handle)
    {
        EnsureMainThread();
        Require(s_api.AssetGetType != null, "AssetRef.Type");
        int type = 0;
        Check(s_api.AssetGetType(handle, &type), "AssetRef.Type");
        return (AssetType)type;
    }

    internal static bool GetBehaviourEnabled(ScriptInstanceHandle instance)
    {
        EnsureMainThread();
        Require(s_api.BehaviourGetEnabled != null, "TomCatBehaviour.Enabled");
        return ReadBoolean(s_api.BehaviourGetEnabled(instance.Value), "TomCatBehaviour.Enabled");
    }

	internal static void SetBehaviourEnabled(ScriptInstanceHandle instance, bool enabled)
    {
        EnsureMainThread();
        Require(s_api.BehaviourSetEnabledDeferred != null, "TomCatBehaviour.Enabled");
		Check(s_api.BehaviourSetEnabledDeferred(instance.Value, enabled ? 1 : 0), "TomCatBehaviour.Enabled");
	}

	internal static void RemoveBehaviour(ScriptInstanceHandle instance)
	{
		EnsureMainThread();
		Require(s_api.BehaviourRemoveDeferred != null, "TomCatBehaviour.RemoveFromEntity");
		Check(s_api.BehaviourRemoveDeferred(instance.Value), "TomCatBehaviour.RemoveFromEntity");
	}

	internal static ulong GetActiveSceneHandle()
	{
		EnsureMainThread();
		Require(s_api.SceneGetActiveHandle != null, "SceneManager.ActiveScene");
		ulong handle = 0;
		Check(s_api.SceneGetActiveHandle(&handle), "SceneManager.ActiveScene");
		return handle;
	}

	internal static int GetActiveSceneBuildIndex()
	{
		EnsureMainThread();
		Require(s_api.SceneGetActiveBuildIndex != null, "SceneManager.ActiveBuildIndex");
		int buildIndex = -1;
		Check(s_api.SceneGetActiveBuildIndex(&buildIndex),
			"SceneManager.ActiveBuildIndex");
		return buildIndex;
	}

	internal static bool RequestLoadScene(ulong sceneHandle)
	{
		EnsureMainThread();
		Require(s_api.SceneRequestLoadHandle != null, "SceneManager.LoadScene(SceneAsset)");
		return ReadBoolean(s_api.SceneRequestLoadHandle(sceneHandle),
			"SceneManager.LoadScene(SceneAsset)");
	}

	internal static bool RequestLoadScene(int buildIndex)
	{
		EnsureMainThread();
		Require(s_api.SceneRequestLoadIndex != null, "SceneManager.LoadScene(int)");
		return ReadBoolean(s_api.SceneRequestLoadIndex(buildIndex),
			"SceneManager.LoadScene(int)");
	}

	internal static bool RequestReloadScene()
	{
		EnsureMainThread();
		Require(s_api.SceneRequestReload != null, "SceneManager.ReloadActiveScene");
		return ReadBoolean(s_api.SceneRequestReload(),
			"SceneManager.ReloadActiveScene");
	}

	internal static bool InstantiatePrefab(Entity context, PrefabAsset prefab,
		Vector3 worldPosition, Entity? parent)
	{
		EnsureMainThread();
		Require(s_api.PrefabInstantiateDeferred != null,
			"TomCatBehaviour.Instantiate");
		if (prefab.Handle == 0)
			return false;
		NativeEntityHandleV1 parentHandle = parent is null
			? default : ToNative(parent);
		return ReadBoolean(s_api.PrefabInstantiateDeferred(ToNative(context),
			prefab.Handle, ToNative(worldPosition), parentHandle),
			"TomCatBehaviour.Instantiate");
	}

    internal static void WriteLog(int level, string message)
    {
        WithUtf8(message, "Log", view => s_api.Log(level, view), s_api.Log != null);
    }

    internal static void ReportManagedException(string message, string? file = null, int line = 0, int column = 0)
    {
		// Host failures can originate in the background metadata compiler. This
		// diagnostic-only callback is thread-safe on the native side and must never
		// throw while an UnmanagedCallersOnly export is already handling an error.
		try
		{
			if (!Volatile.Read(ref s_bound) || s_api.EmitDiagnostic == null)
			{
				Console.Error.WriteLine(message);
				return;
			}

			byte[] messageBytes = Encoding.UTF8.GetBytes(message);
			byte[] fileBytes = Encoding.UTF8.GetBytes(file ?? string.Empty);
			fixed (byte* messagePointer = messageBytes)
			fixed (byte* filePointer = fileBytes)
			{
				NativeDiagnosticV1 diagnostic = new()
				{
					Severity = 2,
					Message = new NativeUtf8View(messagePointer,
						(ulong)messageBytes.Length),
					File = new NativeUtf8View(filePointer,
						(ulong)fileBytes.Length),
					Line = line,
					Column = column
				};
				_ = s_api.EmitDiagnostic(&diagnostic);
			}
		}
		catch
		{
			try { Console.Error.WriteLine(message); }
			catch { }
		}
    }

    private static NativeVector2 ToNative(Vector2 value) => new() { X = value.X, Y = value.Y };
	private static NativeVector3 ToNative(Vector3 value) =>
		new() { X = value.X, Y = value.Y, Z = value.Z };

    private static void WithUtf8(string value, string operation,
        Func<NativeUtf8View, int> callback, bool available)
    {
        ArgumentNullException.ThrowIfNull(value);
        EnsureMainThread();
        Require(available, operation);
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        fixed (byte* pointer = bytes)
            Check(callback(new NativeUtf8View(pointer, (ulong)bytes.Length)), operation);
    }

    private static bool ReadBoolean(int result, string operation)
    {
        if (result >= 0)
            return result != 0;
        throw new TomCatException($"{operation} failed with status {result}.");
    }

    private static void Check(int status, string operation)
    {
        if (status != 0)
            throw new TomCatException($"{operation} failed with status {status}.");
    }

    private static void Require(bool condition, string operation)
    {
        if (!condition)
            throw new TomCatException($"{operation} is unavailable in NativeApiV1.");
    }

    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetPosition => s_api.TransformGetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetPosition => s_api.TransformSetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetRotationEuler => s_api.TransformGetRotationEuler;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetRotationEuler => s_api.TransformSetRotationEuler;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetScale => s_api.TransformGetScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetScale => s_api.TransformSetScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodySetLinearVelocity => s_api.RigidbodySetLinearVelocity;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyForce => s_api.RigidbodyApplyForce;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyLinearImpulse => s_api.RigidbodyApplyLinearImpulse;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputIsKeyHeld => s_api.InputIsKeyHeld;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputWasKeyPressed => s_api.InputWasKeyPressed;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputWasKeyReleased => s_api.InputWasKeyReleased;
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMousePosition => s_api.InputGetMousePosition;
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMouseDelta => s_api.InputGetMouseDelta;
}
