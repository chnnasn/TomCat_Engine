using System.Runtime.InteropServices;
using System.Runtime.InteropServices.JavaScript;

namespace TomCat.WebHost;

internal static unsafe partial class NativeExports
{
    private const string NativeLibrary = "libtomcat_managed_web_entrypoints";

    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_player_boot")]
    internal static partial int PlayerBoot(int width, int height, byte* bytes, nuint size);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_player_frame")]
    internal static partial void PlayerFrame(double delta);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_player_resize")]
    internal static partial void PlayerResize(int width, int height);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_player_shutdown")]
    internal static partial void PlayerShutdown();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_player_error")]
    internal static partial nint PlayerError();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_boot")]
    internal static partial int EditorBoot(int width, int height);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_frame")]
    internal static partial void EditorFrame(double delta);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_resize")]
    internal static partial void EditorResize(int width, int height);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_shutdown")]
    internal static partial void EditorShutdown();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_error")]
    internal static partial nint EditorError();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_rpc", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial nint EditorRpc(string request);
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_state")]
    internal static partial nint EditorState();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_take_actions")]
    internal static partial uint EditorTakeActions();
    [LibraryImport(NativeLibrary, EntryPoint = "tc_web_editor_set_managed_assembly")]
    internal static partial int EditorSetManagedAssembly(byte* assembly, nuint assemblySize,
        byte* pdb, nuint pdbSize);
}

public static unsafe partial class BrowserExports
{
    private static string Utf8(nint value) => value == 0
        ? string.Empty : Marshal.PtrToStringUTF8(value) ?? string.Empty;

    [JSExport]
    public static int PlayerBoot(int width, int height, byte[] package)
    {
        fixed (byte* bytes = package)
            return NativeExports.PlayerBoot(width, height, bytes, (nuint)package.Length);
    }
    [JSExport] public static void PlayerFrame(double delta) => NativeExports.PlayerFrame(delta);
    [JSExport] public static void PlayerResize(int width, int height) => NativeExports.PlayerResize(width, height);
    [JSExport] public static void PlayerShutdown() => NativeExports.PlayerShutdown();
    [JSExport] public static string PlayerError() => Utf8(NativeExports.PlayerError());
    [JSExport] public static int EditorBoot(int width, int height) => NativeExports.EditorBoot(width, height);
    [JSExport] public static void EditorFrame(double delta) => NativeExports.EditorFrame(delta);
    [JSExport] public static void EditorResize(int width, int height) => NativeExports.EditorResize(width, height);
    [JSExport] public static void EditorShutdown() => NativeExports.EditorShutdown();
    [JSExport] public static string EditorError() => Utf8(NativeExports.EditorError());
    [JSExport] public static string EditorRpc(string request) => Utf8(NativeExports.EditorRpc(request));
    [JSExport] public static string EditorState() => Utf8(NativeExports.EditorState());
    [JSExport] public static int EditorTakeActions() =>
        unchecked((int)NativeExports.EditorTakeActions());
    [JSExport]
    public static int EditorSetManagedAssembly(byte[] assembly, byte[] pdb)
    {
        fixed (byte* assemblyBytes = assembly)
        fixed (byte* pdbBytes = pdb)
            return NativeExports.EditorSetManagedAssembly(assemblyBytes,
                (nuint)assembly.Length, pdbBytes, (nuint)pdb.Length);
    }
}
