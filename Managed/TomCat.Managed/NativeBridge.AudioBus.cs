using System.Runtime.InteropServices;
using System.Text;
using TomCat.Interop;
namespace TomCat;
internal static unsafe partial class NativeBridge {
    [StructLayout(LayoutKind.Sequential)] private struct BusApi {
        public uint Version,Size;
        public delegate* unmanaged[Cdecl]<NativeUtf8View,int> Configure;
        public delegate* unmanaged[Cdecl]<uint,float,int,int,int> SetBus;
    }
    private static BusApi s_busApi;
    private static bool s_busBound;
    private static bool TryReadBusCapability(NativeApiV1* api,out BusApi result) {
        result=default;if(api->Size<(uint)sizeof(NativeApiV2))return false;
        var envelope=(NativeApiV2*)api;if(envelope->QueryCapability==null)return false;
        byte[] name=Encoding.UTF8.GetBytes("TomCat.AudioBusApiV1");BusApi candidate=default;
        fixed(byte* pointer=name) {uint required=0;if(envelope->QueryCapability(new NativeUtf8View(pointer,(ulong)name.Length),1,&candidate,(uint)sizeof(BusApi),&required)!=0 || required>(uint)sizeof(BusApi))return false;}
        if(candidate.Version!=1 || candidate.Size<(uint)sizeof(BusApi) || candidate.Configure==null || candidate.SetBus==null)return false;
        result=candidate;return true;
    }
    internal static void ConfigureAudioBuses(string document) {
        if(!Volatile.Read(ref s_busBound))throw new NotSupportedException("TomCat.AudioBusApiV1 is unavailable.");
        ArgumentNullException.ThrowIfNull(document);byte[] data=Encoding.UTF8.GetBytes(document);
        fixed(byte* p=data) Check(s_busApi.Configure(new NativeUtf8View(p,(ulong)data.Length)),"ConfigureAudioBuses");
    }
    internal static void SetAudioBus(uint id,float volume,bool muted,bool solo) {
        if(!Volatile.Read(ref s_busBound))throw new NotSupportedException("TomCat.AudioBusApiV1 is unavailable.");
        Check(s_busApi.SetBus(id,volume,muted?1:0,solo?1:0),"SetAudioBus");
    }
}
