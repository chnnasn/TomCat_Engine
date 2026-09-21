namespace TomCat;

public enum SceneLoadMode : uint { Single, Additive }
public enum SceneLoadState : uint { Idle, Reading, Ready, Completed, Failed, Cancelled }

/// <summary>
/// Frame-end scene transitions. Additive assets share the runtime world, physics
/// and scripting session. Async reads keep the current world running; schema,
/// resource and script activation runs on the main thread.
/// </summary>
public static class SceneManager
{
    public static SceneAsset ActiveScene =>
        new(NativeBridge.GetActiveSceneHandle());

    public static int ActiveBuildIndex =>
        NativeBridge.GetActiveSceneBuildIndex();

    public static bool LoadScene(SceneAsset scene) =>
        NativeBridge.RequestLoadScene(scene.Handle);

    public static bool LoadScene(int buildIndex) =>
        NativeBridge.RequestLoadScene(buildIndex);

    public static bool LoadScene(SceneAsset scene, SceneLoadMode mode) =>
        NativeBridge.RequestSceneLoad(scene.Handle, -1, mode, false);

    public static bool LoadScene(int buildIndex, SceneLoadMode mode) =>
        NativeBridge.RequestSceneLoad(0, buildIndex, mode, false);

    public static bool LoadSceneAsync(SceneAsset scene, SceneLoadMode mode = SceneLoadMode.Single) =>
        NativeBridge.RequestSceneLoad(scene.Handle, -1, mode, true);

    public static bool LoadSceneAsync(int buildIndex, SceneLoadMode mode = SceneLoadMode.Single) =>
        NativeBridge.RequestSceneLoad(0, buildIndex, mode, true);

    /// <summary>Poll after accepting a load request. Progress reaches 0.9 before activation and 1 on success.</summary>
    public static SceneLoadState LoadState => NativeBridge.GetSceneLoadStatus().State;
    public static float LoadProgress => NativeBridge.GetSceneLoadStatus().Progress;
    public static string LastError => NativeBridge.GetSceneLoadError();
    public static SceneAsset[] LoadedScenes => NativeBridge.GetLoadedScenes();

    /// <summary>Set false to hold a loaded scene at its activation barrier; set true to commit at frame end.</summary>
    public static bool AllowSceneActivation
    {
        get => NativeBridge.GetSceneLoadStatus().AllowActivation;
        set => NativeBridge.SetAllowSceneActivation(value);
    }

    public static bool CancelPendingLoad() => NativeBridge.CancelSceneLoad();

    /// <summary>Queues an unload. The final loaded scene cannot be unloaded; load a replacement first.</summary>
    public static bool UnloadScene(SceneAsset scene) => NativeBridge.UnloadScene(scene.Handle);
    public static bool SetActiveScene(SceneAsset scene) => NativeBridge.SetActiveScene(scene.Handle);

    /// <summary>Preserves a root and its descendants, including live managed state, across loads and unloads.</summary>
    public static bool DontDestroyOnLoad(Entity root) => NativeBridge.SetEntityPersistent(root, true);
    public static bool SetPersistent(Entity root, bool persistent) => NativeBridge.SetEntityPersistent(root, persistent);

    public static bool ReloadActiveScene() =>
        NativeBridge.RequestReloadScene();
}
