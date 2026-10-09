using TomCat;
namespace TomCat.Managed.Regression;
internal static unsafe partial class Program
{
    private static void VerifyScriptTasks()
    {
        Throws<InvalidOperationException>(() => new InvalidAsyncMessageProbe().__Bind(
            new Entity(SceneSession, 9, RuntimeGeneration), new ScriptInstanceHandle(900)),
            "async void lifecycle messages must not bypass tracked task handling");
        TaskProbe Make(int mode)
        {
            var probe = new TaskProbe { Mode = mode };
            probe.__Bind(new Entity(SceneSession, 9, RuntimeGeneration), new ScriptInstanceHandle(777));
            probe.__Create();
            return probe;
        }
        void Pump(TaskProbe probe, bool fixedStep = false) => probe.__PumpTasks(fixedStep, probe.__TaskContinuation);
        var flow = Make(0);
        Pump(flow);
        Check(flow.Frames == 0, "NextFrame must not resume in its requesting frame");
        TimeRuntime.BeginFrame(0.025f); Pump(flow);
        Check(flow.Frames == 1 && flow.Fixed == 0, "frame wait phase");
        TimeRuntime.BeginFrame(0.025f); Pump(flow);
        Check(flow.Fixed == 0, "fixed wait must not resume in display dispatch");
        TimeRuntime.BeginFixedStep(0.01f); Pump(flow, true); TimeRuntime.EndFixedStep();
        Check(flow.Fixed == 1 && flow.Thread == 0, "fixed wait phase");
        TimeRuntime.BeginFrame(0.025f); Pump(flow);
        Check(flow.Thread == Environment.CurrentManagedThreadId, "explicit main-thread return");
        flow.__Destroy();

        var cancelled = Make(1);
        cancelled.__Destroy();
        Check(cancelled.Cleaned && cancelled.DestroySawCancellation, "teardown must cancel waits before OnDestroy");
        using var domain = new CancellationTokenSource();
        var domainProbe = new TaskProbe { Mode = 1 };
        domainProbe.__Bind(new Entity(SceneSession, 9, RuntimeGeneration), new ScriptInstanceHandle(778), domain.Token);
        domainProbe.__Create(); domain.Cancel(); Pump(domainProbe);
        Check(domainProbe.Cleaned && domainProbe.Tasks.Cancellation.IsCancellationRequested, "Play domain cancellation propagates to owned work");
        domainProbe.__Destroy();
        var budget = Make(2);
        TimeRuntime.BeginFrame(0.025f); Pump(budget);
        Equal(64, budget.Frames, "per-instance continuation budget");
        TimeRuntime.BeginFrame(0.025f); Pump(budget);
        Equal(100, budget.Frames, "deferred continuation backlog");
        budget.__Destroy();

        var background = Make(3);
        Check(background.Posted.Wait(5000), "background task reached main-thread scheduling");
        TimeRuntime.BeginFrame(0.025f); Pump(background);
        Check(background.Thread == Environment.CurrentManagedThreadId, "background work must resume on engine thread");
        background.__Destroy();
        var failed = Make(4);
        TimeRuntime.BeginFrame(0.025f);
        Throws<AggregateException>(() => Pump(failed), "asynchronous failures must reach host transaction handling");
        failed.__Destroy();
        var callerCancelled = Make(5);
        callerCancelled.External.Cancel(); Pump(callerCancelled);
        Check(callerCancelled.Cleaned, "caller cancellation must settle a fixed wait without requiring a fixed step");
        callerCancelled.__Destroy();
    }
}
internal sealed class TaskProbe : MonoBehaviour
{
    internal int Mode, Frames, Fixed, Thread;
    internal bool Cleaned, DestroySawCancellation;
    internal readonly ManualResetEventSlim Posted = new();
    internal readonly CancellationTokenSource External = new();
    private void Awake()
    {
        if (Mode == 2)
        {
            for (int i = 0; i < 100; ++i) Tasks.Run(async token => { await Tasks.NextFrame(token); ++Frames; });
            return;
        }
        Tasks.Run(Flow);
    }
    private async Task Flow(CancellationToken token)
    {
        if (Mode == 1 || Mode == 5)
        {
            try { await Tasks.NextFixedStep(Mode == 5 ? External.Token : token); }
            finally { Cleaned = true; }
            return;
        }
        if (Mode == 3)
        {
            await Task.Run(() => { }, token).ConfigureAwait(false);
            var wait = Tasks.MainThread(token);
            // Signal after registration rather than before to avoid a test race.
            await new PostedWait(wait, Posted);
            Thread = Environment.CurrentManagedThreadId;
            transform.position = Vector3.right;
            return;
        }
        await Tasks.NextFrame(token);
        ++Frames;
        if (Mode == 4) throw new InvalidOperationException("async probe failure");
        await Tasks.NextFixedStep(token);
        ++Fixed;
        await Tasks.MainThread(token);
        Thread = Environment.CurrentManagedThreadId;
        transform.position = Vector3.up;
    }
    private void OnDestroy() => DestroySawCancellation = Tasks.Cancellation.IsCancellationRequested;
    private sealed class PostedWait(ScriptTasks.FrameAwaitable wait, ManualResetEventSlim posted) : System.Runtime.CompilerServices.INotifyCompletion
    {
        public PostedWait GetAwaiter() => this;
        public bool IsCompleted => false;
        public void GetResult() => wait.GetResult();
        public void OnCompleted(Action continuation) { wait.OnCompleted(continuation); posted.Set(); }
    }
}

internal sealed class InvalidAsyncMessageProbe : MonoBehaviour
{
    private async void Update() => await Task.Yield();
}
