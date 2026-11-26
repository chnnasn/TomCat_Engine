# TomCat 引擎使用指南

中文文档 · 整理自 wakeup!neuron 完整移植（Godot → TomCat）实战（2026-11） ·
[文档索引](README.md) · [项目系统](../PROJECT_SYSTEM.md)

本指南按"一个游戏从零到打包"的顺序讲解引擎各环节，末尾附排障清单与
Godot 移植换算。所有事实均来自源码核对或运行验证，与实现不一致处已标注。

---

## 1. 总览

```text
Project.tcproj ──┐
Assets/          │  TomCatCLI cook ──► Build/Game.tcpak ──► PlayerTemplate ──► 运行
  Art/Audio/Fonts/Scene/Scripts
Library/         │     (DerivedData 派生缓存、AssetRegistry、ScriptProject)
ProjectSettings/ ┘
```

- **编辑器（TomCat.exe）**：场景编辑、资产导入（首次导入分配 64 位句柄）。
- **CLI（Tools/bin/.../TomCatCLI.exe）**：`cook` / `build` 两个子命令；先编译校验全部 C#（成功时输出 `TCSP1000`），再打包。
- **Player（PlayerTemplates/win-x64/TomCatPlayer.exe）**：运行 `Game.tcpak`，数据根 `%LOCALAPPDATA%\TomCat\Games\<Company>\<Product>\`，日志在 `Logs/Player.log`。
- 窗口尺寸与标题来自 PlayerSettings；运行时日志同时打到控制台与文件。

工程既有文档：[PROJECT_SYSTEM.md](../PROJECT_SYSTEM.md)（工程/版本），
[DEBUGGING_AND_PROFILING.md](DEBUGGING_AND_PROFILING.md)（调试总论）。
本篇聚焦使用者视角的规则与坑。

---

## 2. 资产系统

### 2.1 目录与 tcmeta

`Assets/` 下每个资产文件旁都有一个 `.tcmeta`：

```yaml
SchemaVersion: 2
Asset:
  Handle: 6109695844481499194      # 64 位 UUID，首次导入随机；引用一律走句柄
  Type: Texture2D                  # Audio / Font / CSharpScript / Scene ...
  ImportSettings:
    filterMode: nearest
    generateMipmaps: false
    compression: RGBA8
    sRGB: true
  SubAssets: []                    # 图集切片（见下）
```

**要点**

- 句柄一旦分配即持久化（`Library/AssetRegistry.yaml`）。程序化生成工程时可以**自己指定句柄**并保持 handles.json 一致，但绝不能复用/漂移。
- 像素风 2D 建议：`filterMode: nearest`、`generateMipmaps: false`（否则缩小时整图被平均成浊色）。
- 修改 ImportSettings 后要删 `Library/DerivedData/` 里对应缓存重新 cook（缓存键是内容哈希，正常会自动失效；遇到过陈旧工件时整目录删除重 cook 即可）。

### 2.2 图集与子资产（SpriteMode: Multiple）

把若干 16×16 瓦片打进一张 PNG，然后在 ImportSettings 写切片：

```yaml
SpriteMode: Multiple
SpriteAtlasSchema: 2
Sprite.tile_0_0.Name: tile_0_0
Sprite.tile_0_0.Rect: 0,0,16,16        # x,y,w,h（像素）
Sprite.tile_0_0.Pivot: 0.5,0.5
Sprite.tile_0_0.PixelsPerUnit: 100
Sprite.tile_0_0.Border: 0,0,0,0
```

每个切片在 `SubAssets: []` 里有自己的 Handle（在游戏/场景 YAML 中直接引用）。
运行时 `Renderer2D` 会按 Rect 从图集采样绘制子区域。
注意：**目前 PixelsPerUnit 只是元数据，渲染端不参与尺寸计算**（尺寸=
实体世界 Scale，见 §4.2）；切片显隐请用 SpriteRenderer.Enabled。

### 2.3 音频 / 字体

- 支持 wav / ogg / oga / mp3。mp3 由内置 minimp3 解码为 PCM，限制与 ogg 相同
  （单/双声道、≤384 kHz、源文件 ≤256 MiB），不再需要离线转成 ogg。和 ogg 一样
  整段解码，不支持流式播放。
- Font：ttf/otf/ttc 直接导入。引擎内置 **Noto Sans SC (CJK Fallback)**：在
  TextRenderer/UIText 的 FallbackFont 里选它，或点 UIText 检查器里的
  "Use built-in CJK fallback"。只有被场景引用时才进包（约 8 MB）。
- 音频句柄 `AudioSource.Clip = AssetRef<AudioAsset>(handle)`；全局没有
  "播放一次" 的静态 API，见 §6.5 的池化模式。

---

## 3. 场景（Scene YAML，Schema 11）

### 3.1 结构与"双镜像"

每个实体块 = 顶部内联段 + `Components` 属性列表，**两者是同一数据的两份表示，
引擎校验强制相等**（不一致即 `cook failed: ... registry payload disagrees
with its legacy Scene 11 field`）：

```yaml
- Entity: <uuid>
  Tag: { Tag: Player, Visible: true }
  EntityMetadata: { GameplayTag, Layer, HierarchyIcon }
  Transform:
    Translation: [x, y, 0]        # 世界（绝对）
    Rotation: [0, 0, 0]
    Scale: [1, 1, 1]
  LocalTransform:
    Translation: [x, y, 0]       # 相对父级
    Scale: [1, 1, 1]
  SpriteRenderer:
    Enabled: true
    Color: [1, 1, 1, 1]
    SpriteHandle: <纹理句柄>
    TilingFactor: 1
    SortingLayer: 0
    OrderInLayer: 0
    CastShadows: false
  Components:
    - TypeId: <组件UUID>
      StableName: TomCat.SpriteRenderer
      SchemaVersion: 2
      Properties:
        - PropertyId: 201
          StableName: Color
          Value: [1, 1, 1, 1]
        ...
  Parent: <父实体uuid | 0>
```

**最容易翻车的三条铁律**

1. **浮点字面量必须写成 float32 的精确十进制**。解析按 float32 →
   再 dump 比较，写 `4.8` 会与实体内存值 `4.80000019` 不等而拒绝 cook。
   生成器统一用：
   ```python
   f = struct.unpack("<f", struct.pack("<f", x))[0]
   s = f"{f:.9g}"                  # 例：4.80000019
   ```
2. 内联段和 Properties 两处都要写，且值一致（同一把尺子量化）。
3. `Parent` 影响世界变换复合（见 §4.1），写错父级连孩子的世界位置/缩放
   会被父级 Scale 放大。

### 3.2 常用自带组件速查

| 组件 | 关键字段 | 备注 |
|---|---|---|
| TomCat.Transform | Translation/Rotation/Scale + Local* | 镜像校验的对象 |
| TomCat.Tag | Name（层级名）、Visible(=运行时激活) | `World.Find` 按层级名 |
| TomCat.Camera | ProjectionType=1(正交)、OrthographicSize、Primary、BackgroundColor、FixedAspectRatio | **Size=全高**，见 §4.2 |
| TomCat.SpriteRenderer | SpriteHandle、Color、TilingFactor、SortingLayer、OrderInLayer、CastShadows | 排序键 (Layer, Order, EntityId, SubOrder) |
| TomCat.TextRenderer | Font、Text、FontSize、Color、FallbackFont | 世界空间文字 |
| TomCat.LineRenderer | Start/End(Vector3!)、Width、Color | Width≤0 跳过；需有限值 |
| TomCat.Rigidbody2D | BodyType(0/1/2)、FixedRotation | **重力固定 (0,-9.8) 不可改** |
| TomCat.BoxCollider2D | Offset、Size（全尺寸）、Friction、Restitution、Layer/Mask | 无角度 |
| TomCat.CircleCollider2D | Radius | |
| TomCat.Tilemap2D | CellSize、Cells[]、OrderInLayer | 见 §5 |
| TomCat.TilemapCollider2D | 合并 Solid cell → 静态刚体 | 只支持 Rectangle 网格 |
| TomCat.PostProcess2D | PaletteSwapEnabled、Old/New 四色、MatchThreshold、MaskSprite/Color/Enabled/Progress | 见 §7 |
| TomCat.AudioSource | Clip、Loop、PlayOnStart、Volume、Bus | |
| TomCat.CSharpScripts | Scripts[]: AttachmentID/ScriptHandle/ClassName/Fields | 见 §6 |

实体名在本层级内做**兄弟去重**（`Sprite`、`Sprite (1)`…），脚本按名找子
实体每帧都成立。

---

## 4. 坐标系、相机与层级

### 4.1 坐标系

- **Y 轴向上**，2D 单位 = 世界单位（u），像素 → 世界：`world = px / PPU`。
  移植 Godot：`world_x = gx/100, world_y = -gy/100`（PPU=100）。
- 实体世界变换 = 父级矩阵 × 本地矩阵（Move/Scale 全复合）。**子实体的
  LocalTranslation/LocalScale 会被父级 Scale 放大**——这就是"视差板套娃
  变 23×7 巨块"事故的根因。若只是想要世界空间平铺的四边形，把它们做成
  **兄弟根实体**而不是父子。
- `GetRuntimeRenderTransform` 会叠加物理体的姿态，刚体实体不用手动同步变换。

### 4.2 相机：OrthographicSize 语义（重要）

```cpp
// SceneCamera.cpp：size 是"全高"
orthoTop  = +m_OrthographicSize * 0.5f;
orthoBottom = -m_OrthographicSize * 0.5f;
```

- 视野半高 = `Size/2`，半宽 = `Size/2 × aspect`。
- 例：模拟 Godot 480×270@PPU100 视野（±2.40 × ±1.35）→ `Size = 2.7`。
- C# `Camera.OrthographicSize` 的 XML 文档已标明是全高。从 Unity/Godot 移植
  半高数值时用 `Camera.OrthographicHalfHeight`（读写都是 Size/2），不要把
  1.35 直接写进 OrthographicSize，否则画面放大 2 倍、瓦片被裁掉大半。
- 三台相机相关注意：`Primary` 决定渲染视图；相机实体在 boot 场景 + C#
  移动它的 Transform 来跟镜/摇镜。

### 4.3 层级排序

2D draw key：`(SortingLayer, OrderInLayer, EntityID, SubOrder)` **升序**，
后画在上。LineRenderer 在全部 sprite 之后绘制。同层同 Order 时按实体
UUID 决胜——想稳定压栈请显式给 Order。

---

## 5. Tilemap

```yaml
Tilemap2D:
  CellSize: [0.159999996, 0.159999996]
  Cells:
    - Coordinate: [-16, -8]        # 网格坐标（行优先矩形）
      SpriteHandle: <图集子资产句柄>
      Tint: [1, 1, 1, 1]
      FlipX: false
      FlipY: false
      RotationQuarterTurns: 0
      Collision: 1                  # 0 无 / 1 实心
```

- 实体变换即瓦片原点：cell 世界中心 = 根 Translation + Coordinate×CellSize。
- 父级若无 Grid2D，用 Tilemap2D 自身 CellSize/CellGap（我们一直这样用，
  Rectangle 布局足够）。
- `TilemapCollider2D` 在 Play 时把所有 `Collision: 1` 的格子合并成静态
  固定装置（运行时生成，不回写场景）；非 Rectangle 布局只告警不发威。
- CellSize 写错会整体错位/被裁掉：先用相机 viewBounds 对拍（见 §9 诊断）。

---

## 6. C# 脚本（TCG）

源文件放 `Assets/**/*.cs`，cook 时整体编译成 `Assembly-CSharp.dll` 并做
"TCG 规则"静态校验（输出号 TCSP1000）。

### 6.1 硬性规则（违反直接编不过）

- 一个 .cs 只有一个 `MonoBehaviour`；**类名 = 文件名**；无命名空间模板要求；
- 无 abstract 基类挂场景；**生命周期方法沿具体基类链收集**（具体基类 OK）；
- 可序列化字段：`bool/int/long/float/double/string/Vector2/3/4/Color/Enum/Entity/AssetRef`
  —— `Vector2[]`、`Action<>` 委托**不能**做字段（改成 property）
  列入 `Fields: []`；
- 场景里的 `ScriptHandle` = 脚本 .cs 资产句柄；请用空 `Fields` + 代码常量，
  避免计算 FieldID（`lowerhex(SHA256(handle:Type:field))`）。

### 6.2 生命周期与主线程

Awake → OnEnable → Start → Update / LateUpdate / FixedUpdate →
OnCollision/OnTrigger(Enter/Exit)2D → OnDisable → OnDestroy。任何引擎调用
必须在主线程回调内；`Time.deltaTime` 上下文感知（FixedUpdate 里=固定步长）。

### 6.3 常用 API（已验证）

```csharp
var t = GetComponent<Transform>();          // 缺组件抛异常
foreach (var c in Entity.Children) ...      // 只能拿子
World.Find("Name"); World.CreateEntity("N", pos, parent);  // Find 按层级名
World.Find(name: null, tag: "Enemy", within: root);  // 按 Gameplay Tag / 子树查找
World.Find<SpriteRenderer>("Door");          // 直接拿组件，省掉 GetComponent
World.FindAll<AudioSource>(tag: "Sfx"); World.FindWithTag("Coin");
// 以上都是全场景扫描：在 Start 里查一次并缓存结果
// 物理
var hit = Physics2D.Raycast(a, b, mask);     // RaycastHit2D?，用 hit is { } h
rb.LinearVelocity; rb.ApplyLinearImpulse(v);
// 协程
StartCoroutine(IEnum());                     // 只能从本脚本的回调里启动
door.StartDetachedCoroutine(door.Open());    // 从别的脚本启动，下一帧以 door 身份执行
yield return null / Yield.Seconds(s) / Yield.Frames(n) / Yield.While(fn);
// 音频、输入、存档
AudioSystem.ApplySnapshot(master, music, sfx, fade, mutedMask, soloMask);
Input.GetKey(KeyCode.Space);                 // KeyCode=GLFW 码
Input.SimulateKeyTap(KeyCode.Right, 3);      // 合成输入：无焦点也有效，按住 3 帧
Application.GetTestArgument("level");        // Player --test-arg level=2
Application.DisplayMode = DisplayMode.Borderless;  // 运行时切全屏/无边框/窗口
var v = (a - b) / 2f; v.Normalized; Vector2.Dot(a, b);  // Vector2/3 运算符
SaveData.Write(slot, doc); SaveData.ReadDocument(slot);
Application.Quit(0);
```

### 6.4 模式：跨脚本引用与"待办"启动协程

- 脚本不是引擎组件，`TryGetComponent<脚本>` 非法 → 搭一个静态注册表
  （`GameState.RegisterScript(name, s)` / `GameState.Script<T>(name)`）。
- A 脚本想让 B 实体的协程开跑：调用 `b.StartDetachedCoroutine(b.Routine())`
  （即 `ScriptTasks.StartDetached`）。第一步在 B 的下一次任务派发时以 B 的身份
  执行，随 B 一起停止，不再需要 pending 标志/队列。
- 委托字段（`public Action OnDie;`）不能序列化，会报 **TCG012**。按提示改成
  `public event Action OnDie;`、加 `[NonSerialized]` 或改成非 public；要在编辑器
  里连线，就暴露一个 `public void` 方法。
- 一次性音效 = 预建 N 个带 AudioSource 的实体做池，设 `Clip` 后 `Play()`。

### 6.5 Sprite 尺寸语义（重要，与引擎规则强相关）

- **Atlas 子资源**：世界尺寸 = `像素/PPU` × 实体（复合）Scale，`Scale (1,1)`
  即像素原样显示。
- **整图纹理（默认）**：四边形是 1×1 世界单位，尺寸完全等于 Scale，
  `Scale (1,1)` 是一整个世界单位的贴片，不是纹理原始大小。
- **整图纹理 + `SpriteSize: PixelsPerUnit`**：在纹理 `.tcmeta` 的 ImportSettings
  里写 `SpriteSize: PixelsPerUnit` 和 `PixelsPerUnit: 100`（或你的 PPU），
  尺寸规则就和子资源一样，`Scale (1,1)` 即像素原样显示。像素风移植推荐对
  所有纹理开启，这样就不需要 `fix_sprite_scales.py` 那一步。开启后要把
  之前手算的 Scale 改回 `(1,1)`，否则尺寸会再缩小一次。
- 挂在有缩放的父级下，子级 Scale 仍会复合。

---

## 6.6 像素完美渲染（PixelPerfectCamera2D）

在主相机实体上加 **PixelPerfectCamera2D**（正交相机才生效）：

- `ReferenceWidth` × `ReferenceHeight`：可见的参考像素分辨率（如 320×180）。
- `PixelsPerUnit`：每世界单位的参考像素数，应与 sprite 的 PPU 一致。
- `LetterboxColor`：整数缩放后剩余屏幕的黑边颜色。
- 勾选 `SnapToScreenPixels` 时，相机和精灵按渲染目标的单个像素移动。例如 4 倍放大时，对齐步长从 4 个输出像素降为 1 个。

开启后视野按能放下的最大整数倍缩放（1440×810 窗口、320×180 参考 → 4 倍，
居中 1280×720），OrthographicSize 被忽略。默认情况下，相机和 sprite/tile 的平移吸附到
`1/PPU` 网格，UV 内缩百分之一个纹素防止图集缝隙。像素风纹理要同时设
`filterMode: Nearest`。窗口比参考分辨率还小时按 1 倍显示并裁剪。

## 7. 后处理与调色板（PostProcess2D）

4 色替换 + 圆形 mask（用于 iris 转场）同时在相机实体上：

```csharp
var post = GetComponent<PostProcess2D>();
post.PaletteSwapEnabled = true;
post.PaletteOldDark = new Color(0,0,0,1);        // 源四色（黑/白/红/蓝）
post.PaletteOldLight = new Color(1,1,1,1);
post.PaletteOldPrimary = new Color(1,0,0,1);
post.PaletteOldSecondary = new Color(0,0,1,1);
post.PaletteNewDark/Light/Primary/Secondary = ...;   // 目标四色
post.PaletteMatchThreshold = 0.01f;              // 精确匹配阈值
post.MaskSprite = new AssetRef<Texture2DAsset>(handle);  // 圆形/圆角矩形遮罩
post.MaskColor = ...; post.MaskEnabled = ...; post.MaskProgress = ...; // 0敞开 1全遮
```

- 命中阈值内的像素按通道精确替换（threshold 0.01 ≈ 精确匹配），
  非四色像素原样通过。4 色美术（黑白红蓝）→ 一套调色板即可全屏风格化。
- 转场协程：`MaskProgress` 0→1 关屏，换场景，再 1→0 开屏。

**光照**：场景里完全没有 Light2D 时，引擎会自动把 sprite 走"无光照"
路径（v_Lit=0）；一旦放了任意 Light2D，环境光默认变成 0.05，
先想好你的 ambient 再上灯。

---

## 8. 构建打包流水线

### 8.1 命令

```bash
# cook：编译 C# + 资产派生 + 场景收集 → Game.tcpak
TomCatCLI.exe cook --project X/Project.tcproj --output X/Build/Game.tcpak
# build：cook + 生成可分发目录
TomCatCLI.exe build --project X/Project.tcproj --template <PlayerTemplates/win-x64>

# 引擎重建后刷新 Player 模板并部署:
powershell -File Scripts/Build-PlayerTemplate.ps1 -Build   # 输出到
#   Editor/bin/<cfg>-windows-x86_64/TomCatInut/Packages/PlayerTemplates/win-x64/
cp -r .../win-x64/*  YourProject/PlayerTest/
```

### 8.2 BuildSettings 与入口

`ProjectSettings/BuildSettings.json` 的 `scenes[]` 顺序即 buildIndex。
**入口场景必须是常驻系统所在的场景**（我们：boot 场景 = 相机 + Systems +
SFX 池 + 334 句柄的兜底 sprite；level 直接当入口会黑屏无相机）。
`SceneManager.LoadScene(buildIndex)` 帧末生效，旧场景常驻体用
`DontDestroyOnLoad`/`SetPersistent` 保持。

### 8.3 资产进包规则（重要）

**cook 默认只打包构建场景引用到的句柄**（以及它们的依赖）。只被 C# 用句柄
加载、不出现在任何场景里的资产，要在 Content Browser 右键勾选
**Always Package**（即导入设置 `AlwaysPackage: true`）。勾选的资产连同依赖、
图集的全部切片一起进包；资产缺失或不能进包时 cook 直接报错。不再需要用 boot
场景里隐藏的 DEP 实体凑依赖。

---

## 9. 调试与排障

### 9.1 日志

`%LOCALAPPDATA%\TomCat\Games\<Company>\<Product>\Logs\Player.log`，
[Cook 诊断格式见 Tools/TomCatCLI]（`文件(行,列) Code Message`）。

### 9.2 诊断工具

移植期间临时加的 `TC_DUMP_COOKED_SCENES`、`TC_SPRDBG` 环境变量已移除，
由下面两个常驻功能取代：

| 命令 | 作用 |
|---|---|
| `TomCatCLI pak --inspect Game.tcpak [--dump-textures dir] [--dump-scenes dir]` | 列出包内场景/实体/组件/Sprite 句柄（缺失句柄标 `NOT IN PACKAGE`）、纹理尺寸与 Sprite 几何；可导出 PNG 和 cook 后的场景 YAML |
| `TomCatPlayer --package Game.tcpak --render-diag diag.json` | 约每秒原子重写一次 JSON：视野世界范围 `view`、sprite/tile 的 total/inactive/culled/submitted、绘制调用数、排序后前 256 个绘制项（layer/order/entity/sprite） |

`view` 直接给出运行时视野世界范围，例如 `minX -2.4 … maxX 2.4`、
`minY -1.35 … maxY 1.35`。JSON 字段稳定，可以直接作为回归基线比对。

### 9.3 排障清单：'画面不对/精灵看不见'

1. `--render-diag` 的 `view` 是不是预期的 ±W×±H？（把半高当成全高会放大 2 倍）
2. `tiles` / `sprites` 的 culled、submitted 数对不对？
   culled 很大→相机错位；submitted 正常但看不见→绘制端。
3. `pak --inspect` 里纹理是 unit quad 还是像素尺寸，世界尺寸是否合理？
4. `pak --inspect` 里实体的 sprite 句柄是否标了 `NOT IN PACKAGE`（§8.3）？
   `--dump-textures` 导出的 PNG 是否与源图一致？
5. 全场变浊色/发黑 → 场景里有 Light2D（§7 光照短路只在"完全无灯"生效）。
6. 尺寸全错（如 4.8 倍跳变）→ 父级 Scale 复合（§4.1）。
7. 程序化生成的场景 cook 报 mirrored 不一致 → 浮点没写 float32 精确十进制。

### 9.4 自动化验证

Player 自带截图和测试钩子，不再需要 ctypes 截屏和系统键盘注入：

```powershell
TomCatPlayer --package Game.tcpak --headless --fixed-delta 0.0166667 `
  --test-arg level=2 --screenshot out.png --frames 120 --render-diag diag.json
```

- `--headless`：渲染到隐藏窗口，不抢焦点、不会被遮挡，也没有 DPI 截图问题。
- `--screenshot out.png` + `--frames N`：第 N 帧（默认 60）离屏重渲染存 PNG 后退出；
  失败退出码 8。只写 `--frames N` 就是跑 N 帧后退出。
- `--fixed-delta s`：每帧固定推进 s 秒，第 N 帧的画面可复现。
- `--test-arg key[=value]`（可重复）：C# 用 `Application.GetTestArgument(key)` /
  `HasTestArgument(key)` 读取，取代 `TC_E2E_AUTO_LEVEL` 这类环境变量。
- 键盘走查用 `Input.SimulateKey` / `SimulateKeyTap`：不依赖窗口焦点，下一帧生效。
- 相机视野与绘制顺序用 `--render-diag` 的 JSON 对拍。

---

## 10. 从 Godot 移植的换算速查

| Godot | TomCat |
|---|---|
| 像素坐标 | ÷100（PPU=100），Y 取反 |
| Camera2D 480×270 视野 | Camera OrthographicSize=2.7（全高，或 OrthographicHalfHeight=1.35）+ 位置=实体 Transform |
| Sprite2D 像素原样显示 | 纹理开启 `SpriteSize: PixelsPerUnit` + Scale=(1,1)；未开启时 Scale=(w,h)/PPU |
| TileMap tile_data PackedInt32Array | Tilemap2D Cells（Coordinate×CellSize，CellSize=16/100） |
| CanvasLayer 视差 | 兄弟根实体 quad ×2 + 脚本绝对定位滚动（勿父子嵌套） |
| Sprite2D z_index | OrderInLayer（升序在上） |
| RigidBody2D/CollisionObject2D | Rigidbody2D kinematic/dynamic + Collider2D（重力固定 -9.8） |
| AudioStreamPlayer | AudioSource 实体（池化 + Bus） |
| Tween/await | Dynamics 弹簧自实现（f,z,r;k1=z/πf ... 见项目 Scripts/Dynamics.cs）+ 协程 |
| 用户纹理 + 着色器 palette | PostProcess2D PaletteSwap（黑白红蓝 → 目标四色） |
| 引擎字体回退 | FallbackFont 选内置 Noto Sans SC (CJK Fallback) |
| 像素风 Stretch Mode viewport / 整数缩放 | 相机实体加 PixelPerfectCamera2D（参考分辨率 + PPU），纹理设 `filterMode: Nearest` |

---

## 11. 已知限制 / 待改进

- PixelPerfectCamera2D 只吸附 sprite/tile 的平移；旋转的 sprite、粒子不会落到像素网格上。
- mp3/ogg 都是整段解码，不支持流式播放。
- 本文档若与 `TomCat/src/TomCat` 实现冲突，以实现为准（该仓库
  `docs/README.md` 为索引）。

刚体属性 `Rigidbody2D.GravityScale` 默认为 1，设为 0 可由角色脚本独立积分重力。后处理组件启用 `RotatingIris` 时，MaskProgress 从 0 到 1 将遮罩中心开口旋转一周并收拢。

### Windows 游戏发行目录与图标

编辑器的 Build Player 与命令行 `TomCatCLI build` 使用同一导出流程。Windows x64 发行目录根部包含 `<产品名>.exe` 和 `<产品名>_Data/`。数据目录收纳 Game.tcpak、运行时、动态库和着色器。双击根目录的程序即可运行游戏，无需控制台。

在 Project Settings → Player → Icon 中，选择已导入的 Texture2D，或从资源浏览器将 PNG 等纹理拖到该字段。重新导出后，资源管理器中的 EXE 图标与游戏窗口图标均来自该设置。更换图标只需修改项目设置并重新导出。

Windows 交互式 Player 会在游戏窗口内先清屏为纯黑（#000000），再等比居中完整显示启动动画（Contain），窗口留白保持纯黑。默认视频 `TomCat_Splash_1x1.mp4` 为 1080×1080，显示边长随窗口短边变化。播放期间预加载游戏，结束后从黑色淡入首个场景。在 Project Settings → Player → Startup Animation 中点 **Choose MP4...** 可导入自定义视频（推荐 H.264 MP4），点 **Use TomCat logo** 可恢复默认。自定义素材保存在项目的 `ProjectSettings/Startup.mp4`，导出时所选视频随游戏放入 `<产品名>_Data/Startup.mp4`。导出器会解码检查视频，缺失或损坏会使本次导出失败。无窗口测试与包校验跳过播放。

在 Project Settings → Player → Display 的 Fullscreen Mode 中可选择 Fullscreen Window（无边框全屏）、Exclusive Fullscreen（独占全屏）、Maximized Window（最大化窗口）或 Windowed（普通窗口）。Default Is Native Resolution 使用显示器当前桌面分辨率；取消勾选后，通过 Default Screen Width 和 Default Screen Height 指定启动分辨率。无边框全屏和最大化窗口按比例呈现固定分辨率，窗口大小由屏幕或桌面工作区决定。独占模式由系统选择支持的显示模式，普通窗口可受系统边框、工作区及后续手动调整影响。
