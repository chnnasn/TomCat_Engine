using System.Text.Json;
using System.Text.Json.Serialization;

namespace TomCat;

public enum InputActionType
{
	Button,
	Axis1D
}

public enum InputActionPhase
{
	Waiting,
	Started,
	Performed,
	Canceled
}

public enum InputBindingKind
{
	Key,
	MouseButton,
	MouseWheelX,
	MouseWheelY,
	MouseDeltaX,
	MouseDeltaY,
	GamepadButton,
	GamepadAxis
}

public readonly record struct InputActionContext(
	InputAction Action, InputActionPhase Phase, float Value);

public readonly record struct InputBinding
{
	public InputBindingKind Kind { get; init; }
	public uint Code { get; init; }
	public uint Gamepad { get; init; }
	public float Scale { get; init; }
	public float DeadZone { get; init; }

	public static InputBinding Key(KeyCode key, float scale = 1.0f) =>
		new() { Kind = InputBindingKind.Key, Code = (uint)key, Scale = scale };
	public static InputBinding Mouse(MouseButton button, float scale = 1.0f) =>
		new() { Kind = InputBindingKind.MouseButton, Code = (uint)button, Scale = scale };
	public static InputBinding MouseWheelX(float scale = 1.0f) =>
		new() { Kind = InputBindingKind.MouseWheelX, Scale = scale };
	public static InputBinding MouseWheelY(float scale = 1.0f) =>
		new() { Kind = InputBindingKind.MouseWheelY, Scale = scale };
	public static InputBinding MouseDeltaX(float scale = 1.0f) =>
		new() { Kind = InputBindingKind.MouseDeltaX, Scale = scale };
	public static InputBinding MouseDeltaY(float scale = 1.0f) =>
		new() { Kind = InputBindingKind.MouseDeltaY, Scale = scale };
	public static InputBinding GamepadButton(GamepadButton button, uint gamepad = 0,
		float scale = 1.0f) => new()
		{
			Kind = InputBindingKind.GamepadButton,
			Code = (uint)button,
			Gamepad = gamepad,
			Scale = scale
		};
	public static InputBinding GamepadAxis(GamepadAxis axis, uint gamepad = 0,
		float scale = 1.0f, float deadZone = 0.15f) => new()
		{
			Kind = InputBindingKind.GamepadAxis,
			Code = (uint)axis,
			Gamepad = gamepad,
			Scale = scale,
			DeadZone = deadZone
		};

	internal void Validate()
	{
		if (!Enum.IsDefined(Kind) || !float.IsFinite(Scale))
			throw new InvalidDataException("Input binding kind or scale is invalid.");
		if (!float.IsFinite(DeadZone) || DeadZone < 0.0f || DeadZone >= 1.0f)
			throw new InvalidDataException("Input binding dead zone must be in [0, 1).");
		if (Gamepad >= Input.MaximumGamepads)
			throw new InvalidDataException("Input binding gamepad index is out of range.");
		switch (Kind)
		{
			case InputBindingKind.Key when Code > 511:
			case InputBindingKind.MouseButton when Code > (uint)MouseButton.Button8:
			case InputBindingKind.GamepadButton when Code > (uint)TomCat.GamepadButton.DpadLeft:
			case InputBindingKind.GamepadAxis when Code > (uint)TomCat.GamepadAxis.RightTrigger:
				throw new InvalidDataException("Input binding control code is out of range.");
		}
	}

	internal float ReadValue()
	{
		float value = Kind switch
		{
			InputBindingKind.Key => Input.IsKeyHeld((KeyCode)Code) ? 1.0f : 0.0f,
			InputBindingKind.MouseButton => Input.IsMouseButtonHeld((MouseButton)Code) ? 1.0f : 0.0f,
			InputBindingKind.MouseWheelX => Input.ScrollDelta.X,
			InputBindingKind.MouseWheelY => Input.ScrollDelta.Y,
			InputBindingKind.MouseDeltaX => Input.MouseDelta.X,
			InputBindingKind.MouseDeltaY => Input.MouseDelta.Y,
			InputBindingKind.GamepadButton => Input.IsGamepadButtonHeld(
				(TomCat.GamepadButton)Code, Gamepad) ? 1.0f : 0.0f,
			InputBindingKind.GamepadAxis => Input.GetGamepadAxis(
				(TomCat.GamepadAxis)Code, Gamepad, DeadZone),
			_ => 0.0f
		};
		return value * Scale;
	}

	// A digital control can be pressed and released inside one native poll. Its
	// frozen frame then has a neutral held value but both transition bits set.
	// Keep that pulse available to Button actions instead of reconstructing every
	// edge from the final held state.
	internal bool HasPressReleasePulse() => Kind switch
	{
		InputBindingKind.Key => Input.WasKeyPressed((KeyCode)Code)
			&& Input.WasKeyReleased((KeyCode)Code),
		InputBindingKind.MouseButton => Input.WasMouseButtonPressed((MouseButton)Code)
			&& Input.WasMouseButtonReleased((MouseButton)Code),
		InputBindingKind.GamepadButton => Input.WasGamepadButtonPressed(
			(TomCat.GamepadButton)Code, Gamepad) && Input.WasGamepadButtonReleased(
				(TomCat.GamepadButton)Code, Gamepad),
		_ => false
	};

	internal ControlToken Token => new(Kind, Code, Gamepad);
}

internal readonly record struct ControlToken(InputBindingKind Kind, uint Code, uint Gamepad);

/// <summary>
/// An action is evaluated independently for the display-frame and fixed-step
/// timelines. Properties read from OnFixedUpdate expose the current fixed input
/// snapshot; other callbacks expose the display-frame snapshot. The first fixed
/// step for a scene consumes its pending edges, while catch-up steps retain held
/// values without replaying Pressed/Released. Started, Performed, and Canceled
/// retain their display-frame cadence; fixed-step scripts should poll Value,
/// IsPressed, and WasPressed/ReleasedThisFrame from OnFixedUpdate.
/// </summary>
public sealed class InputAction
{
	private readonly List<InputBinding> _bindings = [];
	private readonly EvaluationState _displayState = new();
	private readonly EvaluationState _fixedState = new();

	private sealed class EvaluationState
	{
		internal float Value;
		internal bool Actuated;
		internal bool WasPressed;
		internal bool WasReleased;
	}

	internal InputAction(string name, InputActionType type)
	{
		Name = name;
		Type = type;
	}

	public string Name { get; }
	public InputActionType Type { get; }
	public float PressPoint { get; set; } = 0.5f;
	public float Value => CurrentState.Value;
	public bool IsPressed => CurrentState.Actuated;
	public bool WasPressedThisFrame => CurrentState.WasPressed;
	public bool WasReleasedThisFrame => CurrentState.WasReleased;
	public IReadOnlyList<InputBinding> Bindings => _bindings;

	public event Action<InputActionContext>? Started;
	public event Action<InputActionContext>? Performed;
	public event Action<InputActionContext>? Canceled;

	public InputAction AddBinding(InputBinding binding)
	{
		binding.Validate();
		_bindings.Add(binding);
		return this;
	}

	public void Rebind(int bindingIndex, InputBinding binding)
	{
		binding.Validate();
		if ((uint)bindingIndex >= (uint)_bindings.Count)
			throw new ArgumentOutOfRangeException(nameof(bindingIndex));
		_bindings[bindingIndex] = binding;
	}

	internal void ReplaceBindings(IReadOnlyList<InputBinding> bindings)
	{
		_bindings.Clear();
		_bindings.AddRange(bindings);
		Clear();
	}

	private EvaluationState CurrentState => TimeRuntime.InFixedUpdate
		? _fixedState : _displayState;

	internal void Evaluate(InputActionUpdatePhase phase,
		HashSet<ControlToken> consumed, HashSet<ControlToken>? controlsToConsume,
		InputActionMap owner, ulong evaluationRevision)
	{
		if (!float.IsFinite(PressPoint) || PressPoint <= 0.0f || PressPoint > 1.0f)
			throw new InvalidOperationException(
				$"Input action '{Name}' PressPoint must be in (0, 1].");

		float value = 0.0f;
		bool digitalPulse = false;
		float digitalPulseValue = 0.0f;
		var activeControls = new List<ControlToken>(_bindings.Count);
		foreach (InputBinding binding in _bindings)
		{
			if (consumed.Contains(binding.Token))
				continue;
			float bindingValue = binding.ReadValue();
			bool bindingPulsed = Type == InputActionType.Button
				&& MathF.Abs(binding.Scale) >= PressPoint
				&& binding.HasPressReleasePulse();
			if (bindingPulsed)
			{
				digitalPulse = true;
				if (MathF.Abs(binding.Scale) > MathF.Abs(digitalPulseValue))
					digitalPulseValue = binding.Scale;
			}
			if (Type == InputActionType.Button)
			{
				// Alternative button bindings are alternatives, not contributors to
				// one composite value. Preserve the sign of the strongest binding.
				if (MathF.Abs(bindingValue) > MathF.Abs(value))
					value = bindingValue;
			}
			else
			{
				value += bindingValue;
			}
			if (MathF.Abs(bindingValue) > float.Epsilon || bindingPulsed)
				activeControls.Add(binding.Token);
		}
		EvaluationState state = phase == InputActionUpdatePhase.FixedStep
			? _fixedState : _displayState;
		state.Value = Math.Clamp(value, -1.0f, 1.0f);
		bool actuated = MathF.Abs(state.Value) >= PressPoint;
		state.WasPressed = (actuated && !state.Actuated) || digitalPulse;
		state.WasReleased = (!actuated && state.Actuated) || digitalPulse;
		// Publish the state before invoking user code. Disable clears both timelines;
		// the owner revision checks below then prevent this evaluation from restoring
		// stale state or dispatching later actions.
		state.Actuated = actuated;

		if (controlsToConsume is not null && (actuated || digitalPulse))
			foreach (ControlToken control in activeControls)
				controlsToConsume.Add(control);

		// Fixed evaluation is a polling snapshot for OnFixedUpdate and later physics
		// callbacks in the same native step. Public action events retain their
		// display-frame cadence.
		if (phase != InputActionUpdatePhase.DisplayFrame)
			return;

		if (state.WasPressed)
		{
			float eventValue = actuated ? state.Value
				: Math.Clamp(digitalPulseValue, -1.0f, 1.0f);
			DispatchSubscribers(Started,
				new(this, InputActionPhase.Started, eventValue));
			if (!owner.IsEvaluationCurrent(evaluationRevision))
				return;
			DispatchSubscribers(Performed,
				new(this, InputActionPhase.Performed, eventValue));
			if (!owner.IsEvaluationCurrent(evaluationRevision))
				return;
		}
		else if (actuated && Type == InputActionType.Axis1D)
		{
			DispatchSubscribers(Performed,
				new(this, InputActionPhase.Performed, state.Value));
			if (!owner.IsEvaluationCurrent(evaluationRevision))
				return;
		}
		if (state.WasReleased)
		{
			DispatchSubscribers(Canceled,
				new(this, InputActionPhase.Canceled, state.Value));
			if (!owner.IsEvaluationCurrent(evaluationRevision))
				return;
		}
	}

	internal bool ClearForDisable()
	{
		bool wasActuated = _displayState.Actuated;
		Clear();
		return wasActuated;
	}

	internal void Clear()
	{
		ClearState(_displayState);
		ClearState(_fixedState);
	}

	internal void DispatchDeferredCanceled() =>
		DispatchSubscribers(Canceled,
			new(this, InputActionPhase.Canceled, 0.0f));

	private static void DispatchSubscribers(
		Action<InputActionContext>? subscribers, InputActionContext context)
	{
		if (subscribers is null)
			return;
		// Snapshot the multicast list and preserve subscription order. User failures
		// are isolated to their handler transaction so later subscribers still run;
		// host protocol failures remain fatal and must escape immediately.
		List<Exception>? failures = null;
		foreach (Delegate subscriber in subscribers.GetInvocationList())
		{
			var handler = (Action<InputActionContext>)subscriber;
			try
			{
				InputActionRuntime.DispatchCallback(() => handler(context));
			}
			catch (DeferredCallbackProtocolException)
			{
				throw;
			}
			catch (Exception exception)
			{
				(failures ??= []).Add(exception);
			}
		}
		if (failures is null)
			return;
		if (failures.Count == 1)
			System.Runtime.ExceptionServices.ExceptionDispatchInfo
				.Capture(failures[0]).Throw();
		throw new AggregateException(
			"One or more InputAction subscribers failed.", failures);
	}

	internal bool ResetPhase(InputActionUpdatePhase phase)
	{
		EvaluationState state = phase == InputActionUpdatePhase.FixedStep
			? _fixedState : _displayState;
		bool wasActuated = state.Actuated;
		ResetState(state);
		return phase == InputActionUpdatePhase.DisplayFrame && wasActuated;
	}

	private static void ResetState(EvaluationState state)
	{
		bool wasActuated = state.Actuated;
		state.Actuated = false;
		state.Value = 0.0f;
		state.WasPressed = false;
		state.WasReleased = wasActuated;
	}

	private static void ClearState(EvaluationState state)
	{
		state.Actuated = false;
		state.Value = 0.0f;
		state.WasPressed = false;
		state.WasReleased = false;
	}
}

internal enum InputActionUpdatePhase
{
	DisplayFrame,
	FixedStep
}

/// <summary>
/// Controls named input contexts for the current Play Domain. Contexts are enabled
/// by default; an exclusive context temporarily suppresses every other context.
/// </summary>
public static class InputContext
{
	public const string Gameplay = "Gameplay";
	public const string UI = "UI";

	public static bool IsEnabled(string context)
	{
		NativeBridge.EnsureMainThread();
		ValidateName(context);
		return InputActionRuntime.IsContextEnabled(
			ScriptExecutionContext.CurrentDomainCancellationToken, context);
	}

	public static void Enable(string context) => SetEnabled(context, true);
	public static void Disable(string context) => SetEnabled(context, false);

	public static void SetEnabled(string context, bool enabled)
	{
		NativeBridge.EnsureMainThread();
		ValidateName(context);
		InputActionRuntime.SetContextEnabled(
			ScriptExecutionContext.CurrentDomainCancellationToken, context, enabled);
	}

	public static void ActivateExclusive(string context)
	{
		NativeBridge.EnsureMainThread();
		ValidateName(context);
		InputActionRuntime.ActivateExclusiveContext(
			ScriptExecutionContext.CurrentDomainCancellationToken, context);
	}

	public static void ClearExclusive()
	{
		NativeBridge.EnsureMainThread();
		InputActionRuntime.ClearExclusiveContext(
			ScriptExecutionContext.CurrentDomainCancellationToken);
	}

	/// <summary>Restores the current domain to the default where every context is enabled.</summary>
	public static void Reset()
	{
		NativeBridge.EnsureMainThread();
		InputActionRuntime.ResetContexts(
			ScriptExecutionContext.CurrentDomainCancellationToken);
	}

	private static void ValidateName(string context) =>
		ArgumentException.ThrowIfNullOrWhiteSpace(context);
}

public sealed class InputActionMap
{
	private static readonly JsonSerializerOptions s_jsonOptions = new()
	{
		WriteIndented = true,
		PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
		UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
		Converters = { new JsonStringEnumConverter() }
	};

	private readonly Dictionary<string, InputAction> _actions =
		new(StringComparer.Ordinal);
	private CancellationToken _registeredDomain;
	private bool _hasRegisteredDomain;
	private ulong _evaluationRevision;

	public InputActionMap(string name, string context = "Gameplay", int priority = 0)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		ArgumentException.ThrowIfNullOrWhiteSpace(context);
		Name = name;
		Context = context;
		Priority = priority;
	}

	public string Name { get; }
	public string Context { get; }
	public int Priority { get; set; }
	public bool ConsumesInput { get; set; } = true;
	public bool Active
	{
		get => _active;
		set
		{
			if (_active == value)
				return;
			_active = value;
			unchecked { _evaluationRevision++; }
		}
	}
	public bool Enabled { get; private set; }
	private bool _active = true;
	public IReadOnlyCollection<InputAction> Actions => _actions.Values;

	public InputAction AddAction(string name, InputActionType type = InputActionType.Button)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		if (!Enum.IsDefined(type))
			throw new ArgumentOutOfRangeException(nameof(type));
		var action = new InputAction(name, type);
		if (!_actions.TryAdd(name, action))
			throw new InvalidOperationException($"Input action '{name}' already exists in '{Name}'.");
		return action;
	}

	public InputAction GetAction(string name) => _actions.TryGetValue(name, out InputAction? action)
		? action
		: throw new KeyNotFoundException($"Input action '{name}' does not exist in '{Name}'.");

	public bool TryGetAction(string name, out InputAction? action) =>
		_actions.TryGetValue(name, out action);

	public void Enable()
	{
		NativeBridge.EnsureMainThread();
		CancellationToken token = ScriptExecutionContext.CurrentDomainCancellationToken;
		InputActionRuntime.Register(this, token);
		_registeredDomain = token;
		_hasRegisteredDomain = true;
		Enabled = true;
		unchecked { _evaluationRevision++; }
	}

	public void Disable()
	{
		NativeBridge.EnsureMainThread();
		InputActionRuntime.Unregister(this);
		Enabled = false;
		unchecked { _evaluationRevision++; }
		bool deferCancellation = TimeRuntime.InFixedUpdate;
		CancellationToken registeredDomain = _registeredDomain;
		bool hadRegisteredDomain = _hasRegisteredDomain;
		// Clear registration and every action state before invoking user code. A
		// throwing first Canceled handler therefore cannot leave later actions held,
		// and a reentrant Enable establishes a new registration that survives.
		_hasRegisteredDomain = false;
		var cancellations = new List<InputAction>();
		foreach (InputAction action in _actions.Values)
			if (action.ClearForDisable())
				cancellations.Add(action);

		foreach (InputAction action in cancellations)
		{
			if (deferCancellation)
			{
				if (hadRegisteredDomain)
					InputActionRuntime.DeferDisplayCancellation(
						registeredDomain, this, action);
			}
			else
			{
				DispatchCanceledSafely(action, "Disable");
			}
		}
	}

	public void SaveRebinds(string path)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(path);
		string fullPath = Path.GetFullPath(path);
		string? directory = Path.GetDirectoryName(fullPath);
		if (!string.IsNullOrEmpty(directory))
			Directory.CreateDirectory(directory);
		string temporary = fullPath + ".tmp-" + Guid.NewGuid().ToString("N");
		try
		{
			File.WriteAllText(temporary, ExportRebinds());
			File.Move(temporary, fullPath, true);
		}
		finally
		{
			if (File.Exists(temporary))
				File.Delete(temporary);
		}
	}

	public void LoadRebinds(string path) => ImportRebinds(File.ReadAllText(path));

	public string ExportRebinds()
	{
		var payload = new RebindPayload
		{
			Schema = 1,
			Map = Name,
			Actions = _actions.Values.Select(static action => new RebindAction
			{
				Name = action.Name,
				Bindings = action.Bindings.ToList()
			}).ToList()
		};
		return JsonSerializer.Serialize(payload, s_jsonOptions);
	}

	public void ImportRebinds(string json)
	{
		ArgumentNullException.ThrowIfNull(json);
		RebindPayload payload = JsonSerializer.Deserialize<RebindPayload>(json, s_jsonOptions)
			?? throw new InvalidDataException("Input rebind JSON is empty.");
		if (payload.Schema != 1 || payload.Map != Name || payload.Actions is null
			|| payload.Actions.Count > 1024)
			throw new InvalidDataException("Input rebind JSON has an incompatible schema or map name.");

		var validated = new List<(InputAction Action, List<InputBinding> Bindings)>();
		var seen = new HashSet<string>(StringComparer.Ordinal);
		foreach (RebindAction item in payload.Actions)
		{
			if (string.IsNullOrWhiteSpace(item.Name) || !seen.Add(item.Name)
				|| !_actions.TryGetValue(item.Name, out InputAction? action)
				|| item.Bindings is null || item.Bindings.Count > 256)
				throw new InvalidDataException("Input rebind JSON contains an unknown or duplicate action.");
			foreach (InputBinding binding in item.Bindings)
				binding.Validate();
			validated.Add((action, item.Bindings));
		}

		foreach ((InputAction action, List<InputBinding> bindings) in validated)
			action.ReplaceBindings(bindings);
	}

	internal void Update(InputActionUpdatePhase phase,
		HashSet<ControlToken> consumed, bool contextEnabled)
	{
		if (!Enabled)
			return;
		ulong evaluationRevision = _evaluationRevision;
		// The native UI event system owns navigation/submit controls while it is
		// active. Cancel Gameplay actions before evaluation so a click or gamepad
		// submit cannot also fire gameplay; maps in the UI context still evaluate.
		bool capturedByRuntimeUI = contextEnabled
			&& string.Equals(Context, InputContext.Gameplay, StringComparison.Ordinal)
			&& NativeBridge.IsRuntimeUIInputCaptured();
		if (!Active || !contextEnabled || capturedByRuntimeUI)
		{
			// Clear every action before invoking user code. A Canceled handler can
			// reactivate or re-enable the map, which changes the evaluation revision;
			// clearing action-by-action would otherwise leave later actions actuated.
			List<InputAction>? cancellations =
				phase == InputActionUpdatePhase.DisplayFrame ? [] : null;
			foreach (InputAction action in _actions.Values)
				if (action.ResetPhase(phase) && cancellations is not null)
					cancellations.Add(action);
			if (cancellations is not null)
				foreach (InputAction action in cancellations)
					DispatchCanceledSafely(action, "state reset");
			return;
		}
		HashSet<ControlToken>? controlsToConsume = ConsumesInput ? [] : null;
		try
		{
			foreach (InputAction action in _actions.Values)
			{
				if (!IsEvaluationCurrent(evaluationRevision))
					break;
				action.Evaluate(phase, consumed, controlsToConsume, this,
					evaluationRevision);
			}
		}
		finally
		{
			// A handler may disable the map or throw after an action fired. Preserve
			// that action's input consumption for lower-priority maps this frame.
			if (controlsToConsume is not null)
				consumed.UnionWith(controlsToConsume);
		}
	}

	internal bool IsEvaluationCurrent(ulong revision) =>
		Enabled && Active && _evaluationRevision == revision;

	internal void DispatchCanceledSafely(InputAction action, string reason)
	{
		try
		{
			action.DispatchDeferredCanceled();
		}
		catch (DeferredCallbackProtocolException)
		{
			throw;
		}
		catch (Exception exception)
		{
			ForceDisable();
			string message =
				$"InputActionMap '{Name}' was disabled after a {reason} " +
				$"Canceled handler exception: {exception}";
			NativeBridge.ReportManagedException(message);
		}
	}

	internal void ForceDisable()
	{
		Enabled = false;
		unchecked { _evaluationRevision++; }
		foreach (InputAction action in _actions.Values)
			action.Clear();
	}

	private sealed class RebindPayload
	{
		public int Schema { get; set; }
		public string Map { get; set; } = string.Empty;
		public List<RebindAction>? Actions { get; set; }
	}

	private sealed class RebindAction
	{
		public string Name { get; set; } = string.Empty;
		public List<InputBinding>? Bindings { get; set; }
	}
}

internal static class InputActionRuntime
{
	private sealed record Registration(WeakReference<InputActionMap> Map,
		CancellationToken Domain, long Order);
	private sealed record DeferredCancellation(InputActionMap Map,
		InputAction Action);
	private sealed class DeferredCancellationQueue
	{
		internal readonly List<DeferredCancellation> Items = [];
		internal readonly HashSet<InputAction> Membership = [];
	}
	private sealed class ContextState
	{
		internal readonly HashSet<string> Disabled = new(StringComparer.Ordinal);
		internal string? Exclusive;
	}

	private static readonly object s_gate = new();
	private static readonly List<Registration> s_registrations = [];
	private static readonly Dictionary<CancellationToken, ContextState> s_contexts = [];
	private static readonly Dictionary<CancellationToken, DeferredCancellationQueue>
		s_deferredDisplayCancellations = [];
	private static long s_nextOrder;
	[ThreadStatic]
	private static Action<Action>? s_callbackDispatcher;

	internal static void Register(InputActionMap map, CancellationToken domain)
	{
		lock (s_gate)
		{
			s_registrations.RemoveAll(item =>
				!item.Map.TryGetTarget(out InputActionMap? target) || ReferenceEquals(target, map));
			s_registrations.Add(new(new(map), domain, s_nextOrder++));
		}
	}

	internal static void Unregister(InputActionMap map)
	{
		lock (s_gate)
			s_registrations.RemoveAll(item =>
				!item.Map.TryGetTarget(out InputActionMap? target) || ReferenceEquals(target, map));
	}

	internal static void DeferDisplayCancellation(CancellationToken domain,
		InputActionMap map, InputAction action)
	{
		lock (s_gate)
		{
			if (!s_deferredDisplayCancellations.TryGetValue(domain,
				out DeferredCancellationQueue? queue))
			{
				queue = new();
				s_deferredDisplayCancellations.Add(domain, queue);
			}
			if (queue.Membership.Add(action))
				queue.Items.Add(new(map, action));
		}
	}

	internal static bool IsContextEnabled(CancellationToken domain, string context)
	{
		lock (s_gate)
		{
			return !s_contexts.TryGetValue(domain, out ContextState? state)
				|| (!state.Disabled.Contains(context)
					&& (state.Exclusive is null
						|| string.Equals(state.Exclusive, context, StringComparison.Ordinal)));
		}
	}

	internal static void SetContextEnabled(CancellationToken domain, string context,
		bool enabled)
	{
		lock (s_gate)
		{
			ContextState state = GetOrCreateContextState(domain);
			if (enabled)
				state.Disabled.Remove(context);
			else
				state.Disabled.Add(context);
			RemoveDefaultContextState(domain, state);
		}
	}

	internal static void ActivateExclusiveContext(CancellationToken domain, string context)
	{
		lock (s_gate)
		{
			ContextState state = GetOrCreateContextState(domain);
			state.Disabled.Remove(context);
			state.Exclusive = context;
		}
	}

	internal static void ClearExclusiveContext(CancellationToken domain)
	{
		lock (s_gate)
		{
			if (!s_contexts.TryGetValue(domain, out ContextState? state))
				return;
			state.Exclusive = null;
			RemoveDefaultContextState(domain, state);
		}
	}

	internal static void ResetContexts(CancellationToken domain)
	{
		lock (s_gate)
			s_contexts.Remove(domain);
	}

	internal static void UpdateEnabled(CancellationToken domain,
		InputActionUpdatePhase phase, Action<Action>? invokeCallback = null)
	{
		List<(InputActionMap Map, long Order)> maps;
		List<DeferredCancellation> deferredCancellations = [];
		using CallbackDispatcherScope callbackScope =
			EnterCallbackDispatcher(invokeCallback);
		lock (s_gate)
		{
			s_registrations.RemoveAll(item => item.Domain.IsCancellationRequested
				|| !item.Map.TryGetTarget(out _));
			foreach (CancellationToken canceled in s_contexts.Keys
				.Where(static token => token.IsCancellationRequested).ToArray())
				s_contexts.Remove(canceled);
			maps = s_registrations
				.Where(item => item.Domain == domain && item.Map.TryGetTarget(out _))
				.Select(item =>
				{
					item.Map.TryGetTarget(out InputActionMap? map);
					return (map!, item.Order);
				})
				.OrderByDescending(item => item.Item1.Priority)
				.ThenBy(item => item.Order)
				.ToList();
			if (phase == InputActionUpdatePhase.DisplayFrame
				&& s_deferredDisplayCancellations.Remove(domain,
					out DeferredCancellationQueue? cancellations))
				deferredCancellations = cancellations.Items;
		}

		foreach (DeferredCancellation cancellation in deferredCancellations)
			cancellation.Map.DispatchCanceledSafely(
				cancellation.Action, "deferred");
		var consumed = new HashSet<ControlToken>();
		foreach ((InputActionMap map, _) in maps)
		{
			try
			{
				map.Update(phase, consumed,
					IsContextEnabled(domain, map.Context));
			}
			catch (DeferredCallbackProtocolException)
			{
				throw;
			}
			catch (Exception exception)
			{
				map.ForceDisable();
				NativeBridge.ReportManagedException(
					$"InputActionMap '{map.Name}' was disabled after an exception: {exception}");
			}
		}
	}

	internal static void DisableDomain(CancellationToken domain)
	{
		lock (s_gate)
		{
			foreach (Registration item in s_registrations.Where(item => item.Domain == domain))
				if (item.Map.TryGetTarget(out InputActionMap? map))
					map.ForceDisable();
			s_registrations.RemoveAll(item => item.Domain == domain
				|| !item.Map.TryGetTarget(out _));
			s_contexts.Remove(domain);
			s_deferredDisplayCancellations.Remove(domain);
		}
	}

	internal static void DispatchCallback(Action callback)
	{
		ArgumentNullException.ThrowIfNull(callback);
		Action<Action>? dispatcher = s_callbackDispatcher;
		if (dispatcher is null)
			callback();
		else
			dispatcher(callback);
	}

	internal static CallbackDispatcherScope EnterCallbackDispatcher(
		Action<Action>? dispatcher)
	{
		if (dispatcher is null)
			return default;
		Action<Action>? previous = s_callbackDispatcher;
		s_callbackDispatcher = dispatcher;
		return new(previous, restore: true);
	}

	internal readonly struct CallbackDispatcherScope : IDisposable
	{
		private readonly Action<Action>? _previous;
		private readonly bool _restore;

		internal CallbackDispatcherScope(Action<Action>? previous, bool restore)
		{
			_previous = previous;
			_restore = restore;
		}

		public void Dispose()
		{
			if (_restore)
				s_callbackDispatcher = _previous;
		}
	}

	private static ContextState GetOrCreateContextState(CancellationToken domain)
	{
		if (!s_contexts.TryGetValue(domain, out ContextState? state))
		{
			state = new ContextState();
			s_contexts.Add(domain, state);
		}
		return state;
	}

	private static void RemoveDefaultContextState(CancellationToken domain,
		ContextState state)
	{
		if (state.Exclusive is null && state.Disabled.Count == 0)
			s_contexts.Remove(domain);
	}
}
