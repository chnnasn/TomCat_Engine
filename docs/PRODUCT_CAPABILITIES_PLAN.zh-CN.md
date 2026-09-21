# Prefab、场景、调试与 UI 开发方案

核查日期：2026-09-19。下文保留最初的完整目标清单；具体已实现范围以 [Prefab 工作流](PREFAB_WORKFLOW.zh-CN.md)、[场景组织](SCENE_STREAMING.zh-CN.md)、[运行时 UI](RUNTIME_UI_PRODUCT.zh-CN.md) 和 [Profiler](DEBUGGING_AND_PROFILING.md) 为准，不将目标清单当作完成证明。

本次已实现并进入集成验收：关联 Prefab、整实例覆盖操作、嵌套和变体；异步场景读取、叠加、持久根对象和卸载；CPU/GPU 帧分析与资源估算；基础产品 UI 控件、主题和语言表。实际场景架构采用共享 ECS/物理/脚本世界，以保留持久脚本实例，区别于下文原提案的独立物理世界。逐字段 Apply 面板、完整 IME 组合态、虚拟列表、独立主题资产和托管堆对象归属分析仍属于后续目标。

## 现状与交付目标

| 领域 | 已有实现 | 本轮需求目标 | 用户可验收结果 |
| --- | --- | --- | --- |
| Prefab 内容复用 | 子树快照、LocalID 引用重映射、实例化为普通实体 | 关联更新、Override、Apply/Revert、嵌套、变体 | 修改敌人模板后，各关卡实例更新，关卡独有配置保留 |
| 场景组织 | 同步准备、帧末单场景替换 | 异步加载、叠加、持久对象、显式卸载 | Loading 持续响应，管理器跨关卡存活，地图分区可加载和释放 |
| 调试与性能 | Console、CPU 计时埋点、绘制统计 | C# 断点流程、可视化 CPU/GPU、内存与资源分析 | 能从慢帧定位耗时阶段，并比较切关前后的资源占用 |
| UI 产品能力 | Canvas、文字、图片、按钮、布局、导航和裁剪 | 输入框、滚动列表、滑条、主题、游戏本地化、IME | 设置页、背包和中文名字输入可在 Editor Play 与 Player 使用 |

## 1. Prefab 关联更新

源码入口：[PrefabArchiveCodec](../TomCat/src/TomCat/Scene/Serialization/PrefabArchiveCodec.cpp)、[EntityArchive](../TomCat/src/TomCat/Scene/Serialization/EntityArchive.h)、[ComponentCodecs](../TomCat/src/TomCat/Scene/Serialization/ComponentCodecs.cpp)、[SceneHistory](../TomCat/src/TomCat/Editor/SceneHistory.cpp)。

当前 CaptureSubtree 按遍历顺序生成 `index + 1` 的 LocalID。它保证单份快照内的身份与引用一致，但不能保证修改模板层级后的身份连续性。关联更新必须先解决这个问题。

### 数据和操作约定

- 模板编辑时保留既有 LocalID；新节点分配未使用的 ID；删除后不复用。场景 UUID、脚本 AttachmentID 与模板 LocalID 分开管理。
- 实例根记录模板 AssetHandle、基线修订摘要、LocalID 到场景 UUID 的映射及 Override 集合；关联数据随场景保存、复制和撤销恢复。
- 属性覆盖用组件稳定标识、字段路径和序列化值表达；另行记录增删组件、增删节点与重新挂父节点。数组和脚本字段需定义稳定寻址方式，不能依赖显示名。
- 更新执行三方比较：旧模板基线、新模板、实例覆盖。未覆盖字段跟随模板；已覆盖字段保留；模板删除了被覆盖节点或字段时生成可见冲突，禁止静默丢失编辑。
- Apply 将选定覆盖写回目标模板，再重新计算其他实例；Revert 清除选定覆盖并恢复最新模板值。实例根的位置和外部父级默认作为放置数据保留，界面提供明确的单独恢复动作。
- 写模板前先完整校验，在临时文件完成写入后原子替换；模板更新与实例传播分别报告结果。未打开关卡使用修订摘要在下次加载时重新协调，批量更新关卡应提供预览和失败清单。
- 编辑器菜单提供打开模板、查看覆盖、Apply、Revert、解除关联；每项场景修改接入现有撤销机制。跨资产 Apply 的撤销必须同时恢复模板文件与已更新实例。

### 嵌套与变体

嵌套节点引用另一个模板及其局部覆盖；变体记录基础模板和差异。解析顺序为基础模板、变体、嵌套实例局部覆盖、场景实例覆盖；每层覆盖必须有明确归属，Apply 只能写入用户选定且可编辑的层。

依赖图检查循环和深度上限，错误显示完整依赖链。删除基础模板、丢失嵌套资源、字段类型变更均保留诊断及原覆盖数据。Cook 收集传递依赖并验证展开结果，不依赖编辑器缓存。

### 验收

扩展 [PrefabRegression](../Tests/PhysicsRegression/src/PrefabRegression.h)：两个关卡各放三个敌人，修改模板生命值并保留一个实例的覆盖；插入兄弟节点后原引用不改变；保存重载、Apply/Revert 和 Undo/Redo 结果一致；覆盖节点被删除时报告冲突；嵌套和变体循环拒绝提交；Cook 后的 Player 得到相同最终实体内容。

## 2. 场景组织与生命周期

源码入口：[SceneManager](../TomCat/src/TomCat/Scene/SceneManager.cpp)、[Scene](../TomCat/src/TomCat/Scene/Scene.cpp)、[ScriptEngine](../TomCat/src/TomCat/Scripting/ScriptEngine.h)、[托管 SceneManager](../Managed/TomCat.Managed/SceneManager.cs)。

### 加载和卸载约定

- 引入区别于资源 AssetHandle 的运行时 SceneInstanceID；同一场景资源可拥有多个实例。维护已加载集合和一个活动场景，活动场景决定默认创建目标。
- 同步、异步加载共享同一操作模型，模式包含 Single 和 Additive。状态至少包含排队、读取、准备、等待激活、激活、完成、失败、取消，并记录错误和分阶段进度。
- 工作线程只承担经过线程安全核查的文件读取和纯数据解析；资源发布、GPU 对象、ECS 提交、物理与脚本生命周期仍在主线程安全点完成。不能直接把现有 StageScene 整体放到后台。
- 异步准备结束后允许调用方控制激活，供 Loading 动画和过场使用；进度以已知字节或明确阶段计算，激活完成前不得显示完成。
- Single 保留旧场景直至目标准备成功；定义新场景启动失败后的恢复策略。Additive 独立加入集合，明确相机、UI 焦点、音频监听器和物理世界的选择策略。
- 第一阶段采用每场景独立物理世界；跨场景碰撞不隐式支持。跨场景实体引用需携带场景实例身份，卸载后访问返回明确的失效结果。
- 持久对象移入专用持久场景，迁移整个根子树；重复标记幂等。脚本对象是否能保持原实例以及物理、音频重绑定需由生命周期实现验证，不能仅复制实体冒充持久化。
- Unload 按场景实例请求，在安全点停止更新、派发生命周期、解除输入和音频注册、清理物理、释放场景引用；共享资产按实际所有者保留。卸载当前活动场景须指定替代目标或明确进入空场景状态。
- Stop 取消并回收后台操作后清理全部场景；迟到结果按操作代次丢弃，避免停止 Play 后再次激活。

### 验收

扩展 [SceneManagerRegression](../Tests/PhysicsRegression/src/SceneManagerRegression.h)：Loading 在读取期间继续更新；失败或取消不破坏旧场景；叠加两场景后单独卸载一个；管理器跨 Single 加载保持状态且不重复 Awake；卸载后的句柄失效；反复加载卸载后场景、脚本和资源计数回到稳定基线。Editor、Player 的主循环必须使用同一调度约定。

## 3. 调试与性能分析

源码入口：[Instrumentor](../TomCat/src/TomCat/Debug/Instrumentor.h)、[Renderer2D](../TomCat/src/TomCat/Renderer/Renderer2D.h)、[ConsolePanel](../Editor/TomCatInut/src/panels/ConsolePanel.cpp)、[ScriptHost](../Managed/TomCat.ScriptHost)。

- CPU：在现有 scope 上增加有界帧采样缓冲、线程轨道、帧号及父子关系；面板支持暂停采集、选帧、查看耗时与调用次数。区分墙钟耗时和线程 CPU 时间，避免标签误导。
- GPU：OpenGL 后端使用异步时间查询和延迟读取；结果尚未就绪时显示等待，不强制同步 GPU。按帧关联绘制批次、提交次数和图元数；不支持的后端明确显示不可用。
- 内存：分别报告进程内存、托管堆统计、引擎可追踪分配和 GPU 资源估算，不混成一个精确总量。每项数据标明来源、单位和采样时刻。
- 资源：跟踪 Handle、类型、所有者、加载状态、CPU 字节、GPU 估算字节；提供快照差异和缓存/活跃引用分类。资源仍被缓存持有不自动判定为泄漏。
- C#：交付可复现的 Debug 构建、符号生成、源码匹配、附加进程、异常断点和停止 Play 后重新附加流程。先验证现有宿主和调试器组合，再承诺自动附加功能。
- 采样关闭路径保持低开销；采集开启使用固定容量并报告丢弃数量，性能工具自身不能造成无限内存增长。

验收：植入可控慢 scope 并能在对应帧定位；GPU 查询未就绪不阻塞；反复切关的资源快照可比较；C# 在 Player 和 Editor Play 命中断点并检查变量。记录采样开启/关闭时的开销，CPU 自动化检查与 GPU/断点实机验收分开报告。

## 4. UI 产品能力

源码入口：[RuntimeUI](../TomCat/src/TomCat/Runtime/RuntimeUI.h)、[Components](../TomCat/src/TomCat/Scene/Components.h)、[RuntimeUIComponentDescriptors](../TomCat/src/TomCat/Scene/RuntimeUIComponentDescriptors.cpp)、[RuntimeUIRegression](../Tests/PhysicsRegression/src/RuntimeUIRegression.cpp)。

- 输入框：编辑缓冲、光标、选区、剪贴板、只读/密码/长度限制、提交/取消和焦点；删除和光标移动不得截断 UTF-8，按用户可见字符处理组合字符。
- IME：平台层区分按键、预编辑组合文本和最终提交文本；组合态不写入已提交值；候选窗跟随光标，正确换算 DPI 与 Game 视口坐标。失焦和场景卸载清理组合状态。
- 滚动视图：滚轮/拖动、范围约束、嵌套滚动传递及裁剪命中；列表增加数据绑定、可视项复用和选中项身份。不可为全部长列表项目每帧创建实体。
- 滑条：最小/最大值、步长、横竖方向、键盘和手柄操作、禁用状态；事件只在数值变化时触发，拖动期间正确消费游戏输入。
- 主题：语义颜色、字体、间距与状态样式使用资产引用；主题默认值与控件局部覆盖分离，切换主题不覆盖用户显式设置。
- 本地化：游戏文本采用稳定 key、语言表、参数和明确的回退规则；支持运行时切换并使布局失效重算。Hub 中英文切换和字体回退不能代替游戏文本本地化。
- 所有控件接入组件注册、序列化、Inspector、Prefab、托管接口及 Cook 依赖收集，统一 pointer/键盘/手柄焦点与输入消费约定。

验收：制作设置页、千项背包和中文姓名输入示例；验证 DPI、裁剪、导航、滚动和空数据；切语言后布局更新；Windows IME 验证组合、候选、提交和失焦；Web 单独验证宿主输入桥接，不能用原生测试代替。

## 实施顺序与完成标准

1. **P1：Prefab 关联基础**。稳定身份、持久关联、字段覆盖、更新、Apply/Revert、撤销和格式迁移。
2. **P2：Prefab 组合**。结构覆盖、嵌套、变体、依赖校验与 Cook。
3. **S1：异步 Single**。操作状态、取消、主线程激活及 Loading 示例。
4. **S2：多场景**。叠加、持久对象、卸载、脚本/输入/物理调度和引用失效。
5. **D1/D2：调试**。CPU 与资源快照先落地，再接 GPU 和经实机验证的 C# 断点流程。
6. **U1/U2：UI**。滑条和滚动视图先落地，再实现输入框/IME、虚拟列表、主题和本地化。

各阶段分别提交可运行示例、针对性回归和限制说明。新增持久化字段前确定格式兼容策略；以 [Version.h](../TomCat/src/TomCat/Core/Version.h) 为版本真值，不在设计阶段提前修改版本。

针对性验证完成后运行 `Scripts/Run-Regressions.ps1` 做集成验收；有图形上下文、IME 和断点的操作补充实机记录。只有经过实现与验证的能力才更新项目首页的已支持功能；未执行的验证不得标记通过。
