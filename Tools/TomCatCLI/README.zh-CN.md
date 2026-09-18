# TomCatCLI 无界面构建工具

[English](README.md) | 简体中文 · 核对日期：2026-09-18 · [文档索引](../../docs/README.md)

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

## 参数与迁移

| 参数 | 适用命令 | 行为 |
| --- | --- | --- |
| `--project <路径>` | 两者 | 必填，指定 `.tcproj`；含空格的路径需加引号。 |
| `--output <路径>` | 仅 `cook` | 默认写入 `<项目>/Build/Game.tcpak`。 |
| `--template <目录>` | 仅 `build` | 指定 Player 模板；发布位置仍为项目的 Build 目录。 |
| `--migrate` | 两者 | 明确允许执行预览中的项目事务升级。 |

默认只预览迁移需求并打印受影响文件；检查列表后加 `--migrate` 才允许升级。
若迁移已中断，须在 Editor 中检查并恢复，CLI 不自动修复恢复日志。
两个命令均获取项目写锁，运行前应关闭占用同一项目的 Editor 或其他 CLI。
当前输出 TCPAK v7，包含逐条目 SHA-256 摘要；读取兼容 v5/v6/v7。

## 退出码

| 退出码 | 含义 |
| --- | --- |
| 0 | 执行成功或显示帮助 |
| 2 | 参数错误 |
| 3 | 项目或中断迁移检查失败 |
| 4 | 需要明确批准项目迁移 |
| 5 | 项目加载失败 |
| 6 | 资产注册表初始化失败 |
| 7 | 未配置已启用的入口场景 |
| 8 | 托管编译失败或编译期间源码发生变化 |
| 9 | Cook 失败 |
| 10 | 无法读取新编译的托管元数据 |
| 11 | 无法读取程序集文件 |
| 12 | Player 构建失败 |
| 13 | 项目写锁不可用 |
| 14 | 中断迁移需要在 Editor 中处理 |

进一步阅读：[项目配置与迁移](../../PROJECT_SYSTEM.md)、[托管脚本](../../Managed/README.zh-CN.md)。
