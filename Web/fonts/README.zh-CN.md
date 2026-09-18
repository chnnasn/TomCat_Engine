# 浏览器中文字体

[English](README.md) | 简体中文 · 核对日期：2026-09-18 · [Web 中文指南](../README.zh-CN.md)

`NotoSansSC-Regular.otf` 是 Noto Sans CJK 的简体中文子集，按
[SIL Open Font License 1.1](LICENSE) 分发。

- 来源：[Noto CJK 简体中文子集目录](https://github.com/notofonts/noto-cjk/tree/main/Sans/SubsetOTF/SC)
- 下载日期：2026-09-16。
- SHA-256：`faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`。

Web 编辑器将其中文字形合并到已有 OpenSans 字体中；桌面版继续使用原有字体选择逻辑。
这样无需重新分发 Windows 系统字体。`Web/CMakeLists.txt` 将本目录预加载到编辑器模块的
`/WebFonts` 路径。分发字体时应一同保留原始 `LICENSE`。

在仓库根目录校验本地字体：

```powershell
Get-FileHash Web/fonts/NotoSansSC-Regular.otf -Algorithm SHA256
```
