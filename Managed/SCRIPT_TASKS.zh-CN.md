# 脚本任务与帧调度

TomCat 保留简洁的同步生命周期：`Awake()` 初始化，`Update()` 每帧执行，时间通过 `Time` 获取。
回调名称采用熟悉的写法，但异步工作由每个脚本自己的 `Tasks` 作用域管理，避免在 Update 中
混入无法追踪的 `async void`。编译器拒绝异步生命周期消息；从同步回调调用 `Tasks.Run`。

```csharp
using TomCat;

public sealed class Pulse : MonoBehaviour
{
    void Start()
    {
        Tasks.Run(async stop =>
        {
            while (true)
            {
                await Tasks.NextFrame(stop);
                transform.position += Vector3.up * Time.deltaTime;
            }
        });
    }
}
```

## 返回引擎线程

标准 .NET I/O、`Task.Run`、`ConfigureAwait(false)` 都保持原有语义。TomCat 不安装全局
SynchronizationContext，不会悄悄把所有 .NET 异步操作搬回主线程。读写引擎对象之前，显式返回：

```csharp
void Start()
{
    Tasks.Run(async stop =>
    {
        var result = await System.Threading.Tasks.Task.Run(() => 42, stop)
            .ConfigureAwait(false);
        await Tasks.MainThread(stop);
        name = "Result " + result;
    });
}
```

任务通过 `Tasks.Run(Func<CancellationToken, Task>)` 启动并被跟踪；只能从所属对象的脚本
上下文启动。不要对返回的 Task 使用 `.Wait()` 或 `.Result` 阻塞引擎线程，也不要启动
不受跟踪的 `async void`。作用域不强制终止线程或阻塞计算；长任务仍需主动检查取消令牌。

## 时序与预算

| 接口 | 成功恢复时机 |
| --- | --- |
| `Tasks.NextFrame(token)` | 请求之后的显示帧，在 Update 之前 |
| `Tasks.NextFixedStep(token)` | 请求之后的物理步，在 FixedUpdate 之前 |
| `Tasks.MainThread(token)` | 下一个可用的显示帧任务调度点；即使已在主线程，也会排队 |

等待对象只能 await 一次。显示帧等待不会因物理补步提前完成；固定帧等待不会在显示帧中
成功恢复。续体具有原脚本的引擎上下文和独立的场景修改事务。
在每个调度阶段，同一脚本最多恢复 64 个续体，同一场景最多恢复 256 个；剩余工作顺延，
场景在预算耗尽后从后续脚本继续调度。预算限制的是数量，不是执行毫秒数，耗时计算应放在后台。

`NextFrame` 是“至少下一帧”，不是精确计时器。暂停后不再推进相应帧阶段时，等待也暂停。
同步回调中的 `Time.deltaTime` 语义同样适用于续体：物理阶段使用固定步长。

## 生命周期与错误

- 每个脚本拥有 `Tasks.Cancellation`。脚本移除、对象销毁、场景销毁、停止 Play 或脚本故障都会取消它。
- 任务等待自动绑定所属作用域；不传入额外 token 也会随对象销毁取消。传入 token 可以额外取消单次等待。
- 普通 `enabled = false`、对象暂时隐藏不取消任务；异步工作继续在任务调度点恢复。这与暂停脚本更新明确区分。
- 销毁会先取消并结算已排队的等待，再调用 OnDestroy。即使物理步未继续，取消的固定步等待也能在下一次任务调度时结算。
- 取消通过 OperationCanceledException 传播，`finally` 可做纯托管资源清理。已取消等待可能同步完成，
  或在停止与后台注册竞争时于注册线程完成；清理代码不能假设仍可访问引擎对象。
- 非取消异常进入脚本诊断，故障脚本停止调度；引擎线程续体的本次场景修改会回滚。
- 后台任务必须把作用域 token 传入可取消的 I/O，并主动合作退出。不接受取消的外部任务不能被
  引擎强行结束，仍可能延长脚本程序集的存活时间；退出后只观察其异常，不再调用引擎日志或场景 API。

这里使用标准 Task 管理异步任务体、实例作用域管理取消，通过显式帧等待和主线程返回点控制调度。
现在也提供[迭代器协程](COROUTINES.zh-CN.md)，使用同一个任务作用域和统一的 Yield 等待指令。
当前不提供 Unity 的 WaitForSeconds 类型或 timeScale。
