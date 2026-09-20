# PhysicsPlayground

中文文档 · 核对日期：2026-09-20 · [文档索引](../../docs/README.md)

一个用于体验 TomCat 2D 物理系统的最小示例：倾斜矩形在重力作用下落到静态地板上，
展示 Sprite、Transform、动态刚体与碰撞体之间的配合。

项目包含场景、资源与项目设置，运行缓存由 Editor 自动生成。
效果预览见 [引擎功能展示](../../README.zh-CN.md#功能展示)。

1. 编译并运行当前 TomCat Editor（Windows x64 / Release）：`Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe`。
2. 使用 **File → Open Project** 选择本目录的 `Project.tcproj`。
3. 在 Project 面板打开 `Assets/Scene/sample.tomcat`。
4. 点击 **Play**：矩形下落并落在地板上。使用 **Pause / Step** 检查运行状态，
   再点击同一个 **Stop** 按钮返回编辑场景。

| 实体 | 编辑状态配置 |
| --- | --- |
| MainCamera | 正交相机，天蓝色背景 |
| Square | 位置 `(0, 1.5, 0)`、Z 轴旋转 `25°`、缩放 `(1.4, 1, 1)`；Sprite Renderer、动态 Rigidbody2D 和 BoxCollider2D |
| Ground | 位置 `(0, -1, 0)`、缩放 `(3, 0.3, 1)`；Sprite Renderer、BoxCollider2D，运行时使用隐式静态刚体 |

场景格式为 schema 11，项目格式为 schema 4。此演示不需要 C# 脚本。
入口场景 Handle 为 `12007672766582721512`，由 `ProjectSettings/BuildSettings.json` 保存；
资源及 `.tcmeta` 应一起保留，避免引用身份变化。

## 当前桌面录屏与检查点

![2026-09-20 实际物理播放控制](../../docs/portfolio/2026-09-20/05-play-controls.gif)

2026-09-20 从 File → Open Project 打开本项目，观察到矩形下落、接地、停稳，
再执行 Pause / Step / Stop；停止后位置恢复为 Y=1.5、Z 轴旋转恢复为 25°。
Step 点击时物体已经停稳，这段画面不用于证明单步位移量。

- 选择 Project 中的 `sample`，在主 Inspector 查看 Scene 类型与实体摘要。
- 选择 Packages 中的图片，在单列列表查看真实缩略图，Inspector 显示只读导入信息与预览。
- 使用 **Window → Layouts → Debugging**，或 **Window → Panels → Console / Profiler** 打开诊断面板。
- Console 搜索 `TCSP1000` 查看脚本编译成功信息；Profiler 开启录制、停止后选帧查看 Hierarchy / Timeline。

本次性能录屏采样的是编辑模式，且 OBS 同时运行；不作为物理性能基准。
Hub 片段仅展示模板选择，没有可识别的 Editor 安装。原片来源与旧录制问题见
[录制档案](../../docs/portfolio/README.md)，Profiler 能力与限制见[性能指南](../../docs/DEBUGGING_AND_PROFILING.md)。

## 在浏览器中复现

Web CMake 目标将本目录预加载到 `/Samples/PhysicsPlayground`。
按 [Web 中文指南](../../Web/README.zh-CN.md)构建后，可调用
`tc_web_player_cook_sample()` 在 MEMFS 生成 `/PhysicsPlayground.tcpak`，
用 `FS.readFile` 读取，再传给 `tc_web_player_boot`。
该流程使用真实 Cook、TCPAK 读取、Renderer2D 与 Box2D；浏览器宿主仍需提供画布、帧循环与隔离响应头。

编辑器协议回归入口为 `node Web/tests/editor-rpc.cjs build/web`，需先生成 Web 编辑器模块。
此命令不检验画面；浏览器中应另行观察下落、接地、停止和重新启动。
此示例没有覆盖 C#、音频、第三方 Shader 或全部输入设备。
