namespace TomCat;

internal readonly record struct ScriptInstanceHandle(ulong Value);

public abstract class TomCatBehaviour
{
    private Entity _entity;
    private ScriptInstanceHandle _instance;
	private CancellationToken _domainCancellation;
    private bool _bound;

    protected TomCatBehaviour() { }

    public Entity Entity
    {
        get
        {
            EnsureBound();
            return _entity;
        }
    }

    public Transform Transform => GetComponent<Transform>();

	public bool Enabled
	{
		get
		{
			EnsureBound();
			return NativeBridge.GetBehaviourEnabled(_instance);
		}
		set
		{
			EnsureBound();
			NativeBridge.SetBehaviourEnabled(_instance, value);
			ScriptExecutionContext.NotifyBehaviourEnabled(_instance, value);
		}
	}

	/// <summary>Removes this attachment after the current managed callback returns.</summary>
	public void RemoveFromEntity()
	{
		EnsureBound();
		NativeBridge.RemoveBehaviour(_instance);
		ScriptExecutionContext.NotifyBehaviourRemoved(_instance);
	}

    protected T GetComponent<T>() where T : struct, IEntityComponent => Entity.GetComponent<T>();
    protected bool TryGetComponent<T>(out T component) where T : struct, IEntityComponent =>
        Entity.TryGetComponent(out component);
    protected bool HasComponent<T>() where T : struct, IEntityComponent => Entity.HasComponent<T>();

    protected virtual void OnCreate() { }
    protected virtual void OnEnable() { }
    protected virtual void OnUpdate(float deltaTime) { }
    protected virtual void OnFixedUpdate(float fixedDeltaTime) { }
    protected virtual void OnCollisionEnter2D(Collision2D collision) { }
    protected virtual void OnCollisionExit2D(Collision2D collision) { }
    protected virtual void OnTriggerEnter2D(Trigger2D trigger) { }
    protected virtual void OnTriggerExit2D(Trigger2D trigger) { }
    protected virtual void OnDisable() { }
    protected virtual void OnDestroy() { }

	internal void __Bind(Entity entity, ScriptInstanceHandle instance,
		CancellationToken domainCancellation = default)
    {
        if (_bound)
            throw new InvalidOperationException("A TomCatBehaviour instance cannot be bound more than once.");
        _entity = entity;
        _instance = instance;
		_domainCancellation = domainCancellation;
        _bound = true;
    }

	internal void __Create() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnCreate(); }
	internal void __Enable() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnEnable(); }
	internal void __Update(float dt) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnUpdate(dt); }
	internal void __FixedUpdate(float dt) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnFixedUpdate(dt); }
	internal void __CollisionEnter(Collision2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnCollisionEnter2D(value); }
	internal void __CollisionExit(Collision2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnCollisionExit2D(value); }
	internal void __TriggerEnter(Trigger2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnTriggerEnter2D(value); }
	internal void __TriggerExit(Trigger2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnTriggerExit2D(value); }
	internal void __Disable() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnDisable(); }
	internal void __Destroy() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); OnDestroy(); }

    private void EnsureBound()
    {
        if (!_bound)
            throw new InvalidOperationException(
                "TomCat API cannot be used from a script constructor. Use OnCreate instead.");
    }
}

internal static class ScriptExecutionContext
{
	[ThreadStatic] private static Entity s_current;
	[ThreadStatic] private static bool s_hasCurrent;
	[ThreadStatic] private static CancellationToken s_domainCancellation;
	[ThreadStatic] private static IScriptMutationSink? s_mutationSink;

	internal static Entity CurrentEntity => s_hasCurrent
		? s_current
		: throw new TomCatException("Physics2D must be called from a TomCatBehaviour lifecycle callback.");
	internal static bool IsActive => s_hasCurrent;
	internal static CancellationToken CurrentDomainCancellationToken => s_hasCurrent
		? s_domainCancellation
		: throw new TomCatException(
			"The domain cancellation token is only available during a script lifecycle callback.");

	internal static Scope Enter(Entity entity, CancellationToken domainCancellation)
    {
		Scope scope = new(s_current, s_hasCurrent, s_domainCancellation);
        s_current = entity;
        s_hasCurrent = true;
		s_domainCancellation = domainCancellation;
        return scope;
	}

	internal static MutationScope EnterMutationSink(IScriptMutationSink sink)
	{
		IScriptMutationSink? previous = s_mutationSink;
		s_mutationSink = sink;
		return new MutationScope(previous);
	}

	internal static void NotifyBehaviourEnabled(ScriptInstanceHandle instance, bool enabled) =>
		s_mutationSink?.SetBehaviourEnabled(instance.Value, enabled);

	internal static void NotifyBehaviourRemoved(ScriptInstanceHandle instance) =>
		s_mutationSink?.RemoveBehaviour(instance.Value);

	internal static void NotifyEntityDestroyed(Entity entity) =>
		s_mutationSink?.DestroyEntity(entity);

	internal readonly struct Scope(Entity previous, bool hadPrevious,
		CancellationToken previousCancellation) : IDisposable
    {
        public void Dispose()
        {
            s_current = previous;
            s_hasCurrent = hadPrevious;
			s_domainCancellation = previousCancellation;
		}
	}

	internal readonly struct MutationScope(IScriptMutationSink? previous) : IDisposable
	{
		public void Dispose() => s_mutationSink = previous;
	}
}

/// <summary>Information scoped to the currently executing Play Domain.</summary>
public static class ScriptRuntime
{
	/// <summary>
	/// Cancelled when TomCat stops and begins unloading the current project domain.
	/// Capture this token in OnCreate before starting cooperative background work.
	/// </summary>
	public static CancellationToken DomainCancellationToken =>
		ScriptExecutionContext.CurrentDomainCancellationToken;
}

internal interface IScriptMutationSink
{
	void SetBehaviourEnabled(ulong attachmentId, bool enabled);
	void RemoveBehaviour(ulong attachmentId);
	void DestroyEntity(Entity entity);
}
