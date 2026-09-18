# TomCat 托管脚本 V1

[English](README.md) | 简体中文 · 核对日期：2026-09-18 · [文档索引](../docs/README.md)

V1 表示脚本功能范围，不代表所有协议版本相同：当前 Native ABI 为 v1、Managed ABI 为 v2，
ScriptManifest 为 v1。桌面托管使用 .NET 10；实验性 Web 目标会拒绝 C# 负载。

## 模块与构建

本目录包含可独立构建的 .NET 10 脚本系统：

- `TomCat.Managed`：面向用户的 `using TomCat;` API，以及可直接跨原生边界传递的 ABI 数据定义。
- `TomCat.ScriptHost`：常驻默认 ALC（程序集加载上下文）的宿主，负责可回收项目域、实例生命周期、字段恢复、故障隔离和原生导出。
- `TomCat.ScriptGenerator`：编译期脚本识别、诊断和嵌入清单生成器，引用 .NET SDK 自带 Roslyn，无 NuGet 依赖。
- `TomCat.Managed.Regression`：不依赖测试框架的可执行回归套件。

从仓库根目录使用 .NET 10 SDK 构建和测试：

```powershell
dotnet build Managed/TomCat.Managed.slnx -c Release
dotnet run --project Managed/TomCat.Managed.Regression/TomCat.Managed.Regression.csproj -c Release
```

`TomCat.ScriptHost.runtimeconfig.json`、`TomCat.ScriptHost.dll` 与 `TomCat.Managed.dll`
输出到 `Managed/TomCat.ScriptHost/bin/<Configuration>/net10.0/`。

## 项目输入与生成清单

生成的 `Assembly-CSharp.csproj` 正常引用 `TomCat.Managed`，把 `TomCat.ScriptGenerator`
作为分析器，并且只提供一份 `ScriptAssets.json` 作为 `AdditionalFiles`：

```json
{
  "version": 1,
  "assets": {
    "Assets/Scripts/PlayerController.cs": 8172638172638
  }
}
```

生成器嵌入公开属性 `TomCat.Generated.ScriptManifest.Json`。Manifest V1 记录资产 Handle、
完整类型名、执行顺序、禁止多实例标记、生命周期位掩码及可序列化字段元数据。
字段 ID 由脚本资产 Handle 与字段身份确定性生成，格式为小写 128 位十六进制字符串。

宿主接收 UTF-8 序列化字段，结构与大小写必须如下：

```json
{
  "attachments": [
    {
      "attachmentId": 123,
      "fields": [
        {
          "fieldId": "14a71699a9c84a6c9e21872758c1a401",
          "name": "_moveForce",
          "type": "Float",
          "value": 12.0
        }
      ]
    }
  ]
}
```

类型 token 必须为 `Bool`、`Int32`、`Int64`、`Float`、`Double`、`String`、`Vector2`、
`Vector3`、`Vector4`、`Color`、`Enum`、`Entity` 或 `AssetRef`。
向量与颜色使用数值数组；Entity 与 AssetRef 为无符号 64 位 JSON 数值，枚举为有符号 64 位 JSON 数值。
未知或类型不匹配的字段保留为原生端孤立数据，不应用到实例。

`AssetRef<T>` 接受内置标记 `Texture2DAsset`、`ShaderAsset`、`AudioAsset`、`FontAsset`、
`MeshAsset`、`MaterialAsset`、`SceneAsset` 与 `PrefabAsset`；其他标记产生编译错误 `TCG010`。
`IsValid` 和 Cook 均检查真实资产类型。标记可用只表示支持该资产身份，不代表每种类型已有运行时加载器。

## 原生启动约定

用 `TomCat.ScriptHost.runtimeconfig.json` 初始化一次 CoreCLR，再调用 hostfxr 的
`load_assembly_and_get_function_pointer`，参数为：

```text
assembly path: TomCat.ScriptHost.dll
type name:     TomCat.ScriptHost.EntryPoint, TomCat.ScriptHost
method name:   GetManagedApi
delegate type: UNMANAGEDCALLERSONLY_METHOD
```

入口签名：

```csharp
[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
public static int GetManagedApi(NativeApiV1* nativeApi, ManagedApiV1* managedApi)
```

所有函数指针使用 Cdecl，并返回 `int32` 状态码。两张表都以 `uint32 Version; uint32 Size;`
开头。原生输入表必须传入版本 1、不小于 V1 的表大小和全部 V1 回调。
启动时先完整校验，再发布绑定；缺少回调返回 unavailable（-8），不会覆盖先前有效绑定。

### ManagedApiV1 布局（Managed ABI v2）

类型沿用历史名称，因为 v2 在稳定的 v1 前缀后追加回调。`Version` 和 `Size` 后严格依次为：

```text
int CreateDomain(int32 domainKind, uint64* domainId)
int LoadProjectAssembly(uint64 domainId, NativeByteView assembly, NativeByteView pdb)
int ReadScriptMetadata(uint64 domainId, MetadataReceiver receiver, uint64 receiverToken)
int CreateSceneRuntime(uint64 domainId, uint64 sceneSessionId,
                       uint64 runtimeGeneration, uint64* sceneRuntimeId)
int InstantiateAll(uint64 sceneRuntimeId, NativeScriptAttachmentV1* items, uint32 count)
int ApplySerializedFields(uint64 sceneRuntimeId, NativeByteView fieldsJson)
int InvokeCreateAll(uint64 sceneRuntimeId)
int SetEnabled(uint64 sceneRuntimeId, uint64 attachmentId, int32 enabled)
int UpdateAll(uint64 sceneRuntimeId, float deltaTime)
int FixedUpdateAll(uint64 sceneRuntimeId, float fixedDeltaTime)
int DispatchPhysicsEvents(uint64 sceneRuntimeId, NativePhysicsEventV1* events, uint32 count)
int DestroyAll(uint64 sceneRuntimeId)
int BeginUnloadDomain(uint64 domainId)
int PollUnload(uint64 domainId, int32* unloaded)
int DestroyAttachments(uint64 sceneRuntimeId, uint64* attachmentIds, uint32 count)
int InstantiateAttachments(uint64 sceneRuntimeId, NativeScriptAttachmentV1* items,
                           uint32 count, NativeByteView fieldsJson)
int ResolveDeferredCommandBatch(uint64 sceneRuntimeId, int32 committed)
```

`ResolveDeferredCommandBatch` 在原生命令批次完成暂存校验和实际重放后确认结果。
传 0 丢弃托管侧预计的生命周期状态；传 1 在原生 Scene 提交成功后发布该状态。

`MetadataReceiver` 为 `int(NativeByteView json, uint64 receiverToken)`。
token 对托管代码不透明，只解析为生命周期短且线程安全的原生接收上下文。
域类型为 Metadata = 0、Play = 1。

| 状态码 | 含义 |
| --- | --- |
| 0 | 成功 |
| -1 | 参数无效 |
| -2 | 状态无效 |
| -3 | 未找到 |
| -4 | 版本不匹配 |
| -5 | 已隔离的托管异常 |
| -6 | 缓冲区太小 |
| -7 | 线程错误 |
| -8 | 功能不可用 |

### NativeApiV1 顺序与扩展能力

`Version` 和 `Size` 后的回调顺序必须为：

```text
Log, EmitDiagnostic, IsMainThread,
EntityIsAlive, EntityGetName, EntitySetName, EntityGetTag, EntitySetTag,
EntityGetLayer, EntitySetLayer, DestroyEntityDeferred,
HasComponent, AddComponentDeferred, RemoveComponentDeferred,
TransformGetPosition, TransformSetPosition,
TransformGetRotationEuler, TransformSetRotationEuler,
TransformGetScale, TransformSetScale, TransformGetWorldMatrix,
InputIsKeyHeld, InputWasKeyPressed, InputWasKeyReleased,
InputGetMousePosition, InputGetMouseDelta, InputGetModifiers,
RigidbodyGetLinearVelocity, RigidbodySetLinearVelocity,
RigidbodyApplyForce, RigidbodyApplyLinearImpulse,
PhysicsRaycast, PhysicsQueryAabb,
AssetIsValid, AssetGetType,
BehaviourGetEnabled, BehaviourSetEnabledDeferred, BehaviourRemoveDeferred,
SceneGetActiveHandle, SceneGetActiveBuildIndex,
SceneRequestLoadHandle, SceneRequestLoadIndex, SceneRequestReload,
PrefabInstantiateDeferred
```

`NativeApiV2` 是根据大小检测的扩展封装，稳定的 `NativeApiV1` 前缀仍用版本 1，
后接 `QueryCapability`。可选 V1 能力表覆盖 Input、InputEvents、ApplicationPaths、Gameplay、
Audio、AudioSpatial、RuntimeUI、Component、ComponentString、ComponentSchema、DeferredCommands
和 DeferredCallbackTransactions。确切名称与校验规则见
[NativeBridge.cs](TomCat.Managed/NativeBridge.cs)，不能仅从历史结构体名称推断能力。

### 延迟回调事务

当前原生宿主提供 `TomCat.DeferredCommandsApiV1` 与
`TomCat.DeferredCallbackTransactionsApiV1`。后者在 `Version` 和 `Size` 后的 Cdecl 布局为：

```text
int BeginCallback(NativeEntityHandleV1 context, uint64* token)
int CompleteCallback(uint64 token)
```

每个顶层生命周期、更新、物理和显示帧 InputAction 用户回调各开启一个事务。
命令对同一回调后续读取可见，在临时场景中校验后整体提交或整体丢弃。
用户异常被隔离或变更校验失败被捕获后，`AbortBatch` 将当前事务标记为回滚。
`CompleteCallback` 封存本次命令后缀，原生按 FIFO 顺序处理。
每个已完成的回调（包括空回调）都恰好调用一次 `ResolveDeferredCommandBatch`，
保持托管预计状态与原生状态一致。

事务覆盖延迟命令缓冲区中的实体创建/销毁和层级状态、组件与行为的增删/启用状态、
内置及注册组件的属性设置、Prefab 实例化。物理力/速度、音频播放或混音、Animator 播放等
即时运行控制同步生效，不受 `AbortBatch` 回滚。

显示帧 InputAction 多播中的每个顶层订阅者分别拥有事务，按订阅顺序执行。
一个用户异常只中止当前订阅者，后续订阅者仍执行，之后禁用该 Map 并发出一条诊断。
Disable → Canceled 等同步嵌套事件共用外层订阅者事务。

提交引发的嵌套生命周期回调追加到同一非递归 FIFO 队列。
Begin、Complete 或托管确认发生协议错误会终止当前 Play Scene，避免带着未闭合事务或
部分同步状态继续运行。不提供回调事务能力的旧宿主仍使用场景阶段级批处理兼容行为。

### 数据布局与实例顺序

权威字段签名和可直接跨边界传递的布局位于
[InteropTypes.cs](TomCat.Managed/InteropTypes.cs)，Managed 表位于
[ManagedApiV1.cs](TomCat.ScriptHost/ManagedApiV1.cs)。

- `NativeByteView` / `NativeUtf8View`：指针后接 `uint64` 字节长度。
- `NativeEntityHandleV1`：`{ sceneSessionId, entityId, runtimeGeneration }`。
- `NativeScriptAttachmentV1`：`{ Entity, attachmentId, scriptAsset, int32 enabled, int32 reserved }`。
- `NativePhysicsEventV1`：`{ uint32 kind, uint32 reserved, Entity A, Entity B }`；kind 0–3 分别为碰撞进入、碰撞退出、触发进入、触发退出。

Attachment ID 必须随机生成、非零，并在场景运行时中唯一；ABI 直接将它作为
`ScriptInstanceHandle.Value`，重复值会被拒绝。原生先按场景实体顺序、再按挂载顺序提供附件。
宿主按 `DefaultExecutionOrder` 稳定排序，相同值保持输入顺序，销毁时逆序执行。

Prefab 创建的脚本附件只在当前托管回调返回后通过 `InstantiateAttachments` 接入。
新批次先恢复序列化字段，再调用 `OnCreate` 与 `OnEnable`；已有实例不会重复接收这些回调。

## 场景与快照 Prefab API

`SceneManager` 提供活动 `SceneAsset` 和构建索引，可按场景 Handle/索引请求同步替换或重新加载。
生命周期或物理回调中的请求由原生运行时在帧末安全点提交。

脚本可序列化强类型 `SceneAsset` 与 `PrefabAsset` 字段。
行为通过 `Instantiate(prefab, worldPosition, optionalParent)` 排队实例化快照 Prefab。
实例获得新的 Scene UUID 与 AttachmentID；在托管生命周期派发可见前，完成层级、
`DistanceJoint2D` 和 C# `Entity` 字段的引用重映射。

## 加载与卸载所有权

`TomCat.ScriptHost` 和 `TomCat.Managed` 常驻默认 ALC。
每个项目 DLL/PDB 从字节载入可回收 ALC；自定义加载器始终把 `TomCat.Managed` 解析到默认
ALC 中的副本，以保持 `TomCatBehaviour` 的类型身份。

`BeginUnloadDomain` 停止派发、销毁实例、清理反射缓存，调用 `AssemblyLoadContext.Unload`，
仅保留弱引用。原生必须轮询 `PollUnload`；不能卸载的域必须报告，不能静默累积。
