using System.Reflection;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.Loader;

namespace TomCat.ScriptHost;

internal sealed class ProjectLoadContext(string name) : AssemblyLoadContext(name, isCollectible: true)
{
    private static readonly Assembly SharedManagedAssembly = typeof(TomCatBehaviour).Assembly;
    private static readonly string SharedManagedName = SharedManagedAssembly.GetName().Name!;
    private readonly Dictionary<string, byte[]> _dependencies = new(StringComparer.OrdinalIgnoreCase);

    // Dependencies travel inside Assembly-CSharp, so cooked Players and editor
    // domains resolve exactly the same bytes without probing a developer cache.
    internal void RegisterDependencies(Assembly project)
    {
        foreach (string resource in project.GetManifestResourceNames())
        {
            if (!resource.StartsWith("TomCat.Dependency/", StringComparison.Ordinal))
                continue;
            using Stream stream = project.GetManifestResourceStream(resource)!;
            using var copy = new MemoryStream();
            stream.CopyTo(copy);
            byte[] bytes = copy.ToArray();
            using var pe = new PEReader(new MemoryStream(bytes, writable: false));
            if (!pe.HasMetadata)
                throw new NotSupportedException($"Dependency '{resource}' is native. Only managed dependencies are supported.");
            MetadataReader reader = pe.GetMetadataReader();
            AssemblyDefinition definition = reader.GetAssemblyDefinition();
            string name = reader.GetString(definition.Name);
            string culture = reader.GetString(definition.Culture);
            if (name == SharedManagedName || name == "TomCat.ScriptHost" || name == "Assembly-CSharp")
                throw new InvalidDataException($"Dependency '{name}' conflicts with an engine assembly.");
            if (!_dependencies.TryAdd(Key(name, culture), bytes))
                throw new InvalidDataException($"Duplicate dependency assembly '{name}' ({culture}).");
        }
    }

    private static string Key(string name, string? culture) => $"{culture ?? string.Empty}/{name}";

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        // This is the type-identity boundary: a project must use the exact
        // TomCat.Managed instance already resident in the default ALC.
        if (string.Equals(assemblyName.Name, SharedManagedName, StringComparison.Ordinal))
            return SharedManagedAssembly;
        if (_dependencies.TryGetValue(Key(assemblyName.Name!, assemblyName.CultureName), out byte[]? bytes))
        {
            using var stream = new MemoryStream(bytes, writable: false);
            return LoadFromStream(stream);
        }
        return null;
    }
}
