# Butter 2D 迁移记录

TomCat 的 2D 运行时现使用 Butter，组件序列化、Scene 查询 API 和托管脚本 ABI 保持不变。

- 上游 PR：https://github.com/chnnasn/Butter/pull/1 （已合并）
- 当前验证版本：`TomCat/vendor/Butter`，提交 `dfdc5cc0763f98e35adbf06efa672715629e5c01`
- CCD PR：https://github.com/chnnasn/Butter/pull/2 （已提交，未合并）
- 休眠/预算修正 PR：https://github.com/chnnasn/Butter/pull/3 、 https://github.com/chnnasn/Butter/pull/4 （均已合并）
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

## 上游同步验证（2026-09-29，Windows x64）

- 上游 pin 由 `f1b4e428` 前进到 `18e4858d`（上游单次提交 "Optimize 2D contacts, obstacles, mesh queries, lifetime and CCD scheduling"），子模块内 34 个头文件与上游一致，工作区无本地改动。
- Butter Debug CTest：27/27 通过。
- TomCat Release PhysicsRegression：56/56 通过；其余 9 个原生程序全部通过（`ScriptCompilerRegression` 仍受本机缺少 .NET 10 SDK / 托管暂存步骤限制，非物理回归）。
- 适配层无需改动即可编译通过，但同步后发现一处上游行为变化导致回归，已在适配层修正，见下。

### 适配层修正：`DestroyFixture` 不再隐式唤醒刚体

- 现象：`TestInactiveHierarchyPreservesRuntimeBodyState` 由通过变为失败——「重新激活层级后，原本处于休眠的动态刚体被唤醒」。
- 根因：新版 Butter 的 `World::destroy_fixture()` 会调用 `wake_neighbors(*body)`，其中对动态刚体直接 `wake()`；旧版 `destroy_fixture()` 只做 `forget_contacts` + 移除夹具。而 Scene 的挂起流程是「先销毁不再需要的夹具 → 再读取 `IsAwake()` 快照」，于是快照被这次隐式唤醒污染，休眠状态在挂起/恢复往返中丢失。
- 对照 Box2D：`b2Body::DestroyFixture` 只销毁关联接触并调用 `ResetMassData()`，不唤醒刚体；`b2Fixture` 的摩擦/密度/弹性 setter 同样不唤醒。因此这是上游相对 Box2D 语义的偏离。
- 修正：在 `TomCat/src/TomCat/Physics/Physics2D.h` 的 `Body::DestroyFixture()` 中，跨 `native.destroy_fixture()` 保存并恢复 `sleeping` 与 `sleep_counter`，使夹具销毁对刚体状态保持中性。修正后 PhysicsRegression 恢复 56/56。
- 保留的差异：`World::destroy_fixture()` 仍会通过 `wake_neighbors` 唤醒与该刚体通过关节/接触相连的**其他**刚体，以及 `World::destroy_body()` 仍会唤醒邻居。这些唤醒是新版求解器重建求解岛所需，且不影响现有断言，未在适配层拦截。

### 上游修复落地：适配层绕过已移除（2026-09-29）

上面那处适配层绕过只是临时方案，根因在上游，已提 PR 修正并合并：

- chnnasn/Butter#3「Keep a body's sleep state when its fixture is destroyed」（合并为 `7155457`）——`World::destroy_fixture()` 不再唤醒夹具持有者。`wake_neighbors()` 新增 `wake_self` 形参，且 `wake_connected()` 增加 `skip` 形参把持有者从求解岛传播中排除；邻居唤醒保持不变（`test_2d_optimization` 用例 6 仍断言销毁地板夹具会唤醒压在其上的箱子）。
- chnnasn/Butter#4「Scale the parked-obstacle budget by build configuration」（合并为 `dfdc5cc`）——`test_2d_obstacles` 的墙钟预算按 `NDEBUG` 缩放（优化构建 300 ns、未优化 2000 ns）。未优化构建（含**不带 `CMAKE_BUILD_TYPE` 的默认构建**）不再假失败。

因此 pin 由 `18e4858d` 前进到 `dfdc5cc`，`Body::DestroyFixture()` 里保存/恢复 `sleeping` + `sleep_counter` 的那段绕过已删除——夹具销毁现在由 Butter 自身保证状态中性。删除后 TomCat Release PhysicsRegression 仍为 **56/56**，其余 8 个原生程序 rc=0（`ScriptCompilerRegression` 仍受本机缺少 .NET 10 SDK 限制，非物理回归）。

## 当前边界

CCD 支持 2D 圆、盒和凸多边形，包含双方平移、旋转、碰撞体局部偏移、碰撞后的剩余时间及多次反弹。碰撞过滤、关节的 CollideConnected 和休眠唤醒规则仍然生效。胶囊、网格及 3D 暂未加入 CCD；Trigger 保持步末离散重叠语义。初始穿透、主动传送和关节/穿透求解器的位置修正不做扫掠。

计算预算耗尽或未收敛时会保守丢弃该步剩余的整个 world 运动时间，避免继续无检测地前进；可通过 ccd_statistics() 的 limited / remaining_time 监测，并调整预算。高密度碰撞或复杂旋转场景可能因此出现减速。

求解器轨迹与 Box2D 不保证数值一致，实际关卡仍应检查堆叠、弹性和高速运动。本次没有完整 wasm 构建或浏览器验收。CCD PR 尚未合并，当前验证使用上述已推送的 PR 提交。
