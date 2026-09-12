using System.Linq.Expressions;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace TomCat.ScriptHost;

public enum ScriptDomainKind
{
    Metadata = 0,
    Play = 1
}

public sealed class ScriptDomain : IDisposable
{
    private ProjectLoadContext? _loadContext;
	private Assembly? _assembly;
	private CancellationTokenSource? _domainCancellation = new();
    private readonly Dictionary<ulong, ScriptDescriptor> _descriptors = [];
    private readonly Dictionary<ulong, ScriptSceneRuntime> _scenes = [];
    private WeakReference? _unloadReference;
    private bool _unloadBegun;
    private static long s_nextSceneId;

    public ScriptDomain(ScriptDomainKind kind)
    {
        Kind = kind;
        _loadContext = new ProjectLoadContext($"TomCat-{kind}-{Guid.NewGuid():N}");
    }

    public ScriptDomainKind Kind { get; }
    public ScriptManifest Manifest { get; private set; } = new() { Version = 1 };
    public string ManifestJson { get; private set; } = "{\"version\":1,\"scripts\":[]}";
	public bool IsLoaded => _assembly is not null;
	internal CancellationToken DomainCancellationToken =>
		_domainCancellation?.Token ?? default;

    public void LoadProjectAssembly(ReadOnlySpan<byte> assemblyBytes, ReadOnlySpan<byte> pdbBytes = default)
    {
        ObjectDisposedException.ThrowIf(_unloadBegun, this);
        if (_assembly is not null)
            throw new InvalidOperationException("A ScriptDomain can load only one project assembly.");
        if (assemblyBytes.IsEmpty)
            throw new ArgumentException("Project assembly bytes cannot be empty.", nameof(assemblyBytes));

        LoadProjectAssemblyCore(assemblyBytes.ToArray(), pdbBytes.IsEmpty ? null : pdbBytes.ToArray());
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private void LoadProjectAssemblyCore(byte[] assemblyBytes, byte[]? pdbBytes)
    {
        ProjectLoadContext context = _loadContext ?? throw new ObjectDisposedException(nameof(ScriptDomain));
        using var assemblyStream = new MemoryStream(assemblyBytes, writable: false);
        using var pdbStream = pdbBytes is null ? null : new MemoryStream(pdbBytes, writable: false);
        Assembly assembly = pdbStream is null
            ? context.LoadFromStream(assemblyStream)
            : context.LoadFromStream(assemblyStream, pdbStream);

        Type manifestType = assembly.GetType("TomCat.Generated.ScriptManifest", throwOnError: true,
            ignoreCase: false)!;
        PropertyInfo jsonProperty = manifestType.GetProperty("Json",
            BindingFlags.Public | BindingFlags.Static) ??
            throw new InvalidDataException("Generated script manifest has no public static Json property.");
        string json = jsonProperty.GetValue(null) as string ??
            throw new InvalidDataException("Generated script manifest JSON is null.");
        ScriptManifest manifest = ScriptManifest.Parse(json);

		Dictionary<ulong, ScriptDescriptor> descriptors = BuildDescriptors(assembly, manifest);
		string metadataJson = Kind == ScriptDomainKind.Metadata
			? BuildMetadataJson(manifest, descriptors)
			: json;
		_assembly = assembly;
		ManifestJson = metadataJson;
        Manifest = manifest;
        foreach ((ulong handle, ScriptDescriptor descriptor) in descriptors)
            _descriptors.Add(handle, descriptor);
    }

    public ScriptSceneRuntime CreateSceneRuntime(ulong sceneSessionId, ulong runtimeGeneration)
    {
        ObjectDisposedException.ThrowIf(_unloadBegun, this);
        if (_assembly is null)
            throw new InvalidOperationException("Load a project assembly before creating a scene runtime.");
        if (Kind != ScriptDomainKind.Play)
            throw new InvalidOperationException("Only a Play domain can create script instances.");
        if (sceneSessionId == 0 || runtimeGeneration == 0)
            throw new ArgumentOutOfRangeException(nameof(sceneSessionId), "Scene session and generation must be nonzero.");

        ulong id = unchecked((ulong)Interlocked.Increment(ref s_nextSceneId));
		var scene = new ScriptSceneRuntime(id, sceneSessionId, runtimeGeneration, _descriptors,
			DomainCancellationToken, () => _scenes.Remove(id));
        _scenes.Add(id, scene);
        return scene;
    }

    public void BeginUnload()
    {
        if (_unloadBegun)
            return;
        _unloadBegun = true;

		foreach (ScriptSceneRuntime scene in _scenes.Values.ToArray())
			scene.DestroyAll();
		_scenes.Clear();
		CancellationTokenSource? cancellation = _domainCancellation;
		_domainCancellation = null;
		if (cancellation is not null)
		{
			try
			{
				cancellation.Cancel(throwOnFirstException: false);
			}
			catch (AggregateException exception)
			{
				NativeBridge.ReportManagedException(
					$"One or more Play Domain cancellation callbacks failed: {exception}");
			}
			finally
			{
				cancellation.Dispose();
			}
		}
		_descriptors.Clear();
        Manifest = new ScriptManifest { Version = 1 };
        ManifestJson = "{\"version\":1,\"scripts\":[]}";
        _assembly = null;

        ProjectLoadContext? context = _loadContext;
        _loadContext = null;
        if (context is not null)
        {
            _unloadReference = new WeakReference(context, trackResurrection: false);
            context.Unload();
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    public bool PollUnload()
    {
        if (!_unloadBegun)
            return false;
        if (_unloadReference is null || !_unloadReference.IsAlive)
            return true;
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        return !_unloadReference.IsAlive;
    }

    public void Dispose() => BeginUnload();

    private static Dictionary<ulong, ScriptDescriptor> BuildDescriptors(Assembly assembly,
        ScriptManifest manifest)
    {
        var result = new Dictionary<ulong, ScriptDescriptor>();
        foreach (ScriptTypeManifest script in manifest.Scripts)
        {
            if (script.AssetHandle == 0 || !result.TryAdd(script.AssetHandle,
                    CreateDescriptor(assembly, script)))
                throw new InvalidDataException($"Duplicate or zero script AssetHandle {script.AssetHandle}.");
        }
        return result;
    }

	private static ScriptDescriptor CreateDescriptor(Assembly assembly, ScriptTypeManifest manifest)
    {
        Type type = assembly.GetType(manifest.TypeName, throwOnError: true, ignoreCase: false)!;
        if (!type.IsClass || type.IsAbstract || type.ContainsGenericParameters || type.IsNested ||
            !typeof(TomCatBehaviour).IsAssignableFrom(type))
            throw new InvalidDataException($"Manifest type '{manifest.TypeName}' is not a mountable TomCatBehaviour.");
        ConstructorInfo constructor = type.GetConstructor(BindingFlags.Instance | BindingFlags.Public |
            BindingFlags.NonPublic, binder: null, Type.EmptyTypes, modifiers: null) ??
            throw new InvalidDataException($"Script '{manifest.TypeName}' has no parameterless constructor.");
		Func<TomCatBehaviour> constructorFactory;
		try
		{
			NewExpression create = Expression.New(constructor);
			UnaryExpression convert = Expression.Convert(create, typeof(TomCatBehaviour));
			constructorFactory = Expression.Lambda<Func<TomCatBehaviour>>(convert).Compile();
		}
		catch (Exception exception)
		{
			throw new InvalidDataException(
				$"Script '{manifest.TypeName}' parameterless constructor could not be compiled.",
				exception);
		}

        var byId = new Dictionary<string, FieldDescriptor>(StringComparer.Ordinal);
        var byName = new Dictionary<string, FieldDescriptor>(StringComparer.Ordinal);
        foreach (ScriptFieldManifest fieldManifest in manifest.Fields)
        {
            if (fieldManifest.Id.Length != 32 || !fieldManifest.Id.All(Uri.IsHexDigit))
                throw new InvalidDataException($"Field '{manifest.TypeName}.{fieldManifest.Name}' has an invalid FieldID.");
            FieldInfo field = type.GetField(fieldManifest.Name, BindingFlags.Instance | BindingFlags.Public |
                BindingFlags.NonPublic) ?? throw new InvalidDataException(
                $"Manifest field '{manifest.TypeName}.{fieldManifest.Name}' does not exist.");
            var descriptor = new FieldDescriptor { Manifest = fieldManifest, Field = field };
            if (!byId.TryAdd(fieldManifest.Id, descriptor) || !byName.TryAdd(fieldManifest.Name, descriptor))
                throw new InvalidDataException($"Script '{manifest.TypeName}' has duplicate field metadata.");
            foreach (string formerName in fieldManifest.FormerNames)
            {
                if (!string.IsNullOrWhiteSpace(formerName))
                    byName.TryAdd(formerName, descriptor);
            }
        }

        return new ScriptDescriptor
        {
            Manifest = manifest,
            Type = type,
            ConstructorFactory = constructorFactory,
            FieldsById = byId,
            FieldsByName = byName
		};
	}

	private static string BuildMetadataJson(ScriptManifest manifest,
		Dictionary<ulong, ScriptDescriptor> descriptors)
	{
		foreach (ScriptDescriptor descriptor in descriptors.Values)
		{
			TomCatBehaviour behaviour;
			try
			{
				behaviour = descriptor.ConstructorFactory();
			}
			catch (Exception exception)
			{
				Exception actual = exception is TargetInvocationException { InnerException: not null }
					? exception.InnerException : exception;
				throw new InvalidDataException(
					$"Script '{descriptor.Manifest.TypeName}' could not provide Inspector defaults. " +
					"Constructors and field initializers must use pure C# data.", actual);
			}

			foreach (FieldDescriptor field in descriptor.FieldsById.Values)
				field.Manifest.DefaultValue = ToMetadataValue(field, field.Field.GetValue(behaviour));
		}
		return JsonSerializer.Serialize(manifest, JsonOptions.Instance);
	}

	private static object? ToMetadataValue(FieldDescriptor descriptor, object? value)
	{
		return descriptor.Manifest.Type switch
		{
			ScriptFieldType.Bool or ScriptFieldType.Int32 or ScriptFieldType.Int64 or
				ScriptFieldType.Float or ScriptFieldType.Double => value,
			ScriptFieldType.String => value as string ?? string.Empty,
			ScriptFieldType.Vector2 when value is Vector2 vector => new[] { vector.X, vector.Y },
			ScriptFieldType.Vector3 when value is Vector3 vector =>
				new[] { vector.X, vector.Y, vector.Z },
			ScriptFieldType.Vector4 when value is Vector4 vector =>
				new[] { vector.X, vector.Y, vector.Z, vector.W },
			ScriptFieldType.Color when value is Color color =>
				new[] { color.R, color.G, color.B, color.A },
			ScriptFieldType.Enum when value is not null =>
				EnumToStorageBits(descriptor.Field.FieldType, value),
			ScriptFieldType.Entity when value is Entity entity => entity.Id,
			ScriptFieldType.AssetRef when value is not null =>
				value.GetType().GetProperty("Handle")?.GetValue(value) as ulong? ?? 0UL,
			_ => throw new InvalidDataException(
				$"Could not read the default value of '{descriptor.Field.DeclaringType?.FullName}." +
				$"{descriptor.Field.Name}'.")
		};
	}

	private static long EnumToStorageBits(Type enumType, object value)
	{
		return Type.GetTypeCode(Enum.GetUnderlyingType(enumType)) switch
		{
			TypeCode.Byte => Convert.ToByte(value),
			TypeCode.UInt16 => Convert.ToUInt16(value),
			TypeCode.UInt32 => Convert.ToUInt32(value),
			TypeCode.UInt64 => unchecked((long)Convert.ToUInt64(value)),
			_ => Convert.ToInt64(value)
		};
	}
}

public readonly record struct ScriptAttachment(Entity Entity, ulong AttachmentId, ulong ScriptAsset,
    bool Enabled);

public enum ScriptInstanceState
{
    Ready,
    Faulted,
    Destroyed
}
