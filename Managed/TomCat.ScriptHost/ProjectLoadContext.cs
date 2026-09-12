using System.Reflection;
using System.Runtime.Loader;

namespace TomCat.ScriptHost;

internal sealed class ProjectLoadContext(string name) : AssemblyLoadContext(name, isCollectible: true)
{
    private static readonly Assembly SharedManagedAssembly = typeof(TomCatBehaviour).Assembly;
    private static readonly string SharedManagedName = SharedManagedAssembly.GetName().Name!;

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        // This is the type-identity boundary: a project must use the exact
        // TomCat.Managed instance already resident in the default ALC.
        if (string.Equals(assemblyName.Name, SharedManagedName, StringComparison.Ordinal))
            return SharedManagedAssembly;
        return null;
    }
}
