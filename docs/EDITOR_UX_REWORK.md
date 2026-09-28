# 编辑器 UI 与交互改版记录

当前状态核对：2026-09-20。编辑器依据 Unity 对比调整交互与布局，同时保留 TomCat 原有图标资产。
Hub 已单独恢复原有布局，后续仅更新获确认的应用和场景模板图标；不沿用编辑器改版样式。
旧开发阶段测试记录保留在下文，本次桌面录屏范围见[录制档案](portfolio/README.md)。

## 已实现

| 页面 / 流程 | 修改 |
| --- | --- |
| 全局布局 | 深色菜单与面板统一；移除空的 Services / Jobs / Tools；默认、动画、调试布局；恢复默认布局；会话级界面缩放 |
| Hierarchy | 名称搜索并保留祖先；Ctrl 多选、Shift 连选；场景切换清理多选状态 |
| Inspector | 标签栏最右侧锁定实体或资源；移除 Inspector 顶部组件过滤，仅 Add Component 弹窗保留添加搜索；统一属性标签、溢出提示；紧凑 XYZ；轴值右键重置；多对象公共反射属性编辑及失败回滚 |
| 组件操作 | 属性复制、粘贴、重置；复用编辑历史事务；实体引用不进入属性剪贴板 |
| Project | 项目资源搜索、类型过滤；默认显示 Packages；Assets 使用普通文件夹图标；保留拖放、右键和双击操作；单列图片也使用真实缩略图 |
| Asset Inspector | 选中资源在主 Inspector 显示；覆盖图片、脚本、Shader、音频、字体、Mesh、Material、Scene、Prefab、动画资源、文件夹与通用文件；导入设置 Apply/Revert 保留草稿和冲突检测，Packages 只读 |
| Prefab | 源文件定位、结构变更列表、属性基线/当前值对照、逐属性 Apply/Revert、整体操作说明；Apply 纳入源文件撤销记录 |
| UI 编辑 | Slider / Input Field / Scroll View 创建预设；图形化 4×4 锚点选择；RectTransform 左侧标签；按钮状态颜色预览 |
| 主题 / 本地化 | 主题颜色与字体、样式预览；语言和回退语言；多语言键值表、新增键/语言、缺失与显式空值区分、右键恢复回退、YAML 修复入口 |
| 动画资源 | AnimationClip 帧缩略图、播放预览、帧选择与选中帧详情；Clip / Controller / Palette 保存状态 |
| 图集 / Tile Palette | 图集预览、切片边界和枢轴、缩放、拖动切片、Shift 调整大小、Alt 调整枢轴；调色板工具栏换行 |
| Project Settings | 可调整窗口；按页面关键词搜索；Player 标签与引用选择；物理碰撞矩阵显示层名称；长文字换行 |
| Build Settings | 内容滚动与底部操作分离；明确 Windows 64 位构建能力；移除不可用平台和伪选项 |
| Console | 消息搜索、双行日志列表、重复折叠、严重级别计数、独立详情、复制详情、打开关联 C# 源文件；窄面板将搜索放入 Clear 菜单 |
| Profiler | CPU Usage / GPU Usage / Rendering / Memory 模块图表；Hierarchy / Timeline / C# Debugger 详情；保留录制、帧选择、资源基线和 CPU Trace 导出，未实现模块禁用 |
| Runtime Scenes | Play 模式的加载进度、激活控制、取消、Single/Additive 请求、活动场景、卸载、持久根对象及错误反馈 |
| Hub | 恢复原有 Hub 页面；使用已确认的猫耳立方体应用图标与彩色 2D / 3D 场景模板图标，Windows 可执行文件嵌入应用图标 |

## 当前界面约定

- 原有 Packages 图标尽可能复用到组件、资源、场景、工具栏与面板；不是整体替换为 Unity 图标。
- Inspector 锁定与标签在同一行，靠最右侧；不在内容区额外放置锁定栏或组件搜索栏。
- 窗口标题禁用折叠，标签右键提供 Maximize / Close Tab / Add Tab；实体树和组件内的分组仍可展开收起。
- 资源 Inspector 展示当前引擎真实数据；脚本字段元数据不等于脚本默认引用赋值，Mesh 信息不等于完整 3D 渲染器。

## 历史开发阶段验证记录

以下是改版开发阶段已有记录，不代表 2026-09-20 文档更新重新运行了整套测试。
本次新录屏只覆盖 Hub 模板、资源选择、Console 搜索、Profiler 与基础物理播放控制。

- `Scripts/Run-Regressions.ps1 -Configuration Release`：最终一轮全部通过，包含 Editor 编译、资源导入、物理、脚本、输入、恢复、性能记录及真实 Player 打包/启动验收。
- 新增 Advanced2DRegression：批量属性修改中第二个对象拒绝值时，前面的对象恢复原值；合法值统一生效。
- 新增 ImporterRegression：逐属性 Apply 只应用指定值并传播到兄弟实例，保留其他覆盖；逐属性 Revert 恢复基线；非法属性不改写源文件。
- Hub Release x64 独立编译通过。
- 原生界面实测：默认布局恢复；小窗口 Inspector 与 RectTransform 标签；深色菜单；图形化锚点预设弹窗；Slider 自动创建 Canvas、显示控件，单次 Undo 完整还原；Runtime Scenes 空状态；Hub 项目列表和新建项目表单。
- 图集拖动、组合键多选、跨语言输入等复杂手势未全部逐项进行人工验收；编译和底层回归不能替代这些界面检查。

## 功能边界

- 此次是现有编辑器的全面 UI/交互改造，不等于完整复刻 Unity。
- Prefab 逐属性操作覆盖共同存在组件的反射属性；新增/移除实体、组件与结构变化仍通过整体 Apply/Revert 处理。Inspector 显示其差异路径。
- 多选编辑覆盖公共反射属性；Scene 视口操纵器、实体树拖放和实体操作仍按主选对象工作。
- 资源检查器不是完整的材质球/Shader 图编辑器、音频波形编辑器或字体字形编辑器；它显示现有资源与导入能力。
- Console 可以打开 C# 源文件，但不保证第三方编辑器跳转到诊断行；行列号在详情中保留。
- Profiler 提供 CPU 时间线、GPU 耗时、资源分类统计与进程内存基线；尚不包含托管堆对象引用图、逐资源泄漏归因或 GPU draw-call 分解。
- 界面缩放在当前会话生效；Project Settings 搜索过滤页面，不逐字段定位；本地化表最多同时展示 30 个语言列，其余语言原文保留。
- 本次没有新增平台构建后端、运行时输入法候选窗协议或完整快捷键配置系统；页面没有以占位控件声称提供这些能力。
