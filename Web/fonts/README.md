# Browser CJK font

English | [简体中文](README.zh-CN.md) · Reviewed 2026-09-20 · [Web guide](../README.md)

`NotoSansSC-Regular.otf` is Noto Sans CJK's Simplified Chinese subset, distributed
under the SIL Open Font License 1.1 in `LICENSE`.

- Source: https://github.com/notofonts/noto-cjk/tree/main/Sans/SubsetOTF/SC
- Downloaded: 2026-09-16
- SHA-256: `faa6c9df652116dde789d351359f3d7e5d2285a2b2a1f04a2d7244df706d5ea9`

The Web editor merges its CJK glyphs into the existing OpenSans fonts. Desktop
font selection is unchanged. This avoids redistributing Windows system fonts.

`Web/CMakeLists.txt` preloads this directory at `/WebFonts` in the editor module.
Keep [LICENSE](LICENSE) with redistributed font files. Verify the checked-in bytes with:

```powershell
Get-FileHash Web/fonts/NotoSansSC-Regular.otf -Algorithm SHA256
```

The local SHA-256 and CMake `/WebFonts` mount were checked again on 2026-09-20;
the font bytes were not replaced. This font supplies editor ImGui glyphs. Game
font assets and fallback chains are managed by the runtime font system separately.
Chinese glyph rendering does not establish browser IME composition support;
see the [Web limitations](../README.md).
