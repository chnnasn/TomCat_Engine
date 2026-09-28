using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.JavaScript;
using System.Runtime.Versioning;
using TomCat.Interop;
using TomCat.ScriptHost;

[assembly: SupportedOSPlatform("browser")]

namespace TomCat.WebHost;

internal static unsafe partial class NativeMethods
{
    [LibraryImport("libtc_player_core", EntryPoint = "tc_web_managed_install")]
    internal static partial int Install(nint getManagedApi);
}

internal static unsafe class Program
{
    private static int Main()
    {
        try
        {
            int status = BrowserBootstrap.Initialize();
            if (status != 0)
                BrowserBootstrap.SetError($"Native/managed bootstrap returned {status}.");
            return status;
        }
        catch (Exception exception)
        {
            BrowserBootstrap.SetError(exception.ToString());
            return 100;
        }
    }
}

public static unsafe partial class BrowserBootstrap
{
    private static string s_error = string.Empty;

    internal static void SetError(string error)
    {
        s_error = error;
        Console.Error.WriteLine(error);
    }

    [JSExport]
    public static string Error() => s_error;

    [JSExport]
    public static int Initialize()
    {
        nint entry = (nint)(delegate* unmanaged[Cdecl]<NativeApiV1*, ManagedApiV1*, int>)
            &EntryPoint.GetManagedApi;
        int status = NativeMethods.Install(entry);
        if (status == 0)
            Console.WriteLine("TomCat .NET WebAssembly runtime ready");
        return status;
    }
}
