namespace TomCat;

/// <summary>
/// Synchronous, single-scene runtime transitions. Load requests made from a
/// lifecycle callback are validated immediately and committed at frame end.
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

    public static bool ReloadActiveScene() =>
        NativeBridge.RequestReloadScene();
}
