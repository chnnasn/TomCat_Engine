using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using TomCat.Interop;
using TomCat.ScriptHost;

namespace TomCat.WebHost;

internal static unsafe partial class NativeMethods
{
    [LibraryImport("__Internal", EntryPoint = "tc_web_managed_install")]
    internal static partial int Install(nint getManagedApi);
}

internal static unsafe class Program
{
    private static int Main()
    {
        nint entry = (nint)(delegate* unmanaged[Cdecl]<NativeApiV1*, ManagedApiV1*, int>)
            &EntryPoint.GetManagedApi;
        int status = NativeMethods.Install(entry);
        if (status != 0)
        {
            Console.Error.WriteLine($"TomCat native/managed Web bootstrap failed: {status}");
            return status;
        }
        Console.WriteLine("TomCat .NET WebAssembly runtime ready");
        return 0;
    }
}
