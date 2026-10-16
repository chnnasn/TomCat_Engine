# Shaping fixtures

The fonts were downloaded from [Noto hinted TTF fonts](https://github.com/notofonts/noto-fonts/tree/main/hinted/ttf)
on 2026-10-01 and are licensed under the included [OFL.txt](OFL.txt).

| Local fixture | Upstream filename | Regression text |
| --- | --- | --- |
| `NotoSansDevanagari.ttf` | `NotoSansDevanagari-Regular.ttf` | `क्षि` (Indic conjunct and reordering) |
| `NotoNaskhArabic.ttf` | `NotoNaskhArabic-Regular.ttf` | `لا` (Arabic ligature) |

The [RuntimeUI regression](../../src/RuntimeUIRegression.cpp) uses the real font
bytes with HarfBuzz, verifies shaping clusters and glyph atlas closure, and
checks layout and caret mapping, including right-to-left Arabic carets. These
fixtures cover text shaping and layout; GPU rendering is verified separately by
[RHIRegression](../../../../docs/RHI_VULKAN.zh-CN.md#启动与构建).

From the repository root, build and run the enclosing PhysicsRegression target
with the [complete regression suite](../../../../README.md):

```powershell
$env:TC_RENDERER = 'opengl'
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1 -Configuration Release -Log
```

After building, run only the enclosing executable with
`./Tests/bin/Release-windows-x86_64/PhysicsRegression/PhysicsRegression.exe`.
