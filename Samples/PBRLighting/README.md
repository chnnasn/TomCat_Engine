# PBR Lighting

用 Hub 打开 `Project.tcproj`，再在 Project 中双击 `Assets/Scene/sample.tomcat`。Scene 视图关闭 2D 模式，Game 视图使用已配置的透视相机。

前排蓝色方块为非金属，后排金色方块为金属；每排从左到右粗糙度从 0.08 增加到 0.96。选择 Sun 修改方向和阴影，选择 Sky and environment 修改天空和环境照明，选择任意方块修改 PBR 参数。场景使用内置图元和程序天空，可直接打包，无外部素材依赖。

![OpenGL 渲染结果](preview.png)

[参数说明和实现范围](../../docs/OPENGL_3D.zh-CN.md)

重新生成此场景与 PPM 截图（会覆盖目标场景）：

```powershell
Tests/bin/Release-windows-x86_64/Renderer3DRegression/Renderer3DRegression.exe --demo Samples/PBRLighting/Assets/Scene/sample.tomcat
```
