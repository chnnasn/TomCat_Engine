using System.Text;
using TomCat.Interop;

namespace TomCat;

internal static unsafe partial class NativeBridge
{
    private static NativeSceneApiV1 s_sceneApi;
    private static bool s_sceneBound;

    private static bool TryReadSceneCapability(NativeApiV1* api, out NativeSceneApiV1 scenes)
    {
        scenes = default;
        if (api->Size < (uint)sizeof(NativeApiV2)) return false;
        NativeApiV2* envelope = (NativeApiV2*)api;
        if (envelope->QueryCapability == null) return false;
        byte[] name = Encoding.UTF8.GetBytes("TomCat.SceneApiV1");
        NativeSceneApiV1 candidate = default;
        fixed (byte* pointer = name)
        {
            uint required = 0;
            int status = envelope->QueryCapability(new NativeUtf8View(pointer, (ulong)name.Length),
                1, &candidate, (uint)sizeof(NativeSceneApiV1), &required);
            if (status != 0 || required > (uint)sizeof(NativeSceneApiV1)) return false;
        }
        scenes = candidate;
        return scenes.Version == 1 && scenes.Size >= (uint)sizeof(NativeSceneApiV1)
            && scenes.RequestLoad != null && scenes.RequestUnload != null
            && scenes.SetActive != null && scenes.SetPersistent != null
            && scenes.GetLoadStatus != null && scenes.SetAllowActivation != null
            && scenes.CancelLoad != null && scenes.GetLoadedScenes != null
            && scenes.GetLastError != null;
    }

    private static void RequireSceneApi()
    {
        EnsureMainThread();
        Require(Volatile.Read(ref s_sceneBound), "SceneManager streaming capability");
    }

    internal static bool RequestSceneLoad(ulong handle, int index, SceneLoadMode mode, bool asynchronous)
    {
        RequireSceneApi();
        if (mode is not SceneLoadMode.Single and not SceneLoadMode.Additive)
            throw new ArgumentOutOfRangeException(nameof(mode));
        return ReadBoolean(s_sceneApi.RequestLoad(handle, index, (uint)mode, asynchronous ? 1 : 0), "SceneManager.LoadScene");
    }

    internal static bool UnloadScene(ulong handle)
    {
        RequireSceneApi();
        return ReadBoolean(s_sceneApi.RequestUnload(handle), "SceneManager.UnloadScene");
    }

    internal static bool SetActiveScene(ulong handle)
    {
        RequireSceneApi();
        return ReadBoolean(s_sceneApi.SetActive(handle), "SceneManager.SetActiveScene");
    }

    internal static bool SetEntityPersistent(Entity entity, bool persistent)
    {
        ArgumentNullException.ThrowIfNull(entity);
        RequireSceneApi();
        return ReadBoolean(s_sceneApi.SetPersistent(ToNative(entity), persistent ? 1 : 0), "SceneManager.SetPersistent");
    }

    internal static (SceneLoadState State, float Progress, bool AllowActivation) GetSceneLoadStatus()
    {
        RequireSceneApi();
        uint state = 0;
        float progress = 0;
        int allow = 0;
        Check(s_sceneApi.GetLoadStatus(&state, &progress, &allow), "SceneManager.LoadState");
        return ((SceneLoadState)state, progress, allow != 0);
    }

    internal static void SetAllowSceneActivation(bool allow)
    {
        RequireSceneApi();
        Check(s_sceneApi.SetAllowActivation(allow ? 1 : 0), "SceneManager.AllowSceneActivation");
    }

    internal static bool CancelSceneLoad()
    {
        RequireSceneApi();
        return ReadBoolean(s_sceneApi.CancelLoad(), "SceneManager.CancelPendingLoad");
    }

    internal static SceneAsset[] GetLoadedScenes()
    {
        RequireSceneApi();
        uint count = 0;
        int status = s_sceneApi.GetLoadedScenes(null, 0, &count);
        if (status != NativeBufferTooSmall) Check(status, "SceneManager.LoadedScenes");
        if (count == 0) return [];
        ulong[] handles = new ulong[count];
        fixed (ulong* pointer = handles)
            Check(s_sceneApi.GetLoadedScenes(pointer, count, &count), "SceneManager.LoadedScenes");
        return Array.ConvertAll(handles, static handle => new SceneAsset(handle));
    }

    internal static string GetSceneLoadError()
    {
        RequireSceneApi();
        uint count = 0;
        int status = s_sceneApi.GetLastError(null, 0, &count);
        if (status != NativeBufferTooSmall) Check(status, "SceneManager.LastError");
        if (count == 0) return string.Empty;
        byte[] bytes = new byte[count];
        fixed (byte* pointer = bytes)
            Check(s_sceneApi.GetLastError(pointer, count, &count), "SceneManager.LastError");
        return Encoding.UTF8.GetString(bytes);
    }
}
