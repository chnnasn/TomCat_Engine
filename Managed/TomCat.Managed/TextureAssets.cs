namespace TomCat;

/// <summary>GPU readiness of a texture in the current project or mounted package.</summary>
public enum TextureLoadState : uint { NotRequested = 0, Loading = 1, Ready = 2, Failed = 3, Invalid = 4 }

/// <summary>Explicit texture preloading and side-effect-free readiness queries.</summary>
public static class TextureAssets
{
    public static bool IsSupported => NativeBridge.SupportsTextureAssets;

    /// <summary>
    /// Requests a texture once, without changing queue priority. Cooked players
    /// stream asynchronously; the editor uses its existing synchronous loader.
    /// Failed requests remain failed until resources are released/refreshed.
    /// </summary>
    public static TextureLoadState RequestLoad(AssetRef<Texture2DAsset> texture) =>
        NativeBridge.RequestTextureLoad(texture.Handle);

    /// <summary>Does not read files, queue work, or publish GPU resources.</summary>
    public static TextureLoadState GetLoadState(AssetRef<Texture2DAsset> texture) =>
        NativeBridge.GetTextureLoadState(texture.Handle);

    /// <summary>Returns a diagnostic for Failed/Invalid; otherwise an empty string.</summary>
    public static string GetLoadError(AssetRef<Texture2DAsset> texture) =>
        NativeBridge.GetTextureLoadError(texture.Handle);
}
