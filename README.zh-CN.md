# TomCat Engine — Build_System

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档整理：**2026-09-21**。下方验证进度引用已有记录，本次仅修改文档，未重新运行引擎测试。

## 当前分支的作用

用于桌面构建与分发流程、脚本诊断、重载反馈和外部 IDE 调试支持的开发。本分支通过 `TomCat.Dependencies.csproj` 支持纯托管 NuGet 包、项目引用和本地托管 DLL，并将运行时依赖封装供 Editor 与 Cooked Player 使用。详见 [C# 依赖指南](docs/CSHARP_DEPENDENCIES.md)。原生包资产和 Play Mode 热替换不在支持范围内。

## 当前进度

- [x] 托管依赖还原与封装：支持纯托管 NuGet 包、传递依赖、项目引用和本地 DLL，运行依赖随 `Assembly-CSharp.dll` 进入 Editor 脚本域与 Cooked Player。
- [x] 脚本诊断与重载反馈、portable PDB 加载、自动生成 VS Code 附加调试工作区；构建失败保留最后成功程序集。

## 使用与开发入口

- [依赖配置与范围](docs/CSHARP_DEPENDENCIES.md)：将 [TomCat.Dependencies.csproj](Managed/Templates/TomCat.Dependencies.csproj) 放到游戏项目 `.tcproj` 同级，停止 Play 后编译。
- [调试器接入](docs/DEBUGGING_AND_PROFILING.md)与[托管层构建说明](Managed/README.zh-CN.md)。

## 验证进度

依赖指南记录了还原、依赖更新、隔离加载、失败回退及 Cook 包依赖调用的回归覆盖。调试指南记录了编译诊断和源码行号的回归覆盖；IDE 跳转与交互式断点命中仍需桌面验收。

## 已知限制与后续工作

- 不支持原生库和包内容资产，不能视为兼容所有 NuGet 包。
- 尚无 Play Mode 热替换、内置 NuGet 界面或内置 C# 调试器；引用监听范围外的文件时需显式重编译。
