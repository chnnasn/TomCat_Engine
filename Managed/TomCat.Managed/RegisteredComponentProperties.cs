namespace TomCat;

/// <summary>
/// Typed access to properties published by a script-accessible native
/// ComponentRegistry descriptor. Plugin proxies can use these methods without
/// relying on engine-internal interop classes.
/// </summary>
public static class RegisteredComponentProperties
{
	/// <summary>Maximum encoded size accepted by the V1 UTF-8 transport.</summary>
	public const int MaximumStringUtf8Bytes = 16 * 1024 * 1024;

	/// <summary>Whether the native runtime exposes the additive UTF-8 transport.</summary>
	public static bool IsStringTransportAvailable =>
		NativeBridge.IsComponentStringAvailable;

	public static bool GetBool(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredBool(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetBool(Entity entity, ulong componentTypeId,
		ulong propertyId, bool value) => NativeBridge.SetRegisteredBool(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static int GetInt32(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredInt32(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetInt32(Entity entity, ulong componentTypeId,
		ulong propertyId, int value) => NativeBridge.SetRegisteredInt32(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static long GetInt64(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredInt64(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetInt64(Entity entity, ulong componentTypeId,
		ulong propertyId, long value) => NativeBridge.SetRegisteredInt64(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static uint GetUInt32(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredUInt32(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetUInt32(Entity entity, ulong componentTypeId,
		ulong propertyId, uint value) => NativeBridge.SetRegisteredUInt32(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static ulong GetUInt64(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredUInt64(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetUInt64(Entity entity, ulong componentTypeId,
		ulong propertyId, ulong value) => NativeBridge.SetRegisteredUInt64(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static float GetFloat(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredFloat(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetFloat(Entity entity, ulong componentTypeId,
		ulong propertyId, float value) => NativeBridge.SetRegisteredFloat(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static double GetDouble(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredDouble(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetDouble(Entity entity, ulong componentTypeId,
		ulong propertyId, double value) => NativeBridge.SetRegisteredDouble(ValidateMutation(entity,
		componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static Vector2 GetVector2(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredVector2(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetVector2(Entity entity, ulong componentTypeId,
		ulong propertyId, Vector2 value) => NativeBridge.SetRegisteredVector2(ValidateMutation(
		entity, componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static Vector3 GetVector3(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredVector3(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetVector3(Entity entity, ulong componentTypeId,
		ulong propertyId, Vector3 value) => NativeBridge.SetRegisteredVector3(ValidateMutation(
		entity, componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static Vector4 GetVector4(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredVector4(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetVector4(Entity entity, ulong componentTypeId,
		ulong propertyId, Vector4 value) => NativeBridge.SetRegisteredVector4(ValidateMutation(
		entity, componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	/// <summary>Color convenience access for a native Vector4 property.</summary>
	public static Color GetColor(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredColor(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	/// <summary>Color convenience access for a native Vector4 property.</summary>
	public static void SetColor(Entity entity, ulong componentTypeId,
		ulong propertyId, Color value) => NativeBridge.SetRegisteredColor(ValidateMutation(
		entity, componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	public static string GetString(Entity entity, ulong componentTypeId,
		ulong propertyId) => NativeBridge.GetRegisteredString(Validate(entity,
		componentTypeId, propertyId), componentTypeId, propertyId,
		Operation(componentTypeId, propertyId));

	public static void SetString(Entity entity, ulong componentTypeId,
		ulong propertyId, string value) => NativeBridge.SetRegisteredString(ValidateMutation(
		entity, componentTypeId, propertyId), componentTypeId, propertyId, value,
		Operation(componentTypeId, propertyId));

	private static Entity ValidateMutation(Entity entity, ulong componentTypeId,
		ulong propertyId)
	{
		ArgumentNullException.ThrowIfNull(entity);
		if (componentTypeId == 0)
		{
			NativeBridge.AbortDeferredCommandBatch(entity,
				"Registered component type ID cannot be zero");
			throw new ArgumentOutOfRangeException(nameof(componentTypeId));
		}
		if (propertyId == 0)
		{
			NativeBridge.AbortDeferredCommandBatch(entity,
				"Registered component property ID cannot be zero");
			throw new ArgumentOutOfRangeException(nameof(propertyId));
		}
		return entity;
	}

	private static Entity Validate(Entity entity, ulong componentTypeId,
		ulong propertyId)
	{
		ArgumentNullException.ThrowIfNull(entity);
		if (componentTypeId == 0)
			throw new ArgumentOutOfRangeException(nameof(componentTypeId));
		if (propertyId == 0)
			throw new ArgumentOutOfRangeException(nameof(propertyId));
		return entity;
	}

	private static string Operation(ulong componentTypeId, ulong propertyId) =>
		$"Component 0x{componentTypeId:x16} property 0x{propertyId:x16}";
}
