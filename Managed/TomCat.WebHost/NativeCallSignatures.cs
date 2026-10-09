using System.Runtime.InteropServices;

namespace TomCat.WebHost;

// The .NET WASM SDK discovers P/Invoke and attributed delegate signatures, but
// not delegate* calli sites in NativeBridge. Keep the additional ABI shapes in
// this rooted host assembly so game scripts loaded after publish can call them.
// I = wasm32 pointer/int/indirect struct, L = int64, F = float32.
// These are signature declarations only; calls still use the native API table.
internal static class NativeCallSignatures
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IL(long a);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ILI(long a, nint b);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ILII(long a, nint b, nint c);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ILIII(long a, nint b, nint c, nint d);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IILII(nint a, long b, nint c, nint d);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IILIII(nint a, long b, nint c, nint d, nint e);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IIILIII(nint a, nint b, long c, nint d, nint e, nint f);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IILLIII(nint a, long b, long c, nint d, nint e, nint f);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IIF(nint a, float b);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int IIIF(nint a, nint b, float c);
}
