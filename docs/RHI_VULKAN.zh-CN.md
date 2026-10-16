# RHI 与 Vulkan 迁移

## 目标与边界

在 `dev_vulkan` 上保留 OpenGL/WebGL 路径，增加可选择的桌面 Vulkan 后端。验收对象是现有 Editor、Hub 和 Player：批量精灵、圆形、线条、字体与运行时 UI、Scene/Game 离屏视口、整数实体拾取、资源预览、ImGui 多窗口、纹理导入产物和 GPU 计时。不能用“窗口能清屏”代替编辑器迁移验收。

现有 `Renderer2D` 负责排序、CPU 顶点批处理和场景数据；资源类与 `RendererAPI` 是兼容前端。后端负责设备、GPU 内存、不可变绘制状态、描述符、提交同步和呈现。场景与编辑器不得持有 Vulkan 对象。ImGui 的纹理标识单独通过 UI 互操作接口提供，不能把 64 位 descriptor set 截断为 OpenGL 的 32 位纹理编号。

## 已实现的迁移点

| 现有代码 | 问题 | 迁移契约 |
| --- | --- | --- |
| `RenderCommand.cpp` | 静态初始化时创建 OpenGL 后端 | 在窗口创建前确定 API，在 Renderer 初始化时创建命令前端 |
| `WindowsWindow.cpp` | 创建 GL context，直接调用 swap interval | Vulkan 使用 GLFW_NO_API；呈现与 VSync 交给 context |
| `Renderer2D.cpp` | 多次 Flush 覆盖同一顶点/相机 buffer | 每次绘制获取提交期间不可变的数据快照 |
| `Texture` / `Framebuffer` | GLuint 被直接当作 ImTextureID | 独立的指针宽度 UI texture handle |
| `Framebuffer::ReadPixel` | CPU 立即读取整数 attachment | 完成此前绘制后复制到 host-visible staging，保留当前帧拾取语义 |
| `ImGuiLayer.cpp` | 直接调用 GL backend | 按设备选择 renderer backend，GLFW 平台层共享 |
| `Renderer.cpp` | GPU profiler 直接调用 OpenGL | 后端分发，Vulkan 使用 timestamp query |
| Shader artifact / AssetManager | 已有 Vulkan target，但 authoring import 固定 OpenGL | 产物目标与设备一致，缓存按后端隔离 |

## 提交与资源生命周期

采用单 graphics/present queue、单应用渲染线程。命令按调用顺序记录；每次 draw 固化顶点、索引、uniform、纹理绑定和 pipeline 状态。资源更新、同步拾取、窗口呈现是提交边界。提交完成前不覆盖上传内存、不回收 descriptor、不销毁引用的资源。首版优先正确性；并行 command recording、独立 transfer queue、bindless 和 render graph 不作为迁移前提。

离屏颜色、整数 ID 和深度 attachment 必须独立定义格式与清除值；ID attachment 禁用混合，清除值为 -1。离屏结果在 UI 采样前建立写入到采样的依赖。resize 先创建完整新目标，成功后替换，旧目标等 GPU 完成后回收；零尺寸窗口跳过呈现。surface out-of-date/suboptimal 触发重建，device lost 明确报错。

呈现完成 semaphore 按 swapchain image 管理，不能仅凭 graphics fence 判断 presentation 已完成。依据：[Khronos semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。资源布局与访问依赖依据：[Khronos synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)。

## 坐标、着色器与能力

保留引擎现有 GL 风格投影与离屏纹理行方向，由 Vulkan 顶点阶段把 clip Z 从 [-W,W] 映射到 [0,W]。这样 Scene/Game 的 UV 翻转和拾取坐标继续一致；直接呈现 Player 图像时转换窗口方向。不能全局修改 GLM 的深度宏，否则会影响 OpenGL 和编辑器计算。

相机 UBO 与纹理数组在 Vulkan 中必须使用不同的 descriptor binding 或 set；GL 的两类绑定各自为 0，不能直接照搬为同一个 Vulkan binding。shader artifact target 校验严格执行。资源格式、纹理数组大小、深度格式、宽线与 timestamp 支持必须查询设备能力，失败应有可诊断错误。

## 代码入口

- `TomCat/src/TomCat/RHI/RenderDevice.h`：设备能力、资源工厂、命令工厂、计时和等待接口；现有资源的 `Create` 统一转发到这里。
- `TomCat/src/platform/Vulkan/VulkanDevice.*`：设备选择、内存、命令记录、描述符、同步、验证层与计时。
- `VulkanResources.*`：buffer、texture、vertex array、framebuffer；`VulkanShader.*`：反射、命名 uniform、pipeline 与每次 draw 的状态快照。
- `VulkanContext.*`：surface、swapchain、VSync、直接呈现和 ImGui。平台窗口只依赖 `GraphicsContext`。
- `TomCat/src/TomCat/Asset/ShaderArtifact.cpp`：Vulkan shader 方言转换与离线编译；导入器版本提升使旧缓存失效。
- `Tests/RHIRegression`：运行真实设备的跨后端回归。

## 启动与构建

默认仍为 OpenGL。进程启动时读取 `TC_RENDERER`，同一进程不能动态切换后端。从仓库根目录运行：

```powershell
$env:TC_RENDERER = 'vulkan'
& ./Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe
```

Editor、Hub、CLI 和 Player 都使用这个选择。Vulkan 需要显卡驱动提供的 loader 和 Vulkan 1.2；OpenGL 使用延迟加载链接，不在启动时加载 Vulkan loader。编译依赖由 `vendor/VulkanSDK` 提供。

烘焙与运行必须使用同一后端。打包只包含所选目标的 shader，尚不支持同一包在 GL/Vulkan 之间自动切换。设置 `TC_RENDERER=vulkan` 后执行现有 `TomCatCLI build --project ... --template ...`，启动生成的游戏时也保留该变量。切换回 GL 时重新烘焙。

```powershell
# 编译并运行真实 GPU 回归；需要 Visual Studio C++ 工具链
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport
./Scripts/Run-RHIRegression.ps1 -Backend opengl
# 可选指定设备；默认优先独立显卡
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -Device Intel
```

`-Validation` 需要安装 Khronos validation layer（Vulkan SDK）。普通运行不需要该 layer。可手动设置 `TC_VULKAN_VALIDATION=1`，诊断写入标准错误；`TC_VULKAN_DEVICE` 按设备名子串筛选。

## 验证记录（2026-09-21）

| 项目 | 实际结果 |
| --- | --- |
| Windows x64 Release | Editor、Hub、CLI、Player 及原生 Tests 构建通过 |
| 原生回归 | Physics、SpriteAsset、ScriptCompiler、P0Safety、EditorRecovery、Audio、Importer、Input、Advanced2D、Profiler 和 RHI 均通过 |
| Vulkan 集显 | Intel RaptorLake-S Mobile，驱动 32.0.101.7076；RHI 与同步验证通过 |
| Vulkan 独显 | NVIDIA RTX 5060 Laptop，驱动 592.15；RHI、多窗口、同步验证通过，无 validation error |
| GPU 回归覆盖 | 多批次数据快照、纹理更新顺序、透明裁剪、相机与多目标、圆形/线条、整数拾取、目标及窗口 resize、VSync、UI descriptor、GPU timestamp、离线 shader 命名 uniform、BC3/mipmap 纹理 |
| Editor 交互 | Vulkan 下 Scene/Game 显示、点击实体选择、选择轮廓/gizmo/Inspector、Play 物理下落与 Stop 恢复已检查 |
| 发布路径 | PhysicsPlayground 样例成功烘焙与打包；独立 Player 从包加载纹理、场景和 .NET 脚本，进入运行，无 validation error |
| Web | 保留 GL/WebGL 编译分支并加入 RHI 源目录；本机没有 Emscripten，未执行 Web 构建或浏览器验收 |

## 当前能力与限制

本次迁移覆盖现有二维渲染和编辑器资源入口；RHI 保留即时调用式前端，Vulkan 内部负责显式记录和状态快照。它尚不是面向任意高级渲染功能的 command-list/render-graph API。

- 使用保守的 fence 等待、GENERAL 离屏布局和同步拾取保证正确性，尚未优化多帧并行与上传分配；不声称性能优于 OpenGL。
- 要求 Vulkan 1.2、独立 attachment 混合和至少 32 个采样纹理槽。宽线与 timestamp 按能力降级；默认优先独显，可手动指定设备。
- 当前 framebuffer 支持单采样 RGBA8、R32_SINT 与深度模板；MSAA、compute、storage image/buffer 和 push constant 不在现有前端迁移范围。
- shader 转换支持现有引擎 GLSL 约定，使用 set 0 UBO、set 1 sampler、set 2 命名 uniform；不保证任意第三方 GLSL 能自动转换。自定义 shader 应遵循该布局。
- BC3 导入产物通过 CPU 解压后上传 RGBA，保留 mipmap 和 sRGB 语义，尚未启用设备原生 BC3 上传。
- 编辑器维持原来的单主窗口输入策略。`TC_IMGUI_VIEWPORTS=1` 可开启已验证的辅助窗口渲染，但未新增分离 Scene 窗口的完整输入路由，默认关闭。
