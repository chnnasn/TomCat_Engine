# 协程

协程使用 C# `IEnumerator`，由脚本的 `Tasks` 作用域管理。它与普通异步任务共用主线程调度、
帧恢复预算和生命周期取消；协程不会创建后台线程。

```csharp
using System.Collections;
using TomCat;

public sealed class Pulse : MonoBehaviour
{
    private Coroutine? _pulse;

    void Start() => _pulse = StartCoroutine(Animate());

    IEnumerator Animate()
    {
        try
        {
            while (true)
            {
                transform.position += Vector3.up;
                yield return Yield.Seconds(0.5f);
                transform.position -= Vector3.up;
                yield return Yield.Seconds(0.5f);
            }
        }
        finally
        {
            // 释放纯托管资源；对象销毁时不要再访问已失效的场景对象。
        }
    }

    public void StopPulse()
    {
        if (_pulse != null) StopCoroutine(_pulse);
    }
}
```

## 等待指令

| 写法 | 行为 |
| --- | --- |
| `yield return null` | 下一显示帧，Update 之前恢复 |
| `yield return Yield.Frames(3)` | 至少等待 3 次显示帧推进 |
| `yield return Yield.Seconds(0.5f)` | 等待显示帧运行时间；暂停时间轴时不计时 |
| `yield return Yield.Until(() => ready)` | 下一帧开始检查，条件为 true 后继续 |
| `yield return Yield.While(() => busy)` | 下一帧开始检查，条件为 false 后继续 |
| `yield return Yield.FixedStep` | 下一物理步，FixedUpdate 之前恢复 |
| `yield return ChildRoutine()` | 执行嵌套 IEnumerator，完成后继续父协程 |
| `yield return otherCoroutine` | 等待同一脚本启动的 Coroutine 句柄结束 |

`Seconds(0)` 仍至少等待下一帧，负数、NaN、无穷大不被接受；Frames 必须大于零。
等待指令本身不可变，可以复用，每次 yield 重新开始计时/计帧。条件回调在主线程执行并受相同错误处理约束。
不支持把整数、Task 或 FrameAwaitable 直接 yield 出来；未知类型明确报错，不会默默当作下一帧。
需要后台工作时用 `Tasks.Run`，返回主线程后再更新对象。

## 句柄、停止与清理

- `StartCoroutine(iterator, optionalToken)` 立即执行到首次等待，返回 Coroutine 句柄。
  `Tasks.StartCoroutine` 是同一个入口；协程函数可使用任意名称，生命周期回调仍要求同步 void。
- `StopCoroutine(handle)` 请求停止该脚本的协程；`StopAllCoroutines()` 只停止协程，不影响普通 Tasks.Run 任务。
- 手动停止在下一个可用调度点结算，并遵循恢复预算；可以通过 `IsCancellationRequested` 立即读取停止请求。
  `Status` 为 Running、Completed、Cancelled 或 Faulted，`IsDone` 表示已结束，`Failure` 保存错误。
- 对象销毁、脚本移除、停止 Play、脚本故障会取消所有工作；销毁清理在 OnDestroy 前完成。
  普通禁用脚本或暂时隐藏对象不会自动停止协程，沿用 Tasks 的作用域规则；需要时在 OnDisable 中显式停止。
- 结束、停止和异常都会按最深子迭代器到父迭代器的顺序 Dispose，执行已经进入的 try/finally。
  某个 Dispose 抛错也不会跳过其余父级清理，清理错误会进入诊断。
- 嵌套迭代器属于父协程，父协程停止时一起清理；等待一个独立启动的句柄只建立等待关系，
  停止等待者不会停止被等待者。被等待协程取消后视为结束，失败则传播错误。

## 防卡顿与错误

每脚本每调度阶段最多恢复 64 次，场景合计最多 256 次，和异步等待共享预算。
协程单次推进最多处理 64 个迭代器步骤，空子协程过多时自动让到下一显示帧。
嵌套深度上限 128；嵌套循环、句柄等待循环、同一脚本并发复用同一个迭代器都会报错。

数量预算不能抢占某个 MoveNext、条件函数或 finally 内的长时间计算。这样的代码仍应拆分或放到后台。
协程正文及条件中的异常会使所属脚本故障并取消其剩余工作；本次主线程续体的场景修改事务回滚。

这些等待指令是 TomCat 的统一 Yield 接口，不提供 Unity 的 WaitForSeconds 类型或 timeScale。
