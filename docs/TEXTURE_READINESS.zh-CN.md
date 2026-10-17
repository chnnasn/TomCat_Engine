# 纹理预加载与 GPU 就绪状态

`TomCat.TextureAssets` 让游戏在开放菜单或切换画面之前，确认所需纹理已经发布到 GPU。桌面编辑器、桌面 Player 和 Web Player 使用相同接口；不需要猜测加载帧数或等待固定毫秒数。

```csharp
AssetRef<Texture2DAsset> image = new(6200000000000010001UL);
// 在 Awake 中请求一次；只预加载实际使用、包含在项目或 Cook 闭包中的句柄。
if (!TextureAssets.IsSupported)
    throw new NotSupportedException("当前引擎未提供纹理就绪接口");
TextureAssets.RequestLoad(image);

// 后续 Update 中查询；可以保存游标，每帧只检查首个未完成资源。
TextureLoadState state = TextureAssets.GetLoadState(image);
if (state is TextureLoadState.Invalid or TextureLoadState.Failed)
    throw new InvalidOperationException($"纹理 {image.Handle}: {TextureAssets.GetLoadError(image)}");
if (state == TextureLoadState.Ready)
{
    // 所有所需纹理都 Ready 后，再启用目标界面及其输入。
}
```

| 状态 | 含义 |
| --- | --- |
| `NotRequested` | 当前缓存没有纹理，也没有待执行请求。释放资源后可再次出现。 |
| `Loading` | 请求已排队或正在处理；CPU 已解码但尚未发布到 GPU 仍属于此状态。 |
| `Ready` | 当前有效句柄对应真实、已加载的 GPU 纹理；共享洋红占位图不算就绪。 |
| `Failed` | 导入、读取、解码或 GPU 发布失败；`GetLoadError` 返回诊断。 |
| `Invalid` | 句柄为零、已失效、类型不是 Texture2D，或不属于当前包；诊断可直接读取。 |

`RequestLoad` 对已排队、就绪或失败的请求幂等，不提升队列优先级。Cooked Player 使用现有流式加载队列；编辑器使用现有同步导入与上传路径，因此第一次请求可能阻塞。失败请求在资源释放或刷新之前保持失败，调用方不应无限重试。

`GetLoadState` 不读文件、不排队、不执行 GPU 上传。`GetLoadError` 在 Failed/Invalid 之外返回空字符串；仅读取诊断时构造字符串。三个操作要求主线程的脚本生命周期回调，构造函数和后台线程不可调用。`IsSupported` 可随时读取。

就绪不是永久保证。`Release`、项目切换和包卸载会清理或使状态失效；重新挂载同一包需要重新请求。编辑器精灵子资源按请求的子资源句柄查询，与现有 `LoadTexture`、`Release` 缓存键一致。应用应在启动或明确的资源转换边界检查，不必每帧轮询所有已就绪纹理。

能力表名称为 `TomCat.TextureAssetsApiV1`，版本为 1。它通过已有 `QueryCapability` 提供，保持必需的 NativeApiV1 布局不变。旧引擎缺少能力时 `IsSupported` 为 false，其他三个方法抛出 `NotSupportedException`，不会假报就绪。纹理状态枚举固定依次为 0 至 4。

回归入口：`TomCat.Managed.Regression` 覆盖旧 ABI、状态与 64 位句柄、UTF-8 诊断及线程边界；`SpriteAssetRegression` 覆盖重复请求、CPU 准备与卸载/重挂；`RHIRegression` 在真实 OpenGL/Vulkan 环境绘制就绪纹理，验证占位图、失败、精灵子资源及释放/重载。
