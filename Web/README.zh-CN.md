# 实验性 Web Player 与 ImGui 编辑器

[English](README.md) | 简体中文 · 核对日期：2026-09-20 · [文档索引](../docs/README.md)

此 Emscripten 目标在浏览器中运行已有的 `PlayerRuntimeLayer`、Cooked TCPAK 读取器、
场景运行时、Renderer2D 和 Butter。目前覆盖原生引擎功能，尚不能完整替代桌面 Player。

[2026-09-20 功能录屏](../docs/portfolio/README.md)来自 Windows 桌面程序。
共享源码不代表这些片段验证了浏览器端；下文保留浏览器验收的原始日期，本次文档更新未重跑 Web 构建或协议回归。

## 构建

此前在 Windows 上使用 Emscripten 4.0.15、CMake 与 Ninja 完成验证。
先激活 Emscripten SDK，再从仓库根目录执行下列命令。要求 CMake 3.20 或更新版本；
协议回归还需要 Node.js。下文的历史验收日期不代表本次文档更新重新执行了验收。

```powershell
git submodule update --init TomCat/vendor/Butter TomCat/vendor/glm TomCat/vendor/spdlog TomCat/vendor/ImGuizmo
emcmake cmake -S Web -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web -j 6
```

构建产物是模块，不包含完整浏览器宿主页或持久化服务。
将 `tomcat_player.js`、`tomcat_player.wasm` 与 `tomcat_player.data` 放在同一目录，
通过 localhost HTTP 或 HTTPS 提供访问。pthread Worker 要求页面响应包含：

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

按普通脚本加载 JS，调用 `TomCatPlayerModule({ canvas, locateFile })`，
等待工厂 Promise 完成后再调用导出函数。使用 `_malloc` 和 `HEAPU8.set` 复制 TCPAK 字节，
启动结束后始终 `_free` 临时缓冲区。内存可能增长，因此每次分配后重新获取 `HEAPU8`。

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

构建还输出 `tomcat_editor.js/.wasm/.data`，工厂函数为 `TomCatEditorModule`。
模块编译已有的 `SceneHierarchyPanel`（含 Inspector）、`ContentBrowserPanel`、
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
`4` 请求项目导出。原生 File Save 和 Ctrl+S 会先提交当前编辑手势，再请求宿主持久化。

面板编辑、Gizmo 拖拽与 RPC 共用 `SceneHistory`，一次连续编辑只产生一条撤销记录。
原生手势尚未完成时，RPC 快照和变更返回 `EDIT_IN_PROGRESS`，避免保存半完成状态。

Web 字体沿用 OpenSans，并加入 [Web/fonts 中的授权 Noto Sans SC](fonts/README.zh-CN.md)。
WebGL Framebuffer 对浮点与整数颜色附件分别使用对应类型的清除操作。
Emscripten GLFW 后端禁用不可用的 Vulkan/手柄入口；引擎手柄输入使用独立浏览器适配器。

当前复用桌面核心面板，尚未覆盖全部 `EditorLayer` 工具。资源浏览、缩略图和引用可用；
Project 面板文件修改暂时禁用，图片由宿主导入。Prefab 创建、专用碰撞体编辑、外部 C# 编辑器、
布局持久化及桌面 Build Settings 尚未开放。C++ 文件对话框在浏览器中返回取消。

浏览器端复用桌面的 Play 工具栏与 Project Settings 界面源码。Scene 使用原有工具图标，
Game 绘制主相机；Play 运行由 SceneManager 管理的场景副本，Pause / Step 只推进副本，
Stop 保留创作场景与编辑历史。包含 C# 或缺少主相机的场景会明确拒绝启动；Play 时禁止变更型 RPC。

Project Settings 修改在 MEMFS 中生效并标记会话未保存。宿主须将
`ProjectSettings/ProjectSettings.json` 和 `PlayerSettings.json` 与场景一起持久化，
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
history.undo, history.redo
asset.list, asset.import
preview.control, preview.snapshot
```

`preview.control` 的 payload 为 `{command: "play"}`；command 可取 `play`、`pause`、`resume`、`step` 或 `stop`。

`project.open` 当前只接受 `/Samples/PhysicsPlayground/Project.tcproj`；
`project.new` 使用同一挂载资源根创建场景。其他场景应通过规范化归档导入。

场景变更携带 `sceneHandle`（uint64 十进制字符串）与 `baseRevision`（非负安全整数）。
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

在没有图形上下文的情况下运行真实 WASM 协议回归：

```powershell
node Web/tests/editor-rpc.cjs build/web
```

测试覆盖事务、回滚、uint64 边界、选择、组件、层级、图片、无效引用、历史分叉和修订冲突，
还包含 50 次预览重启、物理单步与创作状态保留，以及缺少主相机导致启动失败后的恢复。
浏览器绘制与持久化仍需宿主层验收。

2026-09-17 的浏览器验收记录覆盖：中文字形、Hierarchy 选择、Inspector 位移编辑、
Sprite 选择赋值、场景像素拾取、ImGuizmo 拖拽、撤销/重做、停靠尺寸调整，以及原生 File Save
后由宿主重新加载并恢复相同变换与 Sprite 引用。尚未覆盖中文输入法组合输入和所有桌面控件。

## 移植边界

- GLES3 替代桌面 DSA Buffer/Texture 操作，Framebuffer 为单采样；内置 Shader 使用 GLSL ES 300 和 16 个纹理槽。
- Butter 运行时句柄使用显式 64 位字段，在 wasm32 中保留完整实体 UUID。
- 键盘、指针、焦点、滚轮和标准浏览器手柄映射接入现有输入快照队列；尚未验收真实手柄硬件。
- 含 C# 的包明确失败：此目标不能使用桌面 hostfxr，浏览器 .NET 运行时与原生 ABI 集成仍待实现。
- 自定义 Cooked SPIR-V Shader 和多重采样 Framebuffer 明确失败；GLSL 转换仅覆盖内置基础 Shader。
- 音频使用 `NullAudioDevice` 回退，无可听 WebAudio 输出。
- 必须支持 SharedArrayBuffer/Workers 和 WebGL2，没有单线程回退。
- 桌面端保留原实现；现有 Windows CI 回归不执行 Web 构建或 WASM 测试，Web 验证也不能代替桌面回归。

共享 TCPAK 当前写入 v7（逐条目 SHA-256），读取兼容 v5/v6/v7。
包格式兼容不意味着上述 Web 功能限制已经解除。

## 2026-09-16 的 Player 验收记录

浏览器示例 Cook、TCPAK 挂载、纹理预加载及入口场景 `12007672766582721512` 启动成功，
创建两个刚体。Square 从 `Y=1.500` 下落至约 `-0.335` 并停在地面上；
画面显示方块和地面，每帧一次绘制调用。停止/重启恢复示例初始状态，损坏 TCPAK 被拒绝并显示诊断。

这项记录只验证仓库中的 PhysicsPlayground，不能推断全部桌面游戏、Shader、脚本或输入设备兼容。

## Scene 工具栏与非运行状态 Game（2026-09-18 功能记录）

桌面与 Web 编译同一份 Scene 工具栏绘制代码：可拖动的 Q/W/E/R 工具组，以及
Pivot/Center、Local/Global 菜单条，包括停靠、重排和放置预览。
Web 保持 2D，没有 2D/3D 切换；localStorage 可用时，工具栏位置保存为浏览器 UI 偏好。
Pivot/Center 沿用单选实体原点行为，不表示新增多选或自定义 Sprite 枢轴编辑。

Game 在 Play 前和 Stop 后也绘制活动场景主相机。编辑模式仅渲染，不启动物理或脚本；
缺少相机时显示 `No cameras rendering`。Web 资产 RPC 同时提供引擎内置 Circle / Square 纹理。
