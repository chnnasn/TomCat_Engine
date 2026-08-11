# TomCat Engine

一款基于 **C++20** 开发的 **2D 游戏引擎 + 编辑器 + 项目中心（Hub）**，构建于 OpenGL / ImGui / Box2D 之上。

**语言**：[English](README.md) | 简体中文

---

## 特性

- **2D 渲染管线**：OpenGL 批渲染（Renderer2D），支持纹理、旋转、相机
- **ECS 场景系统**：Entity / Component / Scene，YAML 场景序列化
- **2D 物理**：内置 Box2D（刚体、碰撞体）
- **编辑器**：ImGui 驱动的 ContentBrowser、SceneHierarchy 面板，布局持久化
- **Hub 项目管理器**：多项目管理、虚拟路径挂载、按最近打开排序
- **中文支持**：为 ImGui 动态生成中文字符集，支持中英切换
- **一键打包**：GitHub Actions 三模式（Editor / Hub / Both），Enigma 单文件封装

## 截图

> 待补充（可放编辑器/演示截图）

## 构建

### 依赖

- Visual Studio 2022+
- premake5（Setup 脚本自动下载）

> Vulkan SDK 通过 git 子模块自动提供（vendor/VulkanSDK，基于 VulkanSDK-Windows），无需手动安装或设置环境变量。

### 步骤

1. 克隆仓库（含子模块：git clone --recurse-submodules ...）
2. 运行 Scripts\Setup.bat（自动下载 premake5）
3. 运行 Scripts\Win_GenProjects.bat 生成 VS 工程
4. 打开生成的 .sln 编译（Release x64）

## 目录结构

    TomCat/            引擎核心库（渲染、ECS、物理、ImGui 集成）
    Editor/TomCatInut/ 编辑器应用
    Builder/Manager/   Hub（项目中心）
    Scripts/           构建与打包脚本
    vendor/            premake 与第三方依赖

## 打包发布

- 本地打包：Scripts\Package-Editor.ps1 / Scripts\Package-Hub.ps1
- CI 打包：GitHub Actions（workflow_dispatch 选择 editor / hub / both），Enigma 封装单 exe 并发布 Release

## Roadmap（待开发）

### 场景运行与物理

- [ ] 场景运行控制完善：暂停 / 重置（当前已有 Play / Stop）
- [ ] 编辑器物理可视化：渲染圆圈 / 线条（碰撞体 gizmo）
- [ ] CircleCollider2D 环形碰撞体组件
- [ ] 物理模拟模式（编辑器内模拟物理，不修改场景数据）
- [ ] 物理回归测试

### 脚本系统（C#）

- [ ] C# 脚本系统（Mono 集成，C# 脚本驱动游戏逻辑）
- [ ] 从 C# 调用 C++ 引擎 API
- [ ] 在 ECS 中挂载 C# 脚本组件
- [ ] 编辑器内读写 C# 字段
- [ ] C# 脚本字段工作流
- [ ] 脚本数据序列化与管理

## 相关项目

- [Ekit](https://github.com/chnnasn/ekit)：自研、面向调用者友好的 header-only ECS 库
- [VulkanSDK-Windows](https://github.com/chnnasn/VulkanSDK-Windows)：预编译 Vulkan SDK 子集（子模块集成，免环境变量）

## License

[MIT](LICENSE) © 2026 chnnasn