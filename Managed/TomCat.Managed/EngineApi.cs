namespace TomCat;

public enum AssetType
{
    None = 0,
    Scene = 1,
    Texture2D = 2,
    Shader = 3,
    Audio = 4,
    Font = 5,
    Mesh = 6,
    Material = 7,
    CSharpScript = 8,
    Other = 9,
    Prefab = 10
}

public sealed class Texture2DAsset;
public sealed class ShaderAsset;
public sealed class AudioAsset;
public sealed class FontAsset;
public sealed class MeshAsset;
public sealed class MaterialAsset;

public readonly struct AssetRef<T> : IEquatable<AssetRef<T>>
{
    public AssetRef(ulong handle) => Handle = handle;
    public ulong Handle { get; }
    private static AssetType ExpectedType => typeof(T) == typeof(Texture2DAsset)
        ? AssetType.Texture2D
        : typeof(T) == typeof(ShaderAsset) ? AssetType.Shader
        : typeof(T) == typeof(AudioAsset) ? AssetType.Audio
        : typeof(T) == typeof(FontAsset) ? AssetType.Font
        : typeof(T) == typeof(MeshAsset) ? AssetType.Mesh
        : typeof(T) == typeof(MaterialAsset) ? AssetType.Material
        : typeof(T) == typeof(SceneAsset) ? AssetType.Scene
        : typeof(T) == typeof(PrefabAsset) ? AssetType.Prefab
        : AssetType.None;
    public bool IsValid => Handle != 0 && ExpectedType != AssetType.None &&
        NativeBridge.AssetIsValid(Handle) && NativeBridge.GetAssetType(Handle) == ExpectedType;
    public AssetType Type => Handle == 0 ? AssetType.None : NativeBridge.GetAssetType(Handle);
    public bool Equals(AssetRef<T> other) => Handle == other.Handle;
    public override bool Equals(object? obj) => obj is AssetRef<T> other && Equals(other);
    public override int GetHashCode() => Handle.GetHashCode();
    public static bool operator ==(AssetRef<T> left, AssetRef<T> right) => left.Equals(right);
    public static bool operator !=(AssetRef<T> left, AssetRef<T> right) => !left.Equals(right);
    public override string ToString() => Handle == 0 ? "None" : $"Asset({Handle})";
}

public readonly struct SceneAsset : IEquatable<SceneAsset>
{
    public SceneAsset(ulong handle) => Handle = handle;
    public ulong Handle { get; }
    public bool IsValid => Handle != 0 && NativeBridge.AssetIsValid(Handle) &&
        NativeBridge.GetAssetType(Handle) == AssetType.Scene;
    public bool Equals(SceneAsset other) => Handle == other.Handle;
    public override bool Equals(object? obj) => obj is SceneAsset other && Equals(other);
    public override int GetHashCode() => Handle.GetHashCode();
    public static bool operator ==(SceneAsset left, SceneAsset right) => left.Equals(right);
    public static bool operator !=(SceneAsset left, SceneAsset right) => !left.Equals(right);
    public override string ToString() => Handle == 0 ? "None" : $"Scene({Handle})";
}

public readonly struct PrefabAsset : IEquatable<PrefabAsset>
{
    public PrefabAsset(ulong handle) => Handle = handle;
    public ulong Handle { get; }
    public bool IsValid => Handle != 0 && NativeBridge.AssetIsValid(Handle) &&
        NativeBridge.GetAssetType(Handle) == AssetType.Prefab;
    public bool Equals(PrefabAsset other) => Handle == other.Handle;
    public override bool Equals(object? obj) => obj is PrefabAsset other && Equals(other);
    public override int GetHashCode() => Handle.GetHashCode();
    public static bool operator ==(PrefabAsset left, PrefabAsset right) => left.Equals(right);
    public static bool operator !=(PrefabAsset left, PrefabAsset right) => !left.Equals(right);
    public override string ToString() => Handle == 0 ? "None" : $"Prefab({Handle})";
}

public enum KeyCode : uint
{
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    Alpha0 = 48, Alpha1 = 49, Alpha2 = 50, Alpha3 = 51, Alpha4 = 52,
    Alpha5 = 53, Alpha6 = 54, Alpha7 = 55, Alpha8 = 56, Alpha9 = 57,
    Semicolon = 59, Equal = 61,
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73,
    J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82,
    S = 83, T = 84, U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
    LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
    Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
    Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267,
    Home = 268, End = 269, CapsLock = 280, ScrollLock = 281, NumLock = 282,
    PrintScreen = 283, Pause = 284,
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295, F7 = 296,
    F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
    Keypad0 = 320, Keypad1 = 321, Keypad2 = 322, Keypad3 = 323, Keypad4 = 324,
    Keypad5 = 325, Keypad6 = 326, Keypad7 = 327, Keypad8 = 328, Keypad9 = 329,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
    RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347, Menu = 348
}

public enum MouseButton : uint
{
	Left = 0,
	Right = 1,
	Middle = 2,
	Button4 = 3,
	Button5 = 4,
	Button6 = 5,
	Button7 = 6,
	Button8 = 7
}

// Values match GLFW's cross-platform standard gamepad mapping, not device-
// specific raw button numbers.
public enum GamepadButton : uint
{
	South = 0,
	East = 1,
	West = 2,
	North = 3,
	LeftBumper = 4,
	RightBumper = 5,
	Back = 6,
	Start = 7,
	Guide = 8,
	LeftStick = 9,
	RightStick = 10,
	DpadUp = 11,
	DpadRight = 12,
	DpadDown = 13,
	DpadLeft = 14
}

public enum GamepadAxis : uint
{
	LeftX = 0,
	LeftY = 1,
	RightX = 2,
	RightY = 3,
	LeftTrigger = 4,
	RightTrigger = 5
}

[Flags]
public enum KeyModifiers : uint
{
    None = 0,
    Shift = 1 << 0,
    Control = 1 << 1,
    Alt = 1 << 2,
    Super = 1 << 3,
    CapsLock = 1 << 4,
    NumLock = 1 << 5
}

public enum InputEventDevice : uint
{
	Keyboard = 1,
	MouseButton = 2,
	GamepadConnection = 3,
	GamepadButton = 4
}

public enum InputEventAction : uint
{
	Pressed = 1,
	Released = 2,
	Repeated = 3
}

/// <summary>An ordered digital input transition from the frozen native queue.</summary>
public readonly record struct InputEvent(ulong Sequence, double TimestampSeconds,
	ulong FrameNumber, InputEventDevice Device, InputEventAction Action,
	uint Code, uint DeviceIndex);

/// <summary>
/// The events visible to the current callback. DroppedEventCount is nonzero only
/// when a display frame or a scene's pending FixedUpdate batch exceeded 16,384
/// transitions; edge bitmaps still preserve whether each control changed.
/// </summary>
public readonly record struct InputEventBatch(ulong FirstFrameNumber,
	ulong LastFrameNumber, ulong FirstSequence, ulong LastSequence,
	ulong DroppedEventCount, IReadOnlyList<InputEvent> Events);

/// <summary>
/// Input is frozen once after native event polling at the start of each display
/// frame. OnUpdate observes that frame's ordered transitions. Each scene carries
/// unconsumed transitions across display frames; its next OnFixedUpdate observes
/// the accumulated ordered batch exactly once. Later catch-up fixed steps expose
/// held state but no transition, mouse delta, or scroll delta. Keyboard, mouse,
/// and gamepad hot-plug edges come from callbacks. GLFW exposes standard gamepad
/// buttons only as a sampled state, so a complete button tap between two frame
/// samples is outside this API's observable boundary.
/// </summary>
public static unsafe class Input
{
	public const uint MaximumGamepads = 16;
	public static InputEventBatch EventBatch => NativeBridge.GetInputEventBatch();

    public static bool IsKeyHeld(KeyCode key) => NativeBridge.InputBoolean(key,
        NativeBridge.InputIsKeyHeld, "Input.IsKeyHeld");
    public static bool WasKeyPressed(KeyCode key) => NativeBridge.InputBoolean(key,
        NativeBridge.InputWasKeyPressed, "Input.WasKeyPressed");
    public static bool WasKeyReleased(KeyCode key) => NativeBridge.InputBoolean(key,
        NativeBridge.InputWasKeyReleased, "Input.WasKeyReleased");
    public static Vector2 MousePosition => NativeBridge.InputVector(NativeBridge.InputGetMousePosition,
        "Input.MousePosition");
    public static Vector2 MouseDelta => NativeBridge.InputVector(NativeBridge.InputGetMouseDelta,
        "Input.MouseDelta");
	public static bool IsMouseButtonHeld(MouseButton button) => NativeBridge.InputMouseBoolean(
		(uint)button, NativeBridge.InputIsMouseButtonHeld, "Input.IsMouseButtonHeld");
	public static bool WasMouseButtonPressed(MouseButton button) => NativeBridge.InputMouseBoolean(
		(uint)button, NativeBridge.InputWasMouseButtonPressed, "Input.WasMouseButtonPressed");
	public static bool WasMouseButtonReleased(MouseButton button) => NativeBridge.InputMouseBoolean(
		(uint)button, NativeBridge.InputWasMouseButtonReleased, "Input.WasMouseButtonReleased");
	public static Vector2 ScrollDelta => NativeBridge.GetScrollDelta();
	public static bool IsWindowFocused => NativeBridge.GetWindowFocused();

	public static bool IsGamepadConnected(uint gamepad = 0) => NativeBridge.InputGamepadBoolean(
		gamepad, NativeBridge.InputIsGamepadConnected, "Input.IsGamepadConnected");
	public static bool WasGamepadConnected(uint gamepad = 0) => NativeBridge.InputGamepadBoolean(
		gamepad, NativeBridge.InputWasGamepadConnected, "Input.WasGamepadConnected");
	public static bool WasGamepadDisconnected(uint gamepad = 0) => NativeBridge.InputGamepadBoolean(
		gamepad, NativeBridge.InputWasGamepadDisconnected, "Input.WasGamepadDisconnected");
	public static string GetGamepadName(uint gamepad = 0) => NativeBridge.GetGamepadName(gamepad);
	public static bool IsGamepadButtonHeld(GamepadButton button, uint gamepad = 0) =>
		NativeBridge.InputGamepadButtonBoolean(gamepad, (uint)button,
			NativeBridge.InputIsGamepadButtonHeld, "Input.IsGamepadButtonHeld");
	public static bool WasGamepadButtonPressed(GamepadButton button, uint gamepad = 0) =>
		NativeBridge.InputGamepadButtonBoolean(gamepad, (uint)button,
			NativeBridge.InputWasGamepadButtonPressed, "Input.WasGamepadButtonPressed");
	public static bool WasGamepadButtonReleased(GamepadButton button, uint gamepad = 0) =>
		NativeBridge.InputGamepadButtonBoolean(gamepad, (uint)button,
			NativeBridge.InputWasGamepadButtonReleased, "Input.WasGamepadButtonReleased");

	/// <summary>
	/// Returns GLFW's standard gamepad value without normalization. Stick axes and
	/// triggers both use the native [-1, 1] range; an idle trigger is therefore -1.
	/// </summary>
	public static float GetGamepadAxisRaw(GamepadAxis axis, uint gamepad = 0) =>
		NativeBridge.GetGamepadAxis(gamepad, (uint)axis);

	public static float GetGamepadAxis(GamepadAxis axis, uint gamepad = 0,
		float deadZone = 0.15f)
	{
		if (!float.IsFinite(deadZone) || deadZone < 0.0f || deadZone >= 1.0f)
			throw new ArgumentOutOfRangeException(nameof(deadZone),
				"Dead zone must be finite and in [0, 1).");
		float raw = GetGamepadAxisRaw(axis, gamepad);
		if (axis is GamepadAxis.LeftTrigger or GamepadAxis.RightTrigger)
		{
			// GLFW exposes standard triggers as -1 (idle) through +1 (fully held).
			// Public processed trigger values are conventional one-sided [0, 1].
			float trigger = Math.Clamp((raw + 1.0f) * 0.5f, 0.0f, 1.0f);
			if (trigger <= deadZone)
				return 0.0f;
			return (trigger - deadZone) / (1.0f - deadZone);
		}
		float magnitude = MathF.Abs(raw);
		if (magnitude <= deadZone)
			return 0.0f;
		return MathF.CopySign((magnitude - deadZone) / (1.0f - deadZone), raw);
	}
    public static KeyModifiers Modifiers => NativeBridge.GetModifiers();
}

/// <summary>
/// Per-game writable directories resolved from PlayerSettings beneath the user's
/// local application-data root. They are null before a Player publishes its
/// validated package configuration.
/// </summary>
public static unsafe class ApplicationPaths
{
	public static string? SaveDirectory => NativeBridge.GetApplicationDirectory(
		NativeBridge.ApplicationGetSaveDirectory, "ApplicationPaths.SaveDirectory");
	public static string? LogDirectory => NativeBridge.GetApplicationDirectory(
		NativeBridge.ApplicationGetLogDirectory, "ApplicationPaths.LogDirectory");
	public static string? CrashDirectory => NativeBridge.GetApplicationDirectory(
		NativeBridge.ApplicationGetCrashDirectory, "ApplicationPaths.CrashDirectory");
}

public static class Physics2D
{
    public static RaycastHit2D? Raycast(Vector2 start, Vector2 end, uint layerMask = 0xFFFF,
		bool includeTriggers = true)
	{
		NativeBridge.EnsureMainThread();
		return NativeBridge.Raycast(ScriptExecutionContext.CurrentEntity,
			start, end, layerMask, includeTriggers);
	}

    public static PhysicsQueryHit2D[] QueryAABB(Vector2 minimum, Vector2 maximum,
		uint layerMask = 0xFFFF, bool includeTriggers = true)
	{
		NativeBridge.EnsureMainThread();
		return NativeBridge.QueryAabb(ScriptExecutionContext.CurrentEntity, minimum, maximum,
			layerMask, includeTriggers);
	}
}

public static class Log
{
    public static void Trace(string message) => NativeBridge.WriteLog(0, message);
    public static void Info(string message) => NativeBridge.WriteLog(1, message);
    public static void Warn(string message) => NativeBridge.WriteLog(2, message);
    public static void Error(string message) => NativeBridge.WriteLog(3, message);
}
