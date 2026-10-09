using System.Runtime.CompilerServices;
using System.Diagnostics.CodeAnalysis;

namespace TomCat;

/// <summary>Instance-owned asynchronous work with explicit engine-thread resumption.</summary>
public sealed class ScriptTasks
{
    private readonly object _gate = new();
    private readonly List<FrameAwaitable> _waiting = [];
    private readonly List<Task> _running = [];
    private readonly HashSet<Coroutine> _coroutines = [];
    private readonly HashSet<System.Collections.IEnumerator> _iterators = new(ReferenceEqualityComparer.Instance);
    private readonly CancellationTokenSource _lifetime;
    private readonly Entity _entity;
    private readonly int _thread = Environment.CurrentManagedThreadId;
    private bool _closed;
    internal const int ResumeBudget = 64;
    public CancellationToken Cancellation { get; }

    // Web compiles game assemblies after publish. Their await builders cannot be inferred by the linker.
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(AsyncTaskMethodBuilder))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(AsyncTaskMethodBuilder<>))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(TaskAwaiter))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(TaskAwaiter<>))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(Task))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.PublicMethods, typeof(Task<>))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(CompilationRelaxationsAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(RuntimeCompatibilityAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(System.Diagnostics.DebuggableAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(AsyncStateMachineAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(CompilerGeneratedAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(InternalsVisibleToAttribute))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(IAsyncStateMachine))]
    [DynamicDependency(DynamicallyAccessedMemberTypes.All, typeof(IteratorStateMachineAttribute))]
    internal ScriptTasks(Entity entity, CancellationToken domain)
    {
        _entity = entity;
        _lifetime = CancellationTokenSource.CreateLinkedTokenSource(domain);
        Cancellation = _lifetime.Token;
    }

    /// <summary>Start on the engine thread. Pass the supplied token to external asynchronous operations.</summary>
    public void Run(Func<CancellationToken, Task> work)
    {
        ArgumentNullException.ThrowIfNull(work);
        EnsureOwnerThread();
        Cancellation.ThrowIfCancellationRequested();
        Task task = work(Cancellation) ?? throw new InvalidOperationException("Task work returned null.");
        lock (_gate) _running.Add(task);
    }

    public Coroutine StartCoroutine(System.Collections.IEnumerator routine, CancellationToken cancellation = default)
    {
        ArgumentNullException.ThrowIfNull(routine);
        EnsureOwnerThread();
        Cancellation.ThrowIfCancellationRequested();
        ClaimIterator(routine);
        var coroutine = new Coroutine(this, routine, cancellation);
        _coroutines.Add(coroutine);
        Run(coroutine.Run);
        return coroutine;
    }
    public void StopCoroutine(Coroutine coroutine)
    {
        ArgumentNullException.ThrowIfNull(coroutine);
        EnsureOwnerThread();
        if (!ReferenceEquals(coroutine.Owner, this)) throw new ArgumentException("Coroutine belongs to another script.", nameof(coroutine));
        coroutine.Stop();
    }
    public void StopAllCoroutines()
    {
        EnsureOwnerThread();
        foreach (var coroutine in _coroutines.ToArray()) coroutine.Stop();
    }
    internal void CoroutineFinished(Coroutine coroutine) => _coroutines.Remove(coroutine);
    internal void ClaimIterator(System.Collections.IEnumerator iterator)
    {
        if (!_iterators.Add(iterator)) throw new InvalidOperationException("An iterator is already running in this script. Create a new iterator for each coroutine.");
    }
    internal void ReleaseIterator(System.Collections.IEnumerator iterator) => _iterators.Remove(iterator);

    /// <summary>Resume before a subsequent Update; never resumes in the frame that requested the wait.</summary>
    public FrameAwaitable NextFrame(CancellationToken cancellation = default) =>
        new(this, false, true, cancellation);
    /// <summary>Resume before a subsequent FixedUpdate.</summary>
    public FrameAwaitable NextFixedStep(CancellationToken cancellation = default) =>
        new(this, true, true, cancellation);
    /// <summary>Queue work for the next available display-frame dispatch, even when already on the engine thread.</summary>
    public FrameAwaitable MainThread(CancellationToken cancellation = default) =>
        new(this, false, false, cancellation);

    private void EnsureOwnerThread()
    {
        if (Environment.CurrentManagedThreadId != _thread || !ScriptExecutionContext.IsActive || ScriptExecutionContext.CurrentEntity != _entity)
            throw new InvalidOperationException("Start tasks from a script callback. Await Tasks.MainThread() before accessing engine objects from background work.");
    }

    internal int Pump(bool fixedStep, Action<Action> dispatch, int budget = ResumeBudget)
    {
        FrameAwaitable[] ready;
        lock (_gate)
        {
            if (_waiting.Count == 0 && _running.Count == 0) return 0;
            ready = _waiting.Where(wait => wait.Cancelled || (wait.FixedStep == fixedStep &&
                (wait.FixedStep ? TimeRuntime.FixedFrameCount : TimeRuntime.FrameCount) >= wait.Target))
                .Take(budget).ToArray();
        }
        foreach (var wait in ready)
            dispatch(() =>
            {
                lock (_gate) _waiting.Remove(wait);
                wait.Resume();
                CheckFailures();
            });
        bool hasCompleted;
        lock (_gate) hasCompleted = _running.Any(task => task.IsCompleted);
        if (hasCompleted) dispatch(CheckFailures);
        return ready.Length;
    }

    internal void CheckFailures()
    {
        Task[] completed;
        lock (_gate)
        {
            if (_running.Count == 0) return;
            completed = _running.Where(task => task.IsCompleted).ToArray();
            foreach (var task in completed) _running.Remove(task);
        }
        var errors = completed.Where(task => task.IsFaulted)
            .SelectMany(task => task.Exception!.Flatten().InnerExceptions).ToArray();
        if (errors.Length != 0) throw new AggregateException("Script task failed", errors);
    }

    internal void Stop()
    {
        FrameAwaitable[] waiting;
        Task[] running;
        lock (_gate)
        {
            if (_closed) return;
            _closed = true;
            waiting = _waiting.ToArray(); _waiting.Clear();
            running = _running.ToArray(); _running.Clear();
        }
        try { _lifetime.Cancel(); }
        catch (AggregateException error) { NativeBridge.ReportManagedException($"Script task cancellation failed: {error}"); }
        foreach (var wait in waiting)
        {
            try { wait.Resume(); }
            catch (Exception error) { NativeBridge.ReportManagedException($"Script task cleanup failed: {error}"); }
        }
        // Observe eventual failures without retaining the behaviour or an engine callback.
        foreach (var task in running)
        {
            if (task.IsFaulted)
            {
                NativeBridge.ReportManagedException($"Script task cleanup failed: {task.Exception!.Flatten()}");
                continue;
            }
            _ = task.ContinueWith(static completed => { _ = completed.Exception; }, CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
        }
        _lifetime.Dispose();
    }

    /// <summary>A single-use awaitable. Cancellation is delivered at a dispatch point or during teardown.</summary>
    public sealed class FrameAwaitable : INotifyCompletion
    {
        private readonly ScriptTasks _owner;
        private readonly CancellationToken _cancellation;
        private Action? _continuation;
        private int _registered;
        internal readonly bool FixedStep;
        internal readonly ulong Target;
        internal bool Cancelled => _owner.Cancellation.IsCancellationRequested || _cancellation.IsCancellationRequested;
        internal FrameAwaitable(ScriptTasks owner, bool fixedStep, bool next, CancellationToken cancellation)
        {
            _owner = owner; FixedStep = fixedStep; _cancellation = cancellation;
            Target = (fixedStep ? TimeRuntime.FixedFrameCount : TimeRuntime.FrameCount) + (next ? 1UL : 0UL);
        }
        public FrameAwaitable GetAwaiter() => this;
        public bool IsCompleted => Cancelled;
        public void GetResult()
        {
            _owner.Cancellation.ThrowIfCancellationRequested();
            _cancellation.ThrowIfCancellationRequested();
        }
        public void OnCompleted(Action continuation)
        {
            ArgumentNullException.ThrowIfNull(continuation);
            if (Interlocked.Exchange(ref _registered, 1) != 0)
                throw new InvalidOperationException("Frame waits can only be awaited once.");
            bool closed;
            lock (_owner._gate)
            {
                closed = _owner._closed;
                if (!closed) { _continuation = continuation; _owner._waiting.Add(this); }
            }
            // A stop racing with registration must settle the task, not strand its state machine.
            if (closed) continuation();
        }
        internal void Resume() => Interlocked.Exchange(ref _continuation, null)?.Invoke();
    }
}
