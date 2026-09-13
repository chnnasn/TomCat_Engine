namespace TomCat;

/// <summary>The value shape persisted and exposed by a registered component property.</summary>
public enum ComponentPropertyKind : uint
{
	Bool = 1,
	Int32 = 2,
	Int64 = 3,
	UInt32 = 4,
	UInt64 = 5,
	Float = 6,
	Double = 7,
	Vector2 = 8,
	Vector3 = 9,
	Vector4 = 10,
	String = 11
}

[Flags]
public enum ComponentSchemaFlags : uint
{
	None = 0,
	ScriptAccessible = 1 << 0,
	InspectorVisible = 1 << 1
}

[Flags]
public enum ComponentPropertySchemaFlags : uint
{
	None = 0,
	AssetReference = 1 << 0,
	EntityReference = 1 << 1
}

public sealed class ComponentPropertySchemaInfo
{
	internal ComponentPropertySchemaInfo(ulong propertyId,
		ComponentPropertyKind kind, ComponentPropertySchemaFlags flags,
		string stableName, string displayName)
	{
		PropertyId = propertyId;
		Kind = kind;
		Flags = flags;
		StableName = stableName;
		DisplayName = displayName;
	}

	public ulong PropertyId { get; }
	public ComponentPropertyKind Kind { get; }
	public ComponentPropertySchemaFlags Flags { get; }
	public string StableName { get; }
	public string DisplayName { get; }
	public bool IsAssetReference =>
		(Flags & ComponentPropertySchemaFlags.AssetReference) != 0;
	public bool IsEntityReference =>
		(Flags & ComponentPropertySchemaFlags.EntityReference) != 0;
}

public sealed class ComponentSchemaInfo
{
	internal ComponentSchemaInfo(ulong typeId, ulong providerId,
		uint schemaVersion, ComponentSchemaFlags flags, string stableName,
		string displayName, IReadOnlyList<ComponentPropertySchemaInfo> properties)
	{
		TypeId = typeId;
		ProviderId = providerId;
		SchemaVersion = schemaVersion;
		Flags = flags;
		StableName = stableName;
		DisplayName = displayName;
		Properties = properties;
	}

	public ulong TypeId { get; }
	/// <summary>Zero identifies an engine-owned component.</summary>
	public ulong ProviderId { get; }
	public uint SchemaVersion { get; }
	public ComponentSchemaFlags Flags { get; }
	public string StableName { get; }
	public string DisplayName { get; }
	public IReadOnlyList<ComponentPropertySchemaInfo> Properties { get; }
	public bool IsScriptAccessible =>
		(Flags & ComponentSchemaFlags.ScriptAccessible) != 0;
	public bool IsInspectorVisible =>
		(Flags & ComponentSchemaFlags.InspectorVisible) != 0;
}

/// <summary>
/// Enumerates the current native component registry. Each call returns a fresh,
/// fully managed snapshot so module unload cannot invalidate its strings.
/// </summary>
public static class ComponentSchema
{
	public static bool IsAvailable => NativeBridge.IsComponentSchemaAvailable;

	public static IReadOnlyList<ComponentSchemaInfo> GetComponents() =>
		NativeBridge.GetComponentSchemas();

	public static bool TryGetComponent(ulong typeId,
		out ComponentSchemaInfo? component)
	{
		foreach (ComponentSchemaInfo candidate in GetComponents())
		{
			if (candidate.TypeId != typeId)
				continue;
			component = candidate;
			return true;
		}
		component = null;
		return false;
	}
}
