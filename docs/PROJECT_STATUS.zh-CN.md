# 项目状态与验收

记录核对：2026-10-22。本文是当前交付、分支边界和验收状态的汇总入口；操作细节由专题指南维护，历史方案和原始验收保留在各自文档中。

## 当前基线

- 产品版本 **0.6.0**，版本常量以 [Version.h](../TomCat/src/TomCat/Core/Version.h) 为准。
- `main` 是功能集成主线。历史整理前代码基线为 `b04d81d8`，恢复发布关联后对应 `17b11340`（首次整理为 `65f38cfb`）；这些节点的 Git tree 完全相同。其后的记录整理提交只修改文档。
- 六个发布标签已重新关联到主线可达的等价发布快照，源码树与原版本一致，Release 附件保留。不能用版本号相同推断当前 `main` 与 `v0.6.0` 内容相同。提交映射、标签目标与同步方式见[项目演进](PROJECT_HISTORY.zh-CN.md#发布标签)。
- 桌面目标为 Windows x64，Vulkan 1.2 优先、OpenGL 4.6 回退；WebGL2/.NET browser-wasm 是实验目标。主线使用 Box2D 和 EnTT，专项分支能力单独记录。

## 已交付能力

这里的“已交付”指代码已进入主线；具体测试、交互和设备覆盖见后面的验收记录。

| 领域 | 主线能力 | 使用与边界 |
| --- | --- | --- |
| 项目与发布 | Hub 项目管理、私有 C# 工具链、CLI Cook/Build、独立 Player、原生模块发布 | [项目系统](../PROJECT_SYSTEM.md)、[模块发布](MODULE_PUBLISHING.md) |
| 资源与纹理 | Handle/.tcmeta、后台导入、派生缓存、压缩与 mip 链、采样参数持久化、可选 GPU 就绪 API | [采样参数](texture-sampling.md)、[纹理就绪](TEXTURE_READINESS.zh-CN.md) |
| 场景与 Prefab | 关联/嵌套/变体、整实例及逐属性覆盖、异步读取、分帧恢复、叠加与持久根对象 | [Prefab](PREFAB_WORKFLOW.zh-CN.md)、[场景流式加载](SCENE_STREAMING.zh-CN.md) |
| C# 与运行时 | 生命周期、物理回调、协程与作用域任务、托管依赖、存档、脚本投影和批量变更优化 | [脚本指南](../Managed/README.zh-CN.md)、[CoinRunner](../Samples/CoinRunner/README.md) |
| UI 与媒体 | Windows 字素/塑形/IME 预编辑、固定行高虚拟列表、本地化参数、2D 光照与后处理、OGG 导入和音频总线 | [Runtime UI](RUNTIME_UI_PRODUCT.zh-CN.md)、[P1 边界](P1_PRODUCTION.md) |
| 桌面渲染 | RHI、Vulkan/OpenGL、自动后端选择、双后端着色器包、多帧提交、上传页复用、source-over alpha 修复 | [RHI 与 Vulkan](RHI_VULKAN.zh-CN.md) |
| Web | 原生编辑面板、Project 编辑与场景持久化、托管脚本生命周期、常驻 WebAudio 音频 | [Web 指南](../Web/README.zh-CN.md) |
| 工具与质量 | Console、CPU/GPU Profiler、结构化 Editor 自动化、Windows Release 与 Managed Web CI | [调试](DEBUGGING_AND_PROFILING.md)、[自动化](Automation.md)、[验证命令](README.md#验证入口) |

## 验收记录

| 对象与基线 | 结果 | 证据与适用范围 |
| --- | --- | --- |
| 原主线 `b04d81d8`，2026-10-17 Windows CI | 通过 | [Regressions #39036211837](https://github.com/chnnasn/TomCat_Engine/actions/runs/39036211837)。Release 入口使用软件 OpenGL，覆盖托管、原生、应用/发布与自动化；不能据此声称 Vulkan 实机验收通过 |
| 同一原主线，2026-10-17 Web CI | 通过 | [Managed Web #39036211835](https://github.com/chnnasn/TomCat_Engine/actions/runs/39036211835)。托管 Web 发布、Chromium 生命周期和浏览器音频输出回归；不等于物理扬声器、多浏览器或真实手柄验收 |
| 2026-10-16 Vulkan 主线集成与多帧优化 | 历史记录通过 | [集成验收](RHI_VULKAN.zh-CN.md#主线集成2026-10-16)、[多帧验收](RHI_VULKAN.zh-CN.md#多帧与上传优化验收2026-10-16)。保留 GPU、命令、计数和本地日志位置；不自动覆盖后续纹理与 alpha 修复 |
| 2026-10-01/02 HUD、字体、构建配置与桌面交互 | 按检查项分别记录 | [生产验收](PRODUCTION_ACCEPTANCE.md)。桌面截图、Headless、Edge 冒烟与人工交互的范围分开，不合并为“全部通过” |
| 2026-10-01，5000 实体、600 秒 soak | **帧耗时门槛未通过** | [原始测量](PRODUCTION_ACCEPTANCE.md#大场景与内存验收)。前三阶段 P99 超过 50 ms；该次内存与资源回收检查通过，后续暂无独占目标机复测记录 |
| 2026-09-20 桌面演示 | 历史素材 | [录制档案](portfolio/README.md)。源码、程序与素材 Hash 保留；不是当前主线的实机验证 |

2026-10-22 的整理只核对源码、Git tree、文档与已完成的远端运行。新提交的 CI 状态应从 [Actions](https://github.com/chnnasn/TomCat_Engine/actions) 按 SHA 查询，不继承旧提交的检查状态。`build/`、临时目录中的历史日志通常未纳入 Git；记录路径不能代替可下载的证据。

## 待实现与待验证

| 状态 | 事项 |
| --- | --- |
| 待性能复测 | 独占目标机器重跑 soak；补真实 C#、资源上传、重物理与 OS Present 工作负载 |
| 待人工/设备验收 | 外部 IDE 断点/Watch/调用栈，真实中文/日文 IME、多屏 DPI、手柄、物理音频输出与跨浏览器覆盖 |
| 已实现但有边界 | 场景与纹理预算为协作式软预算；叠加/持久实体合并仍有原子事务。虚拟列表限纵向固定行高；非 Windows 不具备完整 ICU/IME 行为 |
| 待实现 | Web 流式音频；GPU draw-call 明细和托管堆对象归属分析；独立主题资产；完整移动 Player；主线生产级 3D 工作流 |
| 后端范围外 | Vulkan MSAA、独立传输队列和异步拾取等见 [RHI 限制](RHI_VULKAN.zh-CN.md#当前能力与限制)，不以“RHI 已集成”推断全部图形能力完成 |

## 专项分支

以下是 2026-10-22 从远端读取的开发分支快照，历史整理不改动这些分支。

| 分支 | 核对时提交 | 专项增量及与主线的关系 |
| --- | --- | --- |
| `dev_butter` | `f39996cc` | Butter 物理适配及 CCD，尚未替代主线 Box2D；见[迁移记录](https://github.com/chnnasn/TomCat_Engine/blob/dev_butter/docs/Butter-Migration.md) |
| `dev_ekit` | `c6784dd4` | ekit ECS 迁移，尚未替代主线 EnTT；见[迁移指南](https://github.com/chnnasn/TomCat_Engine/blob/dev_ekit/docs/EKIT_MIGRATION.md) |
| `dev_opengl3D` | `cc2a2001` | 静态模型、PBR、光照与阴影开发，尚未作为主线 3D 能力发布；见[3D 指南](https://github.com/chnnasn/TomCat_Engine/blob/dev_opengl3D/docs/OPENGL_3D.zh-CN.md) |

专项分支的旧验收结果只适用于其原基线。历史整理后继续集成这些分支时，应从新 `main` 准备功能增量，避免把旧主线的整段祖先再次合入。
