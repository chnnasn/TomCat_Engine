# TomCat Engine

一款基于 **C++20** 开发的 **2D 游戏引擎、编辑器与项目中心（Hub）**，构建于 OpenGL、ImGui 和 Box2D 之上。

TomCat 当前优先完成可实际使用的 2D 开发流程。现有 3D 项目模板仍属于实验功能：它只配置透视相机，尚未实现生产级 3D 渲染器。

**语言**：[English](README.md) | 简体中文

---

## 特性

- **2D 渲染**：OpenGL 批渲染 Sprite、线条和圆形，支持相机、基于 Framebuffer 的 Scene/Game 视图与实体拾取
- **场景系统**：ECS 实体、父子层级、稳定 UUID、严格 YAML 场景序列化和原子保存
- **资产身份工作流**：稳定 `AssetHandle` 引用、`.tcmeta` Sidecar、Registry 重建、事务化移动/删除和缺失资产占位
- **2D 物理**：固定 60 Hz Box2D 运行时、显式/隐式静态刚体、Box/Circle 碰撞体、Trigger、过滤、Raycast/AABB 查询、力、冲量和 `DistanceJoint2D`
- **物理编辑体验**：Scene 视图碰撞轮廓、碰撞体句柄、项目 Tag/Layer 与 Physics 2D 碰撞矩阵、Play/Pause/Step/Stop，以及延迟派发的 NativeScript Collision/Trigger 回调
- **编辑器**：ImGui 驱动的 Scene、Game、Hierarchy、Inspector 和 Project 面板，支持项目级布局与用户设置持久化
- **Cooked Runtime 基础**：按资产 Handle 寻址、无作者路径的 `.tcpak` v3，并为最小 Cooked Player 嵌入项目碰撞矩阵
- **Hub 项目管理器**：项目创建与发现、Editor 版本选择和用户级最近项目状态
- **本地化**：动态生成 ImGui 中文字符集，Hub 支持中英文切换
- **回归测试**：第一方 2D 物理套件覆盖固定步长、回调、运行时重建、查询、关节、Scene Schema v9 和 Cooked Package 往返

## 当前范围

- 支持的开发平台：**Windows x64**
- 渲染后端：**OpenGL 4.6**
- 当前引擎主范围：**2D**
- 现有打包脚本发布的是 **Editor 和 Hub**，不是用户制作的独立游戏
- `.tcpak` 与 `--play-cooked` 是运行时基础；Editor 的 Build Game 流程和独立 Player Target 仍待开发

## 构建

### 依赖

- Windows x64，并具备支持 OpenGL 4.6 的显卡与驱动
- Visual Studio 2022+
- Python 3 与 `pip`（供 Setup 辅助脚本使用）
- premake5（Setup 脚本自动下载）

> Vulkan SDK 通过 git 子模块自动提供（vendor/VulkanSDK，基于 VulkanSDK-Windows），无需手动安装或设置环境变量。

### 步骤

1. 克隆仓库（含子模块：git clone --recurse-submodules ...）
2. 首次运行前执行 `Scripts\Setup.bat`，准备 Premake 与 Setup 依赖
3. 运行 Scripts\Win_GenProjects.bat 生成 VS 工程
4. 根据需要打开 `Editor\Editor.sln`、`Builder\Builder.sln` 或 `Tests\Tests.sln`，并以 Release x64 编译目标

由 `Scripts\Setup.bat` 准备 Premake 后，运行 `powershell -ExecutionPolicy Bypass -File Scripts\Run-PhysicsRegression.ps1` 可生成、编译并执行 2D 物理与基础 Sprite 回归套件。

## 目录结构

```
TomCat/            引擎核心库（渲染、ECS、物理、ImGui 集成）
Editor/TomCatInut/ Editor 应用
Builder/Manager/   Hub（项目中心）
Tests/             引擎回归测试工程
Scripts/           构建与打包脚本
vendor/            premake 与第三方依赖
```

## Editor 与 Hub 打包

- 本地打包：Scripts\Package-Editor.ps1 / Scripts\Package-Hub.ps1
- CI 打包：GitHub Actions（workflow_dispatch 选择 editor / hub / both）；Editor 发布为 `TomCat.zip`（`TomCat.exe` + 外置 `Packages/`），Hub 仍发布为封装后的单 exe
- 这些脚本用于发布 TomCat 本身；目前尚不能把用户项目导出为独立游戏。

## 当前状态与 Roadmap

### 已完成的 2D 物理里程碑

- [x] 固定步长 Play / Pause / Step / Stop 状态模型（不设置独立 Simulate 状态）
- [x] 仅 Scene 视图显示 Box/Circle 碰撞轮廓，并支持 Edit Collider 句柄
- [x] CircleCollider2D、隐式静态刚体、Trigger、每 Fixture/项目 Layer 两级碰撞过滤、查询、运动 API 和 DistanceJoint2D
- [x] 延迟派发的引擎/NativeScript Collision 与 Trigger 回调
- [x] 项目级 Tag、16 个稳定 Layer 和对称 Physics 2D 碰撞矩阵设置
- [x] Scene Schema v9 持久化与 Cooked Package v3 物理往返
- [x] 物理回归测试套件（`Scripts\Run-PhysicsRegression.ps1`）

### 下一阶段：项目脚本系统（C#）

- [ ] 集成托管运行时，并构建、加载项目程序集
- [ ] 可序列化的 C# Script 组件，以及 Inspector 挂载与字段编辑
- [ ] Entity、Transform、Input、Physics、Scene 的 C++/C# API 桥接
- [ ] 分离逐显示帧 `OnUpdate` 与固定步长 `OnFixedUpdate`
- [ ] Collision/Trigger 回调、诊断信息和安全热重载
- [ ] Cook 时包含编译后的项目程序集

### 游戏导出与运行时

- [ ] 独立 Player Target，以及 Editor 的 Build Game / Build & Run 流程
- [ ] 游戏名称、图标、分辨率、全屏、VSync 和输出目录等 Build Settings
- [ ] 启动场景依赖遍历、无用资产裁剪和打包游戏端到端启动测试
- [ ] SceneManager、运行时切换场景、叠加/异步加载、Prefab 和存档

### 资产管线与 2D 内容生产

- [ ] 带内容 Hash、Importer 版本、派生数据缓存和依赖图的 Importer/Reimport 管线
- [ ] Audio、Font、Shader、Material、Mesh 和 Script 的强类型运行时资产与 Loader
- [ ] 后台导入/文件监控，以及平台相关的纹理设置、Mipmap 和压缩
- [ ] Sprite Atlas/SubTexture、Pivot、Pixels Per Unit、动画、排序层、文字、运行时 UI、Tilemap、粒子和 2D 光照

### 引擎系统与工具链

- [ ] Input Action/Axis、重绑定、输入上下文和手柄支持
- [ ] 音频播放、AudioSource/Listener、空间音频和 Mixer Bus
- [ ] Undo/Redo、自动保存/恢复、Editor Console 和可用的 Profiler
- [ ] 在 Push/PR CI 中运行回归测试，并把覆盖范围扩展到物理之外
- [ ] Scene/Project Schema 迁移工具、组件注册/反射和插件/模块 SDK
- [ ] Windows/OpenGL 的 2D 流程成熟后，再增加其他平台与渲染后端

### 未来 3D 范围

- [ ] Mesh/模型导入、Material、Light、PBR、阴影、环境渲染和骨骼动画
- [ ] 等 3D Runtime 与编辑流程成立后，再将当前仅有相机的 3D 模板转为正式功能

## 相关项目

- [Ekit](https://github.com/chnnasn/ekit)：自研、面向调用者友好的 header-only ECS 库
- [VulkanSDK-Windows](https://github.com/chnnasn/VulkanSDK-Windows)：预编译 Vulkan SDK 子集（子模块集成，免环境变量）

## License

[MIT](LICENSE) © 2026 chnnasn
