# 实机录制说明 / Capture notes

这些素材来自运行中的 TomCat Hub、TomCat Editor 和 OBS，不含 SVG、重绘界面、
合成鼠标或模拟物理动画。画面裁去底部 40 像素的 Windows 任务栏，保留软件界面与真实鼠标；
展示操作时 Hub / Editor 最大化。GIF 没有放大或单独裁取局部面板。

## 构建与操作

- 录制日期：2026-09-15，Windows x64。
- `Editor/Editor.sln` 与 `Builder/Builder.sln` 均经 MSBuild Release x64 编译成功。
- 新编译 Editor 以 `TomCat.exe` 名称部署到 Hub 的 `Editors/1.0.0/`，其 SHA-256 为
  `1B6E781E893850CCEE5FB8CD8A8AC7BEEE0C55258B8F2DDAFFBB87410600668C`。
- Hub 实际创建 `PhysicsPlayground` 2D 项目；之后在 Editor 中完成右键创建 Square、
  Inspector 旋转/移动/缩放、Sprite 显示、刚体与碰撞体添加、地板创建、运行控制和保存。
- 变换数值通过 Inspector 编辑。此处不声称演示了成功的 Gizmo 拖拽。
- Play 中实际观察到方块下落、接触地板并停稳；Stop 后恢复原始悬空姿态。
- 可复现项目：[Samples/PhysicsPlayground](../../Samples/PhysicsPlayground/README.md)。

## 已知限制

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

## 素材与剪辑

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
