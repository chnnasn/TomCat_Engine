# Butter 2D 迁移记录

TomCat 的 2D 运行时现使用 Butter，组件序列化、Scene 查询 API 和托管脚本 ABI 保持不变。

- 上游 PR：https://github.com/chnnasn/Butter/pull/1 （已合并）
- 当前验证版本：`TomCat/vendor/Butter`，提交 `f1b4e4286f554bc31142e4b78ccf4f076aa919cb`
- CCD PR：https://github.com/chnnasn/Butter/pull/2 （已提交，未合并）
- 引擎适配：`TomCat/src/TomCat/Physics/Physics2D.h`
- 构建：Butter 是 C++20 header-only 库；桌面项目不再链接 Box2D.lib，Web 目标链接 Butter 的 CMake INTERFACE target。
- 发布：第三方许可清单包含 Butter 的 MIT LICENSE。

## 行为

适配层负责组件到 Butter 的转换、碰撞体密度对应的质量/惯量、稳定运行时句柄、查询结果和回调生命周期。积分、碰撞检测及求解由 Butter 执行。支持静态/动态/运动学刚体、多碰撞体和局部偏移、固定旋转、休眠、双重碰撞过滤、触发器与普通碰撞事件、力/冲量、距离关节及精确圆/盒射线查询。实体 UUID 使用显式 64 位字段。

接触仍按实体对聚合，回调在物理步结束后派发。运行时编辑保留未受影响的刚体/碰撞体身份；销毁、停用、重建及连接实体删除走现有安全同步流程。

## 初次迁移验证（2026-09-20，Windows x64）

- Butter Debug CTest：12/12 通过，断言启用；新增原生集成用例覆盖过滤、休眠接触、销毁、回调锁、运动学、力、关节锚点、深度重叠与精确查询。
- TomCat Release PhysicsRegression：55/55 通过，未放宽原有物理行为断言。
- 其他 8 个原生程序全部通过：SpriteAssetRegression、P0SafetyRegression、EditorRecoveryRegression、AudioRegression、ImporterRegression、InputRegression、Advanced2DRegression、ScriptCompilerRegression。
- Release Editor、独立 Player 构建成功。
- 上游 PR 没有配置 CI 检查；以上为本地验证。

复现：先执行 `git submodule update --init --recursive`，再运行 `Scripts/Run-PhysicsRegression.ps1 -Configuration Release`。

Butter 测试可用 `cmake -S TomCat/vendor/Butter -B build/butter -DBUTTER_BUILD_EXAMPLES=OFF`、`cmake --build build/butter --config Debug`、`ctest --test-dir build/butter -C Debug --output-on-failure` 运行。

## CCD 验证（2026-09-20）

- 默认开启动态刚体对静态、运动学刚体的 CCD；无需设置 bullet。Butter 的 bullet 模式额外支持动态刚体之间的 CCD。
- Butter Debug CTest：13/13 通过；新的 CCD 程序在 Debug 和 Release 均通过 87 项检查。
- TomCat Release PhysicsRegression：56/56 通过。新增速度 600 单位/秒的圆撞击薄墙及 Scene 碰撞事件连续性回归。
- 上游 PR #2 基于已合并 PR #1 的主线；当前没有配置 CI 检查，以上是本地执行结果。

## 当前边界

CCD 支持 2D 圆、盒和凸多边形，包含双方平移、旋转、碰撞体局部偏移、碰撞后的剩余时间及多次反弹。碰撞过滤、关节的 CollideConnected 和休眠唤醒规则仍然生效。胶囊、网格及 3D 暂未加入 CCD；Trigger 保持步末离散重叠语义。初始穿透、主动传送和关节/穿透求解器的位置修正不做扫掠。

计算预算耗尽或未收敛时会保守丢弃该步剩余的整个 world 运动时间，避免继续无检测地前进；可通过 ccd_statistics() 的 limited / remaining_time 监测，并调整预算。高密度碰撞或复杂旋转场景可能因此出现减速。

求解器轨迹与 Box2D 不保证数值一致，实际关卡仍应检查堆叠、弹性和高速运动。本次没有完整 wasm 构建或浏览器验收。CCD PR 尚未合并，当前验证使用上述已推送的 PR 提交。
