# 生产验收与构建配置

本轮范围：Windows 2D 桌面、托管 Web 浏览器冒烟、合成大场景重复加载/卸载，以及 C# 开发/正式构建分离。构建通过、自动测试、真实交互与设备覆盖分别记录。

## 文档与交互

README 中 OGG/Vorbis、IME 预编辑、ASTC/ETC2 和 TCPAK v8 已同步；细节以 P1_PRODUCTION.md 为准。

桌面验收发现 CoinRunner 生成器仅写世界变换、未绑定 Sprite、分数文本缺少 Canvas，中心锚点的边缘定位又在高 DPI 下将文字推出视口，使无头逻辑测试通过却无法正常游玩。本轮修复生成器与样例场景，并检查相机配置返回值，HUD 改为左上角锚点加 32 像素参考边距。保留用户修改的文字颜色。`CoinRunnerGen --refresh-scene` 显式刷新现有样例场景，保留脚本文件；默认仍不覆盖已有工程。运行前关闭工程。

补齐 Sprite 后，真实无头 Player 曾在同步场景解析时创建纹理而崩溃；同步解析及叠加场景复制现与异步路径一致，只保留资源句柄，由渲染阶段加载 GPU 资源。修复后 CoinRunner 三轮打包冒烟通过。

2026-10-01 全量 `Run-Regressions.ps1 -Log` 已通过：Managed、全部原生回归、Editor/Hub/CLI/Player 构建、Player 模板及 C# 端到端、CoinRunner 三轮和模块发布。日志：`build/production-acceptance-final.log`。新增 HUD 像素回归覆盖 720p/1080p × 100/150/175/200% DPI，开发/正式编译配置隔离回归也通过。

交互矩阵：

2026-10-02 后续验收发现 ScoreText 间歇消失的独立根因：`RefreshSourceState` 每 0.35 秒调用完整资源刷新，清空已发布字体，直到异步图集重建才恢复文字。源码发现现在保留元数据未变的资源；身份、路径或导入设置变化仍使缓存失效，显式刷新也保留原行为。新增回归检查重复发现不丢失同一字体实例、720p/1080p 和多 DPI 像素、连续三次场景快照恢复后的 Scene/Game 显示，以及显式刷新失效。`build/hud-fixed-regression.log` 与 `build/hud-script-regression.log` 通过。修复版桌面 Game 连续 30 次截图（5.5 秒）和 Scene 连续 30 次截图（62 秒）均无文字空白，取样区域内红色像素数量保持一致；这不是逐帧性能测试。

| 检查 | 当前结果 |
| --- | --- |
| 桌面打开工程、HUD 与脚本组件 | 2026-10-02 修复源码轮询后实看：Scene/Game 文字持续可见，保留用户的红色及 `Coins: 0/4fff`；脚本与原生组件菜单顺序一致，脚本标题栏启用框可切换、撤销恢复 |
| 桌面 Play/Pause/Step/Resume/Stop 与完整游玩 | 修复版播放控制完整序列已复验，Play 显示 Score/Best，Stop 恢复编辑文本；此前用户游玩日志记录 WIN，本轮未重做完整通关 |
| 外部 IDE 断点、Watch、调用栈 | 未验收；编译与 PDB 回归不能替代调试器实测 |
| 真实中文/日文 IME、多屏 DPI、手柄 | 未验收；需要相应输入法、显示器及设备 |
| 浏览器托管编译、生命周期、异常隔离、播放控制 | 本机 Edge headless 通过；不等同人工交互或多浏览器覆盖 |

## Web CI 与浏览器冒烟

```powershell
./Scripts/Build-WebManaged.ps1
npm ci --prefix Web/tests --ignore-scripts
node Web/tests/node_modules/playwright/cli.js install chromium
./Scripts/Run-WebBrowserSmoke.ps1
```

可通过 `TOMCAT_BROWSER_CHANNEL=msedge` 使用已安装 Edge。测试服务器只监听 127.0.0.1，设置 COOP/COEP，并在 finally 中关闭浏览器和服务器。`build/web-smoke-results` 保存 JSON、浏览器截图和日志；失败退出非零。托管脚本在真实浏览器内通过 Roslyn 编译并安装，验证健康脚本继续更新、异常脚本隔离、清理回调只执行一次及 Play/Pause/Step/Resume/Stop。

2026-10-02 Inspector/源码发现修复后再次通过完整托管 Web 构建及 Edge 冒烟：`build/hud-web-build.log`、`build/hud-web-smoke.log`，结果 `ok=true`、`shutdown=true`。

`.github/workflows/web-managed.yml` 在 push/PR 和手动触发时发布托管 Web 并运行 Chromium 冒烟，始终上传证据。提交工作流不代表远端 CI 已运行。

## 大场景与内存验收

```powershell
# 先编译 Tests Release x64
./Scripts/Run-ProductionSoak.ps1 -DurationSeconds 600
```

每轮增量恢复 5000 实体，分帧启动、更新、停止和卸载；每次推进同时绘制 5000 个四边形到 1280×720 离屏缓冲，并通过 glFinish 计入 GPU 完成等待。输出各阶段原始 frame_ms、P95/P99/峰值及卸载后进程 PrivateUsage、纹理/缓冲/帧缓冲数量。

这是合成原生工作负载；不包括 OS Present、YAML 解析、真实游戏 C#、重物理或大量资源上传，不可解释为整个引擎或所有游戏的帧率保证。跳过前两轮后比较前三轮与末三轮的平均私有内存；至少八个完整循环才可通过。默认阈值 P99≤50 ms、峰值≤250 ms、私有内存增长≤32 MiB，资源数量每轮必须回归基线。这是本验收配置，项目应按目标设备收紧预算。

### 本机 2026-10-01 记录

600 秒、22 轮、39,989 帧；卸载后的纹理/缓冲/帧缓冲数量始终为 1/4/1，稳定阶段私有内存增长 0.5625 MiB。

| 阶段 | P95 ms | P99 ms | 峰值 ms |
| --- | ---: | ---: | ---: |
| 增量解码 | 37.584 | 54.416 | 139.793 |
| 分帧激活 | 30.608 | 53.587 | 53.587 |
| 运行 | 37.503 | 52.665 | 80.389 |
| 卸载 | 17.704 | 25.824 | 25.824 |

**本轮验收失败**：前三阶段 P99 超过 50 ms，未放宽门槛。内存与资源回收通过这次有限时长的检查，不代表数小时稳定性。运行时还有编辑器/编译任务，后续应在独占目标机器重测，并补真实资源上传与 OS Present 测量。原始证据在 `build/production-soak/{frames.csv,cycles.csv,summary.json,run.log}`。

## 开发 / 正式构建

- 编辑器脚本默认 Development：关闭 C# 优化，portable PDB，便于外部 IDE 附加。
- CLI cook/build 默认 Production：启用 C# 优化；`--development` 显式切换为开发构建。
- 编辑器 Build Settings 的 Development Build 控制 Player 脚本配置；默认正式构建。本轮开关为编辑器会话设置，不写入项目格式。
- 两种配置均保留 portable PDB。Production 使用 `Library/ScriptAssemblies/Production` 与 `Library/ScriptProject/Production`，开发配置保持原有目录；配置进入源 Hash，避免复用错误配置的 last-good。
- Player 编译使用独立编译器，结束后恢复编辑器原来的托管元数据运行时，避免正式构建替换编辑器开发状态。
- 符号保存在对应构建目录，发布时应归档 DLL/PDB 与 BuildID；本轮未加入自动符号服务器或向 Player 包嵌入 PDB。此配置只控制游戏 C#，不改变原生引擎 Release/Dist 配置，也不承诺 Play 热替换。

配置回归检查优化标志、符号存在、Hash/路径隔离及正式构建不破坏开发 last-good。

## C# Inspector 一致性

脚本与原生组件共用标题栏绘制：折叠箭头、脚本图标、唯一 Enabled 复选框和右侧竖三点。正文首行为只读 Script 引用，字段以左右两列展示；资源/实体引用展示名称及选择器，保留拖放。

组件菜单支持 Reset、Remove、脚本附件内 Move Up/Down、Copy、同脚本 Paste As New/Values、按脚本附件查找场景引用、Properties 和 Edit Script。禁止多实例的脚本禁用 Paste As New；Play 中禁止修改。Edit Script 复用已配置外部 IDE；Web 没有此回调时禁用。移动仅影响脚本附件次序，脚本 ExecutionOrder 规则仍优先；复制粘贴保留字段引用，跨场景复制实体引用仍需要用户重新绑定。

原生、注册式与 C# 组件菜单采用相同项目顺序和分隔线；不适用的操作灰显。原生组件当前只实现属性 Reset/Copy/Paste Values 及可移除组件的 Remove，其余菜单项不冒充可用。注册式组件的单选 Enabled 移入标题栏。Script 引用使用深色内凹字段、左对齐脚本图标与名称、右侧圆形定位按钮；双击字段打开外部 IDE，圆形按钮定位脚本资产，不替换组件类型。
