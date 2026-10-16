# 浏览器中文字体

[English](README.md) | 简体中文 · 核对日期：2026-10-16 · [Web 中文指南](../README.zh-CN.md)

`NotoSansSC-Regular.otf` 是 Noto Sans CJK 的简体中文子集，按
[SIL Open Font License 1.1](LICENSE) 分发。

- 来源：[Noto CJK 简体中文子集目录](https://github.com/notofonts/noto-cjk/tree/main/Sans/SubsetOTF/SC)
- 下载日期：2026-09-16。
- SHA-256：`faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`。

Web 编辑器将其中文字形合并到已有 OpenSans 字体中；桌面版继续使用原有字体选择逻辑。
这样无需重新分发 Windows 系统字体。`Web/CMakeLists.txt` 将本目录预加载到独立编辑器模块的
`/WebFonts` 路径；`Managed/TomCat.WebHost/TomCat.WebHost.csproj` 将同一组文件放入
托管站点使用的 .NET browser-wasm 文件系统 `/WebFonts` 路径。分发字体时应一同保留原始 `LICENSE`。

在仓库根目录校验本地字体：

```powershell
Get-FileHash Web/fonts/NotoSansSC-Regular.otf -Algorithm SHA256
```

2026-10-16 重新核对了本地字体 SHA-256 与原生/托管模块的 `/WebFonts` 挂载路径，字体文件未更换。
这份字体用于编辑器 ImGui 字形显示；游戏里的字体/文字资源与回退链由运行时字体系统管理。
可显示中文不代表已经验证浏览器输入法组合输入，当前边界见 [Web 指南](../README.zh-CN.md)。
