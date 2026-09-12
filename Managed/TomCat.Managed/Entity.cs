using System.Runtime.InteropServices;
using TomCat.Interop;

namespace TomCat;

[StructLayout(LayoutKind.Sequential)]
public readonly struct Entity : IEquatable<Entity>
{
    internal Entity(ulong sceneSessionId, ulong id, ulong runtimeGeneration)
    {
        SceneSessionId = sceneSessionId;
        Id = id;
        RuntimeGeneration = runtimeGeneration;
    }

    public ulong SceneSessionId { get; }
    public ulong Id { get; }
    public ulong RuntimeGeneration { get; }
    public bool IsValid => Id != 0 && NativeBridge.EntityIsAlive(this);

    public string Name
    {
        get => NativeBridge.GetEntityName(this);
        set => NativeBridge.SetEntityName(this, value);
    }

    public string Tag
    {
        get => NativeBridge.GetEntityTag(this);
        set => NativeBridge.SetEntityTag(this, value);
    }

    public uint Layer
    {
        get => NativeBridge.GetEntityLayer(this);
        set => NativeBridge.SetEntityLayer(this, value);
    }

	public void Destroy()
	{
		NativeBridge.DestroyEntity(this);
		ScriptExecutionContext.NotifyEntityDestroyed(this);
	}

    public T GetComponent<T>() where T : struct, IEntityComponent
    {
        if (!HasComponent<T>())
            throw new TomCatException($"Entity {Id} does not have component {typeof(T).Name}.");
        return ComponentProxy<T>.Create(this);
    }

    public bool TryGetComponent<T>(out T component) where T : struct, IEntityComponent
    {
        if (HasComponent<T>())
        {
            component = ComponentProxy<T>.Create(this);
            return true;
        }

        component = default;
        return false;
    }

    public bool HasComponent<T>() where T : struct, IEntityComponent =>
        NativeBridge.HasComponent(this, ComponentProxy<T>.Type);

    public T AddComponent<T>() where T : struct, IEntityComponent
    {
        NativeBridge.AddComponent(this, ComponentProxy<T>.Type);
        return ComponentProxy<T>.Create(this);
    }

    public void RemoveComponent<T>() where T : struct, IEntityComponent =>
        NativeBridge.RemoveComponent(this, ComponentProxy<T>.Type);

    public bool Equals(Entity other) => SceneSessionId == other.SceneSessionId && Id == other.Id &&
        RuntimeGeneration == other.RuntimeGeneration;
    public override bool Equals(object? obj) => obj is Entity other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(SceneSessionId, Id, RuntimeGeneration);
    public static bool operator ==(Entity left, Entity right) => left.Equals(right);
    public static bool operator !=(Entity left, Entity right) => !left.Equals(right);
    public override string ToString() => $"Entity({Id}, session={SceneSessionId}, generation={RuntimeGeneration})";
}

public interface IEntityComponent
{
    Entity Entity { get; }
}

internal static class ComponentProxy<T> where T : struct, IEntityComponent
{
    internal static readonly NativeComponentTypeV1 Type = ResolveType();

    internal static T Create(Entity entity)
    {
        object value = Type switch
        {
            NativeComponentTypeV1.Transform => new Transform(entity),
            NativeComponentTypeV1.Rigidbody2D => new Rigidbody2D(entity),
            NativeComponentTypeV1.BoxCollider2D => new BoxCollider2D(entity),
            NativeComponentTypeV1.CircleCollider2D => new CircleCollider2D(entity),
            NativeComponentTypeV1.DistanceJoint2D => new DistanceJoint2D(entity),
            NativeComponentTypeV1.SpriteRenderer => new SpriteRenderer(entity),
            _ => throw new TomCatException($"Unsupported component proxy {typeof(T).FullName}.")
        };
        return (T)value;
    }

    private static NativeComponentTypeV1 ResolveType()
    {
        Type type = typeof(T);
        if (type == typeof(Transform)) return NativeComponentTypeV1.Transform;
        if (type == typeof(Rigidbody2D)) return NativeComponentTypeV1.Rigidbody2D;
        if (type == typeof(BoxCollider2D)) return NativeComponentTypeV1.BoxCollider2D;
        if (type == typeof(CircleCollider2D)) return NativeComponentTypeV1.CircleCollider2D;
        if (type == typeof(DistanceJoint2D)) return NativeComponentTypeV1.DistanceJoint2D;
        if (type == typeof(SpriteRenderer)) return NativeComponentTypeV1.SpriteRenderer;
        throw new TomCatException($"{type.FullName} is not a TomCat component proxy.");
    }
}

public readonly unsafe struct Transform : IEntityComponent
{
    internal Transform(Entity entity) => Entity = entity;
    public Entity Entity { get; }

    public Vector3 Position
    {
        get => NativeBridge.GetTransformVector(Entity, NativeBridge.TransformGetPosition, "Transform.Position");
        set => NativeBridge.SetTransformVector(Entity, value, NativeBridge.TransformSetPosition, "Transform.Position");
    }

    public Vector3 RotationEuler
    {
        get => NativeBridge.GetTransformVector(Entity, NativeBridge.TransformGetRotationEuler, "Transform.RotationEuler");
        set => NativeBridge.SetTransformVector(Entity, value, NativeBridge.TransformSetRotationEuler, "Transform.RotationEuler");
    }

    public Vector3 Scale
    {
        get => NativeBridge.GetTransformVector(Entity, NativeBridge.TransformGetScale, "Transform.Scale");
        set => NativeBridge.SetTransformVector(Entity, value, NativeBridge.TransformSetScale, "Transform.Scale");
    }

    public Matrix4 WorldMatrix => NativeBridge.GetWorldMatrix(Entity);
}

public readonly unsafe struct Rigidbody2D : IEntityComponent
{
    internal Rigidbody2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }

    public Vector2 LinearVelocity
    {
        get => NativeBridge.GetRigidbodyVelocity(Entity);
        set => NativeBridge.SetRigidbodyVector(Entity, value, NativeBridge.RigidbodySetLinearVelocity,
            "Rigidbody2D.LinearVelocity");
    }

    public void ApplyForce(Vector2 force) => NativeBridge.SetRigidbodyVector(Entity, force,
        NativeBridge.RigidbodyApplyForce, "Rigidbody2D.ApplyForce");

    public void ApplyLinearImpulse(Vector2 impulse) => NativeBridge.SetRigidbodyVector(Entity, impulse,
        NativeBridge.RigidbodyApplyLinearImpulse, "Rigidbody2D.ApplyLinearImpulse");
}

public readonly struct BoxCollider2D : IEntityComponent
{
    internal BoxCollider2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
}

public readonly struct CircleCollider2D : IEntityComponent
{
    internal CircleCollider2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
}

public readonly struct DistanceJoint2D : IEntityComponent
{
    internal DistanceJoint2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
}

public readonly struct SpriteRenderer : IEntityComponent
{
    internal SpriteRenderer(Entity entity) => Entity = entity;
    public Entity Entity { get; }
}

public readonly struct Collision2D(Entity self, Entity other)
{
    public Entity Self { get; } = self;
    public Entity Other { get; } = other;
}

public readonly struct Trigger2D(Entity self, Entity other)
{
    public Entity Self { get; } = self;
    public Entity Other { get; } = other;
}

public readonly struct RaycastHit2D(Entity entity, Vector2 point, Vector2 normal, float fraction,
    bool isTrigger, uint collisionLayer)
{
    public Entity Entity { get; } = entity;
    public Vector2 Point { get; } = point;
    public Vector2 Normal { get; } = normal;
    public float Fraction { get; } = fraction;
    public bool IsTrigger { get; } = isTrigger;
    public uint CollisionLayer { get; } = collisionLayer;
}

public readonly struct PhysicsQueryHit2D(Entity entity, bool isTrigger, uint collisionLayer)
{
    public Entity Entity { get; } = entity;
    public bool IsTrigger { get; } = isTrigger;
    public uint CollisionLayer { get; } = collisionLayer;
}
