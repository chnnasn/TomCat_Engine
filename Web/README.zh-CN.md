# 实验性 Web Player 与 ImGui 编辑器

[English](README.md) | 简体中文 · 核对日期：2026-10-16 · [文档索引](../docs/README.md)

此目标在浏览器中运行已有的 `PlayerRuntimeLayer`、Cooked TCPAK 读取器、
场景运行时、Renderer2D、Box2D 和 TomCat C# 脚本 ABI。最终模块由 .NET
`browser-wasm` 链接，Emscripten C++ 引擎静态库与托管代码共享同一块 WASM 内存。
站点产物内含 .NET 运行时与 Roslyn 编译器，Player 和 Web Editor 用户无需安装本地 .NET。

桌面 RHI 与 Vulkan 后端现已合入 `main`；浏览器目标仍使用 WebGL2/GLES3。
桌面 Vulkan 验证不能作为浏览器支持证据。

[2026-09-20 功能录屏](../docs/portfolio/README.md)来自 Windows 桌面程序。
共享源码不代表这些片段验证了浏览器端。2026-10-16 在 Vulkan 集成后再次通过完整托管发布及本机 Edge 冒烟；
历史记录保留原始日期，范围与待验收项见[主线集成记录](../docs/RHI_VULKAN.zh-CN.md#主线集成2026-10-16)
与[生产验收](../docs/PRODUCTION_ACCEPTANCE.md)。

## 构建

构建机需要 .NET 10 的 `wasm-tools` 工作负载、Emscripten、CMake 3.20+ 与 Ninja；
这些只是发行构建工具，不会要求浏览器客户端安装。脚本优先使用 PATH 中的 `emcmake`，
未找到时会定位 `wasm-tools` 提供的 Windows Emscripten SDK。从仓库根目录执行：

```powershell
git submodule update --init TomCat/vendor/Box2D TomCat/vendor/glm TomCat/vendor/spdlog TomCat/vendor/ImGuizmo
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Build-WebManaged.ps1
```

脚本检查依赖、构建原生静态库，并将完整 `browser-wasm` 站点发布到
`build/web-managed`，原生构建产物位于 `build/web-managed-native`。
它不包含项目持久化服务，宿主页仍负责 Canvas、导航和文件保存。
通过 localhost HTTP 或 HTTPS 提供全部产物；pthread Worker 要求响应包含：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

以模块方式加载 `main.js`，等待 `tomcat-web-ready` 事件后使用
`TomCatWeb.engine.PlayerBoot(width, height, tcpakBytes)`。JS 导出会固定字节数组并调用原 Player 层。

| C 导出函数 | 约定 |
| --- | --- |
| `tc_web_player_boot(width, height, bytes, size)` | 复制并校验 TCPAK，激活原 Player 层；返回 0 表示成功。资源包最大 256 MiB，视口每边最大 8192 像素。 |
| `tc_web_player_frame(deltaSeconds)` | 由 requestAnimationFrame 驱动；模拟沿用引擎固定时间步。 |
| `tc_web_player_resize(width, height)` | 调整画布和视口，并发送引擎窗口尺寸事件。 |
| `tc_web_player_error()` | 返回 UTF-8 诊断；启动后及每帧后检查。 |
| `tc_web_player_shutdown()` | 停止场景并释放应用资源。 |
| `tc_web_player_stats()` | 返回 JSON 诊断，包括帧数、绘制次数、刚体数和示例 Square 高度。 |
| `tc_web_player_cook_sample()` | 开发用入口，调用真实 Cooker，在 MEMFS 中写入 `/PhysicsPlayground.tcpak`。 |

运行示例时先调用 Cooker，再用 `module.FS.readFile` 读取文件，将字节传给启动函数。
销毁时调用 shutdown 和 `module.PThread.terminateAllThreads()`；后续每次运行创建新的模块和画布。
此实验目标目前捆绑示例源资源及开发用 Cooker。

## 原生编辑接口

同一个托管模块通过 `TomCatWeb.engine` 导出 Web Editor 入口，并编译已有的
`SceneHierarchyPanel`（含 Inspector）、`ContentBrowserPanel`、
`ConsolePanel`、`ImGuiLayer` 主题/图标和 ImGuizmo。浏览器宿主负责导航、文件选择与持久化；
面板直接来自 C++。Web 专用层提供停靠、场景 Framebuffer、拾取和变换 Gizmo。

编辑视图通过 `Scene::OnUpdateEditor` 绘制。调用
`tc_web_editor_boot(width, height)` 启动，它不接收 Player 启动接口中的 TCPAK 字节缓冲区；
随后通过 RPC 打开示例或创建场景。`tc_web_editor_frame`、`tc_web_editor_resize`、
`tc_web_editor_error` 和 `tc_web_editor_shutdown` 用法对应 Player 的同类接口。
销毁时终止 pthread 池，下次会话使用新的模块。

轮询 `tc_web_editor_state()` 可取得轻量 JSON，包含场景 Handle、修订号、选中实体、
未保存标记及撤销/重做状态。返回值与 RPC 响应共用存储，下一次调用前应复制或解析字符串。
每帧后消费 `tc_web_editor_take_actions()`：位值 `1` 请求宿主保存，`2` 请求图片导入，
`4` 请求项目导出，`8` 表示项目资产变化或请求宿主打开场景。
原生 File Save 和 Ctrl+S 会先提交当前编辑手势，再请求宿主持久化。

面板编辑、Gizmo 拖拽与 RPC 共用 `SceneHistory`，一次连续编辑只产生一条撤销记录。
原生手势尚未完成时，场景快照与事务 RPC 返回 `EDIT_IN_PROGRESS`，`scene.persist` 则返回
`EDIT_ACTIVE`；宿主须先结束手势再抓取场景。

Web 字体沿用 OpenSans，并加入 [Web/fonts 中的授权 Noto Sans SC](fonts/README.zh-CN.md)。
WebGL Framebuffer 对浮点与整数颜色附件分别使用对应类型的清除操作。
Emscripten GLFW 后端禁用不可用的 Vulkan/手柄入口；引擎手柄输入使用独立浏览器适配器。

共享桌面面板现已支持 Project 资产创建、重命名和删除、脚本附件/字段编辑、碰撞体与
RectTransform 手柄，并包含 Animation、Animator、Tile Palette、Asset Inspector 和 Profiler。
宿主须在变更后持久化 MEMFS 资产目录和元数据。Prefab 创建、外部 C# 编辑器及桌面 Build Settings
尚未开放。宿主可通过 `editor.saveLayout` / `editor.loadLayout` 保存恢复编辑器布局，
通过 `editor.setDisplayScale` 与 `{scale: devicePixelRatio}` 设置显示缩放。
C++ 文件对话框在浏览器中返回取消。

浏览器端复用桌面的 Play 工具栏与 Project Settings 界面源码。Scene 使用原有工具图标，
Game 绘制主相机；Play 运行由 SceneManager 管理的场景副本，Pause / Step 只推进副本，
Stop 保留创作场景与编辑历史。缺少主相机的场景会明确拒绝启动；Play 时禁止创作场景编辑与资产变更。

C# 创作由宿主把源码、引用程序集字节和 `ScriptAssets.json` 交给
`TomCatWeb.compileAndInstall(request)`。Roslyn 与 TomCat 源生成器直接在浏览器中运行，
成功生成的可移植 DLL/PDB 经 ABI 与元数据校验后用于 Play。Play/Pause/Step/Stop 沿用桌面端
`MonoBehaviour` 生命周期及实体、组件、输入、物理桥接；`Tasks` 与协程随引擎帧/固定步恢复，
并在销毁前清理。单个脚本异常会被隔离，其他正常脚本继续运行；异常进入现有汇报通道，
基础设施故障停止当前 Play 会话。browser-wasm 不支持可回收 `AssemblyLoadContext`，所以 Stop 会执行
OnDisable/OnDestroy、场景、回调和取消令牌清理，但重新编译后必须重建浏览器模块，避免旧代际常驻内存。

Project Settings 修改在 MEMFS 中生效并标记会话未保存。宿主须将
`ProjectSettings/ProjectSettings.json` 和 `ProjectSettings/PlayerSettings.json` 与场景一起持久化，
在 `project.open/new` 前恢复这些文件，保存成功后确认 `scene.markSaved`。
桌面显示模式/目录控件在 Web 中仍显示，但处于禁用状态。

## RPC 协议与场景事务

`tc_web_editor_rpc(requestJson)` 同步返回 JSON，字符串有效期到下一次调用。
请求和响应使用 `protocol: "tomcat.web.v1"` 与 `requestId`。
请求通过 `type` 指定命令、`payload` 携带参数。支持：

```text
system.capabilities
project.open, project.new
scene.snapshot, scene.select, scene.transact, scene.loadArchive, scene.markSaved
scene.persist, scene.openAsset
history.undo, history.redo
asset.list, asset.import, asset.refresh
preview.control, preview.snapshot
editor.setDisplayScale, editor.saveLayout, editor.loadLayout
```

`preview.control` 的 payload 为 `{command: "play"}`；command 可取 `play`、`pause`、`resume`、`step` 或 `stop`。

`project.open` 当前只接受 `/Samples/PhysicsPlayground/Project.tcproj`；
`project.new` 在同一挂载项目的 `Assets` 下创建初始场景资产。宿主恢复项目时可传入
`activeSceneHandle`，此时不会创建替代场景文件。其他场景内容可通过规范化归档导入，
已注册的场景可通过 `scene.openAsset` 与 `{handle: "<场景资产 Handle>"}` 打开。

抓取项目文件前，使用空 payload 调用 `scene.persist`，把当前创作场景写回已有资产文件。
若场景文件已删除，内容保留在内存中，不会自动重建已删除资产；此时打开其他场景须明确传入
`discardDetached: true`。宿主快照应保留这些无文件场景的归档，持久化成功后才确认
`scene.markSaved`。在原生面板之外恢复或修改项目文件后，使用 `asset.refresh` 刷新资产。

场景事务、归档加载、历史操作与保存确认携带 `sceneHandle`（uint64 十进制字符串）
与 `baseRevision`（非负安全整数）。
事务另带 `label` 和 `operations`，可执行 `entity.create/delete/rename/set-parent`、
`component.add/remove/patch`。组件 patch 将属性 ID 映射为带类型的值。
实体、组件、属性、资产 ID 和 int64 数值均以字符串跨越 JavaScript 边界。

快照包含规范化归档、实体、属性结构与历史状态。变更先在解码后的临时场景中校验，
通过后才由 `SceneHistory` 提交。编辑、撤销和重做均使修订号递增，绝不回退。
资产类型、实体引用、层级合法性和只读属性由 C++ 校验；第三方组件提供者不在支持范围内。

图片导入时，将新文件名写入 `/Samples/PhysicsPlayground/Assets/WebImports/`，
再调用 `asset.import`，参数为 `{name:"example.png"}`。引擎导入后返回稳定 Handle；
宿主必须同时保存图片与生成的 `.tcmeta`。支持最大 2 MiB 的 PNG/JPEG/TGA。
导入文件不随场景撤销/重做回退。MEMFS 不支持硬链接，因此元数据发布使用仅创建新文件的复制流程。

## 回归与浏览器验收

先构建独立原生编辑器目标，再运行无需图形上下文的真实 WASM 协议回归。
`Build-WebManaged.ps1` 会配置此构建目录，但不会构建 `tomcat_editor`。使用与托管构建相同的
Emscripten 环境；若依赖脚本自动发现 SDK，请先在当前 PowerShell 会话直接调用
`./Scripts/Build-WebManaged.ps1`，以保留它设置的环境变量：

```powershell
cmake --build build/web-managed-native --target tomcat_editor --parallel
node Web/tests/editor-rpc.cjs build/web-managed-native
```

测试覆盖事务、回滚、uint64 边界、选择、组件、层级、图片、无效引用、历史分叉和修订冲突，
还包含 50 次预览重启、物理单步与创作状态保留，以及缺少主相机导致启动失败后的恢复。
浏览器绘制与持久化仍需宿主层验收。

默认 Release 托管发布完成后，安装浏览器测试依赖并运行 Edge 冒烟（须已安装 Edge）：

```powershell
npm ci --prefix Web/tests --ignore-scripts --no-audit
$env:TOMCAT_BROWSER_CHANNEL = 'msedge'
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Run-WebBrowserSmoke.ps1
```

若采用 CI 的 Chromium 路径，执行
`node Web/tests/node_modules/playwright/cli.js install chromium`，并确保未设置
`TOMCAT_BROWSER_CHANNEL`。测试环境还需要 Node.js 与 npm；结果、截图和控制台日志默认写入
`build/web-smoke-results`。2026-10-16 的 Edge 实测覆盖浏览器 Roslyn 编译、MonoBehaviour
生命周期/故障隔离、任务与协程取消、原生查询与 SaveDocument 序列化、Play/Pause/Step/Resume/Stop、
显示缩放、布局保存恢复、场景持久化/删除，以及多纹理打包 Player 的启动、帧推进和关闭。
这些结果不代表全部面板交互、真实手柄、可听音频或跨浏览器均已验收。

2026-09-17 的浏览器验收记录覆盖：中文字形、Hierarchy 选择、Inspector 位移编辑、
Sprite 选择赋值、场景像素拾取、ImGuizmo 拖拽、撤销/重做、停靠尺寸调整，以及原生 File Save
后由宿主重新加载并恢复相同变换与 Sprite 引用。尚未覆盖中文输入法组合输入和所有桌面控件。

## 移植边界

- GLES3 替代桌面 DSA Buffer/Texture 操作，Framebuffer 为单采样；内置 Shader 使用 GLSL ES 300 和 16 个纹理槽。
- Box2D 用户设置在 wasm32 中保留完整 64 位实体 UUID；Box2D 及其调用方必须使用相同的 `B2_USER_SETTINGS` 定义。
- 键盘、指针、焦点、滚轮和标准浏览器手柄映射接入现有输入快照队列；尚未验收真实手柄硬件。
- C# 支持 TomCat 的纯托管依赖封装；原生 NuGet 资产、自定义 MSBuild 任务和平台原生库仍会被拒绝。
- 托管 TCPAK 载荷必须使用 `portable` RID，`win-x64` 载荷会被拒绝；同时校验 ABI、清单、TFM、哈希和大小。
- 自定义 Cooked SPIR-V Shader 和多重采样 Framebuffer 明确失败；GLSL 转换仅覆盖内置基础 Shader。
- 内存 AudioSource 音频通过 WebAudio 播放（PCM WAV 与已解码的 OGG/Vorbis）。Streaming 显式拒绝，常驻音频须关闭 Streaming。浏览器可能要求先在播放器内点击或按键才能出声。
- 必须支持 SharedArrayBuffer/Workers 和 WebGL2，没有单线程回退。
- 桌面渲染通过 RHI 支持 OpenGL 与 Vulkan，桌面 C# 保留 hostfxr 实现；Windows 回归与 Web 验证分别执行。

共享 TCPAK 读写均严格要求 v8（逐条目 SHA-256 与模块载荷），v5/v6/v7 旧包须重新 Cook。
当前格式的包仍受上述 Web 功能限制约束。

## 2026-09-16 的 Player 验收记录

浏览器示例 Cook、TCPAK 挂载、纹理预加载及入口场景 `12007672766582721512` 启动成功，
创建两个刚体。Square 从 `Y=1.500` 下落至约 `-0.335` 并停在地面上；
画面显示方块和地面，每帧一次绘制调用。停止/重启恢复示例初始状态，损坏 TCPAK 被拒绝并显示诊断。

这项记录只验证仓库中的 PhysicsPlayground，不能推断全部桌面游戏、Shader、脚本或输入设备兼容。

## Scene 工具栏与非运行状态 Game

桌面与 Web 编译同一份 Scene 工具栏绘制代码：可拖动的 Q/W/E/R 工具组，以及
Pivot/Center、Local/Global 菜单条，包括停靠、重排和放置预览。
编辑相机跟随项目模板，可通过 Window > 2D Scene 或视口内的 `2` 键切换 2D/3D。
localStorage 可用时，工具栏位置保存为浏览器 UI 偏好；完整布局通过布局 RPC 交给宿主持久化。
Hierarchy 支持 Ctrl/Shift 多选；视口变换 Gizmo 操作当前活动实体。
Center 在可用时使用实体包围盒中心，普通叶子实体的 Pivot 也采用这一位置；这与 Sprite 枢轴创作是不同功能。

Game 在 Play 前和 Stop 后也绘制活动场景主相机。编辑模式仅渲染，不启动物理或脚本；
缺少相机时显示 `No cameras rendering`。Web 资产 RPC 同时提供引擎内置 Circle / Square 纹理。

## 构建状态（2026-10-16）

### 浏览器音频与动态脚本运行库

共享 `AudioSource`、`AudioListener` 与混音接口现可驱动模块内独立的 WebAudio 设备。
常驻 PCM WAV（8/16/24/32 位整数或 32 位浮点）及已解码 OGG/Vorbis 支持播放、暂停续播、
停止、循环、音量与 0.01–4 的 Pitch。单声道按等功率方式定位，立体声保留左右通道；更多声道折入立体声。
浏览器可能等待播放器内的点击或按键才能恢复 AudioContext。播放器/编辑器关闭会停止音轨、
释放音频缓存并关闭 Context。创建 AudioContext 失败时仍回退 Null，并报告原因。
`Streaming=true` 会明确拒绝，不会将有界流偷换为全量内存读取；请按游戏内存预算使用常驻音频。

脚本在发布后才编译，ILLink 无法提前看到它们调用的 BCL。
`ScriptRuntimeRoots.xml` 保留浏览器脚本引用程序集对应的运行库实现，防止数学、集合、文本、JSON API
被裁剪。managed 冒烟会动态编译并执行这些 API，并检查真实烘焙 AudioSource 的非零音频信号和关闭清理。
`node Web/tests/web-audio.cjs` 另检查浏览器实际渲染的 PCM、声道、音量、变调、暂停续播及生命周期；
这些测试不代表物理扬声器输出或所有浏览器/设备均已验收。

完成 Vulkan/RHI 集成后，最终一次 `Scripts/Build-WebManaged.ps1` 已通过原生编译、WASM 链接及
.NET browser-wasm 发布，`Scripts/Run-WebBrowserSmoke.ps1` 本机 Edge 冒烟也已通过。
托管站点应部署完整 `build/web-managed` 目录，包括 `_framework`、`main.js` 及宿主页面。
独立 `tomcat_player.js/.wasm/.data` 属于原生 Emscripten 测试目标，不能替代托管站点。
CI 与其他验收入口见[生产验收](../docs/PRODUCTION_ACCEPTANCE.md)。
