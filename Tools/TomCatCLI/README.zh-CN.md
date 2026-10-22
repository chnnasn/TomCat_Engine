# TomCatCLI 无界面构建工具

[English](README.md) | 简体中文 · 核对日期：2026-10-22 · [文档索引](../../docs/README.md)

`TomCatCLI` 是 CI 资源 Cook 和 Windows/Linux 原生 Player 构建的无窗口入口。
各平台构建本机平台的 Player，尚不提供跨平台交叉导出。
以下 Windows 源码构建命令从仓库根目录、Visual Studio 开发者终端执行；先运行
`Scripts/Setup.bat` 准备 Premake，并安装 .NET 10 SDK 供项目 C# 编译使用。

```powershell
vendor/premake/bin/premake5.exe --file=Tools/premake5.lua vs2022
msbuild Tools/Tools.sln -p:Configuration=Release -p:Platform=x64
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj
```

两个命令都会重新执行托管编译，默认使用 Production 配置，校验资产，并使用项目 Build Settings 中已启用的入口场景。
`cook` 写入资源包；`build` 还将新编译的托管程序集和脚本清单传给 PlayerBuilder，发布独立 Player。
源码构建可使用 `Scripts/Build-PlayerTemplate.ps1 -Build` 生成模板，再通过 `build --template <目录>` 指定。

## Linux 构建与导出

Linux 支持已进入 `main`。先按 [Linux 指南](../../docs/LINUX.zh-CN.md)安装 .NET 10 SDK 与系统依赖，再从仓库根目录执行：

```bash
bash Scripts/Linux_Build.sh
TC_RENDERER=opengl ./build/linux/bin/CLI/TomCatCLI cook --project /absolute/Game/Game.tcproj
TC_RENDERER=opengl ./build/linux/bin/CLI/TomCatCLI build --project /absolute/Game/Game.tcproj \
  --template ./build/linux/bin/Editor/Packages/PlayerTemplates/linux-x64
```

构建脚本同时生成模板。导出在项目 `Build/<ProductName>/` 下写入可执行 ELF、`Game.tcpak`、Managed 程序集和私有 .NET Runtime；目标机器需要兼容的系统库，无需 .NET SDK。Linux 使用 OpenGL 4.5+；Vulkan 探测和 Windows 单文件 Editor 包装器不属于当前 Linux 范围。测试证据及验收游戏下载见 [Linux CI](../../docs/LINUX.zh-CN.md#ci-与验收)。

## Windows 打包版入口

Windows 官方及本地 Editor 发布物的单个 `TomCat.exe` 内含 CLI、Managed 工具链和 Player Template：

```powershell
TomCat.exe --cli cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
TomCat.exe --cli build --project C:/Game/Game.tcproj
```

在 PowerShell 中执行当前目录的程序时使用 `./TomCat.exe`，或指定完整路径。
包装器验证内嵌清单，在 `%LOCALAPPDATA%\TomCat\Editor\Runtime` 下原子释放或复用匹配的运行时，
并返回 CLI 子进程的退出码。源码构建仍可直接运行 `Tools/bin` 下的程序。

## 参数与格式

| 参数 | 适用命令 | 行为 |
| --- | --- | --- |
| `--project <路径>` | 两者 | 必填，指定 `.tcproj`；含空格的路径需加引号。 |
| `--output <路径>` | 仅 `cook` | 默认写入 `<项目>/Build/Game.tcpak`。 |
| `--template <目录>` | 仅 `build` | 指定 Player 模板；发布位置仍为项目的 Build 目录。 |
| `--development` | 两者 | 使用未优化的 Development C#，代替默认优化的 Production C#。 |

使用 `TomCatCLI help`（或 `--help` / `-h`）查看用法；相对路径按调用者当前工作目录解析。

仅支持当前格式，旧格式和未来版本均拒绝；不自动迁移或修改原文件。
旧项目请使用之前的引擎版本处理后再打开。
两个命令均获取项目写锁，运行前应关闭占用同一项目的 Editor 或其他 CLI。
TCPAK 只读写 v8，逐条目 SHA-256 摘要为必需字段。

## 退出码

| 退出码 | 含义 |
| --- | --- |
| 0 | 执行成功或显示帮助 |
| 2 | 参数错误 |
| 5 | 项目加载失败 |
| 6 | 项目模块或资产注册表初始化失败 |
| 7 | 未配置已启用的入口场景 |
| 8 | 托管编译失败或编译期间源码发生变化 |
| 9 | Cook 失败 |
| 10 | 无法读取新编译的托管元数据 |
| 11 | 无法读取程序集或 PDB 文件 |
| 12 | Player 构建失败 |
| 13 | 项目写锁不可用 |

进一步阅读：[项目配置](../../PROJECT_SYSTEM.md)、[托管脚本](../../Managed/README.zh-CN.md)。

## 渲染后端选择

桌面 Editor、Hub、CLI 与 Player 在每个进程中读取一次 `TC_RENDERER`。
Windows 未设置或设为 `auto` 时优先 Vulkan，不支持才回退 OpenGL；设为 `opengl` / `vulkan`
时强制对应后端，强制 Vulkan 不可用时明确报错。无窗口 CLI 不创建窗口也能探测设备支持。
Linux 使用 `TC_RENDERER=opengl`，Vulkan 探测尚未移植。Windows 示例：

```powershell
$env:TC_RENDERER = 'auto'
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj --template C:/Game/PlayerTemplate
```

新的桌面 cook/build 产物包含 OpenGL 与 Vulkan 两种预编译 Shader，Player 可按运行机器
独立选择后端；包中不附带 Shader 源码。旧单后端包仍需使用原后端，或重新烘焙。
Vulkan 运行需要支持 Vulkan 1.2 的驱动和 loader，自动选择会在缺少这些支持时安全回退。
发布前需执行 `Scripts/Build-PlayerTemplate.ps1 -Build` 重建模板。构建器要求 Player Template
格式 2，拒绝无法读取新 Shader bundle 的旧模板；TCPAK 仍为版本 8。
验证层和设备选择见[RHI/Vulkan 集成指南](../../docs/RHI_VULKAN.zh-CN.md)。
浏览器目标继续使用 WebGL 2，独立构建入口见[Web 指南](../../Web/README.zh-CN.md)。

## 从编辑器创作交接到构建

Project 资源检查器中的 Apply 先保存导入设置；场景编辑也需保存到磁盘，CLI 不读取尚未保存的界面草稿。
在 `ProjectSettings/BuildSettings.json` 中设置已启用的入口和其他构建场景；
运行时 Single / Additive 加载仍受该列表约束，见[场景加载指南](../../docs/SCENE_STREAMING.zh-CN.md)。

[PhysicsPlayground](../../Samples/PhysicsPlayground/README.md) 的场景位于
`Assets/Scene/sample.tomcat`。关闭占用它的 Editor 后，可从仓库根执行：

```powershell
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project Samples/PhysicsPlayground/Project.tcproj
```

2026-10-16 主线集成已通过完整 Release 回归、CLI 与 Player 构建、Player Template 和 C# Player 冒烟。
Vulkan 烘焙/构建及真实 Player 脚本也已通过，预期测试退出码为 73。
范围与日志见[集成记录](../../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16)，
完整构建回归入口见[主文档](../../README.zh-CN.md#构建)。
构建与回归脚本应串行运行，避免在测试中替换托管编译输入。

验证真实图形 Player 时，先按当前源码重新构建 Managed Release、`ScriptCompilerRegression` 和 `TomCatCLI`，再执行：

```powershell
./Scripts/Build-PlayerTemplate.ps1 -Configuration Release -Build -Destination ./Tests/bin/Release-windows-x86_64/PlayerTemplateRegression
./Scripts/Run-CSharpPlayerSmoke.ps1 -Configuration Release -RequirePlayer -Graphics
```

默认冒烟是无窗口模式。`-Graphics` 会打开真实图形窗口，对同一包先验证默认后端，再用不存在的 Vulkan 设备筛选验证自动回退 OpenGL，并检查游戏日志中的实际后端和 C# 脚本完成退出码 73。

## 脚本构建配置

`cook` / `build` 默认启用 Production C# 优化，追加 `--development` 使用未优化 Development 配置。
两种配置在 Library 中分别缓存并保留 portable PDB。`cook` 将所选构建的 PDB 写入托管包负载；
`build` 当前不将它写入发布的 Player 包。详见[生产验收](../../docs/PRODUCTION_ACCEPTANCE.md)。
