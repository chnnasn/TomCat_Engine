# TomCat Engine — dev_butter

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档整理：**2026-09-21**。下方验证进度引用已有记录，本次仅修改文档，未重新运行引擎测试。

## 当前分支的作用

将 Box2D 替换为 Butter，并默认启用 2D 连续碰撞检测（CCD）。用于开发和验证物理后端、碰撞/触发回调及已有 2D 物理 API 的兼容性。本分支面向 2D 物理，没有新增 3D 刚体系统。

## 当前进度

- [x] 通过 Butter 适配层替换 Box2D，保留场景组件和脚本物理 API，已迁移桌面与 Web 构建依赖。
- [x] 默认开启动态刚体对静态/运动学刚体的 CCD，接入碰撞过滤、接触聚合、延迟回调、查询和距离关节。

## 使用与开发入口

- [迁移记录、依赖版本与 CCD 行为](docs/Butter-Migration.md)。
- [PhysicsPlayground 示例](Samples/PhysicsPlayground/README.md)；初始化子模块后运行 `Scripts/Run-PhysicsRegression.ps1 -Configuration Release`。

## 验证进度

2026-09-20 迁移记录：Butter Debug CTest 13/13、Debug 与 Release 各 87 项 CCD 检查、TomCat PhysicsRegression 56/56 通过；初次迁移还记录了其他原生回归与 Editor/Player 构建通过。这些是历史本地结果，不代表浏览器验收。

## 已知限制与后续工作

- 尚无完整 WASM 构建和浏览器验收记录，求解轨迹不保证与 Box2D 数值一致。
- Trigger 保持步末离散重叠语义，没有 3D CCD；CCD 预算耗尽可能使密集场景减速，实际关卡仍需验证堆叠、弹性与高速运动。
