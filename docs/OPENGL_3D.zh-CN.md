# OpenGL 3D 分支

`dev_opengl3D` 已整合 `main` 的 `4f507c7`，并把原有静态 3D 渲染接入当前组件、资产和运行时系统。

## 使用

1. 在 Scene 工具栏关闭 2D 模式。
2. Hierarchy 右键 → **3D Object → Cube / Plane**，或把 Project 中的 OBJ、FBX、glTF、GLB 资源拖进 Scene。
3. Inspector 的 **Mesh Renderer** 支持启用状态、颜色、Mesh、Albedo 和 UseTexture。Primitive 值为 0=None、1=Cube、2=Plane；指定 Mesh 时优先显示该资产。
4. Game 使用场景中的主相机。使用 Perspective 投影，相机本地 **+Z** 指向物体，与 main 的相机约定一致。

模型的节点变换会烘焙到静态顶点；多子网格、漫反射颜色及编码的漫反射贴图保存在模型 Artifact 中，Player 从打包数据加载，不依赖开发机模型路径。glTF 的外置 buffer、OBJ 的 MTL 与贴图等文件应随模型一起放入 Assets。外部文件内容参与缓存键和导入完成时的变更检查；资源监视会保守地重新检查模型缓存。

## 与 main 的整合

- Scene/Game 共用 3D 绘制，沿用层级世界变换、运行时插值、激活状态与编辑器隐藏状态。
- Mesh Renderer 注册到 ComponentRegistry，支持场景保存、Play 副本、复制、Prefab、属性编辑与 Undo/Redo 所使用的场景快照。
- Mesh 与 Albedo 使用稳定 AssetHandle；场景和 Prefab 的 Cook 依赖遍历识别这两个引用。
- C# 可使用 `entity.GetComponent<MeshRenderer>()` 访问 Enabled、Color、Mesh、Primitive、Albedo、UseTexture；Transform、生命周期、输入和场景 API 沿用 main。
- 运行时 UI、音频和场景加载流程保留；3D Draw 调用进入 main 的 FrameProfiler。
- 2D/3D UniformBuffer 上传时重新绑定相应槽位；拾取 ID 使用整数传递。

本分支仍是静态网格和方向光 Blinn–Phong 管线。Box2D、2D Light、Tilemap 和 Sprite Animator 保持各自的 2D 语义；此次整合没有新增 3D 刚体、骨骼动画、PBR 或阴影系统。Web 代码保留 packed mesh 和基础图元路径，但本次验收目标为 Windows OpenGL。

## 构建和验证

先执行 `git submodule update --init --recursive`。本机需要 VS 2022 C++ 工具链、CMake、.NET 10；TomCat 的 prebuild 会构建 Assimp，原生应用的 postbuild 会复制其 DLL，Player 模板和 Editor 打包流程也包含该依赖。

```powershell
Scripts/Run-Regressions.ps1 -Configuration Release
Tests/bin/Release-windows-x86_64/Renderer3DRegression/Renderer3DRegression.exe --gpu
```

默认 3D 回归不创建图形窗口，检查场景、Play 副本、Prefab、Cook 引用、属性约束、glTF 节点与材质 Artifact。`--gpu` 额外创建隐藏的 OpenGL 4.6 窗口，检查实体拾取、禁用组件和 2D/3D 相机缓冲切换。
