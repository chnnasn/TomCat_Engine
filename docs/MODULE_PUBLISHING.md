# 原生模块发布

工程中的 `Modules/<name>/module.tomcat` 与原生 DLL 会通过 CLI cook/build 和编辑器 PlayerBuilder 进入 TCPAK v8。桌面 Player 校验包后，在独立临时目录恢复模块，按依赖顺序初始化，再加载场景。发布物不依赖原工程目录。

```yaml
ModuleVersion: 1
Name: Weather
DisplayName: Weather Module
Version: 1.0.0
EngineBuildID: TomCat-0.4.0
Library: lib/Weather.dll
Enabled: true
Runtime: true
Dependencies: []
RuntimeFiles: []
```

`Dependencies` 填其他模块的 Name；`RuntimeFiles` 填与 Library 同目录的附属 DLL 相对路径，例如 `lib/WeatherMath.dll`。这些文件需要由模块作者提前编译并放入工程；发布流程不负责构建模块源码。只在作者环境使用的模块设置 `Runtime: false`。

模块入口检查 `context->Size`，然后根据 `context->Host` 区分 Editor、Tool 和 Player。所有宿主均可注册组件；Player 的导入器和编辑器命令接口返回 ModuleStatusUnavailable，入口应跳过这些注册。API 示例见 `Tests/TestModule/src/TestModule.cpp`。

```powershell
TomCatCLI.exe cook --project C:\Game\Project.tcproj --output C:\Game\Build\Game.tcpak
TomCatCLI.exe build --project C:\Game\Project.tcproj --template C:\Engine\PlayerTemplate
# build 输出：工程 Build/<productName>/，直接运行其中的游戏 exe。
powershell -ExecutionPolicy Bypass -File Scripts\Run-ModulePublishSmoke.ps1
```

启用模块初始化失败、清单版本不匹配、缺失/循环依赖、附属文件缺失或输入在 Cook 期间变化，都使发布失败。已有 TCPAK 使用原子替换保留；包损坏在 DLL 加载之前拒绝。组件/脚本场景销毁后再卸载 DLL，随后清理临时目录。

当前边界：原生模块使用引擎相同头文件、MSVC 工具链和第三方依赖；非空 EngineBuildID 必须匹配，发布时未固定的清单也会固定到当前引擎。Windows x64 Release 已验证；Web 不支持原生 DLL 模块。本功能不包含模块源码编译、包仓库、签名分发或运行期间替换已加载 DLL。

2026-10-01 验证：`Scripts/Run-Regressions.ps1 -Log` 全量通过（记录 `build/logs/regressions-20261001-110733.log`）。模块回归另外覆盖附属 DLL 字节恢复、编辑器专用模块排除、缺失文件拒绝；发布冒烟覆盖真实 CLI build、隐藏作者目录后的 Player 启动、Weather 属性 2.5/3 恢复、CoinRunner WIN/exit 0、损坏模块载荷拒绝和失败 Cook 保留原包。
