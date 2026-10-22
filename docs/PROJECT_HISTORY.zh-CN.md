# 项目演进与 Git 主线

`main` 按功能进入集成基线的顺序记录更新。阅读提交时，标题说明交付内容，正文保留必要说明、PR 编号与原提交来源；当前支持范围和验收结果由[项目状态](PROJECT_STATUS.zh-CN.md)维护。

## 功能演进

| 阶段 | 主要更新 | 对后续开发的作用 |
| --- | --- | --- |
| 引擎基础 | 应用生命周期、窗口、事件、输入、日志、图层、OpenGL 渲染抽象 | 建立平台与引擎模块边界 |
| 2D 渲染 | Shader/纹理、相机、变换、Quad 批处理、多纹理批次与性能追踪 | 提供场景绘制和基础性能观测 |
| 可视化编辑 | Docking、Framebuffer 视口、ECS、Hierarchy/Inspector、场景序列化、拾取和操作轴 | 将运行时对象接入可编辑、可保存的场景工作流 |
| 项目与发行 | 独立 Editor/Hub、项目管理、模板、资源打包、用户设置与发布 CI | 从单一运行程序扩展到项目创建和分发 |
| 资源与物理 | 稳定 Handle、`.tcmeta`、导入缓存、只读 Packages、Box2D、碰撞配置与编辑工具（#9–10） | 保持资源身份和场景物理行为可追踪 |
| 脚本与独立 Player | C# 生命周期、字段、运行时隔离、Prefab、Cook、Player 模板与完整性校验（#11–12） | 支持项目逻辑编写、打包和独立运行 |
| Web 与制作工具 | WebGL2 Player/编辑器、共享面板与预览（#13–15）；图集/动画/Tilemap、关联 Prefab、场景流式、UI、Profiler 和私有脚本工具链 | 扩展制作能力与浏览器运行路径 |
| 生产流程 | 存档、原生模块 SDK、CoinRunner（#20）、分帧激活、复杂文字、虚拟列表、媒体压缩、HUD/输入修复与当前格式策略 | 补齐发布验证、错误恢复和生产边界 |
| 自动化与 Web 交互 | 结构化项目操作（#27–28）、显示比例与布局（#21–26）、Project 持久化和脚本生命周期（#29）、协程与作用域任务 | 使编辑、脚本、运行和回归能够重复执行 |
| Vulkan 集成 | RHI、桌面后端迁移、多帧提交、上传页复用、双后端着色器包和自动回退 | 将既有工作流接入 Vulkan，同时保留 OpenGL |
| 运行时与资源维护 | WebAudio/动态 API/字体（#30）、脚本投影与批量变更（#31/#33/#36）、C# Cook 辅助源文件（#32）、有符号变换（#34–35）、纹理采样/就绪（#37–38）、alpha 合成（#39） | 修复真实流程问题并降低重复工作；性能收益仍需对应基准量化 |

括号中的编号对应仓库 PR。阶段用于概览，不替代逐条提交或验收记录。

## 阅读提交

```powershell
# 按主线开发顺序查看每次更新
git log main --reverse --format='%h %s'

# 聚焦渲染更新，或查看某次更新的文件差异
git log main --oneline --grep='renderer'
git show --stat <commit>
git show <commit>
```

提交标题采用 `feat/fix/perf/refactor/build/ci/docs/test/chore/style/revert` 分类，并在需要时附上模块名。例如：

- `feat(assets): expose GPU texture readiness to C# scripts (#38)`
- `perf(scripting): materialize component projections only when needed (#36)`
- `fix(renderer): preserve source-over alpha coverage (#39)`

后续提交应说明新增行为、修复对象或维护目的；一个提交围绕一个可解释的变更。功能进入主线后同步专题指南和状态入口，验证结果必须关联实际运行及源码基线。

## 历史整理与追溯

2026-10-22 对完整 `main` 历史整理：原可达历史为 393 个提交，第一父链有 208 个集成快照；重建为 206 个单父提交。两次只连接重复祖先、文件内容未变化的合并节点被跳过，其余集成快照逐一保持原 Git tree。之后单独提交文档整理，因此主线总提交数会继续增加。

- 原主线顶点：`b04d81d80a92fdf28ce7c27d37e24005e8a8f984`。
- 对应的新代码基线：`65f38cfb7b1345dae411882f76b3fa7204e17722`。
- 原始完整历史保存在标签 `archive/main-before-cleanup-2026-10-22`，包含原功能分支的细粒度提交和合并关系。
- [新旧提交映射](history/main-rewrite-map.tsv)记录全部 208 个原集成节点。`rewrite` 表示保留同一文件快照；`skip-identical-tree` 指向内容相同的前一个新节点，不代表删除了功能。
- 新提交正文的 `Original-Commit` 指向原集成提交；PR 和旧 CI 仍属于原 SHA，不能冒充新 SHA 的检查结果。
- 现有 `v0.1.0` 至 `v0.6.0` 发布标签及三个专项开发分支保持原引用。它们仍位于原历史，不要求成为新主线的祖先；不要仅用 `git describe` 判断当前产品版本。

```powershell
# 核对重写前后的代码快照；应没有差异
git diff --exit-code archive/main-before-cleanup-2026-10-22 65f38cfb --

# 需要原始开发细节或旧 CI 对应源码时
git log archive/main-before-cleanup-2026-10-22 --graph --oneline
git show <original-commit>
```

这次整理改变提交身份与呈现顺序，不改写历史验收结果，也不把专项分支能力算作已经合入主线。

## 已有克隆如何同步

旧 `main` 与新 `main` 的 SHA 不兼容。先保留本地工作，不要直接把旧主线 `git pull` 合并回新主线。没有未提交改动时，可以保留旧分支再重建本地跟踪分支：

```powershell
git status
git fetch origin --tags
git switch main
git branch -m archive/local-main-before-cleanup
git switch -c main --track origin/main
```

若本地有未提交修改，先提交或另行保存；若有未推送提交，从保留的旧分支选取自己的增量，逐项移植到新主线并验证。不要对包含旧主线历史的专项分支直接整段合并。
