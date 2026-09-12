using System.Runtime.InteropServices;

namespace TomCat.Interop;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct ManagedApiV1
{
    public uint Version;
    public uint Size;
    public delegate* unmanaged[Cdecl]<int, ulong*, int> CreateDomain;
    public delegate* unmanaged[Cdecl]<ulong, NativeByteView, NativeByteView, int> LoadProjectAssembly;
    public delegate* unmanaged[Cdecl]<ulong, delegate* unmanaged[Cdecl]<NativeByteView, ulong, int>, ulong, int> ReadScriptMetadata;
    public delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong*, int> CreateSceneRuntime;
    public delegate* unmanaged[Cdecl]<ulong, NativeScriptAttachmentV1*, uint, int> InstantiateAll;
    public delegate* unmanaged[Cdecl]<ulong, NativeByteView, int> ApplySerializedFields;
    public delegate* unmanaged[Cdecl]<ulong, int> InvokeCreateAll;
    public delegate* unmanaged[Cdecl]<ulong, ulong, int, int> SetEnabled;
    public delegate* unmanaged[Cdecl]<ulong, float, int> UpdateAll;
    public delegate* unmanaged[Cdecl]<ulong, float, int> FixedUpdateAll;
    public delegate* unmanaged[Cdecl]<ulong, NativePhysicsEventV1*, uint, int> DispatchPhysicsEvents;
    public delegate* unmanaged[Cdecl]<ulong, int> DestroyAll;
    public delegate* unmanaged[Cdecl]<ulong, int> BeginUnloadDomain;
	public delegate* unmanaged[Cdecl]<ulong, int*, int> PollUnload;
	public delegate* unmanaged[Cdecl]<ulong, ulong*, uint, int> DestroyAttachments;
}
