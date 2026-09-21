using TomCat.Interop;

namespace TomCat;

/// <summary>
/// A stable proxy for an entity owned by the current scene runtime.
/// The native ABI continues to use <see cref="NativeEntityHandleV1"/>; this
/// reference type only makes chained property setters behave naturally in C#.
/// </summary>
public sealed class Entity : IEquatable<Entity>
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

	/// <summary>The direct parent. Assignment is committed after the current callback.</summary>
	public Entity? Parent
	{
		get => NativeBridge.GetParent(this);
		set => NativeBridge.SetParent(this, value);
	}

	public IReadOnlyList<Entity> Children => NativeBridge.GetChildren(this);

	public bool ActiveSelf
	{
		get => NativeBridge.GetActiveSelf(this);
		set => NativeBridge.SetActiveSelf(this, value);
	}

	/// <summary>
	/// Whether this Entity and every projected parent are active. Deferred
	/// ActiveSelf and Parent writes are visible immediately inside the current
	/// callback batch; lifecycle dispatch still converges from native committed
	/// state after the batch commits.
	/// </summary>
	public bool ActiveInHierarchy
	{
		get
		{
			var visited = new HashSet<Entity>();
			Entity? current = this;
			while (current is not null)
			{
				if (!visited.Add(current) || !current.ActiveSelf)
					return false;
				current = current.Parent;
			}
			return true;
		}
	}

	public void Destroy()
	{
		NativeBridge.DestroyEntity(this);
		ScriptExecutionContext.NotifyEntityDestroyed(this);
	}

    public T GetComponent<T>() where T : class, IEntityComponent
    {
        if (!HasComponent<T>())
            throw new TomCatException($"Entity {Id} does not have component {typeof(T).Name}.");
        return ComponentProxy<T>.Create(this);
    }

    public bool TryGetComponent<T>(out T component) where T : class, IEntityComponent
    {
        if (HasComponent<T>())
        {
            component = ComponentProxy<T>.Create(this);
            return true;
        }

        component = null!;
        return false;
    }

    public bool HasComponent<T>() where T : class, IEntityComponent
	{
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
			return NativeBridge.HasRegisteredComponent(this, typeId);
		throw new TomCatException($"{typeof(T).FullName} is not a registered component proxy.");
	}

    public T AddComponent<T>() where T : class, IEntityComponent
    {
		if (!ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
		{
			NativeBridge.AbortDeferredCommandBatch(this,
				$"{typeof(T).FullName} is not a registered component proxy");
			throw new TomCatException(
				$"{typeof(T).FullName} is not a registered component proxy.");
		}
		NativeBridge.AddRegisteredComponent(this, typeId);
		try
		{
			return ComponentProxy<T>.Create(this);
		}
		catch
		{
			NativeBridge.AbortDeferredCommandBatch(this,
				$"{typeof(T).FullName} could not create its component proxy");
			throw;
		}
    }

    public void RemoveComponent<T>() where T : class, IEntityComponent
	{
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
		{
			NativeBridge.RemoveRegisteredComponent(this, typeId);
			return;
		}
		NativeBridge.AbortDeferredCommandBatch(this,
			$"{typeof(T).FullName} is not a registered component proxy");
		throw new TomCatException(
			$"{typeof(T).FullName} is not a registered component proxy.");
	}

    public bool Equals(Entity? other) => other is not null &&
        SceneSessionId == other.SceneSessionId && Id == other.Id &&
        RuntimeGeneration == other.RuntimeGeneration;
    public override bool Equals(object? obj) => obj is Entity other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(SceneSessionId, Id, RuntimeGeneration);
    public static bool operator ==(Entity? left, Entity? right) =>
        ReferenceEquals(left, right) || (left is not null && left.Equals(right));
    public static bool operator !=(Entity? left, Entity? right) => !(left == right);
    public override string ToString() => $"Entity({Id}, session={SceneSessionId}, generation={RuntimeGeneration})";
}

public interface IEntityComponent
{
    Entity Entity { get; }
}

internal static class ComponentProxy<T> where T : class, IEntityComponent
{
	internal static T Create(Entity entity)
    {
		if (GeneratedRegisteredComponentProxies.TryCreate(entity,
			out T registeredComponent))
			return registeredComponent;
		// Do not cache reflection results: T may belong to the collectible Play
		// AssemblyLoadContext and a default-context static reference would pin it.
		if (GetExtensionAttribute() is null)
			throw new TomCatException($"Unsupported component proxy {typeof(T).FullName}.");
		var constructor = typeof(T).GetConstructor(
			System.Reflection.BindingFlags.Instance |
			System.Reflection.BindingFlags.Public |
			System.Reflection.BindingFlags.NonPublic,
			binder: null, types: new[] { typeof(Entity) }, modifiers: null);
		if (constructor is null)
			throw new TomCatException($"Registered component proxy {typeof(T).FullName} must declare a constructor accepting Entity.");
		return (T)constructor.Invoke(new object[] { entity });
    }

	internal static bool TryGetRegisteredTypeId(out ulong typeId)
	{
		return GeneratedRegisteredComponentProxies.TryGetTypeId<T>(out typeId)
			|| TryGetExtensionTypeId(out typeId);
	}

	internal static ulong GetRegisteredTypeId(string operation)
	{
		if (TryGetRegisteredTypeId(out ulong typeId))
			return typeId;
		throw new TomCatException($"{operation} is not backed by a registered component type.");
	}

	private static RegisteredComponentAttribute? GetExtensionAttribute() =>
		typeof(T).GetCustomAttributes(typeof(RegisteredComponentAttribute), inherit: false)
			.OfType<RegisteredComponentAttribute>().SingleOrDefault();

	private static bool TryGetExtensionTypeId(out ulong typeId)
	{
		RegisteredComponentAttribute? attribute = GetExtensionAttribute();
		typeId = attribute?.ComponentTypeId ?? 0;
		return typeId != 0;
	}
}

public sealed unsafe class Transform : IEntityComponent
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

	public Vector3 LocalPosition
	{
		get => NativeBridge.GetTransformVector(Entity, NativeBridge.TransformGetLocalPosition,
			"Transform.LocalPosition");
		set => NativeBridge.SetTransformVector(Entity, value, NativeBridge.TransformSetLocalPosition,
			"Transform.LocalPosition");
	}

	public Vector3 LocalRotationEuler
	{
		get => NativeBridge.GetTransformVector(Entity,
			NativeBridge.TransformGetLocalRotationEuler, "Transform.LocalRotationEuler");
		set => NativeBridge.SetTransformVector(Entity, value,
			NativeBridge.TransformSetLocalRotationEuler, "Transform.LocalRotationEuler");
	}

	public Vector3 LocalScale
	{
		get => NativeBridge.GetTransformVector(Entity, NativeBridge.TransformGetLocalScale,
			"Transform.LocalScale");
		set => NativeBridge.SetTransformVector(Entity, value, NativeBridge.TransformSetLocalScale,
			"Transform.LocalScale");
	}
}

public sealed unsafe partial class Rigidbody2D
{
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

public enum RigidbodyBodyType
{
	Static = 0,
	Dynamic = 1,
	Kinematic = 2
}

public sealed partial class DistanceJoint2D
{
	public Entity? ConnectedEntity
	{
		get
		{
			ulong id = NativeBridge.GetRegisteredUInt64(Entity, RegisteredTypeId, 601, "DistanceJoint2D.ConnectedEntity");
			return id == 0 ? null : new Entity(Entity.SceneSessionId, id, Entity.RuntimeGeneration);
		}
		set
		{
			if (value is not null && (value.SceneSessionId != Entity.SceneSessionId
				|| value.RuntimeGeneration != Entity.RuntimeGeneration))
			{
				const string reason =
					"DistanceJoint2D.ConnectedEntity must belong to the same scene runtime";
				NativeBridge.AbortDeferredCommandBatch(Entity, reason);
				throw new ArgumentException(
					"ConnectedEntity must belong to the same scene runtime.", nameof(value));
			}
			NativeBridge.SetRegisteredUInt64(Entity, RegisteredTypeId, 601, value?.Id ?? 0, "DistanceJoint2D.ConnectedEntity");
		}
	}
}

/// <summary>Controls the deterministic sprite animation attached to an entity.</summary>
public sealed partial class SpriteAnimator
{
	public bool IsPlaying => NativeBridge.GetGameplayBool(Entity,
		NativeComponentTypeV1.SpriteAnimator, 702, "SpriteAnimator.IsPlaying");

	public uint CurrentFrame => NativeBridge.GetGameplayUInt32(Entity,
		NativeComponentTypeV1.SpriteAnimator, 703, "SpriteAnimator.CurrentFrame");

	public string CurrentState => NativeBridge.SpriteAnimatorGetCurrentState(Entity);

	/// <summary>
	/// Starts a named clip. Returns false when the clip is missing or not playable.
	/// </summary>
	public bool Play(string clip, bool restart = true) =>
		NativeBridge.SpriteAnimatorPlay(Entity, clip, restart);

	/// <summary>Stops playback while preserving the currently displayed frame.</summary>
	public void Stop() => NativeBridge.SpriteAnimatorStop(Entity);

	public void SetBool(string parameter, bool value) =>
		NativeBridge.SpriteAnimatorSetBool(Entity, parameter, value);

	public void SetInt(string parameter, int value) =>
		NativeBridge.SpriteAnimatorSetInt(Entity, parameter, value);

	public void SetFloat(string parameter, float value) =>
		NativeBridge.SpriteAnimatorSetFloat(Entity, parameter, value);

	public void SetTrigger(string parameter) =>
		NativeBridge.SpriteAnimatorSetTrigger(Entity, parameter, reset: false);

	public void ResetTrigger(string parameter) =>
		NativeBridge.SpriteAnimatorSetTrigger(Entity, parameter, reset: true);
}

public enum CameraProjectionType
{
	Perspective = 0,
	Orthographic = 1
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
