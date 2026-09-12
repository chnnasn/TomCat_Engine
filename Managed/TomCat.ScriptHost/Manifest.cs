using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using TomCat.Interop;

namespace TomCat.ScriptHost;

[Flags]
public enum ScriptLifecycle : uint
{
    None = 0,
    Create = 1 << 0,
    Enable = 1 << 1,
    Update = 1 << 2,
    FixedUpdate = 1 << 3,
    CollisionEnter2D = 1 << 4,
    CollisionExit2D = 1 << 5,
    TriggerEnter2D = 1 << 6,
    TriggerExit2D = 1 << 7,
    Disable = 1 << 8,
    Destroy = 1 << 9
}

[JsonConverter(typeof(JsonStringEnumConverter<ScriptFieldType>))]
public enum ScriptFieldType
{
    Bool,
    Int32,
    Int64,
    Float,
    Double,
    String,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Enum,
    Entity,
    AssetRef
}

public sealed class ScriptManifest
{
    [JsonPropertyName("version")]
    public uint Version { get; init; }

    [JsonPropertyName("scripts")]
    public List<ScriptTypeManifest> Scripts { get; init; } = [];

    internal static ScriptManifest Parse(string json)
    {
        ScriptManifest? manifest = JsonSerializer.Deserialize<ScriptManifest>(json, JsonOptions.Instance);
        if (manifest is null || manifest.Version != ManagedAbi.ScriptManifestVersion)
            throw new InvalidDataException($"Script manifest version must be {ManagedAbi.ScriptManifestVersion}.");
        return manifest;
    }
}

public sealed class ScriptTypeManifest
{
    [JsonPropertyName("assetHandle")]
    public ulong AssetHandle { get; init; }

    [JsonPropertyName("typeName")]
    public string TypeName { get; init; } = string.Empty;

    [JsonPropertyName("executionOrder")]
    public int ExecutionOrder { get; init; }

    [JsonPropertyName("disallowMultiple")]
    public bool DisallowMultiple { get; init; }

    [JsonPropertyName("lifecycle")]
    public ScriptLifecycle Lifecycle { get; init; }

    [JsonPropertyName("fields")]
    public List<ScriptFieldManifest> Fields { get; init; } = [];
}

public sealed class ScriptFieldManifest
{
    [JsonPropertyName("id")]
    public string Id { get; init; } = string.Empty;

    [JsonPropertyName("name")]
    public string Name { get; init; } = string.Empty;

    [JsonPropertyName("type")]
    public ScriptFieldType Type { get; init; }

    [JsonPropertyName("typeName")]
    public string? TypeName { get; init; }

    [JsonPropertyName("isPublic")]
    public bool IsPublic { get; init; }

    [JsonPropertyName("hidden")]
    public bool Hidden { get; init; }

    [JsonPropertyName("header")]
    public string? Header { get; init; }

    [JsonPropertyName("tooltip")]
    public string? Tooltip { get; init; }

    [JsonPropertyName("rangeMin")]
    public float? RangeMinimum { get; init; }

    [JsonPropertyName("rangeMax")]
    public float? RangeMaximum { get; init; }

	[JsonPropertyName("formerNames")]
	public List<string> FormerNames { get; init; } = [];

	// Metadata domains populate this by constructing one pristine script instance.
	// It is not trusted for runtime restore; native scene data remains authoritative.
	[JsonPropertyName("defaultValue")]
	public object? DefaultValue { get; set; }
}

internal static class JsonOptions
{
    internal static readonly JsonSerializerOptions Instance = new()
    {
        PropertyNameCaseInsensitive = false,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        Converters = { new JsonStringEnumConverter() }
    };
}

internal sealed class ScriptDescriptor
{
    internal required ScriptTypeManifest Manifest { get; init; }
    internal required Type Type { get; init; }
    internal required Func<TomCatBehaviour> ConstructorFactory { get; init; }
    internal required Dictionary<string, FieldDescriptor> FieldsById { get; init; }
    internal required Dictionary<string, FieldDescriptor> FieldsByName { get; init; }
}

internal sealed class FieldDescriptor
{
    internal required ScriptFieldManifest Manifest { get; init; }
    internal required FieldInfo Field { get; init; }
}
