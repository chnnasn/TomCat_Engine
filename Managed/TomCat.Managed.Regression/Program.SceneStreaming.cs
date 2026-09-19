using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using TomCat;
using TomCat.Interop;

namespace TomCat.Managed.Regression;

internal static unsafe partial class Program
{
    private static (ulong Handle, int Index, uint Mode, int Async) s_sceneRequest;
    private static ulong s_sceneUnload;
    private static ulong s_sceneActive;
    private static NativeEntityHandleV1 s_scenePersistent;
    private static int s_scenePersistentFlag;
    private static int s_sceneAllowActivation = 1;
    private static int s_sceneForcedStatus;
    private static readonly byte[] s_sceneErrorBytes = Encoding.UTF8.GetBytes("场景读取失败");

    private static NativeSceneApiV1 CreateSceneStreamingTestApi() => new()
    {
        Version = 1,
        Size = (uint)sizeof(NativeSceneApiV1),
        RequestLoad = &StubStreamingLoad,
        RequestUnload = &StubStreamingUnload,
        SetActive = &StubStreamingSetActive,
        SetPersistent = &StubStreamingPersistent,
        GetLoadStatus = &StubStreamingStatus,
        SetAllowActivation = &StubStreamingAllow,
        CancelLoad = &StubStreamingCancel,
        GetLoadedScenes = &StubStreamingLoaded,
        GetLastError = &StubStreamingError
    };

    private static void VerifySceneStreamingCapability()
    {
        Equal(8 + 9 * IntPtr.Size, sizeof(NativeSceneApiV1), "SceneApiV1 layout");
        byte[] name = Encoding.UTF8.GetBytes("TomCat.SceneApiV1");
        delegate* unmanaged[Cdecl]<NativeUtf8View, uint, void*, uint, uint*, int> query = &QueryTestCapability;
        fixed (byte* pointer = name)
        {
            uint required = 0;
            NativeUtf8View view = new(pointer, (ulong)name.Length);
            Equal(-4, query(view, 2, null, 0, &required), "SceneApi version negotiation");
            Equal((uint)sizeof(NativeSceneApiV1), required, "SceneApi required size");
            Equal(-6, query(view, 1, null, 0, &required), "SceneApi buffer negotiation");
        }
        var probe = new SceneStreamingProbe();
        probe.__Bind(new Entity(SceneSession, 501, RuntimeGeneration), new ScriptInstanceHandle(9501));
        probe.__Create();
    }

    private sealed class SceneStreamingProbe : TomCatBehaviour
    {
        protected override void OnCreate()
        {
            Check(SceneManager.LoadSceneAsync(new SceneAsset(8201), SceneLoadMode.Additive), "async scene handle request");
            Equal((8201UL, -1, 1U, 1), s_sceneRequest, "async Scene mode and handle marshalling");
            Check(SceneManager.LoadSceneAsync(3), "async scene index request");
            Equal((0UL, 3, 0U, 1), s_sceneRequest, "async Scene index marshalling");
            Check(SceneManager.LoadScene(4, SceneLoadMode.Additive), "sync additive scene request");
            Equal((0UL, 4, 1U, 0), s_sceneRequest, "sync additive Scene marshalling");
            Check(!SceneManager.LoadSceneAsync(-1), "rejected scene request must return false");
            Throws<ArgumentOutOfRangeException>(() => SceneManager.LoadSceneAsync(0, (SceneLoadMode)99), "invalid Scene mode must be rejected");
            Equal(SceneLoadState.Ready, SceneManager.LoadState, "Scene load state");
            Equal(0.9f, SceneManager.LoadProgress, "Scene load progress");
            SceneManager.AllowSceneActivation = false;
            Check(!SceneManager.AllowSceneActivation, "Scene activation barrier round trip");
            SceneManager.AllowSceneActivation = true;
            Check(SceneManager.CancelPendingLoad(), "Scene cancellation");
            SceneAsset[] loaded = SceneManager.LoadedScenes;
            Check(loaded.Length == 2 && loaded[0].Handle == 7001 && loaded[1].Handle == 8201, "loaded Scene snapshot");
            Check(SceneManager.UnloadScene(loaded[1]), "Scene unload request");
            Equal(8201UL, s_sceneUnload, "Scene unload handle");
            Check(SceneManager.SetActiveScene(loaded[0]), "Scene active request");
            Equal(7001UL, s_sceneActive, "Scene active handle");
            Check(SceneManager.DontDestroyOnLoad(Entity), "persistent root request");
            Equal(Entity.Id, s_scenePersistent.EntityId, "persistent root entity identity");
            Equal(Entity.SceneSessionId, s_scenePersistent.SceneSessionId, "persistent root session identity");
            Equal(Entity.RuntimeGeneration, s_scenePersistent.RuntimeGeneration, "persistent root generation");
            Equal(1, s_scenePersistentFlag, "persistent root flag");
            Check(SceneManager.SetPersistent(Entity, false), "remove persistent flag");
            Equal(0, s_scenePersistentFlag, "remove persistent flag marshalling");
            Equal("场景读取失败", SceneManager.LastError, "UTF-8 Scene error round trip");
            s_sceneForcedStatus = -8;
            try
            {
                Throws<TomCatException>(() => SceneManager.LoadSceneAsync(1), "native Scene errors must not become accepted requests");
                Throws<TomCatException>(() => _ = SceneManager.LoadState, "native status errors must surface");
            }
            finally { s_sceneForcedStatus = 0; }
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingLoad(ulong handle, int index, uint mode, int asynchronous)
    {
        s_sceneRequest = (handle, index, mode, asynchronous);
        return s_sceneForcedStatus != 0 ? s_sceneForcedStatus : handle != 0 || index >= 0 ? 1 : 0;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingUnload(ulong handle) { s_sceneUnload = handle; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingSetActive(ulong handle) { s_sceneActive = handle; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingPersistent(NativeEntityHandleV1 entity, int persistent)
    { s_scenePersistent = entity; s_scenePersistentFlag = persistent; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingStatus(uint* state, float* progress, int* allow)
    { *state = (uint)SceneLoadState.Ready; *progress = 0.9f; *allow = s_sceneAllowActivation; return s_sceneForcedStatus; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingAllow(int allow) { s_sceneAllowActivation = allow; return 0; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingCancel() => 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingLoaded(ulong* handles, uint capacity, uint* required)
    {
        *required = 2;
        if (handles is null || capacity < 2) return -6;
        handles[0] = 7001; handles[1] = 8201;
        return 0;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StubStreamingError(byte* bytes, uint capacity, uint* required)
    {
        *required = (uint)s_sceneErrorBytes.Length;
        if (bytes is null || capacity < *required) return -6;
        s_sceneErrorBytes.CopyTo(new Span<byte>(bytes, (int)capacity));
        return 0;
    }
}
