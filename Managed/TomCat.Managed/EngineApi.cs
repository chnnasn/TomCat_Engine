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

public static unsafe class Input
{
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
    public static KeyModifiers Modifiers => NativeBridge.GetModifiers();
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
