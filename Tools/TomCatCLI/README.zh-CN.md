# TomCatCLI 无界面构建工具

[English](README.md) | 简体中文 · 核对日期：2026-09-20 · [文档索引](../../docs/README.md)

`TomCatCLI` 是 CI 资源 Cook 和 Windows 独立 Player 构建的无窗口入口。
以下源码构建命令从仓库根目录、Visual Studio 开发者终端执行；先运行
`Scripts/Setup.bat` 准备 Premake，并安装 .NET 10 SDK 供项目 C# 编译使用。

```powershell
vendor/premake/bin/premake5.exe --file=Tools/premake5.lua vs2022
msbuild Tools/Tools.sln -p:Configuration=Release -p:Platform=x64
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project C:/Game/Game.tcproj --output C:/Game/Build/Game.tcpak
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe build --project C:/Game/Game.tcproj
```

两个命令都会重新执行托管 Release 编译、校验资产，并使用项目 Build Settings 中已启用的入口场景。
`cook` 写入资源包；`build` 还将新编译的托管程序集和脚本清单传给 PlayerBuilder，发布独立 Player。
源码构建可使用 `Scripts/Build-PlayerTemplate.ps1` 生成模板，再通过 `build --template <目录>` 指定。

## 打包版入口

官方及本地 Editor 发布物的单个 `TomCat.exe` 内含 CLI、Managed 工具链和 Player Template：

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
| 6 | 资产注册表初始化失败 |
| 7 | 未配置已启用的入口场景 |
| 8 | 托管编译失败或编译期间源码发生变化 |
| 9 | Cook 失败 |
| 10 | 无法读取新编译的托管元数据 |
| 11 | 无法读取程序集文件 |
| 12 | Player 构建失败 |
| 13 | 项目写锁不可用 |

进一步阅读：[项目配置](../../PROJECT_SYSTEM.md)、[托管脚本](../../Managed/README.zh-CN.md)。

## 从编辑器创作交接到构建

Project 资源检查器中的 Apply 先保存导入设置；场景编辑也需保存到磁盘，CLI 不读取尚未保存的界面草稿。
在 `ProjectSettings/BuildSettings.json` 中设置已启用的入口和其他构建场景；
运行时 Single / Additive 加载仍受该列表约束，见[场景加载指南](../../docs/SCENE_STREAMING.zh-CN.md)。

[PhysicsPlayground](../../Samples/PhysicsPlayground/README.md) 的场景位于
`Assets/Scene/sample.tomcat`。关闭占用它的 Editor 后，可从仓库根执行：

```powershell
Tools/bin/Release-windows-x86_64/TomCatCLI/TomCatCLI.exe cook --project Samples/PhysicsPlayground/Project.tcproj
```

本页命令按当前源码核对；[2026-09-20 桌面录屏](../../docs/portfolio/README.md)只演示 Hub/Editor，
未重新执行 CLI Cook 或独立 Player 构建验收。完整构建回归入口见[主文档](../../README.zh-CN.md#构建)。

## 脚本构建配置

`cook` / `build` 默认启用正式版 C# 优化，追加 `--development` 使用未优化开发配置。两者缓存隔离并保留 portable PDB；符号不自动嵌入 Player 包。详见[生产验收](../../docs/PRODUCTION_ACCEPTANCE.md)。
