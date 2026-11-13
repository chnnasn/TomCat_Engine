# 静态 3D 渲染

`dev_opengl3D` 的静态 3D 功能已接入主线组件、资产、编辑器和 Player。3D 绘制使用共用 RHI，支持桌面 OpenGL 与 Vulkan。

## 使用

1. 在 Scene 工具栏关闭 2D 模式。
2. Hierarchy 右键 → **3D Object → Cube / Sphere / Capsule / Cylinder / Plane / Quad**，或把 Project 中的 OBJ、FBX、glTF、GLB 资源拖进 Scene。
3. Inspector 的 **Mesh Renderer** 支持启用状态、颜色、Mesh、Albedo、UseTexture，以及 Metallic、Roughness、AmbientOcclusion、自发光和投射/接收阴影。Primitive 持久化值为 0=None、1=Cube、2=Quad、3=Sphere、4=Capsule、5=Cylinder、6=Plane；指定 Mesh 时优先显示该资产。
4. Game 使用场景中的主相机。使用 Perspective 投影，相机本地 **+Z** 指向物体，与 main 的相机约定一致。

内置图元示例见 `Samples/Primitives3D/Project.tcproj`。Plane 为水平 XZ 网格（10×10），Quad 为竖直 XY 矩形（1×1）；Sphere 直径 1，Capsule/Cylinder 沿 Y 轴、直径 1、高 2。旧场景的 Primitive=2 仍生成原来的竖直矩形，显示名称改为 Quad，不改变原场景朝向。旧 C++ `CreatePlane` API 保留 XY 语义，新代码使用 `CreateGroundPlane` / `CreateQuad`。

模型的节点变换会烘焙到静态顶点；多子网格、漫反射颜色及编码的漫反射贴图保存在模型 Artifact 中，Player 从打包数据加载，不依赖开发机模型路径。glTF 的外置 buffer、OBJ 的 MTL 与贴图等文件应随模型一起放入 Assets。外部文件内容参与缓存键和导入完成时的变更检查；资源监视会保守地重新检查模型缓存。

## 主线集成

- Scene/Game 共用 3D 绘制，沿用层级世界变换、运行时插值、激活状态与编辑器隐藏状态。
- Mesh Renderer 注册到 ComponentRegistry，支持场景保存、Play 副本、复制、Prefab、属性编辑与 Undo/Redo 所使用的场景快照。
- Mesh、Albedo 与环境 Panorama 使用稳定 AssetHandle；场景和 Prefab 的 Cook 依赖遍历会收集这些引用。
- C# 可使用 `entity.GetComponent<MeshRenderer>()`、`entity.GetComponent<Light3D>()`、`entity.GetComponent<Environment3D>()` 访问对应 Inspector 参数；Transform、生命周期、输入和场景 API 沿用 main。
- 运行时 UI、音频和场景加载流程保留；3D Draw 调用进入 main 的 FrameProfiler。
- 2D/3D UniformBuffer 上传时重新绑定相应槽位；拾取 ID 使用整数传递。

## 天空、灯光、阴影与 PBR

可直接用 Hub 打开 `Samples/PBRLighting/Project.tcproj`，再打开 `Assets/Scene/sample.tomcat`。前排为非金属，后排为金属；每排从左到右粗糙度递增。该场景不需要外部素材。

![真实 OpenGL 渲染结果](../Samples/PBRLighting/preview.png)

- **天空**：Hierarchy → 3D Object → Sky / Environment。默认生成天空与地面渐变；将等距柱状全景图拖到 Panorama 可替换背景。Rotation 绕世界 Y 轴旋转全景，ShowSky 只隐藏背景，仍保留环境照明。Intensity 同时调整背景和照明，AmbientIntensity 只调整物体环境照明，Exposure 调整 3D 输出曝光。
- **HDR**：`.hdr` 导入保留线性浮点亮度，生成浮点 mip，Cook 写入包内；OpenGL GPU 纹理使用 RGBA16F，Vulkan 使用 RGBA32F。PNG/JPG 按资产导入的 sRGB 设置采样。环境输入为一张等距柱状全景图。
- **可编辑灯光**：Hierarchy → 3D Object → Directional / Point / Spot Light。方向光和聚光灯沿实体本地 **+Z**，可使用 Transform 旋转。点光/聚光灯使用平方反比衰减与 Range 截断；聚光灯的内外半角以度为单位，内部会把内角限制到外角以内。
- **阴影**：每帧选择首个可投影的方向光，使用 2048² 深度图和 3×3 PCF。ShadowExtent 是以相机为中心的阴影覆盖半径；调小可提高近景精度，调大可覆盖远处物体。ShadowBias 控制自阴影偏移。Mesh Renderer 可分别开关 CastShadows / ReceiveShadows。
- **材质**：Cook–Torrance GGX + Smith + Schlick 的金属度/粗糙度模型，支持 Albedo、Metallic、Roughness、AO、自发光。环境漫反射使用余弦采样，环境镜面反射使用 GGX 重要性采样；随后进行 ACES 曲线与显示 gamma。当前每个片元各取 32 个漫反射/镜面样本，亮点很集中的全景图可能出现采样误差，复杂场景后续可改成预过滤探针以降低开销。

灯光按实体 UUID 排序，最多取 16 盏；多个启用的 Environment 取最小 UUID。没有 Light3D 组件的旧场景使用默认方向光；有组件但全部禁用时不会重新补光。Mesh Renderer 使用组件 schema 2，沿用主线的严格版本校验。两个示例场景的 Camera 已更新为主线 schema 4。

范围：阴影目前仅支持一盏方向光，点光/聚光灯不投影；没有级联阴影、屏幕空间反射、透明材质排序、法线/金属粗糙度贴图、独立材质资产或模型 PBR 参数自动导入。模型仍沿用已有的漫反射纹理与颜色，再应用实体上的 PBR 参数。Box2D、2D Light、Tilemap 和 Sprite Animator 保持各自的 2D 语义，没有新增 3D 刚体或骨骼动画。Web 的着色器转换与 Framebuffer 接口同步适配。浏览器中的源模型导入仍支持 OBJ，桌面可导入 OBJ、FBX、glTF 和 GLB。


## 构建和验证

先执行 `git submodule update --init --recursive`。本机需要 VS 2022 或更新版本的 C++ 工具链、v143、支持该 VS 版本的 CMake，以及 .NET 10。TomCat 的 prebuild 会构建 Assimp，原生应用的 postbuild 会复制其 DLL，Player 模板和 Editor 打包流程也包含该依赖。

```powershell
Scripts/Run-Regressions.ps1 -Configuration Release
Scripts/Run-RHIRegression.ps1 -Backend vulkan -Validation
```

Linux 依赖与构建入口见 [Linux 使用说明](LINUX.zh-CN.md)。CMake 的 CTest 包含无窗口 3D 回归，以及通过 Xvfb 运行的 OpenGL、Vulkan 3D 绘制回归。

默认 3D 回归检查六种图元、场景、Play 副本、Prefab、Cook 引用、属性约束、glTF 节点与材质 Artifact、中文模型路径、外部 buffer 更新和离线包加载。`--gpu` 检查模型包围盒、实体拾取、禁用组件、2D/3D 相机缓冲切换、灯光方向与锥角、阴影、PBR 参数、天空拾取、HDR 采样和渲染状态恢复。使用 `TC_RENDERER` 选择后端，`--expect-backend opengl` 或 `--expect-backend vulkan` 校验实际选用的后端。

HDR Artifact 使用独立格式编号 7，保留主线 BC1、BC5、ASTC4x4、ETC2RGBA8 的编号与采样设置。纹理导入器版本更新为 7，以便已有项目重新生成 HDR 缓存。
