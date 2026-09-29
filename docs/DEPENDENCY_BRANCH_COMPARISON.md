# dev_ekit / dev_butter 依赖同步与 main 差距对比

记录 2026-09-29 这一轮：把 `dev_ekit` 的 ekit、`dev_butter` 的 Butter 同步到上游最新，
然后与 `main`（entt + Box2D）做正确性回归和性能基准对比。

- `dev_ekit`：`47ce3858` chore(deps): update vendored ekit to e26a5327
- `dev_butter`：`62b1d70e` chore(deps): update Butter submodule to 18e4858d
- `main`：未改动，作为对照基线

> **后续（2026-09-29 同日）**：下面第 4 节记录的那处适配层绕过已在上游修复并合并
> （chnnasn/Butter#3 合并为 `7155457`，另有 #4 合并为 `dfdc5cc`）。`dev_butter` 的 Butter pin
> 随后前进到 `dfdc5cc`，`Body::DestroyFixture()` 中的保存/恢复 `sleeping` 已删除。
> 本文其余部分保留当时（pin `18e4858d`）的记录，见 `docs/Butter-Migration.md` 的
> 「上游修复落地」一节。

## 1. 依赖 pin 与上游增量

| 依赖 | 分支 | 旧 pin | 新 pin | 上游增量 |
| --- | --- | --- | --- | --- |
| ekit | dev_ekit | `82d4de67` | `e26a5327` | 7 个提交，+422 / −215 行，仅 `component/query/world` |
| Butter | dev_butter | `f1b4e428` | `18e4858d` | 1 个压缩提交 "Optimize 2D contacts, obstacles, mesh queries, lifetime and CCD scheduling"，+6589 / −397 行 |
| Box2D | main | `b3c3e1f5` | 未变 | 已与上游 HEAD 一致，无落后 |
| entt | main | 3.15.0 | 未变 | — |

两个分支的 vendored 副本都与上游逐字节一致（忽略行尾），没有夹带本地修改。

## 2. 正确性回归

构建方式：CMake 4.3 + Ninja + MSVC 19.50.35724（VS 2026 Pro，cl 14.50.35717），
Release `/O2`。本环境的 MSBuild / dotnet 被沙箱策略拦截，因此用 CMake 手工镜像了 premake
工程来编译 `TomCat` 静态库和 `Tests/` 下的 10 个原生回归可执行文件。

| 分支 | ECS / 物理 | PhysicsRegression | 其余 9 个套件 | 上游 CTest |
| --- | --- | --- | --- | --- |
| main | entt 3.15.0 / Box2D 2.4.1 | 55 / 55 | 全部 rc=0 | — |
| dev_ekit | ekit `e26a5327` / Box2D 2.4.1 | 55 / 55 | 全部 rc=0 | ekit：124,965 checks / 0 failures |
| dev_butter | entt 3.15.0 / Butter `18e4858d` | 56 / 56 | 全部 rc=0 | Butter：27 / 27 |

说明：

- `ScriptCompilerRegression` 在三个分支上都以 `ScriptProjectCompiler configuration failed for
  the injection test` 退出。它需要仓库的 .NET 10 SDK 暂存步骤（`Run-Regressions.ps1` 会先跑
  `Build-TomCatManagedRelease`），本环境无法执行。**在 main 上同样失败**，因此是环境限制，不是
  分支回归。
- `dev_butter` 的 PhysicsRegression 比另外两个分支多 1 个用例（56 vs 55），是 Butter 分支自己
  新增的休眠/CCD 用例。
- 三个分支的适配层都能直接编译通过新依赖，`dev_ekit` 无需任何改动。

### 2.1 dev_butter 上发现并修复的一处回归

升级到 `18e4858d` 后 `PhysicsRegression` 从 56/56 掉到 55 PASS + 1 FAIL：

```
FAIL inactive hierarchy preserves dynamic body state and clears stale cache:
     re-enabled hierarchy woke a previously sleeping body
```

定位过程（在测试和引擎里临时插桩，定位后已全部移除）：

1. 在断言前打印 `IsAwake()`，确认刚体在挂起/恢复往返之后确实被唤醒。
2. 在 Butter 的 `Body::wake()` 里打印「从休眠到唤醒」的转换，拿到唤醒发生的精确位置——它发生在
   `SetAwake(false)` 与 Scene 读取挂起快照之间。
3. 在 Scene 的挂起分支打印 `uuid`/`awake`，看到快照记录的是 `awake=1`，即快照本身被污染。

根因：新版 Butter 的 `World::destroy_fixture()` 会调用 `wake_neighbors(*body)`，其中对动态刚体
直接 `wake()`；旧版 `destroy_fixture()` 只做 `forget_contacts` + 移除夹具。而 Scene 的挂起流程是
「先销毁不再需要的夹具 → 再读取 `IsAwake()` 快照」，于是快照被这次隐式唤醒污染，休眠状态在
挂起/恢复往返中丢失。

对照 Box2D：`b2Body::DestroyFixture` 只销毁关联接触并调用 `ResetMassData()`，不唤醒刚体；
`b2Fixture` 的摩擦/密度/弹性 setter 同样不唤醒。`Physics2D.h` 适配层的职责正是提供 Box2D 语义，
所以这是上游相对 Box2D 的一次行为偏离。

修复（`TomCat/src/TomCat/Physics/Physics2D.h`，+14 行）：在 `Body::DestroyFixture()` 里跨
`native.destroy_fixture()` 保存并恢复 `sleeping` 与 `sleep_counter`，使夹具销毁对刚体状态保持中性。
修复后 PhysicsRegression 恢复 56/56。

保留的差异：`destroy_fixture()` 仍会通过 `wake_neighbors` 唤醒与该刚体通过关节/接触相连的**其他**
刚体，`destroy_body()` 也仍会唤醒邻居。这些唤醒是新版求解器重建求解岛所需，不破坏任何断言，因此
没有在适配层拦截。

## 3. 物理基准：Butter `18e4858d` vs Box2D 2.4.1

统一的单场景 harness，同一份 `.cpp` 编译两次（`-DTC_BACKEND_BOX2D` / `-DTC_BACKEND_BUTTER`），
场景与 Butter 自带的 `benchmarks/bench_2d.cpp` 对齐：静态地板 + N 个半径/半宽 0.5 的刚体，
密度 1、摩擦 0.5、弹性 0，间距 1.1，外加一个远离堆叠的运动学探针；dt = 1/60，360 帧（6 秒），
开启动态对静态 CCD 与休眠。Box2D 走 `Step(1/60, 8, 3)`，Butter 用默认 `solver_iterations = 8`。

两侧的活跃度完全一致，说明对比是同轨迹、同求解负担的：

```
circles/boxes  100 → 4900 active body-steps
               500 → 24500
              1000 → 49000
              5000 → 245000
```

中位数（5 次运行取中位，单位 ms/步）：

| 场景 | 数量 | Box2D | Butter | Butter / Box2D |
| --- | ---: | ---: | ---: | ---: |
| circles | 100 | 0.015424 | 0.024688 | 1.60 |
| circles | 500 | 0.072483 | 0.119297 | 1.65 |
| circles | 1000 | 0.157703 | 0.254074 | 1.61 |
| circles | 5000 | 1.164599 | 1.424006 | 1.22 |
| boxes | 100 | 0.019310 | 0.035416 | 1.83 |
| boxes | 500 | 0.101731 | 0.171048 | 1.68 |
| boxes | 1000 | 0.198500 | 0.393592 | 1.98 |
| boxes | 5000 | 1.330677 | 1.997219 | 1.50 |

结论：在这个「落地并静止」的场景下，Butter 仍比 Box2D 慢约 1.2–2.0×，盒堆叠的差距（1.5–2.0×）
比圆形（1.2–1.7×）更大。与上一轮 `.scratch/comparison-latest-r5` 记录的 1000 体 2.17×（圆）/
3.84×（盒）相比，本次明显收窄——但两轮的场景参数、harness 和适配路径都不同，只能当作观察，
不能当作受控的版本加速比。

## 4. ECS 基准：ekit `e26a5327` vs entt 3.15.0

同一个 translation unit 内用运行期开关切换两个后端，保证编译选项和场景代码完全一致。
10 万实体、5 次运行取中位（ms）：

| 任务 | entt | ekit_raw | ekit_scene | raw/entt | scene/entt |
| --- | ---: | ---: | ---: | ---: | ---: |
| create2（建实体 + 2 组件） | 4.6971 | 8.7640 | 14.5146 | 1.87 | 3.09 |
| foreach200（批量遍历 200 轮） | 42.6075 | 44.7838 | 30.8333 | 1.05 | **0.72** |
| handle200（按句柄逐个查 2 组件 ×200） | 110.6497 | 168.6898 | 181.6873 | 1.53 | 1.64 |
| random10（乱序单组件读 ×10） | 4.9685 | 12.6111 | 12.9710 | 2.54 | 2.61 |
| churn10（Tag 增删 ×10） | 30.6848 | 13.3617 | 13.9536 | **0.44** | **0.46** |
| destroy（销毁全部） | 6.5219 | 1.9141 | 4.4734 | **0.29** | **0.69** |

`ekit_scene` 是 `TomCat::SceneWorld`，即 `dev_ekit` 引擎真正使用的包装类；它额外维护 picking 索引，
所以 create/destroy 带着这部分引擎级开销（destroy 从 1.91 ms 涨到 4.47 ms 就是这个原因）。
`ekit_raw` 是裸 `ekit::World`。

三处校验和跨实现完全一致（`foreach200` = 5550006、`handle200` = 6150012、`random10` = 53500131），
说明测的是同一份计算。

差距画像：

- **ekit 更快**：批量遍历（经引擎包装后比 entt 少约 28% 时间）、结构增删（约 2.2×）、批量销毁
  （裸库约 3.4×，经包装约 1.5×）。这与上游 `ekit_comprehensive_compare` 的 iterate 0.495×（标量）
  / 0.300×（SoA 批量）方向一致。
- **entt 更快**：实体创建（ekit 慢 1.9–3.1×）、按句柄逐个查组件（慢约 1.5–1.6×）、乱序单实体读
  （慢约 2.5×）。上游 `ekit_native_compare`（10000 boids 融合算法）同样报 ekit/entt 1.13–1.62×，
  随机访问 1.10–1.22×，方向一致。
- 注意上游的 churn 定义不同：`ekit_comprehensive_compare` 的 churn 是「创建 + 2 组件 + 销毁」，
  报 ekit 1.908×（更慢）；本表的 `churn10` 是「对已有实体增删 Tag」，报 ekit 0.44×（更快）。
  两者不矛盾，量的是不同操作。

## 5. 复现方式

```bash
# 依赖
git submodule update --init --recursive

# 上游 CTest
cmake -S TomCat/vendor/Butter -B build/butter -DBUTTER_BUILD_EXAMPLES=OFF
cmake --build build/butter --config Debug
ctest --test-dir build/butter -C Debug --output-on-failure

# 仓库原生回归（需要可用的 MSBuild + .NET 10 SDK）
Scripts/Run-PhysicsRegression.ps1 -Configuration Release
Scripts/Run-Regressions.ps1 -Configuration Release
```

本轮因为沙箱里 MSBuild / dotnet 不可用，回归是用 CMake + Ninja 手工镜像 premake 工程跑的：
`TomCat` 静态库 + 10 个回归可执行文件，Release，手工补 `Packages/`（精灵、字体）和
`shaderc_shared.dll` 到输出目录后直接运行。

## 6. 结论

- 两个分支都跟上了上游，编译与正确性回归全部通过；只有 `dev_butter` 需要一处 14 行的适配层
  修正（夹具销毁不再隐式唤醒刚体），已在 `62b1d70e` 里记录。
- ECS 侧 ekit 与 entt 是明确的取舍而非全面替代：批量遍历和结构增删 ekit 赢，稀疏随机访问和
  实体创建 entt 赢。引擎里随机访问和创建都发生在热路径上，这部分差距需要在使用中留意。
- 物理侧 Butter 相对 Box2D 仍有 1.2–2.0× 的差距，且本次同步明显收窄了盒堆叠的差距；活动量
  完全一致，说明不是「少算了什么」换来的速度。
