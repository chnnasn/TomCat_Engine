using System.Collections;

namespace TomCat;

public enum CoroutineStatus { Running, Completed, Cancelled, Faulted }

/// <summary>Immutable instructions; each yield establishes a fresh wait.</summary>
public sealed class CoroutineWait
{
    internal enum WaitKind { Frames, Seconds, Until, While, FixedStep }
    internal WaitKind Kind { get; }
    internal int Count { get; }
    internal float Duration { get; }
    internal Func<bool>? Predicate { get; }
    internal CoroutineWait(WaitKind kind, int count = 0, float duration = 0, Func<bool>? predicate = null)
    { Kind = kind; Count = count; Duration = duration; Predicate = predicate; }
}

public static class Yield
{
    public static CoroutineWait Frames(int count)
    {
        if (count < 1) throw new ArgumentOutOfRangeException(nameof(count));
        return new(CoroutineWait.WaitKind.Frames, count: count);
    }
    /// <summary>Uses display-frame runtime time; pauses when that timeline stops. Always yields at least once.</summary>
    public static CoroutineWait Seconds(float seconds)
    {
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        return new(CoroutineWait.WaitKind.Seconds, duration: seconds);
    }
    public static CoroutineWait Until(Func<bool> predicate)
    { ArgumentNullException.ThrowIfNull(predicate); return new(CoroutineWait.WaitKind.Until, predicate: predicate); }
    public static CoroutineWait While(Func<bool> predicate)
    { ArgumentNullException.ThrowIfNull(predicate); return new(CoroutineWait.WaitKind.While, predicate: predicate); }
    public static CoroutineWait FixedStep { get; } = new(CoroutineWait.WaitKind.FixedStep);
}

/// <summary>A handle to an iterator owned by one script task scope.</summary>
public sealed class Coroutine
{
    private readonly ScriptTasks _owner;
    private readonly Stack<IEnumerator> _stack = new();
    private readonly CancellationTokenSource _stop;
    private readonly CancellationToken _cancellation;
    private Coroutine? _waitingOn;
    public CoroutineStatus Status { get; private set; } = CoroutineStatus.Running;
    public bool IsDone => Status != CoroutineStatus.Running;
    public bool IsCancellationRequested => _cancellation.IsCancellationRequested;
    public Exception? Failure { get; private set; }
    internal ScriptTasks Owner => _owner;
    internal Coroutine(ScriptTasks owner, IEnumerator routine, CancellationToken cancellation)
    {
        _owner = owner;
        _stack.Push(routine);
        _stop = CancellationTokenSource.CreateLinkedTokenSource(owner.Cancellation, cancellation);
        _cancellation = _stop.Token;
    }
    internal void Stop() { if (!IsDone) _stop.Cancel(); }

    internal async Task Run(CancellationToken unused)
    {
        Exception? failure = null;
        bool cancelled = false;
        int steps = 0;
        try
        {
            while (_stack.Count != 0)
            {
                _cancellation.ThrowIfCancellationRequested();
                // Empty nested iterators can otherwise monopolize the main thread without yielding.
                if (++steps > 64)
                {
                    await _owner.NextFrame(_cancellation);
                    steps = 0;
                }
                IEnumerator current = _stack.Peek();
                if (!current.MoveNext())
                {
                    _stack.Pop();
                    _owner.ReleaseIterator(current);
                    (current as IDisposable)?.Dispose();
                    continue;
                }
                _cancellation.ThrowIfCancellationRequested();
                object? instruction = current.Current;
                switch (instruction)
                {
                    case null:
                        await _owner.NextFrame(_cancellation);
                        steps = 0;
                        break;
                    case IEnumerator nested:
                        if (_stack.Count >= 128 || _stack.Any(item => ReferenceEquals(item, nested)))
                            throw new InvalidOperationException("Coroutine nesting is cyclic or exceeds 128 iterators.");
                        _owner.ClaimIterator(nested);
                        _stack.Push(nested);
                        break;
                    case Coroutine child:
                        if (!ReferenceEquals(child.Owner, _owner))
                            throw new InvalidOperationException("Only coroutines from the same script can be joined.");
                        for (Coroutine? cursor = child; cursor != null; cursor = cursor._waitingOn)
                            if (ReferenceEquals(cursor, this)) throw new InvalidOperationException("Coroutine wait cycle detected.");
                        _waitingOn = child;
                        try
                        {
                            while (!child.IsDone) await _owner.NextFrame(_cancellation);
                            if (child.Failure != null) throw new InvalidOperationException("Joined coroutine failed.", child.Failure);
                        }
                        finally { _waitingOn = null; }
                        break;
                    case CoroutineWait wait:
                        switch (wait.Kind)
                        {
                            case CoroutineWait.WaitKind.Frames:
                                for (int i = 0; i < wait.Count; ++i) await _owner.NextFrame(_cancellation);
                                break;
                            case CoroutineWait.WaitKind.Seconds:
                                double deadline = TimeRuntime.ElapsedTime + wait.Duration;
                                do { await _owner.NextFrame(_cancellation); } while (TimeRuntime.ElapsedTime < deadline);
                                break;
                            case CoroutineWait.WaitKind.FixedStep:
                                await _owner.NextFixedStep(_cancellation);
                                break;
                            case CoroutineWait.WaitKind.Until:
                            case CoroutineWait.WaitKind.While:
                                do { await _owner.NextFrame(_cancellation); }
                                while (wait.Predicate!() == (wait.Kind == CoroutineWait.WaitKind.While));
                                break;
                        }
                        steps = 0;
                        break;
                    default:
                        throw new InvalidOperationException($"Unsupported coroutine yield: {instruction.GetType().FullName}.");
                }
            }
        }
        catch (OperationCanceledException) when (_cancellation.IsCancellationRequested) { cancelled = true; }
        catch (Exception error) { failure = error; }
        finally
        {
            // Dispose deepest-first so iterator finally blocks run on cancellation and errors too.
            while (_stack.TryPop(out IEnumerator? iterator))
            {
                _owner.ReleaseIterator(iterator);
                try { (iterator as IDisposable)?.Dispose(); }
                catch (Exception error) { failure = failure is null ? error : new AggregateException(failure, error); }
            }
            _waitingOn = null;
            _stop.Dispose();
            Failure = failure;
            Status = failure != null ? CoroutineStatus.Faulted : cancelled ? CoroutineStatus.Cancelled : CoroutineStatus.Completed;
            _owner.CoroutineFinished(this);
        }
        // An arbitrary OCE from user code or Dispose is a failure unless our token actually cancelled the wait.
        // Do not let Task's builder turn such a fault into an unreported cancelled Task.
        if (failure is OperationCanceledException)
            throw new InvalidOperationException("Coroutine cancellation or cleanup failed.", failure);
        if (failure != null) System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
    }

}
