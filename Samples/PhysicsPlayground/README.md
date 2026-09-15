# PhysicsPlayground

一个用于体验 TomCat 2D 物理系统的最小示例：倾斜矩形在重力作用下落到静态地板上，
展示 Sprite、Transform、动态刚体与碰撞体之间的配合。

项目包含场景、资源与项目设置，运行缓存由 Editor 自动生成。
效果预览见 [引擎功能展示](../../README.zh-CN.md#功能展示)。

1. 编译并运行当前 TomCat Editor（Windows x64 / Release）。
2. 使用 **File → Open Project** 选择本目录的 `Project.tcproj`。
3. 在 Project 面板打开 `Assets/sample.tomcat`。
4. 点击 **Play**：矩形下落并落在地板上。使用 **Pause / Step** 检查运行状态，
   再点击同一个 **Stop** 按钮返回编辑场景。

| Entity | Authored configuration |
| --- | --- |
| MainCamera | Orthographic camera, sky-blue background |
| Square | Position `(0, 1.5, 0)`, Z rotation `25°`, scale `(1.4, 1, 1)`; Sprite Renderer, Dynamic Rigidbody2D, BoxCollider2D |
| Ground | Position `(0, -1, 0)`, scale `(3, 0.3, 1)`; Sprite Renderer, BoxCollider2D with an implicit static body |

场景格式为 schema 11，项目格式为 schema 4。此演示不需要 C# 脚本。
若 Hub 因 Windows 扩展路径问题无法自动加载项目，可使用上述手动打开方式。
