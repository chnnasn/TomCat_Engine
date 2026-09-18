# TomCat 中文文档索引

核对日期：2026-09-18。本文覆盖仓库自有文档；`vendor/`、`TomCat/vendor/` 内的上游资料及许可证按原项目维护。

| 文档 | 中文版本 | 英文版本 |
| --- | --- | --- |
| 项目介绍、构建与功能范围 | [项目总览](../README.zh-CN.md) | [Overview](../README.md) |
| 项目配置、资产、场景、迁移与发布 | [项目系统](../PROJECT_SYSTEM.md) | — |
| C# 脚本、原生 ABI 与生命周期 | [托管脚本](../Managed/README.zh-CN.md) | [Managed scripting](../Managed/README.md) |
| 无界面 Cook 与 Player 构建 | [TomCatCLI](../Tools/TomCatCLI/README.zh-CN.md) | [TomCatCLI](../Tools/TomCatCLI/README.md) |
| 浏览器构建、编辑接口与限制 | [Web 指南](../Web/README.zh-CN.md) | [Web guide](../Web/README.md) |
| 浏览器中文字形与字体来源 | [字体说明](../Web/fonts/README.zh-CN.md) | [Font notes](../Web/fonts/README.md) |
| 2D 物理示例 | [PhysicsPlayground](../Samples/PhysicsPlayground/README.md) | — |
| 桌面实机录制、历史问题与复现 | [录制说明](portfolio/README.md) | — |

## 版本依据

产品和格式版本以 [Version.h](../TomCat/src/TomCat/Core/Version.h) 为准。
当前产品版本为 `0.2.0`，项目写入 v4、场景写入 v11、Prefab 写入 v1、TCPAK 写入 v7；
TCPAK 读取兼容 v5/v6/v7，Native ABI 为 v1、Managed ABI 为 v2、脚本清单为 v1。
TCPAK v6 引入 BootManifest，v7 在索引中增加逐条目 SHA-256 摘要。

## 验证入口

以下命令从仓库根目录执行，先按对应构建指南安装工具和依赖。

```powershell
# Windows：托管、原生、Editor、Hub、CLI、Player 与模板/启动回归
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1

# Web：先生成 tomcat_editor.js/.wasm/.data，再运行无图形上下文的协议回归
node Web/tests/editor-rpc.cjs build/web
```

Windows CI 使用 [.github/workflows/regressions.yml](../.github/workflows/regressions.yml)。
Web 协议回归不验证浏览器绘制、宿主持久化、中文输入法或真实手柄，需另做浏览器验收。
演示文档中的日期、截图、Hash 与验收结果属于当时的记录，不能作为后续提交的测试结果。

## 文档维护

新增项目文档应提供完整中文正文；已有英文版时使用同目录 `README.zh-CN.md` 并添加双向语言链接。
已有纯中文文档沿用原路径。更改格式版本、CLI 参数或平台范围时，同步更新两种语言及本索引。
API 名称、字段大小写、命令参数和路径保持源码原文。
