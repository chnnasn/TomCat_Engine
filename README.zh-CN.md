# TomCat Engine — dev_vulkan

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档整理：**2026-09-21**。下方验证进度引用已有记录，本次仅修改文档，未重新运行引擎测试。

## 当前分支的作用

为现有 2D 渲染与编辑器流程引入统一 RHI 和 Vulkan 1.2 后端。默认仍为 OpenGL，启动前设置 `TC_RENDERER=vulkan` 可选择 Vulkan。用于渲染后端集成与跨后端验证，不代表已包含独立的 OpenGL 3D 分支功能。详见 [Vulkan 指南](docs/RHI_VULKAN.zh-CN.md)。

## 当前进度

- [x] 为已有 2D 渲染引入统一 RHI 和 Vulkan 1.2 后端，覆盖 Scene/Game 离屏目标、整数拾取、ImGui 纹理与 GPU 计时。
- [x] 已适配着色器产物、逐次绘制数据快照、资源同步与呈现，默认后端仍为 OpenGL。

## 使用与开发入口

- [Vulkan 配置、需求与验证指南](docs/RHI_VULKAN.zh-CN.md)。启动前设置 `TC_RENDERER=vulkan`，同一进程内不能动态切换后端。
- 运行 `Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation -MultiViewport`，再运行 `Scripts/Run-RHIRegression.ps1 -Backend opengl`；验证模式需要 Khronos validation layer。

## 验证进度

2026-09-21 指南记录了 Windows Release 构建/原生回归、Intel 与 NVIDIA GPU 验证、Editor 交互及 Cook 后独立 Player 运行通过，未出现 validation error；未执行 Web 构建或浏览器验收。

## 已知限制与后续工作

- 本分支聚焦 2D 后端迁移，不包含独立 OpenGL 3D 专项功能。
- 保守同步策略不代表性能优于 OpenGL；多帧/上传优化、Render Graph、MSAA、Compute 与 Storage 资源不在当前前端范围，辅助视口的完整输入路由仍未完成。
