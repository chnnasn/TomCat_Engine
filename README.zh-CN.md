# TomCat Engine — dev_ekit

**语言**：[English](README.md) | 简体中文

本页仅记录本分支的增量与进度。项目总览、通用功能、展示和桌面基础构建流程见[主分支 README](https://github.com/chnnasn/TomCat_Engine/blob/main/README.zh-CN.md)；本分支特有的依赖与入口见下文。

文档整理：**2026-09-21**。下方验证进度引用已有记录，本次仅修改文档，未重新运行引擎测试。

## 当前分支的作用

将 EnTT 替换为 ekit，适配组件注册、存储与 SceneWorld 引用遍历，包括热循环中的 const 正确性。用于 ECS 集成和兼容性验证。详见 [迁移指南](docs/EKIT_MIGRATION.md)；Web 代码迁移本身不代表浏览器运行验证已经完成。

## 当前进度

- [x] 将 EnTT 替换为 ekit，采用显式稀疏组件注册，支持持有字符串、容器和资源引用的组件。
- [x] 保留 64 位实体索引/代次及场景 UUID/序列化契约，编辑器拾取使用独立整数 ID；SceneWorld 已支持热循环所需的 const 正确引用遍历。

## 使用与开发入口

- [迁移与组件注册指南](docs/EKIT_MIGRATION.md)。
- [依赖来源](TomCat/vendor/ekit/README.tomcat.md)；集成修复覆盖组件所有权、存储增长时的引用稳定性、注册冲突和多翻译单元链接。

## 验证进度

此前分支 README 记录了 Windows x64 MSVC Release 下十个原生回归程序、Editor/Player/Hub/CLI 构建、CLI smoke 与发布脚本测试通过，ekit 测试通过 4,417 项检查。这里保留的是迁移时的验证基线，不代表本次重新验证了后续遍历改动。

## 已知限制与后续工作

- Web 代码和 include 路径已迁移，但因当时缺少 Emscripten，尚未验证 WebAssembly 构建。
- 后续组件/插件接入需遵循注册存储与实体生命周期规则，见迁移指南。
