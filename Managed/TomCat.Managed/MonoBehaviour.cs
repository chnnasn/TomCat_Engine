namespace TomCat;

internal readonly record struct ScriptInstanceHandle(ulong Value);

public abstract class MonoBehaviour
{
    private Entity _entity = null!;
    private ScriptInstanceHandle _instance;
	private CancellationToken _domainCancellation;
    private bool _bound;
    private ScriptTasks? _tasks;
    public ScriptTasks Tasks { get { EnsureBound(); return _tasks!; } }

    public Coroutine StartCoroutine(System.Collections.IEnumerator routine, CancellationToken cancellation = default) => Tasks.StartCoroutine(routine, cancellation);
    public void StopCoroutine(Coroutine coroutine) => Tasks.StopCoroutine(coroutine);
    public void StopAllCoroutines() => Tasks.StopAllCoroutines();

    protected MonoBehaviour() { }

    public Entity Entity
    {
        get
        {
            EnsureBound();
            return _entity;
        }
    }

    public Transform Transform => GetComponent<Transform>();
    public GameObject gameObject => new(Entity);
    public Transform transform => Transform;
    public string name { get => Entity.Name; set => Entity.Name = value; }
    public string tag { get => Entity.Tag; set => Entity.Tag = value; }
    public bool enabled { get => Enabled; set => Enabled = value; }
    public bool isActiveAndEnabled => Enabled && Entity.ActiveInHierarchy;
    public bool CompareTag(string value) => Entity.Tag == value;
    public static void Destroy(GameObject target) => target.Entity.Destroy();
    public static void Destroy(MonoBehaviour target) => target.RemoveFromEntity();

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

    public T GetComponent<T>() where T : class, IEntityComponent => Entity.GetComponent<T>();
    public bool TryGetComponent<T>(out T component) where T : class, IEntityComponent =>
        Entity.TryGetComponent(out component);
    protected bool HasComponent<T>() where T : class, IEntityComponent => Entity.HasComponent<T>();

	/// <summary>
	/// Queues a snapshot Prefab for instantiation after the current lifecycle callback.
	/// </summary>
	protected bool Instantiate(PrefabAsset prefab, Vector3 worldPosition,
		Entity? parent = null)
	{
		EnsureBound();
		return NativeBridge.InstantiatePrefab(_entity, prefab, worldPosition, parent);
	}

    private readonly Dictionary<string, Delegate> _messages = [];

    private void BindMessages()
    {
        foreach (string name in new[] { "Awake", "Start", "OnEnable", "Update", "LateUpdate", "FixedUpdate", "OnDisable", "OnDestroy", "OnCollisionEnter2D", "OnCollisionExit2D", "OnTriggerEnter2D", "OnTriggerExit2D" })
        {
            Type? argument = name.StartsWith("OnCollision", StringComparison.Ordinal) ? typeof(Collision2D)
                : name.StartsWith("OnTrigger", StringComparison.Ordinal) ? typeof(Trigger2D) : null;
            for (Type? type = GetType(); type != null && type != typeof(MonoBehaviour); type = type.BaseType)
            {
                var method = type.GetMethod(name, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public
                    | System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.DeclaredOnly,
                    null, argument is null ? Type.EmptyTypes : [argument], null);
                if (method is null) continue;
                if (method.ReturnType != typeof(void) || method.ContainsGenericParameters || method.IsDefined(typeof(System.Runtime.CompilerServices.AsyncStateMachineAttribute), false))
                    throw new InvalidOperationException($"{type.FullName}.{name} must be a synchronous non-generic void callback; use Tasks.Run for asynchronous work.");
                Type callbackType = argument is null ? typeof(Action) : typeof(Action<>).MakeGenericType(argument);
                _messages[name] = method.CreateDelegate(callbackType, this);
                break;
            }
        }
    }
    private void Message(string name) { if (_messages.TryGetValue(name, out var callback)) ((Action)callback)(); }
    private void Message<T>(string name, T value) { if (_messages.TryGetValue(name, out var callback)) ((Action<T>)callback)(value); }

	internal void __Bind(Entity entity, ScriptInstanceHandle instance,
		CancellationToken domainCancellation = default)
    {
        if (_bound)
            throw new InvalidOperationException("A MonoBehaviour instance cannot be bound more than once.");
        _entity = entity;
        _instance = instance;
		_domainCancellation = domainCancellation;
        BindMessages();
        _tasks = new ScriptTasks(entity, domainCancellation);
        _bound = true;
    }

	internal void __Create() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("Awake"); }
	internal void __Start() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("Start"); }
	internal void __Enable() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnEnable"); }
	internal void __Update(float dt) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("Update"); }
	internal void __LateUpdate(float dt) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("LateUpdate"); }
	internal void __FixedUpdate(float dt) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("FixedUpdate"); }
	internal void __CollisionEnter(Collision2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnCollisionEnter2D", value); }
	internal void __CollisionExit(Collision2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnCollisionExit2D", value); }
	internal void __TriggerEnter(Trigger2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnTriggerEnter2D", value); }
	internal void __TriggerExit(Trigger2D value) { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnTriggerExit2D", value); }
	internal void __Disable() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); Message("OnDisable"); }
	internal void __Destroy() { using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation); _tasks?.Stop(); Message("OnDestroy"); }
    internal int __PumpTasks(bool fixedStep, Action<Action> dispatch, int budget = ScriptTasks.ResumeBudget) => _tasks?.Pump(fixedStep, dispatch, budget) ?? 0;
    internal void __CheckTasks() => _tasks?.CheckFailures();
    internal void __TaskContinuation(Action continuation)
    {
        using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation);
        continuation();
    }
    internal void __CancelTasks()
    {
        using var scope = ScriptExecutionContext.Enter(_entity, _domainCancellation);
        _tasks?.Stop();
    }

    private void EnsureBound()
    {
        if (!_bound)
            throw new InvalidOperationException(
                "TomCat API cannot be used from a script constructor. Use Awake instead.");
    }
}

internal static class ScriptExecutionContext
{
	[ThreadStatic] private static Entity? s_current;
	[ThreadStatic] private static bool s_hasCurrent;
	[ThreadStatic] private static CancellationToken s_domainCancellation;
	[ThreadStatic] private static IScriptMutationSink? s_mutationSink;

	internal static Entity CurrentEntity => s_hasCurrent
		? s_current!
		: throw new TomCatException("Physics2D must be called from a MonoBehaviour lifecycle callback.");
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

	internal readonly struct Scope(Entity? previous, bool hadPrevious,
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
	/// Capture this token in Awake before starting cooperative background work.
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
