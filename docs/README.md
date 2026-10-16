# TomCat 中文文档索引

核对日期：2026-10-16。本文覆盖仓库自有文档及依赖集成说明；`vendor/`、`TomCat/vendor/` 内的上游原始资料及许可证按原项目维护。

| 文档 | 中文版本 | 英文版本 |
| --- | --- | --- |
| 项目介绍、构建与功能范围 | [项目总览](../README.zh-CN.md) | [Overview](../README.md) |
| 项目配置、资产、场景、迁移与发布 | [项目系统](../PROJECT_SYSTEM.md) | — |
| C# 脚本、原生 ABI 与生命周期 | [托管脚本](../Managed/README.zh-CN.md) | [Managed scripting](../Managed/README.md) |
| NuGet 还原、项目引用与托管依赖加载 | [C# 项目依赖](CSHARP_DEPENDENCIES.md) | — |
| 无界面 Cook 与 Player 构建 | [TomCatCLI](../Tools/TomCatCLI/README.zh-CN.md) | [TomCatCLI](../Tools/TomCatCLI/README.md) |
| 浏览器构建、编辑接口与限制 | [Web 指南](../Web/README.zh-CN.md) | [Web guide](../Web/README.md) |
| 浏览器中文字形与字体来源 | [字体说明](../Web/fonts/README.zh-CN.md) | [Font notes](../Web/fonts/README.md) |
| 2D 物理示例 | [PhysicsPlayground](../Samples/PhysicsPlayground/README.md) | — |
| 完整游戏、存档与独立 Player 示例 | — | [CoinRunner](../Samples/CoinRunner/README.md) |
| Editor 结构化操作、输入测试与截图 | [原生自动化](Automation.md) | [工具协议目录](AutomationTools.json) |
| 桌面实机录制、历史问题与复现 | [录制说明](portfolio/README.md) | — |
| Prefab、场景、调试与 UI 目标及交付边界 | [能力开发方案](PRODUCT_CAPABILITIES_PLAN.zh-CN.md) | — |
| 关联 Prefab、覆盖、嵌套和变体 | [Prefab 工作流](PREFAB_WORKFLOW.zh-CN.md) | — |
| 异步加载、叠加、持久对象与显式卸载 | [场景流式加载](SCENE_STREAMING.zh-CN.md) | — |
| 性能采集、资源统计与 C# 断点流程 | [调试与性能分析](DEBUGGING_AND_PROFILING.md) | — |
| 滑条、滚动、输入、主题与游戏本地化 | [运行时 UI 控件](RUNTIME_UI_PRODUCT.zh-CN.md) | — |
| 分帧激活、国际化、2D 制作与媒体生产 | [P1 使用与边界](P1_PRODUCTION.md) | — |
| 编辑器面板、资源检查器与界面约定 | [UI 与交互记录](EDITOR_UX_REWORK.md) | — |
| RHI 分层、Vulkan 后端、运行与 GPU 回归 | [RHI 与 Vulkan](RHI_VULKAN.zh-CN.md) | — |

最新主线集成验收：[2026-10-16 RHI/Vulkan 与跨平台验证](RHI_VULKAN.zh-CN.md#主线集成2026-10-16)。此前的 HUD、字体与构建配置验收见[生产验收与构建配置](PRODUCTION_ACCEPTANCE.md)。

## 当前主线

产品版本为 **0.6.0**。`dev_vulkan` 已合入 `main`；桌面默认 OpenGL 4.6，可在进程启动前设置 `TC_RENDERER=vulkan` 选择 Vulkan 1.2。CLI 烘焙与 Player 启动必须选择同一后端。浏览器仍使用 WebGL2，主线已包含 .NET browser-wasm C# 宿主、Project 编辑与场景持久化。

2026-10-16 的完整 Release 套件（Managed、14 个原生回归、应用构建、模板、Player、CoinRunner、模块发布、Editor 自动化）、Vulkan 同步验证/多窗口/Editor/Player，以及托管 Web 构建和 Edge 浏览器冒烟均通过。硬件范围、日志与未重跑项目见上方集成记录；这些结果不等同新录屏、全部设备验收或性能结论。

## 最新桌面演示（2026-09-20）

[录制说明](portfolio/README.md)收录 5 段新 GIF：Hub 模板、资源 Inspector、Console 搜索、
Profiler 与物理播放控制，另有 3 张截图。[capture.json](portfolio/2026-09-20/capture.json)
记录源码提交、运行程序与原片 SHA-256、裁剪和剪辑区间；重导脚本支持按日期清单导出。
旧的 2026-09-15 素材仍保留作历史记录。2026-10-16 的 README 更新同步当前源码、构建和测试入口，未重新录制或改变历史素材的验收范围。

## 版本依据

产品和格式版本以 [Version.h](../TomCat/src/TomCat/Core/Version.h) 为准。
当前产品版本为 `0.6.0`，项目写入 v4、场景写入 v11、Prefab 写入 v1、TCPAK 写入 v8；
只接受各自当前格式，不读取旧版或未来版本；Native API 为 v1、Managed API 为 v6、Module ABI 为 v2、脚本清单为 v1。
TCPAK v6 引入 BootManifest，v7 在索引中增加逐条目 SHA-256 摘要，v8 增加原生模块发布载荷。

## 验证入口

以下命令从仓库根目录按顺序执行，先按对应构建指南安装工具和依赖。不要在原生/脚本回归运行时并行重建 Managed 或覆盖测试程序的 DLL。

```powershell
# Windows：托管、14 个原生程序（含 OpenGL RHI）、应用/发布与自动化回归
$env:TC_RENDERER = 'opengl'
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1 -Configuration Release -Log

# Vulkan：真实 GPU、同步验证和 ImGui 多窗口；需要 Khronos validation layer
powershell -ExecutionPolicy Bypass -File Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport

# Vulkan Editor 与真实 C# Player（使用上面已构建的 Release 产物）
$env:TC_RENDERER = 'vulkan'
$env:TC_VULKAN_VALIDATION = '1'
./Scripts/Run-AutomationRegression.ps1 -Configuration Release -SkipBuild
./Scripts/Run-CSharpPlayerSmoke.ps1 -Configuration Release -RequirePlayer
$env:TC_RENDERER = 'opengl'
Remove-Item Env:TC_VULKAN_VALIDATION

# Web：完整托管发布与真实浏览器生命周期冒烟
./Scripts/Build-WebManaged.ps1
npm ci --prefix Web/tests --ignore-scripts
node Web/tests/node_modules/playwright/cli.js install chromium
./Scripts/Run-WebBrowserSmoke.ps1

# 可选：5000 实体的 10 分钟帧耗时/内存验收（本次合并未重跑）
./Scripts/Run-ProductionSoak.ps1 -DurationSeconds 600
```

Windows CI 使用 [.github/workflows/regressions.yml](../.github/workflows/regressions.yml)。
若使用本机 Edge，可设置 `$env:TOMCAT_BROWSER_CHANNEL = 'msedge'`，无需下载测试 Chromium；仍需安装 `Web/tests` 的 Playwright 依赖。
Web 协议回归不验证浏览器绘制、宿主持久化、中文输入法或真实手柄，需另做浏览器验收。
演示文档中的日期、截图、Hash 与验收结果属于当时的记录，不能作为后续提交的测试结果。

## 文档维护

新增项目文档应提供完整中文正文；已有英文版时使用同目录 `README.zh-CN.md` 并添加双向语言链接。
已有纯中文文档沿用原路径。更改格式版本、CLI 参数或平台范围时，同步更新两种语言及本索引。
API 名称、字段大小写、命令参数和路径保持源码原文。
