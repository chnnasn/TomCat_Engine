# Unity 风格视觉规范与覆盖范围

2026-09-20。本轮仅美化编辑器及子页面；Hub 使用此前已提交版本的布局、字体、颜色和启动尺寸，Player 窗口参数不变。

## 视觉系统

- 深灰背景、较深的输入区域、稍亮的可点击按钮、蓝色选中状态；字段保留细描边。
- 编辑器使用 Segoe UI / Microsoft YaHei（不存在时回退到原字体），字号按启动屏幕 DPI 设置；偏好设置仍可调整界面缩放。
- 控件圆角 2、浮动窗口 3、弹出层 4 个基础单位；停靠边界保持平直。尺寸随 DPI 缩放。
- 场景、组件、工具栏、文件夹和资源图标统一使用 Packages 中对应的原有图片，沿用原映射，不因 Unity 参考而替换素材。
- 实际纹理缩略图继续显示资源图像。
- Inspector 锁定入口位于标签栏最右端；搜索框预留图标空间。


## 默认尺寸

- 编辑器启动参考尺寸 1440 × 900，按显示缩放换算，最大不超过主屏工作区宽 92%、高 88%，居中打开。
- 默认停靠布局将 Inspector 放在右侧，Project 放在底部，Hierarchy 和 Scene/Game 位于上方；动画与调试布局继续可用。
- 保存过的用户停靠布局优先，未保存布局时使用新的默认比例。
- 浮动工具窗口首次打开时居中，宽高受编辑器工作区约束；之后保留用户调整。

## 页面覆盖

| 层级 | 页面和变化 |
| --- | --- |
| 主页面 | Scene/Game 工具按钮、Hierarchy 行图标与搜索、Inspector 字段与锁定入口、Project 原有文件树图标和图片缩略图、Console 搜索 |
| 独立子页面 | Project Settings（含 Tags/Layers、Physics 2D、Player）、Build Settings、Editor Preferences、Runtime Scenes、Asset Inspector、Profiler（CPU、GPU、Rendering、Memory）、Animator、Animation、Tile Palette |
| 资源子编辑器 | Sprite Atlas Tools、Sprite 选择器、资源选择器、实体图标选择器；统一初始尺寸与弹出层外观 |
| 二级菜单 | Add Component 分类与搜索、组件菜单、动画状态菜单、对象引用选择；继承相同字体、控件和弹出层样式 |
| 操作弹窗 | 重命名资源/动画项、删除资源、未保存场景、场景恢复、项目升级预览、迁移恢复、Prefab Apply/Revert、锚点预设 |

新样式通过 EditorStyling 显式启用，不会自动改变 Hub 或其他使用 ImGui 的程序。

## 验证记录

- 已对照本机 Unity 2022.3.48f1 的实际窗口检查图标、控件密度和面板层级。
- 已原生界面检查默认布局重置、Hierarchy/Inspector、工具图标、锁定入口、添加组件二级菜单。
- 已原生界面检查 Project Settings 的 Tags/Layers、Physics 2D、Player 子页、Build Settings、Window/Panels 次级菜单，以及 Profiler 的 CPU/GPU、Memory/resources、C# debugger 三个模块。
- 已检查旧布局下的窄窗口；补充 Player 图标说明和锚点说明换行，工具窗口最小尺寸随字号变化，并复查构建窗口。
- Release 编辑器编译通过；Scripts/Run-Regressions.ps1 -Configuration Release 完整回归通过（包含 CLI 构建及实际 Player 运行验收）。最后的文字换行、最小尺寸和菜单圆角修正再次通过 Release 编译及原生界面检查。
- 未逐一手工打开所有资源类型、弹窗状态，也未验证所有显示器 DPI 组合；页面覆盖表表示代码样式覆盖范围。
- Hub 目录无本轮差异；公共字体和主题的新增设置仅在 EditorStyling 启用时生效。

## 截图反馈修正（2026-09-20）

- Game Stats 锚定面板可见区域右上角，不随游戏图片留白或滚动偏移；尺寸按字体和内容计算。
- Console 工具栏保持单行，窄面板裁切而不堆叠；日志使用严重程度图标、时间、双行内容和交替底色，Collapse 显示重复次数。窄面板的筛选和搜索仍可从下拉菜单访问。
- Profiler 改为模块开关列表、可选帧图表、上下可调分隔和详情区；保留真实录制、前后帧、CPU 表格/时间线、资源基线、跟踪导出与 C# 调试入口。尚无独立采集器的模块禁用并说明原因。
- 标签栏右键提供 Maximize/Restore、Close Tab、Add Tab；Game 的 Overlay Menu 可控制 Stats。
- Inspector 锁定按钮与标签同一水平线；移除独立锁定行及组件过滤搜索，Add Component 内的搜索仍保留。
- Project 恢复原有资源图标/图片预览绘制；Packages 固定显示，不再设置显示开关。

本次截图修正验证：Release 编译通过；ProfilerRegression、InputRegression 通过。原生界面已核对 Stats 定位和完整内容、Console 宽窄布局和搜索过滤、Profiler 录制/暂停/选帧/模块弹层、标签 Add Tab 和最大化还原，以及 Inspector 标题锁定在切换实体后保持原对象。

最终素材复核：EditorIconSet 沿用 HEAD 原始映射；组件、场景、播放、可见性、搜索与资源树均从 Packages 图标集取图。Add Tab 子菜单复用 Scene/Game/Hierarchy/Inspector/Project 图标，Add Component 复用 add.png。再次 Release 编译通过；原生检查确认锁定位于标签栏最右端、Packages 固定显示、Two Column 下 Circle/Square 显示真实图片缩略图。
