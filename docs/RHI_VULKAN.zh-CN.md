# RHI 与 Vulkan 迁移

## 目标与边界

`dev_vulkan` 的 RHI 与桌面 Vulkan 后端已于 2026-10-16 合入 `main`。当前桌面默认优先 Vulkan，不支持时自动回退 OpenGL；浏览器保留 WebGL 路径。验收对象是现有 Editor、Hub 和 Player：批量精灵、圆形、线条、字体与运行时 UI、Scene/Game 离屏视口、整数实体拾取、资源预览、ImGui 多窗口、纹理导入产物和 GPU 计时。不能用“窗口能清屏”代替编辑器迁移验收。

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

采用单 graphics/present queue、单应用渲染线程。命令按调用顺序记录；每次 draw 固化顶点、索引、uniform、纹理绑定和 pipeline 状态。普通呈现异步提交，主窗口允许两帧在途；三个提交槽各自拥有 command pool/buffer、fence、descriptor pool、资源引用和 timestamp query。只在复用仍忙的槽或帧时等待对应 fence。提交完成前不覆盖上传内存、不回收 descriptor、不销毁引用的资源。这里的多帧在途用于重叠 CPU 准备与 GPU 执行，未引入多线程 command recording、独立 transfer queue、bindless 或 render graph。

顶点、索引、uniform 快照和纹理 mip staging 使用提交槽内持久映射的 host-visible/coherent 上传页，按 offset 子分配；uniform 遵守设备的 `minUniformBufferOffsetAlignment`，纹理复制和索引遵守各自对齐。普通页为 1 MiB，大绘制快照也可复用缓存，每槽缓存总计不超过 8 MiB；一次性大纹理 staging 和超出缓存上限的页在 fence 完成后释放。每次 draw 只复制实际使用的索引数量和反射声明的 uniform 大小。常规绘制不再为每份快照创建、映射和销毁独立 Vulkan buffer。

主窗口使用按帧复用的 acquire semaphore，GPU 在首次访问 swapchain image 前等待；呈现完成 semaphore 按 image 管理。辅助 ImGui 窗口提交前先提交离屏生产命令并建立内存依赖，帧末提交的 fence 覆盖同队列的辅助窗口工作。主窗口暂不可呈现时仍提交帧尾标记，保护 UI 顶点/索引缓冲区的复用。GPU 计时按提交分段，fence 完成后非阻塞读取并汇总到原帧。

`ReadPixel`、RGBA 截图、显式 `WaitIdle`、资源销毁/resize、字体重建和 swapchain 重建仍保留同步边界；拾取与截图继续返回当前调用位置的结果。上传页优化没有改变纹理格式或压缩产物的 CPU 解压语义。

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

桌面默认优先 Vulkan。进程首次选择后端时读取 `TC_RENDERER`：

| 配置 | 行为 |
| --- | --- |
| 未设置或 `auto` | 探测 Vulkan loader、设备与所需能力；不支持时回退 OpenGL |
| `vulkan` | 强制 Vulkan；不支持时报告原因并终止初始化 |
| `opengl` | 强制 OpenGL，跳过 Vulkan 探测 |
| 其他值 | 报配置错误 |

自动模式还会在实际窗口的 Vulkan surface 不支持时清理未完成的初始化，重建 OpenGL 窗口。正常运行期间不切换后端；设备丢失、Shader 错误和验证配置错误不会被当作兼容性回退。启动日志记录所选后端和回退原因。从仓库根目录运行：

```powershell
$env:TC_RENDERER = 'auto' # 与不设置变量相同
& ./Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe
```

Editor、Hub、CLI 和 Player 都使用这个选择。无窗口 CLI 也能安全探测 Vulkan，不需要先创建 GLFW 窗口；缺少 loader 时不会因延迟加载失败而崩溃。Vulkan 要求兼容 loader、Vulkan 1.2、独立 attachment 混合、至少 32 个采样纹理槽、swapchain 扩展和可呈现的图形队列。编译依赖由 `vendor/VulkanSDK` 提供。

新的桌面烘焙包同时包含 OpenGL 与 Vulkan 的预编译 SPIR-V 和反射数据，不包含 Shader 源码；运行时按所选后端读取对应产物。因此 CLI 烘焙与 Player 启动不需要相同的 `TC_RENDERER`，同一个新包可在不支持 Vulkan 的机器上回退 OpenGL。Editor 日常资源导入仍只编译当前后端。旧单后端包仍可在原后端运行，跨后端运行前需重新烘焙。

TCPAK 版本仍为 8，桌面 Shader 使用 TCSB v1 容器保存两个 TCSH v1 目标。Player Template 格式升级到 2，以防旧 Player 模板导出无法读取新 Shader 的游戏；发布前执行 `Scripts/Build-PlayerTemplate.ps1 -Build` 重建模板。旧格式 1 模板会被明确拒绝。

```powershell
# 编译并运行真实 GPU 回归；需要 Visual Studio C++ 工具链
./Scripts/Run-BackendSelectionRegression.ps1
# 可选断言测试机自动选择 Vulkan；否则按实际能力检查自动策略
./Scripts/Run-BackendSelectionRegression.ps1 -SkipBuild -ExpectedAutoBackend vulkan
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport
./Scripts/Run-RHIRegression.ps1 -Backend opengl
# 可选指定设备；默认优先独立显卡
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -Device Intel
```

`Run-BackendSelectionRegression.ps1` 在独立进程中验证未设置/`auto`、强制后端、非法配置，以及使用不匹配的 `TC_VULKAN_DEVICE` 模拟无可用 Vulkan 设备。支持 Vulkan 的机器还会在探测成功后改变设备筛选，验证实际 Vulkan 初始化失败后的窗口重建与显式模式报错。回退用例会执行完整 OpenGL RHI 绘制与读回，包含桌面 Shader bundle 消费；初始化用例会创建隐藏窗口并呈现。`Run-Regressions.ps1` 为了适配 CI 软件驱动，对整套通用回归显式设置 OpenGL，退出时恢复调用者环境。

`-Validation` 需要安装 Khronos validation layer（Vulkan SDK）。普通运行不需要该 layer。可手动设置 `TC_VULKAN_VALIDATION=1`，诊断写入标准错误；`TC_VULKAN_DEVICE` 按设备名子串筛选。

验证同一个新包的真实图形 Player 启动与回退时，先按当前源码构建 Managed Release、`ScriptCompilerRegression` 和 `TomCatCLI`，再重建测试专用 Player Template：

```powershell
./Scripts/Build-PlayerTemplate.ps1 -Configuration Release -Build -Destination ./Tests/bin/Release-windows-x86_64/PlayerTemplateRegression
./Scripts/Run-CSharpPlayerSmoke.ps1 -Configuration Release -RequirePlayer -Graphics
```

`-Graphics` 开启真实图形窗口；不加该参数的默认冒烟是无窗口模式。图形模式对同一包先验证默认后端，再以 `auto` 和不存在的设备筛选验证 OpenGL 回退，检查 C# 脚本完成退出码 73，并从游戏日志断言实际后端。

## 主线集成（2026-10-16）

合并保留主线的 Editor 面板拆分、Web Module/Save 模块、DPI 字体缩放和后处理生命周期，并补齐以下跨后端行为：

- Editor 公共图标和视口使用指针宽度的 UI 纹理句柄。
- Vulkan 支持主线曝光、饱和度和暗角处理，保留颜色 alpha、实体 ID 与深度附件。
- 字体缩放在帧边界等待 GPU 后重建 atlas、回收旧 descriptor，不关闭辅助窗口。
- `Framebuffer::ReadColorAttachment` 提供同步 RGBA8 读回；Editor 自动化截图使用该接口，保留 PPM 格式与方向。
- 新增的 SaveData、SceneMigration 和 ModuleSdk 测试补齐 Vulkan loader 延迟加载配置。
- `Run-Regressions.ps1` 包含默认 OpenGL RHI 回归；RHI 回归验证调色后的实际像素、字体重建、BC3/ETC2/ASTC 上传和读回错误边界。

复验命令（按顺序运行，避免 Managed DLL 重建与脚本编译测试相互干扰）：

```powershell
./Scripts/Run-Regressions.ps1 -Configuration Release -Log
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport
$env:TC_RENDERER = 'vulkan'
$env:TC_VULKAN_VALIDATION = '1'
./Scripts/Run-AutomationRegression.ps1 -Configuration Release -SkipBuild
./Scripts/Run-CSharpPlayerSmoke.ps1 -Configuration Release -RequirePlayer
Remove-Item Env:TC_RENDERER, Env:TC_VULKAN_VALIDATION
./Scripts/Build-WebManaged.ps1
./Scripts/Run-WebBrowserSmoke.ps1
```

本次实际验证结果：

| 项目 | 结果 |
| --- | --- |
| Release 完整入口 | Managed、14 个原生回归、Editor/Hub/CLI/Player 构建、Player Template、C# Player、CoinRunner、模块发布与编辑器自动化全部通过 |
| Vulkan RHI | NVIDIA GeForce RTX 5060 Laptop GPU，Khronos 同步验证与多窗口通过，无 validation error |
| Vulkan Editor | 创建项目、编写/编译/挂载脚本、Prefab、保存、输入/步进/断言、截图、停止和 Player 导出通过，验证层 stderr 为空 |
| Vulkan Player | C# 烘焙/打包、私有运行时和真实 Player 脚本完成退出码 73 通过 |
| Web | 最终 Emscripten/.NET Web 构建通过；Edge 浏览器生命周期、故障隔离、预览控制及多纹理打包 Player 启动/帧推进/关闭通过 |

本地完整日志位于 `build/logs/merge-vulkan-*`。首次完整运行曾遇到场景原子替换 `Access denied`，定向与最终串行全套复测均通过；另一次并行构建干扰了测试 DLL 和编译输入，最终验证已串行执行。未将这些瞬时失败解释为已修复的文件系统缺陷。本轮未重跑集显测试或长时间性能 soak。

## 多帧与上传优化验收（2026-10-16）

本次新增回归在连续帧结束后才读取像素，避免每帧 readback 掩盖在途资源被提前复用的问题。覆盖同一纹理的多次更新、CPU 源数据覆盖、跨提交的顶点/uniform/descriptor 快照、超过 4 MiB 的纹理上传、满批 20,000 个精灵、GPU 计时中途 readback、在途资源 resize/销毁，以及辅助窗口持续渲染、移动、resize 和关闭。

| 验证 | 本次结果 |
| --- | --- |
| NVIDIA RTX 5060 Laptop，Vulkan 单窗口及多窗口 | Release RHI 回归与 Khronos 同步验证通过，无 validation error |
| Intel RaptorLake-S Mobile，Vulkan 多窗口 | 同一回归与同步验证通过，无 validation error |
| 连续帧压力段 | 24 帧产生 96 次提交、0 次显式队列等待；后 12 帧复用暖缓存，上传页新分配为 0；最多使用 3 个提交槽 |
| 20,000 精灵满批次 | 6 帧热身后再运行 6 帧，上传页新分配和显式队列等待均为 0，最终实体像素正确 |
| 辅助窗口稳态 | 关闭 GPU profiling 后连续 12 帧正常提交，显式队列等待未增加 |
| OpenGL | 默认后端 RHI 回归通过 |
| Vulkan Editor | 重新构建后，项目创建、编辑、C# 编译、Prefab、输入/步进、截图、保存和 Player 导出通过；Editor 验证层 stderr 为空 |
| Vulkan Player | 使用本次构建的 Player 刷新模板，显式包路径及默认启动均完成 C# 脚本并返回验收退出码 73 |

这里的“0 次显式队列等待”针对普通绘制/呈现压力段，不包含必要的提交槽 fence 复用等待，也不包含后续同步读回、resize 或销毁。计数验证说明消除了对应的逐提交队列等待与重复上传分配；没有进行优化前后 FPS、帧延迟或吞吐量基准，不据此推断固定的性能提升比例。

复现 GPU 回归：

```powershell
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation
./Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport -Device Intel
./Scripts/Run-RHIRegression.ps1 -Backend opengl
```

本次本地日志为 `build/logs/vulkan-frames-*.log`。初次受限编译发生 MSVC C1902，并留下旧对象文件，导致一次类布局不一致的启动异常；在正常构建环境重新编译受影响单元后，上述回归全部通过。

## 分支原始验证记录（2026-09-21）

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

- 已实现两帧在途呈现、按 fence 回收的提交槽和持久映射上传页；仍使用 GENERAL 离屏布局、保守的全局内存 barrier 与同步拾取。未实现独立传输队列、设备本地静态几何缓存或异步拾取；不据此声称性能优于 OpenGL。
- 要求 Vulkan 1.2、独立 attachment 混合和至少 32 个采样纹理槽。宽线与 timestamp 按能力降级；默认优先独显，可手动指定设备。
- 当前 framebuffer 支持单采样 RGBA8、R32_SINT 与深度模板；MSAA、compute、storage image/buffer 和 push constant 不在现有前端迁移范围。
- shader 转换支持现有引擎 GLSL 约定，使用 set 0 UBO、set 1 sampler、set 2 命名 uniform；不保证任意第三方 GLSL 能自动转换。自定义 shader 应遵循该布局。
- BC1、BC3、BC5、ETC2RGBA8 与 ASTC4x4 导入产物通过 CPU 解压后上传 RGBA，保留 mipmap 和 sRGB 语义，尚未启用设备原生压缩纹理上传。
- 编辑器维持原来的单主窗口输入策略。`TC_IMGUI_VIEWPORTS=1` 可开启已验证的辅助窗口渲染，但未新增分离 Scene 窗口的完整输入路由，默认关闭。
