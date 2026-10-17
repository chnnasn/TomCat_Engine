using System.Text;
using TomCat.Interop;

namespace TomCat;

internal static unsafe partial class NativeBridge
{
    private static NativeTextureAssetsApiV1 s_textureAssetsApi;
    private static bool s_textureAssetsBound;
    internal static bool SupportsTextureAssets => Volatile.Read(ref s_textureAssetsBound);

    private static bool TryReadTextureAssetsCapability(NativeApiV1* api, out NativeTextureAssetsApiV1 textures)
    {
        textures = default;
        if (api->Size < (uint)sizeof(NativeApiV2)) return false;
        var envelope = (NativeApiV2*)api;
        if (envelope->QueryCapability == null) return false;
        byte[] name = Encoding.UTF8.GetBytes("TomCat.TextureAssetsApiV1");
        NativeTextureAssetsApiV1 candidate = default;
        fixed (byte* pointer = name)
        {
            uint required = 0;
            int status = envelope->QueryCapability(new NativeUtf8View(pointer, (ulong)name.Length),
                1, &candidate, (uint)sizeof(NativeTextureAssetsApiV1), &required);
            if (status != 0 || required > (uint)sizeof(NativeTextureAssetsApiV1)) return false;
        }
        textures = candidate;
        return textures.Version == 1 && textures.Size >= (uint)sizeof(NativeTextureAssetsApiV1)
            && textures.RequestLoad != null && textures.GetLoadState != null && textures.GetLoadError != null;
    }

    private static void RequireTextureAssets()
    {
        if (!SupportsTextureAssets)
            throw new NotSupportedException("This engine does not provide TomCat.TextureAssetsApiV1.");
        EnsureMainThread();
    }

    private static TextureLoadState ReadTextureState(ulong handle, bool request)
    {
        RequireTextureAssets();
        uint state = 0;
        int status = request ? s_textureAssetsApi.RequestLoad(handle, &state)
            : s_textureAssetsApi.GetLoadState(handle, &state);
        Check(status, request ? "TextureAssets.RequestLoad" : "TextureAssets.GetLoadState");
        if (state > (uint)TextureLoadState.Invalid)
            throw new TomCatException("The engine returned an unknown texture load state.");
        return (TextureLoadState)state;
    }

    internal static TextureLoadState RequestTextureLoad(ulong handle) => ReadTextureState(handle, true);
    internal static TextureLoadState GetTextureLoadState(ulong handle) => ReadTextureState(handle, false);

    internal static string GetTextureLoadError(ulong handle)
    {
        RequireTextureAssets();
        uint required = 0;
        int status = s_textureAssetsApi.GetLoadError(handle, null, 0, &required);
        if (status != NativeBufferTooSmall) Check(status, "TextureAssets.GetLoadError");
        if (required == 0) return "";
        byte[] buffer = new byte[checked((int)required)];
        fixed (byte* pointer = buffer)
            Check(s_textureAssetsApi.GetLoadError(handle, pointer, (uint)buffer.Length, &required), "TextureAssets.GetLoadError");
        if (required > buffer.Length) throw new TomCatException("Invalid texture error buffer size.");
        return Encoding.UTF8.GetString(buffer, 0, checked((int)required));
    }
}
