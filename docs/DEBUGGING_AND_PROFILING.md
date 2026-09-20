# 调试与性能分析

界面入口核对：2026-09-20 · [文档索引](README.md) · [本次实机录屏](portfolio/README.md)

## 编辑器 Profiler

通过 **Window → Panels → Profiler** 打开，或切换 **Window → Layouts → Debugging**。
点击顶部圆形录制按钮开始采样，重现问题后再次点击停止。点击图表选择一帧，使用 `<` / `>`
逐帧查看，**Live** 恢复跟随最新帧。关闭面板会停止采样，已采集数据保留；Clear 清空。
Profiler 默认关闭，不会在后台无限累积数据。

**Profiler Modules** 控制 CPU Usage、GPU Usage、Rendering、Memory 图表的显示，
未接入的 Audio、Video、Physics、UI 等专用模块为禁用项。下方视图可选 **Hierarchy**、
**Timeline** 或 **C# Debugger**；CPU scope 搜索用于表格和时间线过滤。选择 Memory 模块查看内存与资源明细。
标签栏右键菜单可最大化、关闭或添加面板，窗口标题不提供折叠三角。
顶部更多菜单提供 **Clear on Play**、跟随最新帧和 **Export CPU trace...**。

2026-09-20 的[录屏](portfolio/2026-09-20/04-profiler.gif)展示编辑模式采样与选帧，
没有进行外部调试器附加、泄漏诊断或性能基准测试。

- 最多保留 240 帧，每帧最多 512 条 CPU scope，scope 名称最多 160 字节。超出预算的事件数显示在所选帧中。
- CPU 时间线按线程和嵌套深度绘制，悬停显示完整名称、起始时间和耗时；表格按 inclusive 耗时降序排列。嵌套 scope 相互重叠，不能将 inclusive 列直接相加作为整帧时间。
- 自动测量 Scene Runtime Update、Fixed Step、Managed Update / FixedUpdate、Box2D Step、Runtime UI Update、Audio Update、Scene Runtime / Editor Render，以及现有 `TC_PROFILE_SCOPE` / `TC_PROFILE_FUNCTION` 埋点。
- CPU frame 是主循环从输入轮询到 Present 返回的实际耗时，包含 VSync 等待和 Profiler 自身的开销；Delta 是传入该帧的时间步。定位逻辑瓶颈时先检查 `Present / VSync wait`，避免把限帧误判成游戏逻辑卡顿。
- GPU 使用桌面 OpenGL `GL_TIME_ELAPSED` 查询，覆盖主图形上下文的资源发布、场景和编辑器绘制，止于 Present 之前。它不是逐 draw 的 GPU 分析器，可能包含 GPU 队列空闲时间；不能简单地用 CPU 减 GPU 得到“驱动耗时”。独立 ImGui 平台窗口的其他上下文不作为完整 GPU 捕获承诺。
- 查询环有 4 个槽位。仅在 `GL_QUERY_RESULT_AVAILABLE` 为真后读取结果；忙时跳过该帧，绝不等待 GPU。缺失值在曲线上画为零，但明细明确标记 pending / skipped；不支持查询的平台显示 unavailable。
- Draws 统计引擎提交的索引/线段绘制，包含一帧内全部场景视口，排除 ImGui 自己的绘制。Submitted elements 是提交的索引数或顶点数，不是去重顶点数。

**Export CPU trace** 将当前有界历史写为 Chrome Trace JSON，可导入兼容的离线 trace 查看器。事件名称进行 JSON 转义；GPU 耗时、帧号、draw 数和丢弃事件数位于帧事件参数中。已有启动参数 `--profile` 的文件采集保持可用；它和面板采集是独立的。Dist 默认关闭 `TC_PROFILE` scope 宏，日常编辑器分析使用 Debug / Release。

CPU scope 在开始时绑定帧编号，线程安全地提交到这一帧。跨帧才结束的后台 scope 会丢弃，避免归到下一帧；这个面板适合逐帧分析，不替代长任务、所有线程的完整系统级采样器。

## 内存与资源占用

选择 **Memory** 模块后，下方详情提供当前进程 working set、private committed、peak working set（Windows，每 0.5 秒刷新），以及引擎资源数量、估算字节和流式加载暂存字节。资源历史曲线保存的是每帧资源快照。Set memory baseline 记录当前值，后续显示差额。

资源统计直接在创建、销毁和成功调整大小时维护，捕获开始前已经加载的资源也会计入：

| 类别 | 计算范围 |
| --- | --- |
| Textures | Texture2D 所有 mip；BC3 使用压缩 mip 字节数，回退解压时使用 RGBA 字节数；包括经过 Texture2D 创建的字体 atlas |
| Vertex / index buffers | OpenGL 顶点/索引 buffer 分配的 payload 大小 |
| Framebuffer attachments | RGBA8 / R32I / Depth24Stencil8 附件，按宽 × 高 × 4 × samples 计，成功 resize 才更新 |
| Texture / font staging | 后台准备好、等待主线程发布的资源字节与任务计数 |

这些是分配 payload 估算，不是显卡驱动报告的驻留 VRAM。统计不覆盖驱动开销、shader program、音频内存、全部 buffer 类型或逐对象托管堆归属。进程内存包含编辑器和 .NET，不能直接解释为场景内存。

建议先让加载任务完成，再设 baseline；执行相同的加载 → 游玩 → 卸载循环，返回同一场景/相同编辑器窗口布局后比较。字体 atlas、资源缓存和 CLR 的复用会保留内存，一次差额不能证明泄漏。观察多次循环中的持续增长，并结合资源活跃数量、pending worker 数判断该查生命周期还是缓存策略。

## C# 断点流程

引擎在 `TomCatInut.exe` 内通过 hostfxr 托管 .NET 10，没有独立的项目脚本进程。当前项目脚本编译器生成 **Release** 程序集和 portable PDB，并将 DLL / PDB 一起传给可回收 `AssemblyLoadContext.LoadFromStream`。不要只因原生 Editor 编译成 Debug 就假定 C# 没有优化。

1. 在编辑器中打开含 C# 脚本的项目，等待 Console 的编译错误清空。至少运行一次 Play，让 .NET 初始化，然后停止 Play。
2. 使用支持 .NET 10 的 Visual Studio，打开项目脚本源文件。在 **Debug → Attach to Process** 选择 `TomCatInut.exe`；Profiler 的 C# debugger 区显示当前 PID。将代码类型选为托管 .NET / .NET Core（具体文字随 IDE 版本），需要调试 C++ 时可同时选择 native。微软的[附加进程文档](https://learn.microsoft.com/en-us/visualstudio/debugger/attach-to-running-processes-with-the-visual-studio-debugger?view=visualstudio)说明此流程也支持非 Visual Studio 启动的进程。
3. 在 `OnUpdate` 或 `OnCreate` 的可执行语句上设断点，再进入 Play。附加后才进入新的 Play 可以捕捉初始化回调。断点命中后使用 Locals、Watch、Call Stack；继续执行前不要认为游戏的实时帧率仍有意义。
4. 如果断点为空心，在 **Debug → Windows → Modules** 查找当前 `Assembly-CSharp`，检查 symbol 状态。实际产物在项目 `Library/ScriptAssemblies/<build-id>/`；`last-good.json` 标识最后成功构建。DLL 与 PDB 必须来自同一次构建，不要将旧 PDB 与新 DLL 混用。参见[微软符号与源码匹配说明](https://learn.microsoft.com/en-us/visualstudio/debugger/specify-symbol-dot-pdb-and-source-files-in-the-visual-studio-debugger?view=visualstudio)。
5. 编译器将项目根目录映射为 `.` 写入 PDB。如果 IDE 提示缺少 `./Assets/...cs`，定位到当前项目根目录下对应源码。源码应与构建版本完全一致；保存脚本后先停止 Play，等待新编译成功再开始下一次 Play。
6. Release 优化可能导致局部变量被优化、步进跳行或被 Just My Code 跳过。需要逐行排查时，按微软的[Just My Code 说明](https://learn.microsoft.com/en-us/visualstudio/debugger/just-my-code?view=visualstudio)调整该选项；在附加后、加载下一次 Play 程序集前启用 [Suppress JIT optimization on module load](https://learn.microsoft.com/en-us/visualstudio/debugger/debugging-options-dialog-box?view=vs-2022)。该设置不能逆转已经完成的 C# 编译优化，也不会修改项目发布配置。

本流程基于当前 DLL/PDB 编译与载入路径，编辑器不会代替 IDE 建立调试会话，也没有内置 C# 单步调试器。托管热重载若报告 load context 无法卸载，应先取消断点/Watch 中对旧对象的长期引用，结束调试并重启 Editor，再继续排查；不要将调试器导致的对象保留直接判为运行时泄漏。

## 回归验证

`ProfilerRegression` 是无需窗口的原生测试，包含有界历史、并发 scope、过期帧隔离、清空后编号隔离、GPU 结果回填、JSON 转义、资源增减和平衡 resize 检查；用假的 OpenGL 入口驱动真实查询环，验证未 ready 时不读结果、4 槽忙时跳过、暂停后排空、销毁查询。它由 `Scripts/Run-Regressions.ps1` 调用。

桌面交互验收：打开面板并 Record，进入/退出 Play，选帧检查嵌套 scope；切换 VSync 观察 Present；暂停后 GPU 最后几帧应继续回填；关面板应停止新增帧；导出 JSON 后检查可解析；调整 Game/Scene 视口，framebuffer 字节应随尺寸变化，返回原尺寸应回到相同数量/估算大小。
