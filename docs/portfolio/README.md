# 实机录制说明

中文文档 · 整理日期：2026-09-20 · [文档索引](../README.md)

主 README 使用最新的 2026-09-20 桌面录屏。2026-09-15 的创作流程、原片信息与素材保留在
本页后半部分，便于对照；它们不代表当前提交重新通过同样的验收。

## 2026-09-20：Hub 图标与编辑器界面更新

本次运行源码提交 `8393eeb` 对应的 Windows Release Hub / Editor，使用 OBS 32.0.2 实际录屏。
没有重绘界面、合成鼠标、模拟物理或 AI 插帧。程序最大化显示，仅裁掉底部 40 像素任务栏，
保留完整应用视图与真实鼠标。GIF 无音频、字幕、标题卡或其他覆盖物。

| 文件 | 实际展示 | 时长 / 速度 |
| --- | --- | --- |
| [01-hub-templates.gif](2026-09-20/01-hub-templates.gif) | Hub 原有布局、猫耳立方体图标、2D / 3D 场景模板选择 | 6.00 秒 / 150 帧，1.25× |
| [02-asset-inspector.gif](2026-09-20/02-asset-inspector.gif) | 实体组件、Scene 资源摘要、包内图片导入设置与预览、单列缩略图 | 8.04 秒 / 201 帧，1.5× |
| [03-console-search.gif](2026-09-20/03-console-search.gif) | 真实编译日志、搜索 TCSP1000、选择消息并查看详情 | 6.52 秒 / 163 帧，1.5× |
| [04-profiler.gif](2026-09-20/04-profiler.gif) | 标签右键最大化、录制与停止、选帧、Hierarchy / Timeline、模块菜单 | 9.56 秒 / 239 帧，采样段 1×，界面操作 1.5× |
| [05-play-controls.gif](2026-09-20/05-play-controls.gif) | 方块下落、接地停稳、Pause / Step / Stop，恢复编辑状态 | 9.04 秒 / 226 帧，全程 1× |

截图：[Hub 模板](2026-09-20/hub-templates.png)、[资源 Inspector](2026-09-20/editor-assets.png)、
[Profiler 时间线](2026-09-20/profiler-timeline.png)。GIF 与截图尺寸为 **1280×680**，GIF 为 **25 fps**。
精确片段、速度、截图时间及二进制 Hash 见 [capture.json](2026-09-20/capture.json)。

当前为精简剪辑：删去点击前后的空等，每段约 6–10 秒；物理下落保持原速。
GIF 使用 128 色调色板、关闭抖色并优化变化区域，保持原有分辨率和帧率。
Profiler GIF 由约 11.6 MiB 降至 2.1 MiB，减少首次加载等待。

### 运行程序与原片

| 程序 | 源码构建入口 |
| --- | --- |
| Hub | `Builder/bin/Release-windows-x86_64/Manager/Manager.exe` |
| Editor | `Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe` |

原片 `2026-09-20 11-39-52.mp4` 为 1280×720、30 fps、8 分 22.83 秒，
留在本机 Videos 目录，没有加入 Git。SHA-256：
`3ff5805a3bf33478c5c4cfbaa8819ef01e330ca209315404b51f33e260a60010`。

### 本次观察与覆盖边界

- Hub 未识别到可用 Editor 安装，录屏保留了真实提示；仅演示模板选择，没有声称成功创建项目或从 Hub 启动。
- Editor 通过 **File → Open Project** 打开仓库中的 [PhysicsPlayground](../../Samples/PhysicsPlayground/README.md)，场景为 `Assets/Scene/sample.tomcat`。
- Inspector 选择的 `material.png` 是 Packages 中的图片图标，显示 Texture2D 信息，不是 Material 资产编辑演示。包资源只读，Apply / Revert 禁用。
- Profiler 在 **Edit 模式**采样，OBS 同时运行；曲线不能作为引擎帧率或物理性能基准。CPU Usage / GPU Usage / Rendering / Memory 可选，其余未接入模块禁用。
- Play 中矩形实际下落并停稳，Stop 恢复 Y=1.5 和 Z 轴旋转 25°；Step 点击时物体已停稳，不用于证明单步位移量。
- 本次没有演示 C# 断点附加、Prefab 编辑、异步场景切换、UI 控件创作、Player 打包或浏览器端，也未重跑完整回归套件。

### 重新导出

需要 Python 3.11+ 和包含 libx264、palettegen、paletteuse 的 FFmpeg。从仓库根目录执行：

```powershell
python docs/portfolio/export.py "PATH/2026-09-20 11-39-52.mp4" --ffmpeg "PATH/ffmpeg.exe" --manifest docs/portfolio/2026-09-20/capture.json
```

脚本先校验原片 SHA-256，再按清单输出到同日期目录。未传 `--manifest` 时仍使用旧的
2026-09-15 剪辑配置；不同日期的输出不会相互覆盖。只导出已有原片画面，删除空等，按清单改变播放速度。

## 2026-09-15：历史 2D 创作流程

以下构建 Hash、操作结果和问题均来自 **2026-09-15** 的桌面录制，保留为历史证据。
它们不表示当前提交重新通过了验收；2026-09-16/17 的浏览器验收另见 [Web 记录](../../Web/README.zh-CN.md)。

这些素材来自运行中的 TomCat Hub、TomCat Editor 和 OBS，不含 SVG、重绘界面、
合成鼠标或模拟物理动画。画面裁去底部 40 像素的 Windows 任务栏，保留软件界面与真实鼠标；
展示操作时 Hub / Editor 最大化。GIF 没有放大或单独裁取局部面板。

### 构建与操作

- 录制日期：2026-09-15，Windows x64。
- `Editor/Editor.sln` 与 `Builder/Builder.sln` 均经 MSBuild Release x64 编译成功。
- 新编译 Editor 以 `TomCat.exe` 名称部署到 Hub 的 `Editors/1.0.0/`，其 SHA-256 为
  `1B6E781E893850CCEE5FB8CD8A8AC7BEEE0C55258B8F2DDAFFBB87410600668C`。
- Hub 实际创建 `PhysicsPlayground` 2D 项目；之后在 Editor 中完成右键创建 Square、
  Inspector 旋转/移动/缩放、Sprite 显示、刚体与碰撞体添加、地板创建、运行控制和保存。
- 变换数值通过 Inspector 编辑。此处不声称演示了成功的 Gizmo 拖拽。
- Play 中实际观察到方块下落、接触地板并停稳；Stop 后恢复原始悬空姿态。
- 可复现项目：[Samples/PhysicsPlayground](../../Samples/PhysicsPlayground/README.md)。

### 历史录制遇到的问题与覆盖范围

Hub 启动的是本次编译的 Editor，但项目自动加载触发了：

```text
Project migration recovery inspection failed:
Could not pin project directory for migration: could not pin directory '//?/':
The filename, directory name, or volume label syntax is incorrect.
```

本次使用 **File → Open Project** 和普通 Windows 路径打开新建项目。
第一段 GIF 从 Hub 创建完成切到已最大化的 Editor，再展示手动打开；未把失败的自动加载描述为成功。
旧版 Hub 的旧 `PhysicsPlayground` 没有被覆盖。

这些片段覆盖基础 2D 创作与物理流程；没有在本次录制中演示 C#、动画、音频、Prefab、
资源导入或独立 Player 导出。工程其他功能的列表见主 README。

### 素材与剪辑

原片为本机 OBS 32.0.2 录制的 `2026-09-15 12-05-05.mp4`：
1280×720、30 fps、13 分 13.63 秒。原片留在本机 Videos 目录，没有加入 Git 仓库。

原片 SHA-256：`40E253F9F8A899928BCBFE42F25FCB9F542485392BA17B7879A50F24E3089206`。

| 文件 | 内容 | 剪辑 |
| --- | --- | --- |
| `01-hub-project.gif` | 创建 2D 项目、最大化 Editor、手动打开项目 | 删除空等，操作 1.5–2× |
| `02-sprite-transform.gif` | 右键创建 Sprite，编辑旋转/位置/缩放 | 删除空等，操作 1.5–2× |
| `03-physics-authoring.gif` | 动态刚体、碰撞体、第二个矩形地板 | 删除空等，操作 1.5–2× |
| `04-play-pause-step-stop.gif` | 真实下落与播放控制 | 下落与停止 1×，暂停/单步操作 1.5× |
| `editor-fullscreen.png` | 配置完成后的软件界面 | 原片第 686 秒抽帧，去除任务栏 |

GIF 与截图为 1280×680，已去除 Windows 任务栏。GIF 从 30 fps 原片重新剪辑导出为 25 fps，
保留原片鼠标，删除无操作的停顿，菜单与参数只短暂停留。物理下落、接触和停稳保持原速。
帧率转换仅采样原片画面，不使用 AI 插帧或运动补偿；没有添加字幕、标题卡或画面覆盖物。
精确剪辑区间见 [cuts.json](cuts.json)，
每项依次为原片开始秒、结束秒、播放速度。

如需从本机原片重新导出：

```powershell
python docs/portfolio/export.py "PATH/2026-09-15 12-05-05.mp4" --ffmpeg "PATH/ffmpeg.exe"
```

展示形式参考用户指定的 [Unity-Skills 安装演示](https://github.com/Besty0728/Unity-Skills/blob/main/docs/installation-demo.gif)，
本目录所有展示画面均录自 TomCat 自身。
