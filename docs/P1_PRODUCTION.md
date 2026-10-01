# P1 生产能力与验收边界

## 大场景加载

`SceneManager::RequestLoadSceneAsync` 在资产任务线程读取、校验摘要并解析 YAML；组件恢复和层级验证通过 `FrameTask` 在主线程分帧执行。`SetActivationBudget({64, 2.0})` 分别设置每帧工作单元上限和毫秒软预算。解码完成之前不向运行场景发布部分实体。

单场景替换的启动过程也按阶段执行：物理构建、音频准备、动画/UI 准备、脚本附件收集、实例化、字段恢复和生命周期回调。启动期间暂停新场景的游戏更新，完成后恢复。`GetActivationStatistics()` 返回帧数、工作单元数、峰值和超预算帧数。

状态新增 `Decoding=6`、`Activating=7`，保留已有数值。`allowSceneActivation=false` 停在 Ready；提交激活之后不能取消为旧场景，必须 Stop。启动失败尝试重启旧场景。

`AssetManager::PumpTexturePublishes(count, bytes, milliseconds)` 默认每帧 2 次、32 MiB、2 ms。第一张超过字节预算的纹理仍会上传，避免队列永远阻塞；统计中的 `OversizedUploads` 和 `PublishOverruns` 会记录。驱动单次上传不是可抢占操作。

**边界：这是协作式软预算。** 单次组件解码、Prefab 刷新、层级同步、物理世界构建、脚本批次和用户回调仍可能超预算；加法加载及持久实体合并仍走原子事务。它不能保证任何场景都在 2 ms 内激活。回归包含 5000 实体分帧和取消测试，会输出实际峰值，不能用单机数字代替目标设备验收。

## UI 与国际化

Windows 使用系统 ICU 进行字素簇边界、Unicode 双向顺序、CLDR 基数复数和数字格式化；HarfBuzz 10.4.0 负责 OpenType GSUB/GPOS 塑形，包括阿拉伯连写、印度文字重排、连字、字偶距和组合附标定位。输入框按字素簇移动/删除/限制长度；排版先按逻辑字素换行，再对每行排序，并保存逻辑偏移到视觉光标的位置。字体仍需覆盖对应文字。

Windows IMM 预编辑文本与提交文本分离；运行时绘制预编辑和下划线，并按 viewport 与 framebuffer 缩放定位候选窗。提交仍经 GLFW 字符回调进入输入框。失焦会清除组合状态。真实中文/日文输入法、多显示器 DPI 和触摸键盘交互仍需人工验收；自动测试验证组合状态、提交分离和失焦。

塑形按字体、文字系统和方向分段，并在换行后重新塑形。字体图集保留字体表，后台构建时收集 GSUB 替换字形，避免上下文形式在运行时缺图。字形簇保留 UTF-8 逻辑偏移，光标仍按字素移动；连字内部光标按字素均分，尚未读取 GDEF caret 表。字体回退要求一个字素簇由同一字体覆盖。

边界：当前为水平、灰度轮廓字形，语言标签默认 `und`，尚无逐段语言/特性编辑、彩色 emoji 或竖排。混合方向选择高亮仍需进一步覆盖。非 Windows 的双向算法保留简化回退，不能把 HarfBuzz 编译通过视为完整 Web ICU/IME 支持。

### 虚拟列表

给 `UIScrollView` 设置 `Virtualized=true`、`VirtualItemCount`、`VirtualItemHeight`、`VirtualOverscan`。只建立足够覆盖视口和预取行的子实体池。布局根据 Offset 重排这些子实体，不为每条数据建立实体。

C# 用 `FirstVisibleIndex` 或 `TryGetItemIndex(poolSlot, out itemIndex)` 将数据绑定到池里的固定子实体；数据索引变化时更新 UIText、图片及点击回调的数据。当前为固定行高、纵向列表，尚无可变行高测量。

### 本地化参数

`UILocalizedText.Parameters` 是 YAML/JSON 映射，最多 64 个参数、64 KiB。`count` 选择 `key.zero/one/two/few/many/other`；缺分类使用 `.other`，再回退基础 key 和 FallbackLocale。`{name}` 插值字符串，`{value:number}` 根据实际使用的 locale 格式化数字；无效参数回退作者文本。

```yaml
en:
  coins.one: "{name} has {count:number} coin"
  coins.other: "{name} has {count:number} coins"
ru:
  coins.one: "{name}: {count:number} монета"
  coins.few: "{name}: {count:number} монеты"
  coins.many: "{name}: {count:number} монет"
  coins.other: "{name}: {count:number} монеты"
```

设置 Key=`coins`，Parameters=`{name: Player, count: 2}`。

## 2D 材质、光照与后处理

精灵、瓦片和粒子的既有 lit 路径改为片元计算。每次场景提交最多 32 个点光源、64 条阴影边。`SpriteRenderer.NormalMap` 指向切线空间、OpenGL +Y 法线图；使用 Linear 色彩空间，可选 BC5。法线图的 UV/图集布局必须与颜色图一致。

`CastShadows=true` 将精灵单位矩形四边作为不透明遮挡体；它不依据透明像素生成轮廓，也不是软阴影。自身遮挡边不影响自身着色。应优先把有限的阴影边预算用于主要墙体。

可复用 `.tcmat`：

```yaml
SchemaVersion: 1
Shader: BuiltinSprite2D
Textures:
  NormalMap: 123456789 # 替换为真实纹理 AssetHandle
Parameters:
  Tint: {Type: Float4, Value: [1, 0.9, 0.8, 1]}
  Lit: {Type: Bool, Value: true}
```

将材质句柄赋给 `SpriteRenderer.Material`。Tint 与精灵颜色相乘；精灵显式 NormalMap 优先于材质。BuiltinSprite2D 是引擎内置程序标识，不要求用户复制 shader 文件；正常材质仍保留非零自定义 Shader 句柄协议。材质与纹理引用会进入 cook 依赖图。

相机新增 `Exposure`（-10..10 档）、`Saturation`（0..2）、`Vignette`（0..1）。效果在场景色彩合成后、屏幕 UI 之前运行，不改变实体拾取附件。保存相机为 Prefab 可复用调色预设。默认值跳过整个后处理 pass。当前是 LDR 色彩处理，不包含 HDR/Bloom 或节点材质编辑器。

## 音频与纹理生产

支持 WAV、OGG/Vorbis（`.ogg` / `.oga`），Vorbis 解码器为 vendored stb_vorbis 1.22。导入后统一生成 PCM16 WAV 产物，因此现有 Player 流式播放继续使用 PCM 分块读取；包内不是实时 Vorbis 流式解压。限制为单声道/双声道，编码源 <=256 MiB，解码 PCM <=512 MiB；损坏输入明确失败。

纹理新增二进制 PNM/PPM/PGM 源格式。compression 可选 Auto、RGBA8、BC1、BC3、BC5、ASTC4x4（别名 ASTC）、ETC2RGBA8（别名 ETC2）；BC1 要求不透明，BC5 要求 Linear。ASTC 使用 Arm astcenc 5.0.0，ETC2 使用 Google etc2comp，离线生成真实块压缩和完整 mip 链；支持 Linear/sRGB。

Auto 按 cook 平台选择：`windows-x64` → BC3，`android`/`android-arm64` → ETC2RGBA8，`ios`/`ios-arm64` → ASTC4x4，其他 → RGBA8。这些是纹理产物策略，不代表新增了对应平台的完整 Player。运行时查询 GPU 压缩格式支持；不支持时逐 mip CPU 解码到 RGBA8。当前 ASTC 限 4×4 LDR，ETC2 为 RGBA8，不包含 ASTC HDR/其他块尺寸。移动设备原生上传仍需设备验收。

C# 音频控制：

```csharp
AudioSystem.SetMuted(AudioMixerGroup.Music, true);
AudioSystem.SetSolo(AudioMixerGroup.SFX, true);
AudioSystem.ApplySnapshot(master: 1, music: .4f, sfx: 1, fadeSeconds: .5);
AudioSystem.SetDucking(AudioMixerGroup.SFX, AudioMixerGroup.Music, .25f);
AudioSystem.ClearDucking();
```

三个既有总线 Master/Music/SFX 保留为 ID 0/1/2。静音优先于独奏；快照音量线性渐变，静音/独奏位在终点切换；ducking 按触发组的播放状态控制并使用 attack/release 平滑。通过可选 `TomCat.AudioMixerApiV1` 提供，不改冻结的 AudioApiV1。

### 自定义音频总线图

通过可选 `TomCat.AudioBusApiV1` 配置有向无环图。支持多级路由、分支发送、增益、静音、独奏以及运行时事务替换；多个有效路径的增益相加。静音作用于经过该节点的路径；开启独奏后只保留经过独奏节点的路径。

```csharp
AudioSystem.ConfigureBuses("""
SchemaVersion: 1
Buses:
  - {ID: 0, Name: Master}
  - {ID: 1, Name: Music, Sends: [{Target: 0}]}
  - {ID: 2, Name: SFX, Sends: [{Target: 0}]}
  - {ID: 10, Name: Dialogue, Volume: 0.8, Sends: [{Target: 2, Gain: 1}]}
  - {ID: 11, Name: Radio, Sends: [{Target: 10, Gain: 0.5}, {Target: 2, Gain: 0.2}]}
""");
// source 为 AudioSource 组件；先配置图，再启动引用该总线的声音。
source.Bus = 11;
AudioSystem.SetBus(10, volume: .7f, muted: false, solo: false);
```

`AudioSource.Bus = uint.MaxValue` 表示使用原有 MixerGroup。Bus 随场景保存；图配置由游戏启动脚本调用，可将 YAML/JSON 存为项目文本资源。ID 0 是唯一根输出，0/1/2 必须存在；每个其他节点最终必须到达 0。最多 128 个节点、每节点 16 条发送、配置 64 KiB；节点音量和发送增益均为 0..4，最终 Voice 音量限制为 0..4。循环、缺失目标、重复 ID/名称/发送以及删除正在使用的总线都会失败并保留原图。

当前图处理线性音量路由，通过每个 Voice 的合成增益实现；不含独立 PCM 总线缓冲、混响/EQ/压缩器、侧链或 DSP 插件。这些处理需要后续音频渲染图，不应把当前发送理解为已带效果器的辅助发送。

## 验证

运行 `Scripts/Run-Regressions.ps1 -Log`。其中新增真实 OGG 样本、BC5 与内置精灵材质协议、混音快照、ICU 字素/复数、IME 状态、100000 数据项虚拟列表、5000 实体分帧和真实 OpenGL 光照/阴影/后处理像素断言。

### 本次桌面测量（2026-10-01，Release x64）

5000 个实体、32 单元/2 ms 软预算：全量回归中一次复测为 1493 次推进，峰值 5.77 ms；最后的字素编辑定向回归中为 2478 次推进、峰值 17.14 ms，说明单机负载和不可拆分单元仍会影响峰值。计时达到预算后才在单元边界让出，因此这些推进均记为软预算超时；这些推进次数不是可见渲染帧数，也不构成 2 ms 硬保证。测试同时验证取消后不会发布部分实体。

GPU 测试使用隐藏 OpenGL 上下文和像素读回，断言大精灵内部的局部光源、遮挡变暗，以及曝光/去饱和输出。输入法测试覆盖状态协议，不能替代真实 IME 交互验收。

此前 P1 验收记录：两轮 `Scripts/Run-Regressions.ps1 -Log` 全绿，覆盖原生/Managed、Player、CoinRunner 和模块发布；最后的组合音标长度上限修复另跑 PhysicsRegression（包含 Runtime UI）通过。真实 IME 操作和目标设备最坏帧耗时仍待验收。

### 复杂塑形 / 移动纹理 / 总线图验收（2026-10-01）

`Scripts/Run-Regressions.ps1 -Log` 全量通过（`build/production-final2.log`），涵盖原生、Managed、独立 Player、CoinRunner 三轮和模块发布。新增真实 Noto 印度文字/阿拉伯连字、塑形字形图集与光标映射测试；九组既有 UI 截图基准保持一致。ASTC/ETC2 测试验证真实编码、mip 块尺寸、CPU 解码像素误差、sRGB 编码及截断数据拒绝。音频验证分支路由增益、父级静音、独奏、环路拒绝及使用中总线保护；另跑 AudioRegression 验证非默认 Bus 的场景与 Prefab 往返（`build/production-audio-final.log`）。

一次中间回归遇到 .NET 脚本编译子进程 CLR 内部错误，重跑全量通过。Web `tc_player_core` 编译并链接成功（`build/production-web-core.log`）；完整 WebManaged 构建停在既有共享 ProjectSettingsView 的 `m_Layer` 未声明错误，未计为完整 Web 验收。GPU 的移动压缩格式原生上传、真实 IME 和非 Windows 完整双向文字仍待对应设备验证。
