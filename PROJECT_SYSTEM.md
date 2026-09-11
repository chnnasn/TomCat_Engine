# TomCat Engine 项目系统

## 文件与职责

每个项目使用根目录下的 `Project.tcproj`。该文件是可纳入版本控制的 YAML 项目配置；Hub 的最近打开时间、已知项目列表和本机目录等用户状态不写入项目文件或 `imgui.ini`，而是保存在真实外部目录 `%LOCALAPPDATA%\TomCat\TomCatSettings\hub.json`。

- `Project`：创建、加载、校验、保存及重新加载项目配置。
- `ProjectManager`：扫描项目、维护 Hub 本地状态，并启动对应版本的 Editor。
- Builder (`Manager.exe`)：项目列表、创建/添加/移除项目和 Editor 版本选择。
- Editor (`TomCat.exe`)：加载项目、按项目模板配置编辑器，并打开启动场景。

## Project.tcproj schema v3

```yaml
SchemaVersion: 3
Project:
  Name: MyGame
  Version: 1.0.0
  Description: A sample game project
  EditorVersion: 1.0.0
  Template: 3D
  AssetDirectory: Assets
  StartScene: Scenes/Main.tomcat
  StartSceneHandle: 14891334345401054091
```

字段约束：

- `Name` 必须非空。
- `Template` 只能是 `2D` 或 `3D`，且是项目模式的唯一真源；Builder 不再向 Editor 传递额外模式参数。
- `AssetDirectory` 必须是项目内的非空相对路径。
- `StartScene` 必须是相对于 `AssetDirectory` 的安全相对路径，不能使用绝对路径或 `..` 跳出资源目录。
- `StartSceneHandle` 字段必须存在，是启动场景的唯一身份真源；非零 Handle 可由 Registry 解析并修复仅供作者阅读的 `StartScene` 定位信息。显式的 `0` 表示项目没有入口场景，Editor 保持空白，Cook 会拒绝生成可运行包。
- Content Browser 的当前目录和展开节点属于 Editor 本机状态，保存在项目的 `UserSettings/editor.json`；加载时会限制在项目资源目录内，越界或不存在的路径会被忽略。
- `Project` map 只允许上面列出的八个字段；Hub 与 Editor 的本机状态只能写入各自的 JSON 设置文件。

加载器只接受显式的 `SchemaVersion: 3`，并要求顶层与 `Project` map 精确匹配当前 writer 定义的字段；缺失、重复或未知字段以及其他版本都会直接拒绝，不执行自动升级或路径到 Handle 的迁移。通常加载和扫描不会重写 `Project.tcproj`；若 `StartSceneHandle` 解析出的当前位置与作者定位字段不同，Editor 会按 Handle 修复 `StartScene` 并原子保存。

项目文件先写入同目录临时文件；Windows 使用带同目录恢复备份的 `ReplaceFileW` / `MoveFileExW` 原子安装，其他平台使用同文件系统原子重命名，保存结果通过 `bool` 返回给调用方。安装失败时保留已完整写入的临时文件；Windows 若替换中途失败会先尝试恢复原目标，恢复失败则同时保留临时文件和备份，避免清理流程造成二次数据丢失。创建项目会拒绝已有的非空目标目录；若首次保存失败，只逆序删除本次确实创建且仍为空的目录，不递归删除并发出现的内容。

## 项目结构

```text
MyGame/
├── Project.tcproj
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
`Textures/Cat.png.tcmeta`。它保存稳定的 64 位 `AssetHandle`、`AssetType`
与导入设置；不保存机器相关的绝对路径。资源文件和 `.tcmeta` 是一个逻辑
整体，移动、重命名和删除必须通过 `AssetManager`，由它同步 sidecar 与
`AssetRegistry`。目录本身不生成 meta，其后代资源仍各自拥有独立 Handle。

`Library/AssetRegistry.yaml` 只是启动缓存，丢失后会从 `Assets/` 与 `.tcmeta`
完整重建；缓存不能凭空恢复没有有效 sidecar 的旧 UUID。资产移动、删除会先在
sidecar 中写入可恢复事务，删除中的源文件暂存于 `Library/DeletedAssets/`，因此进程
中断后下一次 Refresh 可以提交或回滚操作，而不会覆盖同路径下的新文件。
`UserSettings/` 只存个人 Editor 状态，不属于资产系统。

## 启动流程

1. Builder 加载并严格校验 `Project.tcproj`。
2. Builder 使用 Unicode 版 `CreateProcessW` 启动 Editor，仅传递项目文件路径；带空格、非 ASCII 字符和 Windows 长路径的参数会被正确引用。
3. Editor 重新加载项目，从 `Project.Template` 决定 2D/3D 模式。
4. Editor 只按 `StartSceneHandle` 从 Registry 解析启动场景。Handle 为 0、缺失或类型无效时保留空白编辑场景并报告错误，绝不使用 `StartScene` 路径回退或绑定同路径下的新资源。
5. Editor 启动成功后，Builder 才在本地 Hub 配置中记录最近打开时间。

新建项目的 `sample.tomcat` 由 Builder 直接通过 `SceneSerializer` 生成，不再复制带独立版本字面量的静态场景模板；场景版本与字段集合因此始终来自同一个 writer。

## 布局与用户设置

- Editor 程序根目录的 `imgui.ini` 是只读默认布局，并随 Editor 一起封装进 Enigma Virtual Box。
- Editor 启动或切换项目时先加载封装的默认布局，再加载当前可写布局。有活动项目时写入该 `Project.tcproj` 同目录下的 `UserSettings/imgui.ini`；无活动项目时写入真实外部目录 `%LOCALAPPDATA%\TomCat\TomCatSettings\editor-layout.ini`。窗口、Docking、Scene 工具栏和 Content Browser 布局均遵循这一选择。
- Editor 的非布局项目状态写入同一项目下的 `UserSettings/editor.json`，不会混入 `imgui.ini` 或 `Project.tcproj`。
- Hub 没有用户可调整布局；它仍封装只读的默认 `imgui.ini`，但最近打开时间、已知项目列表、本机项目目录和 Editor 目录只写入 `%LOCALAPPDATA%\TomCat\TomCatSettings\hub.json`。
- `%LOCALAPPDATA%\TomCat\TomCatSettings` 是 EVB 虚拟树之外的真实文件系统目录；`hub.json`、`editor.json`、`editor-layout.ini`、`UserSettings` 和 `TomCatSettings` 均不参与 EVB 打包。
- 构建脚本只把各程序源码目录中的默认 `imgui.ini` 复制到输出目录，不复制运行产生的 JSON 或用户布局。新建项目会生成包含 `/UserSettings/`、`/Library/` 和 `/Cache/` 的 `.gitignore`；加载现有项目时会保留原内容并原子补齐缺失规则。
- 项目根目录不再读取或生成旧式 `imgui.ini`；它只允许作为 Editor 可执行文件的封装默认布局存在。
- Hub 只读取当前 `hub.json`；文件不存在时使用默认状态，不扫描或迁移旧 INI/YAML 配置。
- Content Browser 的 `editor.json` 路径只接受相对于项目 `Assets/` 的安全相对路径，不按进程工作目录或项目根目录尝试旧路径回退。

## 场景文件 schema v6

`.tomcat` 场景只接受顶层 `SchemaVersion: 6`、`SceneName` 和 `Entities`。实体、组件以及层级字段必须与当前 writer 的完整字段集合精确匹配；任何层级出现缺失、重复或未知字段都会被拒绝。实体关系只使用 `Parent`。

Sprite Renderer 必须保存 `Enabled`、`SpriteHandle`、`Color` 和 `TilingFactor`。`SpriteHandle` 是唯一的 Sprite 来源，运行时通过资产系统解析并统一走 textured-quad 渲染路径；Handle 为零时不渲染，资源缺失时使用资产系统的缺失资源占位。程序化 `DrawCircle` 不作为普通对象的 Sprite 来源，只保留给碰撞体、遮罩和 Gizmo 等工具渲染。Line Renderer 必须保存 `Enabled`、`Color`、局部空间的 `Start`/`End` 以及像素宽度 `Width`。实体 UUID 必须非零且唯一，父子关系会校验缺失引用、多父节点和环。

当前阶段一张 `Texture2D` 图片就是一个整图 Sprite；Inspector 的 Sprite 选择窗从 Registry 枚举所有有效图片，而不是枚举形状。新项目会在 `Assets/Sprites/TomCat/` 创建 Circle、Square 两个普通 TGA 资源及其 `.tcmeta`；右键创建预设通过 `.tcmeta` 中的 `Usage=Sprite`、`Primitive=...` 查找 Handle，因此这些资源移动或重命名后仍可使用。以后引入图集区域、Pivot 和 Pixels Per Unit 的实际导入语义时，再增加真正的 Sprite 子资源类型。

Hierarchy 中实体可以拖到目标节点的上部、中部或下部，分别成为目标的前一个同级、目标子级或后一个同级；拖到根区域会解除父级。实体顺序由场景的实体顺序持久化。Content Browser 只通过拖入目录或面包屑改变文件与目录层级，不提供手工排序，条目继续使用默认排序；所有实际移动都由资产系统执行。

资源移动或重命名只改变 Registry 中的项目相对路径，场景 Handle 不变，也不需要重写场景。删除前资产系统扫描场景中的 Handle 引用；用户强制删除后引用仍被保留，加载时由 `AssetManager` 返回共享的洋红棋盘占位，以便定位并恢复缺失资源。

场景保存使用与项目文件相同的原子替换并返回 `bool`。反序列化先构造临时场景，只有全部字段和关系校验成功后才替换当前场景，因此损坏或不受支持的文件不会留下半加载状态。变换、相机、颜色、线宽和碰撞体数值会检查有限值及有效范围，非法数据不会进入渲染器或 Box2D。

实体变换只存储平移、旋转和缩放（TRS），不保存剪切矩阵。层级重挂、世界/局部变换更新和层级同步会先验证整棵受影响子树；若矩阵无法无损分解为有限 TRS（例如非均匀缩放叠加错位旋转产生剪切），操作整体失败并保留原状态，避免静默近似造成累计漂移。

## 当前格式边界

- `Project.tcproj` 只接受 schema v3，`.tomcat` 只接受 schema v6，`.tcpak` 只接受 v2；旧格式不会自动迁移、补字段或重新保存。
- 项目资源加载只接受 `AssetHandle`；`StartScene` 仅是作者定位信息，不参与身份解析。
- 不再提供未实现的运行时场景序列化 API。
- 项目打开历史属于本机 Hub 状态，不应提交到项目仓库。
- Hub 的“移除项目”只移出列表，不删除磁盘文件；被移除路径保存在 `IgnoredProjects`，默认目录扫描不会自动把它重新加入，用户显式添加、创建或加载时解除忽略。

## Cook 与 Player

Editor 模式下，Registry 可以由 Handle 解析到 `Assets/` 中的源文件，用于导入和预览。
发布时由 `AssetManager` 将资源 Cook 成带启动场景 Handle、Handle/类型索引的 v2 `.tcpak`；
Player 挂载后通过 `GetCookedStartSceneHandle()` 取得入口，并按 Handle 读取场景和依赖字节，
不读取原始 `Assets/` 路径、`.tcmeta` 或 `Library/`。项目配置中的 `StartSceneHandle` 是 Cook
入口的真源，`StartScene` 仅作为 Editor 侧的作者定位信息。Cook 只接受当前 schema v6 场景及
当前字段集合；任何旧 schema 或未知字段都会使 Cook 失败，不会被转换或带进 Player。挂载器只接受
当前 v2 包，其他版本直接拒绝。

当前可执行程序的发布运行入口为 `TomCatInut.exe --play-cooked <Game.tcpak>`。该模式不加载
项目文件或 Editor Layer：`CookedPlayerLayer` 只接收包路径，挂载包、读取包头中的启动场景
Handle，再调用 `SceneSerializer::Deserialize(AssetHandle)` 启动运行时场景。没有有效启动场景
Handle、包索引损坏或资源类型不匹配时会直接拒绝运行，不会回退到源文件路径。
Player 模式也不会创建 ImGui 层；Renderer2D 的基础 shader 已内嵌到程序，因此运行时不需要
Editor 的 `Packages/fonts`、`Packages/Shaders` 或系统字体。相对包路径以可执行程序目录解析，
便于启动器从任意工作目录运行；包缺失、损坏或启动场景无效会返回非零进程退出码。
