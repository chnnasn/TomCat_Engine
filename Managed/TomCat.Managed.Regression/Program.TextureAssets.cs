using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using TomCat;
using TomCat.Interop;

namespace TomCat.Managed.Regression;

internal static unsafe partial class Program
{
    private static ulong s_textureHandle;
    private static uint s_textureState;
    private static int s_textureRequestCount;
    private static int s_textureQueryCount;
    private static int s_textureForcedStatus;
    private static readonly byte[] s_textureErrorBytes = Encoding.UTF8.GetBytes("纹理不存在或 GPU 发布失败");

    private static NativeTextureAssetsApiV1 CreateTextureAssetsTestApi() => new()
    {
        Version = 1,
        Size = (uint)sizeof(NativeTextureAssetsApiV1),
        RequestLoad = &StubTextureRequest,
        GetLoadState = &StubTextureState,
        GetLoadError = &StubTextureError
    };

    private static void VerifyTextureAssetsUnavailable()
    {
        Check(!TextureAssets.IsSupported, "legacy engine must not advertise texture readiness");
        AssetRef<Texture2DAsset> texture = new(6200000000000010001UL);
        Throws<NotSupportedException>(() => TextureAssets.RequestLoad(texture), "legacy texture request must be explicit");
        Throws<NotSupportedException>(() => TextureAssets.GetLoadState(texture), "legacy texture query must not pretend Ready");
        Throws<NotSupportedException>(() => TextureAssets.GetLoadError(texture), "legacy texture diagnostic must be explicit");
    }

    private static void VerifyTextureAssetsCapability()
    {
        Check(TextureAssets.IsSupported, "texture capability should be available");
        Equal(8 + 3 * IntPtr.Size, sizeof(NativeTextureAssetsApiV1), "TextureAssetsApiV1 layout");
        byte[] name = Encoding.UTF8.GetBytes("TomCat.TextureAssetsApiV1");
        delegate* unmanaged[Cdecl]<NativeUtf8View, uint, void*, uint, uint*, int> query = &QueryTestCapability;
        fixed (byte* pointer = name)
        {
            uint required = 0;
            NativeUtf8View view = new(pointer, (ulong)name.Length);
            Equal(-4, query(view, 2, null, 0, &required), "texture version negotiation");
            Equal((uint)sizeof(NativeTextureAssetsApiV1), required, "texture required size");
            Equal(-6, query(view, 1, null, 0, &required), "texture buffer negotiation");
        }
        Throws<TomCatException>(() => TextureAssets.GetLoadState(default), "texture query requires lifecycle context");
        var probe = new TextureAssetsProbe();
        probe.__Bind(new Entity(SceneSession, 502, RuntimeGeneration), new ScriptInstanceHandle(9502));
        probe.__Create();
    }

    private sealed class TextureAssetsProbe : MonoBehaviour
    {
        private void Awake()
        {
            AssetRef<Texture2DAsset> texture = new(6200000000000010001UL);
            s_textureState = (uint)TextureLoadState.Loading;
            Equal(TextureLoadState.Loading, TextureAssets.RequestLoad(texture), "texture request result");
            Equal(texture.Handle, s_textureHandle, "64-bit texture handle must remain exact");
            Equal(1, s_textureRequestCount, "one explicit texture request");
            foreach (TextureLoadState state in Enum.GetValues<TextureLoadState>())
            {
                s_textureState = (uint)state;
                Equal(state, TextureAssets.GetLoadState(texture), "texture readiness state marshalling");
            }
            Equal(1, s_textureRequestCount, "queries must not enqueue hidden requests");
            Equal(5, s_textureQueryCount, "each status query reaches the native query only");
            Equal("纹理不存在或 GPU 发布失败", TextureAssets.GetLoadError(default), "Invalid handles preserve UTF-8 diagnostics");
            Equal(0UL, s_textureHandle, "zero handle diagnostic must reach native without validation exception");
            s_textureState = (uint)TextureLoadState.Failed;
            Equal("纹理不存在或 GPU 发布失败", TextureAssets.GetLoadError(texture), "Failed diagnostic round trip");
            s_textureState = (uint)TextureLoadState.Ready;
            Equal("", TextureAssets.GetLoadError(texture), "ready texture has no error");
            int queries = s_textureQueryCount;
            Task.Run(() => Throws<TomCatException>(() => TextureAssets.GetLoadState(texture), "texture APIs reject background threads")).GetAwaiter().GetResult();
            Equal(queries, s_textureQueryCount, "wrong-thread query must not cross the native boundary");
            s_textureState = 99;
            Throws<TomCatException>(() => TextureAssets.GetLoadState(texture), "unknown texture state must fail explicitly");
            s_textureForcedStatus = -8;
            try
            {
                Throws<TomCatException>(() => TextureAssets.RequestLoad(texture), "native texture request errors surface");
                Throws<TomCatException>(() => TextureAssets.GetLoadState(texture), "native texture query errors surface");
                Throws<TomCatException>(() => TextureAssets.GetLoadError(texture), "native texture diagnostic errors surface");
            }
            finally { s_textureForcedStatus = 0; s_textureState = 0; }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubTextureRequest(ulong handle, uint* state)
    { s_textureHandle = handle; ++s_textureRequestCount; *state = s_textureState; return s_textureForcedStatus; }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubTextureState(ulong handle, uint* state)
    { s_textureHandle = handle; ++s_textureQueryCount; *state = s_textureState; return s_textureForcedStatus; }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubTextureError(ulong handle, byte* bytes, uint capacity, uint* required)
    {
        s_textureHandle = handle;
        *required = 0;
        if (s_textureForcedStatus != 0) return s_textureForcedStatus;
        if (s_textureState != (uint)TextureLoadState.Failed && s_textureState != (uint)TextureLoadState.Invalid) return 0;
        *required = (uint)s_textureErrorBytes.Length;
        if (bytes is null || capacity < *required) return -6;
        s_textureErrorBytes.CopyTo(new Span<byte>(bytes, (int)capacity));
        return 0;
    }
}
