using System.Runtime.InteropServices;
using System.Text;
using TomCat.Interop;
namespace TomCat;
internal static unsafe partial class NativeBridge
{
    [StructLayout(LayoutKind.Sequential)]
    private struct MixerApi {
        public uint Version, Size;
        public delegate* unmanaged[Cdecl]<int,int,int> SetMuted, SetSolo;
        public delegate* unmanaged[Cdecl]<float*,uint,uint,double,int> ApplySnapshot;
        public delegate* unmanaged[Cdecl]<int,int,float,double,double,int> SetDucking;
        public delegate* unmanaged[Cdecl]<int> ClearDucking;
    }
    private static MixerApi s_mixerApi;
    private static bool s_mixerBound;
    private static bool TryReadMixerCapability(NativeApiV1* api, out MixerApi result) {
        result=default;
        if (api->Size<(uint)sizeof(NativeApiV2)) return false;
        var envelope=(NativeApiV2*)api;
        if (envelope->QueryCapability==null) return false;
        byte[] name=Encoding.UTF8.GetBytes("TomCat.AudioMixerApiV1");
        MixerApi candidate=default;
        fixed(byte* pointer=name) {
            uint required=0;
            if (envelope->QueryCapability(new NativeUtf8View(pointer,(ulong)name.Length),1,&candidate,(uint)sizeof(MixerApi),&required)!=0 || required>(uint)sizeof(MixerApi)) return false;
        }
        if (candidate.Version!=1 || candidate.Size<(uint)sizeof(MixerApi) || candidate.SetMuted==null || candidate.SetSolo==null || candidate.ApplySnapshot==null || candidate.SetDucking==null || candidate.ClearDucking==null) return false;
        result=candidate; return true;
    }
    private static void RequireMixer() { if (!Volatile.Read(ref s_mixerBound)) throw new NotSupportedException("TomCat.AudioMixerApiV1 is unavailable."); }
    internal static void MixerMute(AudioMixerGroup group,bool muted) { RequireMixer(); Check(s_mixerApi.SetMuted((int)group,muted?1:0),"Mixer mute"); }
    internal static void MixerSolo(AudioMixerGroup group,bool solo) { RequireMixer(); Check(s_mixerApi.SetSolo((int)group,solo?1:0),"Mixer solo"); }
    internal static void MixerSnapshot(float master,float music,float sfx,uint muted,uint solo,double seconds) { RequireMixer(); float* values=stackalloc float[]{master,music,sfx}; Check(s_mixerApi.ApplySnapshot(values,muted,solo,seconds),"Mixer snapshot"); }
    internal static void MixerDuck(AudioMixerGroup trigger,AudioMixerGroup target,float gain,double attack,double release) { RequireMixer(); Check(s_mixerApi.SetDucking((int)trigger,(int)target,gain,attack,release),"Mixer ducking"); }
    internal static void MixerClearDucking() { RequireMixer(); Check(s_mixerApi.ClearDucking(),"Clear mixer ducking"); }
}
