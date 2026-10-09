namespace TomCat;

/// <summary>A scene object owned by the current runtime.</summary>
public sealed class GameObject : IEquatable<GameObject>
{
    internal GameObject(Entity entity) => Entity = entity;
    internal Entity Entity { get; }
    public string name { get => Entity.Name; set => Entity.Name = value; }
    public string tag { get => Entity.Tag; set => Entity.Tag = value; }
    public int layer { get => checked((int)Entity.Layer); set => Entity.Layer = checked((uint)value); }
    public bool activeSelf => Entity.ActiveSelf;
    public bool activeInHierarchy => Entity.ActiveInHierarchy;
    public Transform transform => Entity.GetComponent<Transform>();
    public void SetActive(bool value) => Entity.ActiveSelf = value;
    public bool CompareTag(string value) => tag == value;
    public T GetComponent<T>() where T : class, IEntityComponent => Entity.GetComponent<T>();
    public bool TryGetComponent<T>(out T component) where T : class, IEntityComponent => Entity.TryGetComponent(out component);
    public T AddComponent<T>() where T : class, IEntityComponent => Entity.AddComponent<T>();
    public bool Equals(GameObject? other) => other is not null && Entity == other.Entity;
    public override bool Equals(object? other) => other is GameObject value && Equals(value);
    public override int GetHashCode() => Entity.GetHashCode();
    public static bool operator ==(GameObject? left, GameObject? right) => ReferenceEquals(left, right) || (left?.Equals(right) ?? false);
    public static bool operator !=(GameObject? left, GameObject? right) => !(left == right);
}

public static class Debug
{
    public static void Log(object? message) => TomCat.Log.Info(message?.ToString() ?? "null");
    public static void LogWarning(object? message) => TomCat.Log.Warn(message?.ToString() ?? "null");
    public static void LogError(object? message) => TomCat.Log.Error(message?.ToString() ?? "null");
}
