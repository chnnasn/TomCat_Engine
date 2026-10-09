using System.Collections;
using TomCat;
namespace TomCat.Managed.Regression;
internal static unsafe partial class Program
{
    private static void VerifyCoroutines()
    {
        CoroutineProbe Make()
        {
            var probe = new CoroutineProbe();
            probe.__Bind(new Entity(SceneSession, 9, RuntimeGeneration), new ScriptInstanceHandle(901));
            return probe;
        }
        Coroutine Start(CoroutineProbe probe, IEnumerator iterator)
        {
            Coroutine? result = null;
            probe.__TaskContinuation(() => result = probe.StartCoroutine(iterator));
            return result!;
        }
        void Pump(CoroutineProbe probe, bool fixedStep = false) => probe.__PumpTasks(fixedStep, probe.__TaskContinuation);
        void Frame(CoroutineProbe probe) { TimeRuntime.BeginFrame(0.025f); Pump(probe); }
        var flow = Make();
        var handle = Start(flow, flow.Sequence());
        Check(flow.Trace == "start ", "coroutine executes to first yield at start");
        Pump(flow); Check(flow.Trace == "start ", "null must wait for next frame");
        Frame(flow); Check(flow.Trace == "start frame ", "null continuation");
        Frame(flow); Check(flow.Trace == "start frame ", "Frames must count complete display steps");
        Frame(flow); Check(flow.Trace.EndsWith("frames "), "multi-frame wait");
        Frame(flow); Check(flow.Trace.EndsWith("frames "), "seconds must not finish early");
        Frame(flow); Check(flow.Trace.EndsWith("seconds "), "runtime time delay");
        Frame(flow); Check(flow.Trace.EndsWith("seconds "), "until false");
        flow.Ready = true; Frame(flow); Check(flow.Trace.EndsWith("until "), "until true");
        Frame(flow); Check(flow.Trace.EndsWith("until "), "while true");
        flow.Blocked = false; Frame(flow); Check(flow.Trace.EndsWith("while "), "while false");
        Frame(flow); Check(flow.Trace.EndsWith("while "), "fixed wait ignores display frames");
        TimeRuntime.BeginFixedStep(0.01f); Pump(flow, true); TimeRuntime.EndFixedStep();
        Check(flow.Trace.EndsWith("fixed child "), "fixed and nested initial execution");
        Frame(flow);
        Check(handle.Status == CoroutineStatus.Completed && flow.Trace.EndsWith("child-end end "), "nested completion resumes parent");
        flow.__Destroy();

        var stopped = Make();
        var pending = Start(stopped, stopped.Parent());
        stopped.__TaskContinuation(() => stopped.StopCoroutine(pending));
        Pump(stopped);
        Check(pending.Status == CoroutineStatus.Cancelled && stopped.Trace == "inner outer ", "stop disposes nested iterators deepest first");
        stopped.__Destroy();
        var destroyed = Make(); var dying = Start(destroyed, destroyed.Parent());
        destroyed.__Destroy();
        Check(dying.Status == CoroutineStatus.Cancelled && destroyed.Trace == "inner outer ", "destroy cancels and disposes iterator stack");

        var cleanupFailure = Make();
        var cleanupHandle = Start(cleanupFailure, cleanupFailure.BadCleanup());
        int diagnosticCount = s_diagnostics;
        cleanupFailure.__Destroy();
        Check(cleanupHandle.Status == CoroutineStatus.Faulted && cleanupFailure.Trace == "outer ", "cleanup error must not skip outer finally");
        Equal(diagnosticCount + 1, s_diagnostics, "synchronous teardown failures must be reported");
        var join = Make(); var child = Start(join, join.Child()); var parent = Start(join, join.Join(child));
        Frame(join); Check(child.IsDone && parent.IsDone, "joining coroutine handle");
        join.__Destroy();
        var failed = Make(); var failure = Start(failed, failed.FailingPredicate());
        Throws<AggregateException>(() => Frame(failed), "predicate exception participates in host error handling");
        Check(failure.Status == CoroutineStatus.Faulted && failure.Failure is InvalidOperationException && failed.Trace == "finally ", "fault records and disposes coroutine");
        failed.__Destroy();
        var unexpectedCancel = Make(); var badCancel = Start(unexpectedCancel, unexpectedCancel.UnrequestedCancellation());
        Throws<AggregateException>(() => Frame(unexpectedCancel), "unrequested OCE must be reported as a fault");
        Check(badCancel.Status == CoroutineStatus.Faulted, "unrequested cancellation is not normal completion");
        unexpectedCancel.__Destroy();
        var unsupported = Make(); var invalid = Start(unsupported, unsupported.InvalidYield());
        Throws<AggregateException>(() => unsupported.__CheckTasks(), "unknown yields must report an error");
        Check(invalid.Status == CoroutineStatus.Faulted, "unknown yields cannot silently advance");
        unsupported.__Destroy();

        var bounded = Make(); var boundedHandle = Start(bounded, bounded.ManyChildren());
        Check(!boundedHandle.IsDone && bounded.Children < 200, "empty nesting must respect step budget");
        for (int i = 0; i < 20 && !boundedHandle.IsDone; ++i) Frame(bounded);
        Check(boundedHandle.IsDone && bounded.Children == 200, "nested backlog eventually completes");
        bounded.__Destroy();
        var all = Make();
        var first = Start(all, all.Parent()); var second = Start(all, all.Parent());
        all.__TaskContinuation(all.BeginOrdinaryTask);
        all.__TaskContinuation(all.StopAllCoroutines); Pump(all);
        Check(first.Status == CoroutineStatus.Cancelled && second.Status == CoroutineStatus.Cancelled, "stop all owned coroutines");
        Frame(all); Equal(1, all.Children, "StopAllCoroutines must preserve ordinary tasks");
        all.__Destroy();
        var cyclic = Make(); Coroutine? a = null, b = null;
        a = Start(cyclic, cyclic.Cycle(() => b!)); b = Start(cyclic, cyclic.Cycle(() => a!));
        Throws<AggregateException>(() => Frame(cyclic), "cyclic handle waits must fail rather than deadlock");
        Check(b.Status == CoroutineStatus.Faulted, "cycle diagnostics");
        cyclic.__Destroy(); Check(a.IsDone, "cycle teardown settles remaining waiters");
        var duplicate = Make(); var iterator = duplicate.Child(); Start(duplicate, iterator);
        Throws<InvalidOperationException>(() => Start(duplicate, iterator), "reject concurrent reuse of same iterator");
        duplicate.__Destroy();
        Throws<ArgumentOutOfRangeException>(() => Yield.Seconds(float.NaN), "invalid duration");
        Throws<ArgumentOutOfRangeException>(() => Yield.Frames(0), "invalid frame count");
    }
}
internal sealed class CoroutineProbe : MonoBehaviour
{
    internal string Trace = "";
    internal bool Ready, Blocked = true;
    internal int Children;
    internal IEnumerator Sequence()
    {
        Trace += "start "; yield return null;
        Trace += "frame "; yield return Yield.Frames(2);
        Trace += "frames "; yield return Yield.Seconds(0.05f);
        Trace += "seconds "; yield return Yield.Until(() => Ready);
        Trace += "until "; yield return Yield.While(() => Blocked);
        Trace += "while "; yield return Yield.FixedStep;
        Trace += "fixed "; yield return Child();
        transform.position = Vector3.right;
        Trace += "end ";
    }
    internal IEnumerator Child() { Trace += "child "; yield return null; Trace += "child-end "; }
    internal IEnumerator Join(Coroutine child) { yield return child; Trace += "joined "; }
    internal IEnumerator Parent() { try { yield return Inner(); } finally { Trace += "outer "; } }
    private IEnumerator Inner() { try { yield return Yield.FixedStep; } finally { Trace += "inner "; } }
    internal IEnumerator FailingPredicate()
    {
        try { yield return Yield.Until(() => throw new InvalidOperationException("predicate failure")); }
        finally { Trace += "finally "; }
    }
    internal void BeginOrdinaryTask() => Tasks.Run(async token => { await Tasks.NextFrame(token); Children++; });
    internal IEnumerator Cycle(Func<Coroutine> other) { yield return null; yield return other(); }
    internal IEnumerator UnrequestedCancellation() { yield return null; throw new OperationCanceledException("not requested"); }
    internal IEnumerator InvalidYield() { yield return 123; }
    internal IEnumerator BadCleanup() { try { yield return new ThrowingIterator(); } finally { Trace += "outer "; } }
    private sealed class ThrowingIterator : IEnumerator, IDisposable
    {
        public object? Current => null;
        public bool MoveNext() => true;
        public void Reset() => throw new NotSupportedException();
        public void Dispose() => throw new InvalidOperationException("iterator cleanup failed");
    }
    private IEnumerator Empty() { ++Children; yield break; }
    internal IEnumerator ManyChildren() { for (int i = 0; i < 200; ++i) yield return Empty(); }
}
