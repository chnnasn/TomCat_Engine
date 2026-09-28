# TomCat Engine — main_web

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档更新：**2026-09-28**。下方验证进度引用已有记录，本次合并未重新运行完整引擎测试套件。

## 当前分支的作用

用于 Emscripten/WebGL2 目标、复用原生 ImGui 编辑面板和浏览器宿主集成，包括 Scene 工具栏与 Game 视图行为；持久化由宿主负责。构建和接入见 [Web 指南](Web/README.zh-CN.md)。需要 SharedArrayBuffer/Workers，不支持 C# 负载。本分支在 **2026-09-28** 与 `main` 同步，同时保留 Web 专属历史。

## 当前进度

- [x] Emscripten/WebGL2 模块复用原生 Player Runtime、TCPAK 读取、2D 场景和 ImGui 编辑面板，文件选择与持久化由浏览器宿主负责。
- [x] 共享可拖动 Q/W/E/R Scene 工具与 Pivot/Center、Local/Global 控件；Game 在 Play 前和 Stop 后也显示主相机，localStorage 可用时保存工具栏位置。
- [x] 已合入最新桌面主线，包括结构化 C# 诊断、portable PDB，以及供原生 Editor/Player 使用的纯托管 NuGet 包、项目引用、传递依赖和本地 DLL 封装；Web 端仍拒绝 C# 负载。
- [x] 打包后的桌面 Editor 自带固定私有 .NET 10 SDK，不再依赖机器上的全局 C# 环境；浏览器运行时在本分支单独接入。

## 使用与开发入口

- [Web 构建、宿主 API 与回归指南](Web/README.zh-CN.md)：需要 Emscripten、CMake、Ninja 和 Node.js，构建输出为模块，并非完整托管网站。
- 宿主必须支持 WebGL2、SharedArrayBuffer/Workers，并配置指南中的 COOP/COEP 响应头；接入和测试入口位于 [Web](Web/README.zh-CN.md)。

## 验证进度

Web 指南记录了 2026-09-16 PhysicsPlayground 浏览器 Cook/加载/渲染/物理及非法包拒绝，以及 2026-09-18 工具栏与空闲 Game 行为。验证范围限于示例和记录流程，不代表任意桌面游戏兼容；Windows CI 不运行 Web 构建/WASM 测试。

## 已知限制与后续工作

- 不支持 C# 负载、可听 WebAudio、自定义 Cooked SPIR-V 着色器、多重采样 Framebuffer 或单线程回退，Web 编辑仍限定为 2D。
- 宿主持久化及更广泛的设备/控件/输入法验收仍需接入工作；后续桌面改动仍需显式合并。
