# TomCat Engine — dev_opengl3D

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档整理：**2026-09-21**。下方验证进度引用已有记录，本次仅修改文档，未重新运行引擎测试。

## 当前分支的作用

在 2D 基线上开发静态模型导入、六种内置图元、天空/HDR 环境、可编辑灯光、方向光阴影与 PBR，并接入资产、Prefab、Cook 和 Player。入口见 [3D 指南](docs/OPENGL_3D.zh-CN.md)、[图元示例](Samples/Primitives3D/README.md)和[光照示例](Samples/PBRLighting/README.md)。已记录的验收目标为 Windows OpenGL 4.6，尚未实现 3D 物理与骨骼动画。

## 当前进度

- [x] 静态 OBJ/FBX/glTF/GLB 模型导入，以及 Cube、Sphere、Capsule、Cylinder、Plane、Quad 六种内置图元。
- [x] 天空/HDR 全景、方向光/点光/聚光灯、方向光阴影、金属度/粗糙度 PBR 与环境照明。
- [x] Mesh、Light3D、Environment3D 接入组件注册、C# 访问、资产 Handle、场景/Prefab 序列化、Cook 和 Player。

## 使用与开发入口

- [3D 操作与构建指南](docs/OPENGL_3D.zh-CN.md)：需要 Windows OpenGL 4.6、VS 2022 C++、CMake、.NET 10 及已初始化的 Assimp 子模块。
- 打开[内置图元](Samples/Primitives3D/README.md)或 [PBR 光照](Samples/PBRLighting/README.md)示例，关闭 Scene 的 2D 模式并使用透视相机。

## 验证进度

3D 指南记录的实际验收平台为 Windows OpenGL 4.6。`Renderer3DRegression` 覆盖序列化、Prefab、Cook 引用及模型产物，`--gpu` 增加拾取、相机缓冲切换、灯光/阴影/PBR/HDR 与 GL 状态检查；复现命令见指南。

## 已知限制与后续工作

- 尚无 3D 刚体与骨骼动画；阴影目前仅支持一盏方向光。
- 独立材质资产、PBR 贴图、模型 PBR 自动导入、透明材质排序及更多阴影类型仍待完善；Web 着色器适配不代表浏览器验收。
