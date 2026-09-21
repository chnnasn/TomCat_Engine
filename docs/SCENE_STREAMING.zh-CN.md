# 场景加载、叠加与持久对象

`SceneManager` 在 Editor Play 和桌面 Player 使用同一套帧末提交。所有场景必须列入项目 Build Settings 的已启用场景列表；`int` 参数是过滤禁用项后的构建索引。

## C# 用法

```csharp
// 原有同步接口仍可用：调用时完成准备，帧末替换。
SceneManager.LoadScene(1);

// 异步读取地图分区；已有场景继续更新和渲染。
bool accepted = SceneManager.LoadSceneAsync(zoneAsset, SceneLoadMode.Additive);

// 可以在之后的 Update 中观察 LoadedScenes，再设置默认新建对象所属场景。
SceneManager.SetActiveScene(zoneAsset);

// 卸载请求同样在帧末生效。
SceneManager.UnloadScene(zoneAsset);

// 从管理器根对象的 OnCreate 调用。根及其当前子树跨 Single/Unload 保留。
SceneManager.DontDestroyOnLoad(Entity);

// 取消持久标记后，子树重新归属当前活动场景。
SceneManager.SetPersistent(Entity, false);
```

加载和卸载返回 `true` 表示请求已接受，不表示已完成。失败原因可读取 `SceneManager.LastError`。`LoadedScenes` 返回当前已提交的场景资产快照；`ActiveScene` 与 `ActiveBuildIndex` 返回默认创建目标。叠加不自动切换活动场景。

一个典型 Loading 控制器先设置 `AllowSceneActivation = false`，再调用 `LoadSceneAsync(target)`。之后每帧读取 `LoadState` 和 `LoadProgress`，在 `Ready` 时完成过场动画，再设置 `AllowSceneActivation = true`。加载完成前原场景持续运行；需要保留 Loading 控制器本身时，应先标记其根对象为持久。

| 状态 | 含义 |
| --- | --- |
| `Idle` | 当前没有加载操作 |
| `Reading` | 后台读取场景字节；进度按读取字节更新 |
| `Ready` | 场景完成反序列化与资源准备，进度为 0.9，等待允许激活 |
| `Completed` | 帧末提交成功，进度为 1 |
| `Failed` | 读取、校验或启动失败；检查 `LastError` |
| `Cancelled` | `CancelPendingLoad()` 已取消尚未激活的加载 |

`AllowSceneActivation` 是管理器级别的开关，影响同步和异步请求；用完需恢复为 `true`。异步请求要求前一次操作已结束或取消。同步请求保留原有覆盖已准备请求的行为。停止 Play 会取消后台结果；迟到的任务只能释放自己的字节缓冲，不能重新激活场景。

## 生命周期与引用

叠加场景按资产记录实体归属，合入同一个 ECS、物理世界、音频与脚本会话，因此分区实体可相互碰撞，脚本可引用同一世界的实体。输入焦点、相机和音频监听器也共享；有多台相机时由游戏逻辑通过 `Camera.Primary` 明确选择。相同场景资产不能重复叠加。

合入时重新分配实体和脚本附件标识，并重映射场景内部引用。卸载仅销毁该资产所属的非持久实体，执行已有脚本、物理和音频清理。属于其他场景但挂在被卸载父对象下的子对象会先脱离父级，并保留世界变换。对被销毁实体保留的 C# 引用，其 `IsAlive` 返回 `false`。

持久对象保留原生实体、脚本实例和运行中状态，不通过复制或重新执行 `OnCreate` 实现。仅允许标记根对象；其子树跟随保留。最后一个已加载场景不允许单独卸载，应加载替代场景或停止运行。根对象动态创建后归属活动场景；带父对象的新实体继承父对象归属。

普通 Single 替换在启动失败时尝试恢复原场景；该恢复会重建非持久脚本会话。已有持久对象时，Single 先在现有会话中初始化新实体，成功后再清理旧场景内容。叠加启动失败会回滚新实体批次，不重启已有脚本。

新实体回滚会清理启动回调额外创建的对象并恢复场景归属与持久标记，但不对原生回调任意修改的已有对象状态进行快照恢复；回调显式调用 `Stop` 后仍保持停止。

## 当前边界与验证

后台负责文件读取和包内容摘要验证；场景模式校验、Prefab 关联更新、资源解析、GPU 操作及脚本启动仍在主线程。大场景的激活阶段仍可能造成长帧，尚未提供逐帧预算式 ECS/GPU 激活。进度反映字节读取和明确阶段，不估算剩余秒数。

场景卸载会清理实体拥有的运行时状态；资产缓存仍遵循 `AssetManager` 的全局缓存策略，卸载场景不会强制清空共享资源。此实现不提供多个同资产运行实例、独立物理世界或自动大地图分区调度。

`Tests/PhysicsRegression/src/SceneManagerRegression.h` 覆盖帧末替换与回滚、叠加、重复加载拒绝、显式卸载、跨场景子对象保留、实体引用重映射、持久脚本句柄保持、异步激活屏障、取消和同资产重载。托管桥通过可选 `TomCat.SceneApiV1` 能力表提供新接口，保留原 V1 ABI。
