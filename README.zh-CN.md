# TomCat Engine

一款基于 **C++20** 开发的 **2D 游戏引擎、编辑器与项目中心（Hub）**，构建于 OpenGL、ImGui 和 Box2D 之上。

TomCat 当前优先完成可实际使用的 2D 开发流程。现有 3D 项目模板仍属于实验功能：它只配置透视相机，尚未实现生产级 3D 渲染器。

**语言**：[English](README.md) | 简体中文

---

## 特性

- **2D 渲染**：OpenGL 批渲染 Sprite、线条和圆形，支持相机、基于 Framebuffer 的 Scene/Game 视图与实体拾取
- **场景系统**：ECS 实体、父子层级、稳定 UUID、严格 YAML 场景序列化、有序 Build Settings 与帧末单场景替换
- **资产身份工作流**：稳定 `AssetHandle` 引用、`.tcmeta` Sidecar、Registry 重建、事务化移动/删除和缺失资产占位
- **2D 物理**：固定 60 Hz Box2D 运行时、显式/隐式静态刚体、Box/Circle 碰撞体、Trigger、过滤、Raycast/AABB 查询、力、冲量和 `DistanceJoint2D`
- **物理编辑体验**：Scene 视图碰撞轮廓、碰撞体句柄、项目 Tag/Layer 与 Physics 2D 碰撞矩阵、Play/Pause/Step/Stop，以及延迟派发的 C# Collision/Trigger 回调
- **C# 脚本**：.NET 10 项目编译、Inspector 序列化字段、可回收 Play Domain、完整生命周期、Entity/Transform/Input/Physics/Scene API、诊断、last-good 程序集与 Cooked 托管负载
- **快照 Prefab**：使用稳定 LocalID 的 `.tcprefab` 实体子树、引用重映射、运行时 C# `Instantiate`，实例化后为普通非关联实体
- **编辑器**：ImGui 驱动的 Scene、Game、Hierarchy、Inspector 和 Project 面板，支持项目级布局与用户设置持久化
- **独立 Player**：按 Handle 寻址且无作者路径的 `.tcpak` v5、与 Editor 分离的运行程序、固定 Hash 白名单 win-x64 Player Template 与私有 .NET Runtime
- **Hub 项目管理器**：项目创建与发现、Editor 版本选择和用户级最近项目状态
- **本地化**：动态生成 ImGui 中文字符集，Hub 支持中英文切换
- **回归测试**：统一 Release 入口覆盖托管 ABI/生命周期、物理、Sprite 资产、脚本编译、SceneManager、Prefab、Cook 与隔离 Player 启动

## 当前范围

- 支持的开发平台：**Windows x64**
- 渲染后端：**OpenGL 4.6**
- 当前引擎主范围：**2D**
- Editor 编译项目 C# 脚本需要安装 **.NET 10 SDK**
- 导出的 Player 携带固定私有 .NET Runtime 和所需 C++ 运行库，不依赖用户电脑的全局 .NET 环境或 Visual Studio
- V1 明确不支持 NuGet/第三方托管 DLL、Play Mode 热重载、脚本调试、叠加/异步场景、Prefab 关联更新、Override、Nested Prefab 和 Variant

## 构建

### 依赖

- Windows x64，并具备支持 OpenGL 4.6 的显卡与驱动
- Visual Studio 2022+
- .NET 10 SDK（Editor 编译项目 C# 脚本必需）
- Python 3 与 `pip`（供 Setup 辅助脚本使用）
- premake5（Setup 脚本自动下载）

> Vulkan SDK 通过 git 子模块自动提供（vendor/VulkanSDK，基于 VulkanSDK-Windows），无需手动安装或设置环境变量。

### 步骤

1. 克隆仓库（含子模块：git clone --recurse-submodules ...）
2. 首次运行前执行 `Scripts\Setup.bat`，准备 Premake 与 Setup 依赖
3. 运行 Scripts\Win_GenProjects.bat 生成 VS 工程
4. 根据需要打开 `Editor\Editor.sln`、`Builder\Builder.sln` 或 `Tests\Tests.sln`，并以 Release x64 编译目标

由 `Scripts\Setup.bat` 准备 Premake 后，运行 `powershell -ExecutionPolicy Bypass -File Scripts\Run-Regressions.ps1` 可执行完整 Release 回归套件。

## 目录结构

```
TomCat/            引擎核心库（渲染、ECS、物理、ImGui 集成）
Editor/TomCatInut/ Editor 应用
Player/            独立 Windows x64 游戏运行时
Managed/           .NET 10 运行时 API、源码生成器、Host 与回归
Builder/Manager/   Hub（项目中心）
Tests/             引擎回归测试工程
Scripts/           构建与打包脚本
vendor/            premake 与第三方依赖
```

## Editor 与 Hub 打包

- 本地打包：Scripts\Package-Editor.ps1 / Scripts\Package-Hub.ps1
- Push 与 Pull Request 会自动运行统一 Release 回归；打包 Workflow 仍可选择发布 Editor / Hub / 两者
- Editor 包将 `Managed/` 和 `Packages/PlayerTemplates/win-x64/` 保持为外部目录；UserSettings、项目源码与作者态 JSON 不进入可执行文件
- Editor 的 **Build** / **Build And Run** 会把已启用 Build Settings 场景 Cook 为 `Game.tcpak`，在 staging 中复制严格 Player Template，校验全部 SHA-256 与兼容版本后原子发布

## 当前状态与 Roadmap

### 已完成的 2D 物理里程碑

- [x] 固定步长 Play / Pause / Step / Stop 状态模型（不设置独立 Simulate 状态）
- [x] 仅 Scene 视图显示 Box/Circle 碰撞轮廓，并支持 Edit Collider 句柄
- [x] CircleCollider2D、隐式静态刚体、Trigger、每 Fixture/项目 Layer 两级碰撞过滤、查询、运动 API 和 DistanceJoint2D
- [x] 延迟派发的引擎监听器/C# Collision 与 Trigger 回调
- [x] 项目级 Tag、16 个稳定 Layer 和对称 Physics 2D 碰撞矩阵设置
- [x] Scene Schema v10 持久化与 Cooked Package v5 物理往返
- [x] 物理回归测试套件（`Scripts\Run-PhysicsRegression.ps1`）

### 已完成的 C#、Player、Scene 与 Prefab V1

- [x] 托管运行时、源码生成器、Inspector 字段、last-good 编译、确定性生命周期/物理回调、异常隔离与可回收 Play Domain
- [x] 独立 win-x64 Player、私有 .NET Runtime、严格版本/Hash Player Template、Build / Build And Run 与 Player 子进程冒烟测试
- [x] 共享 `ProjectSettings/BuildSettings.json`、`.tcpak` v5 有序场景、帧末同步 SceneManager 切换与 C# SceneManager API
- [x] 使用稳定 LocalID 的快照 Prefab V1、Hierarchy/Joint/C# Entity 重映射、新 AttachmentID、延迟 C# 创建、Editor 创建/拖入操作与 Cook 依赖遍历
- [ ] 游戏名称、图标、分辨率/全屏/VSync 配置、叠加/异步场景、关联/Nested Prefab、Override/Variant 与存档

### 资产管线与 2D 内容生产

- [ ] 带内容 Hash、Importer 版本、派生数据缓存和依赖图的 Importer/Reimport 管线
- [ ] Audio、Font、Shader、Material、Mesh 和 Script 的强类型运行时资产与 Loader
- [ ] 后台导入/文件监控，以及平台相关的纹理设置、Mipmap 和压缩
- [ ] Sprite Atlas/SubTexture、Pivot、Pixels Per Unit、动画、排序层、文字、运行时 UI、Tilemap、粒子和 2D 光照

### 引擎系统与工具链

- [ ] Input Action/Axis、重绑定、输入上下文和手柄支持
- [ ] 音频播放、AudioSource/Listener、空间音频和 Mixer Bus
- [ ] Undo/Redo、自动保存/恢复、Editor Console 和可用的 Profiler
- [x] 在 Push/PR CI 中运行托管、原生与 Player Release 回归
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
