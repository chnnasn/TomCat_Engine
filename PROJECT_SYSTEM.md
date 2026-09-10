# TomCat Engine 项目系统

## 文件与职责

每个项目使用根目录下的 `Project.tcproj`。该文件是可纳入版本控制的 YAML 项目配置；Hub 的最近打开时间、已知项目列表和本机目录等用户状态不写入项目文件，而是保存在运行目录 `imgui.ini` 的 `[HubConfig]` 段中。

- `Project`：创建、加载、校验、保存及重新加载项目配置。
- `ProjectManager`：扫描项目、维护 Hub 本地状态，并启动对应版本的 Editor。
- Builder (`Manager.exe`)：项目列表、创建/添加/移除项目和 Editor 版本选择。
- Editor (`TomCat.exe`)：加载项目、按项目模板配置编辑器，并打开启动场景。

## Project.tcproj schema v2

```yaml
SchemaVersion: 2
Project:
  Name: MyGame
  Version: 1.0.0
  Description: A sample game project
  EditorVersion: 1.0.0
  Template: 3D
  AssetDirectory: Assets
  StartScene: Scenes/Main.tomcat
  TwoColumnCurrentFolder: ""
  ExpandedNodes: []
```

字段约束：

- `Name` 必须非空。
- `Template` 只能是 `2D` 或 `3D`，且是项目模式的唯一真源；Builder 不再向 Editor 传递额外模式参数。
- `AssetDirectory` 必须是项目内的非空相对路径。
- `StartScene` 必须是相对于 `AssetDirectory` 的安全相对路径，不能使用绝对路径或 `..` 跳出资源目录。
- Content Browser 的当前目录和展开节点加载时会限制在项目资源目录内；无效旧路径会被忽略。
- `LastOperationTime` 是 schema v1 的旧字段。加载时可迁移到 Hub 本地状态，但 schema v2 保存时不再写出。

加载器接受没有 `SchemaVersion` 的 schema v1 文件，以兼容已有项目；高于当前版本的 schema 会被拒绝，避免旧程序覆盖新格式。显式保存时会更新上述已知字段，同时保留文件中的未知扩展字段。仅加载、扫描或打开项目不会重写 `Project.tcproj`。

项目文件先写入同目录临时文件；Windows 使用带同目录恢复备份的 `ReplaceFileW` / `MoveFileExW` 原子安装，其他平台使用同文件系统原子重命名，保存结果通过 `bool` 返回给调用方。安装失败时保留已完整写入的临时文件；Windows 若替换中途失败会先尝试恢复原目标，恢复失败则同时保留临时文件和备份，避免清理流程造成二次数据丢失。创建项目会拒绝已有的非空目标目录；若首次保存失败，只逆序删除本次确实创建且仍为空的目录，不递归删除并发出现的内容。

## 项目结构

```text
MyGame/
├── Project.tcproj
└── Assets/
    ├── Scenes/
    │   └── Main.tomcat
    ├── Scripts/
    └── ...
```

## 启动流程

1. Builder 加载并严格校验 `Project.tcproj`。
2. Builder 使用 Unicode 版 `CreateProcessW` 启动 Editor，仅传递项目文件路径；带空格、非 ASCII 字符和 Windows 长路径的参数会被正确引用。
3. Editor 重新加载项目，从 `Project.Template` 决定 2D/3D 模式。
4. Editor 将 `AssetDirectory / StartScene` 作为启动场景；不存在或无效时保留空白编辑场景并报告错误。
5. Editor 启动成功后，Builder 才在本地 Hub 配置中记录最近打开时间。

## 场景文件 schema v2

新保存的 `.tomcat` 场景包含顶层 `SchemaVersion: 2`、`SceneName` 和 `Entities`。实体关系只写 `Parent`，不再写旧的 `m_Father` / `m_Children` 双份关系；加载器仍可读取 schema v1 场景。

Sprite Renderer 会保存 `Color`、`TilingFactor` 以及可选的 `TexturePath`。纹理路径优先相对于场景文件保存，加载时也以场景目录解析。实体 UUID 必须非零且唯一，父子关系会校验缺失引用、多父节点和环。

场景保存使用与项目文件相同的原子替换并返回 `bool`。反序列化先构造临时场景，只有全部字段和关系校验成功后才替换当前场景，因此损坏或不受支持的文件不会留下半加载状态。变换、相机、颜色和碰撞体数值会检查有限值及有效范围，非法物理参数不会进入 Box2D。

实体变换只存储平移、旋转和缩放（TRS），不保存剪切矩阵。层级重挂、世界/局部变换更新和层级同步会先验证整棵受影响子树；若矩阵无法无损分解为有限 TRS（例如非均匀缩放叠加错位旋转产生剪切），操作整体失败并保留原状态，避免静默近似造成累计漂移。

## 兼容边界

- 支持读取 schema v1 项目和场景；下一次显式保存只输出当前 schema 的规范字段。
- 不再提供未实现的运行时场景序列化 API。
- 项目打开历史属于本机 UI 状态，不应提交到项目仓库。
- Hub 的“移除项目”只移出列表，不删除磁盘文件；被移除路径保存在 `IgnoredProjects`，默认目录扫描不会自动把它重新加入，用户显式添加、创建或加载时解除忽略。
- `HubConfig.tomcat` 仅用于一次性迁移；当前 Hub 配置写入 `imgui.ini`。
