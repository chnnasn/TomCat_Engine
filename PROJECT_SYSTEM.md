# TomCat Engine 项目系统

## 文件与职责

每个项目使用根目录下的 `Project.tcproj`。该文件是可纳入版本控制的 YAML 项目配置；Hub 的最近打开时间、已知项目列表和本机目录等用户状态不写入项目文件或 `imgui.ini`，而是保存在真实外部目录 `%LOCALAPPDATA%\TomCat\Hub\hub.json`。

产品发布版本、Engine Build ID、Project/Scene/Prefab/TCPAK 格式版本、Player Template 和脚本 ABI 版本统一定义在 `TomCat/src/TomCat/Core/Version.h`；C++ 兼容层直接引用这些常量，PowerShell 发布流程通过 `Scripts/VersionTools.ps1` 读取同一文件并拒绝不一致的 Release 版本。

- `Project`：创建、加载、校验、保存及重新加载项目配置。
- `ProjectManager`：扫描项目、维护 Hub 本地状态，并启动对应版本的 Editor。
- Builder (`Manager.exe`)：项目列表、创建/添加/移除项目和 Editor 版本选择。
- Editor (`TomCat.exe`)：加载项目、按项目模板配置编辑器，并打开启动场景。

## Project.tcproj schema v4

```yaml
SchemaVersion: 4
Project:
  Name: MyGame
  Version: 1.0.0
  Description: A sample game project
  EditorVersion: 1.0.0
  Template: 3D
  AssetDirectory: Assets
```

字段约束：

- `Name` 必须非空。
- `Template` 只能是 `2D` 或 `3D`，且是项目模式的唯一真源；Builder 不再向 Editor 传递额外模式参数。
- `AssetDirectory` 必须是项目内的非空相对路径。
- Content Browser 的当前目录和展开节点属于 Editor 本机状态，保存在项目的 `UserSettings/editor.json`；加载时会限制在项目资源目录内，越界或不存在的路径会被忽略。
- `Project` map 只允许上面列出的六个字段；入口场景和场景构建顺序不再保存在 `Project.tcproj`。

当前 writer 只写 schema v4，并严格校验顶层和 `Project` map。遗留 schema v3 项目仍可加载；其 `StartSceneHandle`/`StartScene` 会迁移到 `ProjectSettings/BuildSettings.json`，随后将项目文件原子升级为 v4。

## ProjectSettings/BuildSettings.json schema v1

```json
{
  "schemaVersion": 1,
  "entrySceneHandle": 14891334345401054091,
  "scenes": [
    {
      "handle": 14891334345401054091,
      "enabled": true,
      "pathHint": "Scenes/Main.tomcat"
    }
  ]
}
```

`BuildSettings.json` 是入口场景与 Scenes In Build 的唯一持久化真源。`scenes` 保留作者顺序；Handle 必须非零且唯一，`pathHint` 只是 `AssetDirectory` 内的作者定位信息。非零 `entrySceneHandle` 必须指向列表中已启用的场景；为 `0` 时没有可运行入口，项目 Cook 会拒绝。schema v4 项目缺少或损坏该文件时加载失败，不从 `Project.tcproj` 回退。

项目文件先写入同目录临时文件；Windows 使用带同目录恢复备份的 `ReplaceFileW` / `MoveFileExW` 原子安装，其他平台使用同文件系统原子重命名，保存结果通过 `bool` 返回给调用方。安装失败时保留已完整写入的临时文件；Windows 若替换中途失败会先尝试恢复原目标，恢复失败则同时保留临时文件和备份，避免清理流程造成二次数据丢失。创建项目会拒绝已有的非空目标目录；若首次保存失败，只逆序删除本次确实创建且仍为空的目录，不递归删除并发出现的内容。

## ProjectSettings/ProjectSettings.json schema v2

可纳入版本控制的项目级 Tag、Layer 和 2D 碰撞矩阵不写入 `Project.tcproj`，而保存在 `ProjectSettings/ProjectSettings.json`：

```json
{
  "schemaVersion": 2,
  "tagsAndLayers": {
    "tags": ["Untagged"],
    "layerNames": ["Default", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""]
  },
  "physics2D": {
    "collisionMasks": [65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535, 65535]
  }
}
```

Loader 只接受有效 JSON、schema v1/v2 及精确的 lowerCamel 字段集，writer 固定写 schema v2。`tags` 必须非空且唯一，第一项固定为 `Untagged`；`layerNames` 始终包含 16 个稳定槽位，第 0 层固定为 `Default`，其余槽位可留空，所有非空名称必须唯一。层数选择 16 是因为 Box2D 的 Category/Mask 均为 16 位。`collisionMasks` 也必须恰好有 16 行，并表示对称矩阵；某一对 Layer 的两个方向不一致时拒绝加载。

新项目会原子写入默认设置，默认所有 Layer 两两允许碰撞。为兼容已有项目，仅当 JSON 不存在时才读取旧的 `ProjectSettings/ProjectSettings.tcsettings` schema v1；加载旧文件本身不会改写磁盘，下一次 `SetSettings` 或 `SaveSettings` 会生成权威 JSON。JSON 一旦存在但内容损坏、版本错误或违反约束，整个项目加载失败，不会回退到旧文件掩盖错误。JSON 与旧文件都不存在时使用内存默认值。Editor 的 Project Settings 中的 `Tags and Layers` 页管理 Tag 和 Layer 名称，`Physics 2D` 页管理对称碰撞矩阵；所有有效修改都会立即通过原子替换写入 JSON，不需要额外点击 Apply，保存失败时 UI 会恢复为最后一次成功保存的值并显示错误。

## ProjectSettings/PlayerSettings.json schema v1

`PlayerSettings.json` 是产品名称、公司/版本、图标、窗口尺寸与模式、Resizable/VSync 以及 Save/Log/Crash 目录的版本化项目真源。新项目创建默认文件，加载时严格校验 schema 和字段；Cook 将其写入 TCPAK v6 的 BootManifest，Player 在创建窗口前应用。兼容读取的 v5 包不含 BootManifest，使用受控默认值。

## 项目结构

```text
MyGame/
├── Project.tcproj
├── ProjectSettings/
│   ├── ProjectSettings.json # Tag、Layer 与 Physics 2D 碰撞矩阵
│   ├── BuildSettings.json   # 有序 Scenes In Build 与入口场景
│   └── PlayerSettings.json  # 产品、窗口与可写目录设置
├── .gitignore          # 忽略 Library、Cache、UserSettings，不忽略 .tcmeta
├── UserSettings/       # Editor 运行时生成，不应提交，也不参与 EVB 打包
│   ├── imgui.ini       # 仅保存布局
│   └── editor.json     # 保存非布局 Editor 状态
├── Library/            # 注册表缓存、导入产物、缩略图；可删除重建，不提交
├── Cache/              # 可选的临时缓存根；可删除重建，不提交
└── Assets/             # 原始项目内容及 .tcmeta；必须提交
    ├── Scenes/
    │   ├── Main.tomcat
    │   └── Main.tomcat.tcmeta
    ├── Scripts/
    └── ...
```

`Assets/` 中每个资源文件都有同名追加后缀的 sidecar，例如
`Textures/Cat.png.tcmeta`。当前 writer 写 schema v2，保存稳定的 64 位 `AssetHandle`、`AssetType`
与版本化导入设置；不保存机器相关的绝对路径。资源文件和 `.tcmeta` 是一个逻辑
整体，移动、重命名和删除必须通过 `AssetManager`，由它同步 sidecar 与
`AssetRegistry`。目录本身不生成 meta，其后代资源仍各自拥有独立 Handle。

`Library/AssetRegistry.yaml` 只是启动缓存，丢失后会从 `Assets/` 与 `.tcmeta`
完整重建；缓存不能凭空恢复没有有效 sidecar 的旧 UUID。资产移动、删除会先在
sidecar 中写入可恢复事务，删除中的源文件暂存于 `Library/DeletedAssets/`，因此进程
中断后下一次 Refresh 可以提交或回滚操作，而不会覆盖同路径下的新文件。
`UserSettings/` 只存个人 Editor 状态，不属于资产系统。

当前资产基础设施包含 ImporterRegistry、SHA-256 ArtifactKey、原子安装的派生数据缓存、依赖图和后台 `AssetImportCoordinator`。Scene/Prefab 的真实 Handle 引用由 `AssetReferenceVisitor` 自动发现，Atlas 子资源会归一化为其父资源；正反向图原子保存到可删除重建的 `Library/AssetDependencies.yaml`，因此重启或删除整个 `Library` 后仍能从作者数据恢复。Coordinator 监控 `Assets/` 中的源文件与 `.tcmeta`，以内容 Hash 而非单纯 mtime 判定真实变化；普通轮询先筛选时间/大小候选，并周期性或按请求执行完整内容校验，因此只触碰 mtime 不会触发重导。连续快速写入会去抖并合并为最终版本；每个批次只导入实际变化的资源及其传递反向依赖，外部修改导入设置也会失效对应 ArtifactKey。

扫描、Hash 与 Importer 工作在后台线程；`PumpMainThread` 才提交 Registry/子资源元数据、清理运行时资源缓存并派发完成事件，OpenGL 等主线程资源创建不会从监控线程发生。停止 Coordinator 会取消并等待在途任务，不再晚到回调。生产级真实格式转码、平台纹理压缩与 Mipmap 生成仍未完成；当前能力不能等同于这些格式处理已经可用于生产。

## 启动流程

1. Builder 加载并严格校验 `Project.tcproj`、`ProjectSettings/BuildSettings.json` 与 `ProjectSettings/PlayerSettings.json`；若存在 `ProjectSettings/ProjectSettings.json`，也同时严格校验；只有后者缺失时才兼容读取旧 `.tcsettings`。
2. Builder 使用 Unicode 版 `CreateProcessW` 启动 Editor，仅传递项目文件路径；带空格、非 ASCII 字符和 Windows 长路径的参数会被正确引用。
3. Editor 重新加载项目，从 `Project.Template` 决定 2D/3D 模式。
4. Editor 从 `BuildSettings.json` 读取有序 Scenes In Build，并按 `entrySceneHandle` 从 Registry 解析入口场景。Handle 为 0、未启用、缺失或类型无效时保留空白编辑场景并报告错误，绝不使用 `pathHint` 回退或绑定同路径下的新资源。
5. Editor 启动成功后，Builder 才在本地 Hub 配置中记录最近打开时间。

新建项目的 `sample.tomcat` 由 Builder 直接通过 `SceneSerializer` 生成，不再复制带独立版本字面量的静态场景模板；场景版本与字段集合因此始终来自同一个 writer。

## 布局与用户设置

- Editor 程序根目录的 `imgui.ini` 是只读默认布局，并随 Editor 一起封装进 Enigma Virtual Box。
- Editor 的 `Packages/` 不进入 Enigma 虚拟文件系统，而是与 `TomCat.exe` 一同放在发布目录；Project 面板将它与项目 `Assets/` 作为两个同级根节点展示，并在 UI 与所有资产变更入口中强制只读。
- Editor 启动或切换项目时先加载封装的默认布局，再加载当前可写布局。有活动项目时写入该 `Project.tcproj` 同目录下的 `UserSettings/imgui.ini`；无活动项目时写入真实外部目录 `%LOCALAPPDATA%\TomCat\Editor\editor-layout.ini`。窗口、Docking、Scene 工具栏和 Content Browser 布局均遵循这一选择。
- Editor 的非布局项目状态写入同一项目下的 `UserSettings/editor.json`，不会混入 `imgui.ini` 或 `Project.tcproj`。
- Hub 没有用户可调整布局；它仍封装只读的默认 `imgui.ini`，但最近打开时间、已知项目列表、本机项目目录和 Editor 目录只写入 `%LOCALAPPDATA%\TomCat\Hub\hub.json`。
- `%LOCALAPPDATA%\TomCat\Editor`、`Hub`、`Player` 是 EVB 虚拟树之外的真实文件系统目录；三个产品的 `TomCat.log` 和无项目 Editor 布局分别写入对应目录，不参与 EVB 打包。
- 构建脚本只把各程序源码目录中的默认 `imgui.ini` 复制到输出目录，不复制运行产生的 JSON 或用户布局。新建项目会生成包含 `/UserSettings/`、`/Library/` 和 `/Cache/` 的 `.gitignore`；加载现有项目时会保留原内容并原子补齐缺失规则。
- 项目根目录不再读取或生成旧式 `imgui.ini`；它只允许作为 Editor 可执行文件的封装默认布局存在。
- Hub 只读取当前 `hub.json`；文件不存在时使用默认状态，不扫描或迁移旧 INI/YAML 配置。
- Content Browser 的 `editor.json` 使用明确的 `@assets` / `@packages` 根前缀保存导航状态；旧版 Assets 相对路径仍可读取，任何越出这两个根目录的值都会回退到 `Assets/`。

## 场景文件 schema v11

`.tomcat` 当前 writer 输出顶层 `SchemaVersion: 11`、`SceneName` 和 `Entities`；reader 严格接受 schema v9/v10/v11，其中 v9/v10 作为只读迁移输入。实体、组件以及层级字段必须与对应版本的完整字段集合精确匹配；任何层级出现缺失、重复或未知字段都会被拒绝。实体关系只使用 `Parent`。schema v11 的每个实体额外保存注册表驱动的 `Components` 序列，组件类型和属性都使用显式稳定 UUID；当前缺失的插件组件作为只读 Missing Component 显示，并连同其嵌套 YAML 载荷无损写回。组件注册表、Opaque Missing Component 往返及 SCB/ComponentApiV1 Bridge 已可用，独立插件/模块 SDK 尚未完成。

每个实体都必须保存 `EntityMetadata`：`GameplayTag` 是项目定义的非空 Tag 字符串，`Layer` 是 0–15 的稳定槽位索引，`HierarchyIcon` 是 `Automatic` 或稳定的显式图标 token。实体名称仍由 `Tag` 组件保存，不与 Gameplay Tag 混用。Inspector 顶部按“图标、启用、名称”排列，并在下一行并排显示 Tag 与 Layer；Inspector 与 Hierarchy 读取同一图标字段，修改后立即同步。新实体默认使用通用 `Entity` 图标；用户主动选择 `Automatic` 后，才按 Camera、Sprite、Rigidbody2D、Collider2D、普通 Entity 的优先级解析。场景中暂时无法在当前项目设置中解析的旧 Tag/Layer 值会保留为 `Undefined`，不会在加载时擅自改写。

Sprite Renderer 必须保存 `Enabled`、`SpriteHandle`、`Color`、`TilingFactor`、`SortingLayer` 和 `OrderInLayer`。`SpriteHandle` 是唯一的 Sprite 来源，可以指向整图 Texture2D，也可以指向 Atlas 子资源；运行时通过资产系统解析并统一走 textured-quad 渲染路径。Handle 为零时不渲染，资源缺失时使用资产系统的缺失资源占位；排序键最终以稳定 Entity UUID 消除同层同 Order 的不确定性。程序化 `DrawCircle` 不作为普通对象的 Sprite 来源，只保留给碰撞体、遮罩和 Gizmo 等工具渲染。Line Renderer 必须保存 `Enabled`、`Color`、局部空间的 `Start`/`End` 以及像素宽度 `Width`。实体 UUID 必须非零且唯一，父子关系会校验缺失引用、多父节点和环。

Texture Importer 的 Multiple 模式使用 `.tcmeta` schema v2 中的持久化切片 ID 生成稳定子资源 Handle；每个切片保存以源图左上角为原点的 `Rect`、归一化左下角 `Pivot`、`PixelsPerUnit` 和四边 `Border`。Content Browser 的 `Sprite Atlas...` 列表编辑器可创建、重命名、校验和删除这些切片；稳定 ID 只读，因此改显示名不会改变 Handle。字段参与确定性 ArtifactKey、Registry 重建和渲染几何解析，单纯改变文件 mtime 不会重建或重编号子资源。Whole-image Texture2D Handle 继续作为兼容 Sprite 路径。Cook 遇到子资源引用时写入包含切片元数据与 Atlas 导入载荷的自包含条目，Player 无需作者态路径或 `.tcmeta` 即可按同一 Handle 渲染。P0 不包含自动 Atlas Packing、自动 Slicer 或可视化切片画布。

`SpriteAnimator` 既保留按帧时长播放的内嵌 Clip，也支持由状态、过渡和 Bool/Int/Float/Trigger 参数构成的状态机；过渡支持条件、AnyState 与归一化 Exit Time，Trigger 在成功使用后消费。动画步进对等总时长的不同帧分割保持确定性，作者数据随 Scene/Prefab/Cook 保存，瞬态播放游标不会序列化；C# 可 Play/Stop、设置各类参数/Trigger 并读取当前状态。P0 不包含独立 Animator Controller 资产或可视化 Animator Graph 编辑器。

Font Importer 当前严格接受 TTF/OTF/TTC SFNT；运行时使用 stb_truetype 为实际出现的 Unicode 码点构建排序稳定的字形图集。`TextRenderer` 和 `UIText` 分别保存主字体、Fallback Font 与 Emoji Font Handle，每个码点严格按该顺序选择并栅格化真实字形；UTF-8 解码拒绝 Overlong、Surrogate 和超出 U+10FFFF 的值并使用 U+FFFD，整条字体链仍不可用时才使用程序化替代字形，而不是留下空白或越界读取。`TextRenderer` 提供世界空间文字；屏幕空间 UI 由 `Canvas`、`RectTransform`、`UIImage`、`UIText`、`UIButton`、`UIEventSystem` 与 `UILayoutGroup` 组成，支持 Anchor/Pivot、参考分辨率与系统 DPI 缩放、横/纵基础布局、父级裁剪、图片保持宽高比、文字换行/对齐、`RaycastTarget` 遮挡/穿透，以及鼠标、键盘和手柄焦点/提交。Editor Game 视口会先换算局部指针坐标，Player 使用窗口 DPI；静止 Hover 只更新视觉状态，按下/按住/释放、导航或提交被 UI 消费的帧才捕获输入；启用 `ConsumeGameplayInput` 时只取消 Gameplay Context 的 Action，UI Context 仍可求值。组件和三个 Font/Image Handle 通过注册表组件序列化、Prefab 和 Cook 依赖闭包持久化，并提供 C# 运行时组件与按钮控制接口。

Hierarchy 中实体可以拖到目标节点的上部、中部或下部，分别成为目标的前一个同级、目标子级或后一个同级；拖到根区域会解除父级。实体顺序由场景的实体顺序持久化。Content Browser 只通过拖入目录或面包屑改变文件与目录层级，不提供手工排序，条目继续使用默认排序；所有实际移动都由资产系统执行。

资源移动或重命名只改变 Registry 中的项目相对路径，场景 Handle 不变，也不需要重写场景。删除前资产系统扫描场景中的 Handle 引用；用户强制删除后引用仍被保留，加载时由 `AssetManager` 返回共享的洋红棋盘占位，以便定位并恢复缺失资源。

场景保存使用与项目文件相同的原子替换并返回 `bool`。反序列化先构造临时场景，只有全部字段和关系校验成功后才替换当前场景，因此损坏或不受支持的文件不会留下半加载状态。变换、相机、颜色、线宽和碰撞体数值会检查有限值及有效范围，非法数据不会进入渲染器或 Box2D。

实体变换只存储平移、旋转和缩放（TRS），不保存剪切矩阵。层级重挂、世界/局部变换更新和层级同步会先验证整棵受影响子树；若矩阵无法无损分解为有限 TRS（例如非均匀缩放叠加错位旋转产生剪切），操作整体失败并保留原状态，避免静默近似造成累计漂移。

## P0 音频运行时

`AudioSource` 支持 Play/Stop/Pause、Loop、Pitch、Volume、Streaming、Mixer Group，以及 `SpatialBlend`、Min/Max Distance；`AudioListener` 提供场景中的主监听者。短 PCM WAV 作为内存 Clip 解码，Streaming WAV 则使用每 Voice 独立游标和固定大小缓冲队列按需读取，不保留随音频时长增长的完整驻留副本。Authoring 模式直接读取 Registry 解析出的 WAV 源文件区间，因为 P0 Audio Importer 仍是逐字节透传；Cooked Player 从已校验的 TCPAK Offset/Size 区间读取。若未来 Importer 引入转码，Authoring Streaming 必须切换到验证后的 DDC Payload 区间。

空间音频在 2D 平面上按 Listener 朝向计算左右声像，以 Spatial Blend 在 2D 与空间结果间混合，并在 Min/Max Distance 间衰减。实体或父级失活、组件禁用、Clip/Streaming 模式变更、实体销毁和 Scene Stop 都会确定性停止并释放旧 Voice；XAudio2 设备失效时引擎降级并重建内存与流式 Voice，保留期望的 Playing/Paused/Stopped 状态。OGG/Vorbis 当前没有捆绑解码器，会明确返回 Unsupported，而不会伪装为可播放格式。

## 2D 物理运行时

运行时将脚本和 Box2D 统一推进为固定 `1/60s` 步长。显示帧差先钳制到 `0.25s`，再进入 accumulator；单个显示帧最多执行 8 个物理子步，超过预算的完整欠步会被丢弃，只保留不足一个固定步的余量。Editor 状态语义固定为：Edit 不运行脚本或物理；Play 同时运行两者；Pause 只渲染当前运行副本；Step 清空显示帧余量并精确推进一次脚本和物理；Stop 销毁运行副本并恢复编辑场景。不提供独立 Simulate 状态。

`BoxCollider2D` 和 `CircleCollider2D` 都保存 `Enabled`、`IsTrigger`、`CollisionLayer`、`CollisionMask`、`Offset` 与共享的 Density/Friction/Restitution 材质字段；Box 另存 `Size`、`RestitutionThreshold`，Circle 另存 `Radius`。这里的 `CollisionLayer` 是每个 Fixture 的 16 位 Box2D Category 位集，至少包含一个位；`CollisionMask` 是 Fixture Mask，可以为零。它们与实体 `EntityMetadata.Layer`/项目碰撞矩阵是相互独立的两道门：首先必须满足 `(A.Category & B.Mask) != 0` 且 `(B.Category & A.Mask) != 0`，然后项目矩阵也必须允许 A/B 的实体 Layer 组合，才会建立接触；项目矩阵不会覆盖或改写 Fixture 过滤。Trigger 作为 Box2D sensor 参与查询和 Enter/Exit 判定，但不产生碰撞响应。Inspector 的 Edit Collider 句柄直接编辑 Size/Radius/Offset，并使用与 Fixture 创建相同的变换规则。

碰撞体不依赖 `Rigidbody2D` 才能进入物理世界：只挂 Collider 的实体在 Play 中会创建隐式静态 `b2Body`。Circle 在非均匀缩放下不能退化为椭圆，因此 Offset 按带符号 XY 缩放和 Z 旋转变换，Radius 统一乘世界 XY 绝对缩放的最大值；编辑轮廓、运行时 Fixture 和调试轮廓都遵守这一规则。

运行中的 Rigidbody、Collider、Transform 物理相关字段或 `DistanceJoint2D` 被添加、移除或修改后，会在下一次固定步开始前安全重建 Box2D 定义；动态刚体的速度会尽量保留。重建、实体删除和 Stop 都会清除旧 runtime 指针与待派发接触，Box2D world 锁定期间不修改 world。`b2Body` user data 只保存实体 UUID；ContactListener 只收集并按“实体对 + Collision/Trigger 类型”去重，`Step()` 返回后才向 Scene 监听器和托管脚本的 `OnCollisionEnter2D/Exit2D`、`OnTriggerEnter2D/Exit2D` 派发，所以回调内删除实体不会留下悬空指针或陈旧 Exit。

Scene 提供带 Layer Mask 和 Trigger 选项的最近命中 `Raycast2D`、按 Entity UUID 去重并稳定排序的 broad-phase `QueryAABB2D`，以及动态刚体的 Force、指定点 Force、Impulse、指定点 Impulse、设置/读取线速度 API。查询的 Layer Mask 按 `EntityMetadata.Layer` 的槽位位图解释，不复用 Collider 的底层 Fixture Category Bits。首个 Joint 类型为 `DistanceJoint2D`，保存 Connected Entity UUID、本体/连接端局部 Anchor、Distance、Frequency、Damping 与 Collide Connected；连接始终通过 UUID 解析，不持久化 Box2D 指针。所有这些组件字段与 `EntityMetadata` 都属于当前 scene schema v11，会随 Scene Copy、实体复制、Cook 和 Player 完整保留，runtime 指针永不序列化。

Scene 视图有独立的“显示碰撞体”开关：Edit 从组件数据画轮廓，Play/Pause 从实际 Box2D Fixture 画轮廓。覆盖层使用现有 `DrawRect`、`DrawCircle`、`DrawLine`，默认只进入 Scene framebuffer，不写 Game framebuffer，也不参与实体 ID 拾取。

仓库中的 `Tests/PhysicsRegression` 覆盖固定步进、SceneManager、Prefab LocalID/重映射、动态脚本安全点、Cook 依赖闭包和 Player 包读回；其中 Runtime UI 回归还覆盖 Cooked Font/Fallback/Emoji 的真实字形选择、16:9/4:3/超宽与 100%/150%/200% DPI 共九组离屏 OpenGL RGBA Golden 截图、裁剪、射线遮挡、鼠标/键盘/手柄输入所有权，以及 logical window/framebuffer/DPI 坐标契约和 Scene/Prefab/Cook 往返。`Tests/SpriteAssetRegression` 覆盖 Atlas 子资源、Animator、Scene/Prefab 与 Cook/Player；`Tests/AudioRegression` 覆盖 WAV 解码、有界流式读取、空间计算、生命周期和设备恢复；`Tests/ImporterRegression` 覆盖内容监控、去抖、正反向依赖重导、跨 Handle 去重、增删改名、中断与停止语义；`Tests/ScriptCompilerRegression` 覆盖 C# 编译、last-good 与私有运行时端到端链路。首次运行前先用 `Scripts\Setup.bat` 准备 Premake；统一入口 `Scripts/Run-Regressions.ps1` 会构建 Managed Release、全部原生回归与独立 Player，并执行模板校验及 Player 冒烟测试。

## 当前格式边界

- `Project.tcproj` 当前写 schema v4，并自动迁移严格合法的 v3；`BuildSettings.json` 与 `PlayerSettings.json` 只接受 schema v1；`ProjectSettings.json` 当前写 v2 并读取 v1/v2；`.tomcat` 当前写 v11 并读取 v9/v10/v11；`.tcpak` 当前写 v6，Player/loader 读取 v5/v6。旧 `.tcsettings` 仅在 JSON 缺失时以只读兼容方式加载。
- 项目资源加载只接受 `AssetHandle`；`BuildSettings.json` 中的 `pathHint` 仅是作者定位信息，不参与身份解析。
- 不再提供未实现的运行时场景序列化 API。
- 项目打开历史属于本机 Hub 状态，不应提交到项目仓库。
- Hub 的“移除项目”只移出列表，不删除磁盘文件；被移除路径保存在 `IgnoredProjects`，默认目录扫描不会自动把它重新加入，用户显式添加、创建或加载时解除忽略。

## Cook 与 Player

Editor 模式下，Registry 可以由 Handle 解析到 `Assets/` 中的源文件，用于导入和预览。
发布时由 `AssetManager` 以 `ProjectSettings/BuildSettings.json` 为真源，将已启用场景按作者顺序写入 v6 `.tcpak`，并保存入口场景 Handle。Cook 从这些场景出发递归收集 Scene、Prefab 和强类型 AssetRef 依赖；未引用资源不进入包，C# 源文件也不会进入包。v6 包同时包含 Handle/类型索引、项目 Physics 2D 碰撞矩阵、可选托管发布载荷，以及携带版本化 PlayerSettings 的 BootManifest。场景输入通过 v9-v11 reader 严格解析，并由当前 v11 writer 规范化后写入；缺失、类型错误或未知字段会使 Cook 失败。

独立发布入口为 `TomCatPlayer.exe`，不加载项目文件、Editor Layer 或原始 `Assets/`、`.tcmeta`、`Library/`。无参数时运行可执行文件旁的 `Game.tcpak`，也可使用 `--package <path>`；`--validate-package <path>` 验证 v5/v6 包、兼容版本及随 Player 发布的私有运行时。Player 挂载包后读取入口与有序 build scenes；v6 还会在创建窗口前应用 BootManifest 中的 PlayerSettings，v5 使用兼容默认值。Player 按 Handle 启动和切换场景；无效入口、损坏索引、版本或资源类型不匹配都会返回非零退出码，不回退到作者路径或全局 .NET 安装。
