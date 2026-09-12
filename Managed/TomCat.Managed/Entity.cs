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

	public bool ActiveInHierarchy => NativeBridge.GetActiveInHierarchy(this);

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
		if (typeof(T) == typeof(AudioSource))
			return NativeBridge.AudioHasSource(this);
		if (typeof(T) == typeof(AudioListener))
			return NativeBridge.AudioHasListener(this);
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
			return NativeBridge.HasRegisteredComponent(this, typeId);
		return NativeBridge.HasComponent(this, ComponentProxy<T>.GetNativeType());
	}

    public T AddComponent<T>() where T : class, IEntityComponent
    {
		if (typeof(T) == typeof(AudioSource))
		{
			NativeBridge.AudioAddSource(this);
			return ComponentProxy<T>.Create(this);
		}
		if (typeof(T) == typeof(AudioListener))
		{
			NativeBridge.AudioAddListener(this);
			return ComponentProxy<T>.Create(this);
		}
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
			NativeBridge.AddRegisteredComponent(this, typeId);
		else
			NativeBridge.AddComponent(this, ComponentProxy<T>.GetNativeType());
        return ComponentProxy<T>.Create(this);
    }

    public void RemoveComponent<T>() where T : class, IEntityComponent
	{
		if (typeof(T) == typeof(AudioSource))
		{
			NativeBridge.AudioRemoveSource(this);
			return;
		}
		if (typeof(T) == typeof(AudioListener))
		{
			NativeBridge.AudioRemoveListener(this);
			return;
		}
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong typeId))
			NativeBridge.RemoveRegisteredComponent(this, typeId);
		else
			NativeBridge.RemoveComponent(this, ComponentProxy<T>.GetNativeType());
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
		if (typeof(T) == typeof(HealthComponent))
			return (T)(object)new HealthComponent(entity);
		if (typeof(T) == typeof(AudioSource))
			return (T)(object)new AudioSource(entity);
		if (typeof(T) == typeof(AudioListener))
			return (T)(object)new AudioListener(entity);
		if (typeof(T) == typeof(TextRenderer))
			return (T)(object)new TextRenderer(entity);
		if (typeof(T) == typeof(Canvas))
			return (T)(object)new Canvas(entity);
		if (typeof(T) == typeof(RectTransform))
			return (T)(object)new RectTransform(entity);
		if (typeof(T) == typeof(UIImage))
			return (T)(object)new UIImage(entity);
		if (typeof(T) == typeof(UIText))
			return (T)(object)new UIText(entity);
		if (typeof(T) == typeof(UIButton))
			return (T)(object)new UIButton(entity);
		if (typeof(T) == typeof(UIEventSystem))
			return (T)(object)new UIEventSystem(entity);
		if (typeof(T) == typeof(UILayoutGroup))
			return (T)(object)new UILayoutGroup(entity);
        object value = GetNativeType() switch
        {
            NativeComponentTypeV1.Transform => new Transform(entity),
            NativeComponentTypeV1.Rigidbody2D => new Rigidbody2D(entity),
            NativeComponentTypeV1.BoxCollider2D => new BoxCollider2D(entity),
            NativeComponentTypeV1.CircleCollider2D => new CircleCollider2D(entity),
            NativeComponentTypeV1.DistanceJoint2D => new DistanceJoint2D(entity),
            NativeComponentTypeV1.SpriteRenderer => new SpriteRenderer(entity),
			NativeComponentTypeV1.Camera => new Camera(entity),
			NativeComponentTypeV1.SpriteAnimator => new SpriteAnimator(entity),
            _ => throw new TomCatException($"Unsupported component proxy {typeof(T).FullName}.")
        };
        return (T)value;
    }

	internal static bool TryGetRegisteredTypeId(out ulong typeId)
	{
		if (typeof(T) == typeof(HealthComponent))
		{
			typeId = HealthComponent.TypeId;
			return true;
		}
		if (typeof(T) == typeof(TextRenderer)) typeId = TextRenderer.TypeId;
		else if (typeof(T) == typeof(Canvas)) typeId = Canvas.TypeId;
		else if (typeof(T) == typeof(RectTransform)) typeId = RectTransform.TypeId;
		else if (typeof(T) == typeof(UIImage)) typeId = UIImage.TypeId;
		else if (typeof(T) == typeof(UIText)) typeId = UIText.TypeId;
		else if (typeof(T) == typeof(UIButton)) typeId = UIButton.TypeId;
		else if (typeof(T) == typeof(UIEventSystem)) typeId = UIEventSystem.TypeId;
		else if (typeof(T) == typeof(UILayoutGroup)) typeId = UILayoutGroup.TypeId;
		else
		{
			typeId = 0;
			return false;
		}
		return true;
	}

	internal static bool TryGetNativeType(out NativeComponentTypeV1 nativeType)
	{
		Type type = typeof(T);
		if (type == typeof(Transform)) nativeType = NativeComponentTypeV1.Transform;
		else if (type == typeof(Rigidbody2D)) nativeType = NativeComponentTypeV1.Rigidbody2D;
		else if (type == typeof(BoxCollider2D)) nativeType = NativeComponentTypeV1.BoxCollider2D;
		else if (type == typeof(CircleCollider2D)) nativeType = NativeComponentTypeV1.CircleCollider2D;
		else if (type == typeof(DistanceJoint2D)) nativeType = NativeComponentTypeV1.DistanceJoint2D;
		else if (type == typeof(SpriteRenderer)) nativeType = NativeComponentTypeV1.SpriteRenderer;
		else if (type == typeof(Camera)) nativeType = NativeComponentTypeV1.Camera;
		else if (type == typeof(SpriteAnimator)) nativeType = NativeComponentTypeV1.SpriteAnimator;
		else
		{
			nativeType = default;
			return false;
		}
		return true;
	}

    // Do not retain typeof(T) in a static field. A project can attempt to call
    // this generic API with a collectible type, and the default ALC must never
    // acquire a static reference that prevents the Play Domain from unloading.
    internal static NativeComponentTypeV1 GetNativeType()
    {
		if (TryGetNativeType(out NativeComponentTypeV1 nativeType))
			return nativeType;
		throw new TomCatException($"{typeof(T).FullName} is not a TomCat component proxy.");
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

public sealed unsafe class Rigidbody2D : IEntityComponent
{
    internal Rigidbody2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }

	public bool Enabled
	{
		get => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.Rigidbody2D,
			100, "Rigidbody2D.Enabled");
		set => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.Rigidbody2D,
			100, value, "Rigidbody2D.Enabled");
	}

	public RigidbodyBodyType BodyType
	{
		get => (RigidbodyBodyType)NativeBridge.GetGameplayInt32(Entity,
			NativeComponentTypeV1.Rigidbody2D, 101, "Rigidbody2D.BodyType");
		set => NativeBridge.SetGameplayInt32(Entity, NativeComponentTypeV1.Rigidbody2D,
			101, (int)value, "Rigidbody2D.BodyType");
	}

	public bool FixedRotation
	{
		get => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.Rigidbody2D,
			102, "Rigidbody2D.FixedRotation");
		set => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.Rigidbody2D,
			102, value, "Rigidbody2D.FixedRotation");
	}

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

public sealed class BoxCollider2D : IEntityComponent
{
    internal BoxCollider2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
	public bool Enabled { get => GetBool(400, "Enabled"); set => SetBool(400, value, "Enabled"); }
	public bool IsTrigger { get => GetBool(401, "IsTrigger"); set => SetBool(401, value, "IsTrigger"); }
	public uint CollisionLayer { get => GetUInt(402, "CollisionLayer"); set => SetUInt(402, value, "CollisionLayer"); }
	public uint CollisionMask { get => GetUInt(403, "CollisionMask"); set => SetUInt(403, value, "CollisionMask"); }
	public Vector2 Offset { get => GetVector(404, "Offset"); set => SetVector(404, value, "Offset"); }
	public Vector2 Size { get => GetVector(405, "Size"); set => SetVector(405, value, "Size"); }
	public float Density { get => GetFloat(406, "Density"); set => SetFloat(406, value, "Density"); }
	public float Friction { get => GetFloat(407, "Friction"); set => SetFloat(407, value, "Friction"); }
	public float Restitution { get => GetFloat(408, "Restitution"); set => SetFloat(408, value, "Restitution"); }
	public float RestitutionThreshold { get => GetFloat(409, "RestitutionThreshold"); set => SetFloat(409, value, "RestitutionThreshold"); }
	private bool GetBool(uint id, string name) => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.BoxCollider2D, id, $"BoxCollider2D.{name}");
	private void SetBool(uint id, bool value, string name) => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.BoxCollider2D, id, value, $"BoxCollider2D.{name}");
	private uint GetUInt(uint id, string name) => NativeBridge.GetGameplayUInt32(Entity, NativeComponentTypeV1.BoxCollider2D, id, $"BoxCollider2D.{name}");
	private void SetUInt(uint id, uint value, string name) => NativeBridge.SetGameplayUInt32(Entity, NativeComponentTypeV1.BoxCollider2D, id, value, $"BoxCollider2D.{name}");
	private float GetFloat(uint id, string name) => NativeBridge.GetGameplayFloat(Entity, NativeComponentTypeV1.BoxCollider2D, id, $"BoxCollider2D.{name}");
	private void SetFloat(uint id, float value, string name) => NativeBridge.SetGameplayFloat(Entity, NativeComponentTypeV1.BoxCollider2D, id, value, $"BoxCollider2D.{name}");
	private Vector2 GetVector(uint id, string name) => NativeBridge.GetGameplayVector2(Entity, NativeComponentTypeV1.BoxCollider2D, id, $"BoxCollider2D.{name}");
	private void SetVector(uint id, Vector2 value, string name) => NativeBridge.SetGameplayVector2(Entity, NativeComponentTypeV1.BoxCollider2D, id, value, $"BoxCollider2D.{name}");
}

public sealed class CircleCollider2D : IEntityComponent
{
    internal CircleCollider2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
	public bool Enabled { get => GetBool(500, "Enabled"); set => SetBool(500, value, "Enabled"); }
	public bool IsTrigger { get => GetBool(501, "IsTrigger"); set => SetBool(501, value, "IsTrigger"); }
	public uint CollisionLayer { get => GetUInt(502, "CollisionLayer"); set => SetUInt(502, value, "CollisionLayer"); }
	public uint CollisionMask { get => GetUInt(503, "CollisionMask"); set => SetUInt(503, value, "CollisionMask"); }
	public Vector2 Offset { get => GetVector(504, "Offset"); set => SetVector(504, value, "Offset"); }
	public float Radius { get => GetFloat(505, "Radius"); set => SetFloat(505, value, "Radius"); }
	public float Density { get => GetFloat(506, "Density"); set => SetFloat(506, value, "Density"); }
	public float Friction { get => GetFloat(507, "Friction"); set => SetFloat(507, value, "Friction"); }
	public float Restitution { get => GetFloat(508, "Restitution"); set => SetFloat(508, value, "Restitution"); }
	private bool GetBool(uint id, string name) => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.CircleCollider2D, id, $"CircleCollider2D.{name}");
	private void SetBool(uint id, bool value, string name) => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.CircleCollider2D, id, value, $"CircleCollider2D.{name}");
	private uint GetUInt(uint id, string name) => NativeBridge.GetGameplayUInt32(Entity, NativeComponentTypeV1.CircleCollider2D, id, $"CircleCollider2D.{name}");
	private void SetUInt(uint id, uint value, string name) => NativeBridge.SetGameplayUInt32(Entity, NativeComponentTypeV1.CircleCollider2D, id, value, $"CircleCollider2D.{name}");
	private float GetFloat(uint id, string name) => NativeBridge.GetGameplayFloat(Entity, NativeComponentTypeV1.CircleCollider2D, id, $"CircleCollider2D.{name}");
	private void SetFloat(uint id, float value, string name) => NativeBridge.SetGameplayFloat(Entity, NativeComponentTypeV1.CircleCollider2D, id, value, $"CircleCollider2D.{name}");
	private Vector2 GetVector(uint id, string name) => NativeBridge.GetGameplayVector2(Entity, NativeComponentTypeV1.CircleCollider2D, id, $"CircleCollider2D.{name}");
	private void SetVector(uint id, Vector2 value, string name) => NativeBridge.SetGameplayVector2(Entity, NativeComponentTypeV1.CircleCollider2D, id, value, $"CircleCollider2D.{name}");
}

public sealed class DistanceJoint2D : IEntityComponent
{
    internal DistanceJoint2D(Entity entity) => Entity = entity;
    public Entity Entity { get; }
	public bool Enabled { get => GetBool(600, "Enabled"); set => SetBool(600, value, "Enabled"); }
	public Entity? ConnectedEntity
	{
		get
		{
			ulong id = NativeBridge.GetGameplayUInt64(Entity, NativeComponentTypeV1.DistanceJoint2D, 601, "DistanceJoint2D.ConnectedEntity");
			return id == 0 ? null : new Entity(Entity.SceneSessionId, id, Entity.RuntimeGeneration);
		}
		set
		{
			if (value is not null && (value.SceneSessionId != Entity.SceneSessionId || value.RuntimeGeneration != Entity.RuntimeGeneration))
				throw new ArgumentException("ConnectedEntity must belong to the same scene runtime.", nameof(value));
			NativeBridge.SetGameplayUInt64(Entity, NativeComponentTypeV1.DistanceJoint2D, 601, value?.Id ?? 0, "DistanceJoint2D.ConnectedEntity");
		}
	}
	public Vector2 Anchor { get => GetVector(602, "Anchor"); set => SetVector(602, value, "Anchor"); }
	public Vector2 ConnectedAnchor { get => GetVector(603, "ConnectedAnchor"); set => SetVector(603, value, "ConnectedAnchor"); }
	public float Distance { get => GetFloat(604, "Distance"); set => SetFloat(604, value, "Distance"); }
	public float Frequency { get => GetFloat(605, "Frequency"); set => SetFloat(605, value, "Frequency"); }
	public float Damping { get => GetFloat(606, "Damping"); set => SetFloat(606, value, "Damping"); }
	public bool CollideConnected { get => GetBool(607, "CollideConnected"); set => SetBool(607, value, "CollideConnected"); }
	private bool GetBool(uint id, string name) => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.DistanceJoint2D, id, $"DistanceJoint2D.{name}");
	private void SetBool(uint id, bool value, string name) => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.DistanceJoint2D, id, value, $"DistanceJoint2D.{name}");
	private float GetFloat(uint id, string name) => NativeBridge.GetGameplayFloat(Entity, NativeComponentTypeV1.DistanceJoint2D, id, $"DistanceJoint2D.{name}");
	private void SetFloat(uint id, float value, string name) => NativeBridge.SetGameplayFloat(Entity, NativeComponentTypeV1.DistanceJoint2D, id, value, $"DistanceJoint2D.{name}");
	private Vector2 GetVector(uint id, string name) => NativeBridge.GetGameplayVector2(Entity, NativeComponentTypeV1.DistanceJoint2D, id, $"DistanceJoint2D.{name}");
	private void SetVector(uint id, Vector2 value, string name) => NativeBridge.SetGameplayVector2(Entity, NativeComponentTypeV1.DistanceJoint2D, id, value, $"DistanceJoint2D.{name}");
}

public sealed class SpriteRenderer : IEntityComponent
{
    internal SpriteRenderer(Entity entity) => Entity = entity;
    public Entity Entity { get; }
	public bool Enabled
	{
		get => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.SpriteRenderer, 200, "SpriteRenderer.Enabled");
		set => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.SpriteRenderer, 200, value, "SpriteRenderer.Enabled");
	}
	public Color Color
	{
		get => NativeBridge.GetGameplayColor(Entity, NativeComponentTypeV1.SpriteRenderer, 201, "SpriteRenderer.Color");
		set => NativeBridge.SetGameplayColor(Entity, NativeComponentTypeV1.SpriteRenderer, 201, value, "SpriteRenderer.Color");
	}
	public AssetRef<Texture2DAsset> Sprite
	{
		get => new(NativeBridge.GetGameplayUInt64(Entity, NativeComponentTypeV1.SpriteRenderer, 202, "SpriteRenderer.Sprite"));
		set => NativeBridge.SetGameplayUInt64(Entity, NativeComponentTypeV1.SpriteRenderer, 202, value.Handle, "SpriteRenderer.Sprite");
	}
	public float TilingFactor
	{
		get => NativeBridge.GetGameplayFloat(Entity, NativeComponentTypeV1.SpriteRenderer, 203, "SpriteRenderer.TilingFactor");
		set => NativeBridge.SetGameplayFloat(Entity, NativeComponentTypeV1.SpriteRenderer, 203, value, "SpriteRenderer.TilingFactor");
	}
	public int SortingLayer
	{
		get => NativeBridge.GetGameplayInt32(Entity, NativeComponentTypeV1.SpriteRenderer, 204, "SpriteRenderer.SortingLayer");
		set => NativeBridge.SetGameplayInt32(Entity, NativeComponentTypeV1.SpriteRenderer, 204, value, "SpriteRenderer.SortingLayer");
	}
	public int OrderInLayer
	{
		get => NativeBridge.GetGameplayInt32(Entity, NativeComponentTypeV1.SpriteRenderer, 205, "SpriteRenderer.OrderInLayer");
		set => NativeBridge.SetGameplayInt32(Entity, NativeComponentTypeV1.SpriteRenderer, 205, value, "SpriteRenderer.OrderInLayer");
	}
}

/// <summary>Controls the deterministic sprite animation attached to an entity.</summary>
public sealed class SpriteAnimator : IEntityComponent
{
	internal SpriteAnimator(Entity entity) => Entity = entity;
	public Entity Entity { get; }

	public bool Enabled
	{
		get => NativeBridge.GetGameplayBool(Entity,
			NativeComponentTypeV1.SpriteAnimator, 700, "SpriteAnimator.Enabled");
		set => NativeBridge.SetGameplayBool(Entity,
			NativeComponentTypeV1.SpriteAnimator, 700, value, "SpriteAnimator.Enabled");
	}

	public float Speed
	{
		get => NativeBridge.GetGameplayFloat(Entity,
			NativeComponentTypeV1.SpriteAnimator, 701, "SpriteAnimator.Speed");
		set => NativeBridge.SetGameplayFloat(Entity,
			NativeComponentTypeV1.SpriteAnimator, 701, value, "SpriteAnimator.Speed");
	}

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

public sealed class Camera : IEntityComponent
{
	internal Camera(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Primary { get => GetBool(300, "Primary"); set => SetBool(300, value, "Primary"); }
	public bool FixedAspectRatio { get => GetBool(301, "FixedAspectRatio"); set => SetBool(301, value, "FixedAspectRatio"); }
	public Color BackgroundColor
	{
		get => NativeBridge.GetGameplayColor(Entity, NativeComponentTypeV1.Camera, 302, "Camera.BackgroundColor");
		set => NativeBridge.SetGameplayColor(Entity, NativeComponentTypeV1.Camera, 302, value, "Camera.BackgroundColor");
	}
	public CameraProjectionType ProjectionType { get => (CameraProjectionType)GetInt(303, "ProjectionType"); set => SetInt(303, (int)value, "ProjectionType"); }
	public float OrthographicSize { get => GetFloat(304, "OrthographicSize"); set => SetFloat(304, value, "OrthographicSize"); }
	public float OrthographicNearClip { get => GetFloat(305, "OrthographicNearClip"); set => SetFloat(305, value, "OrthographicNearClip"); }
	public float OrthographicFarClip { get => GetFloat(306, "OrthographicFarClip"); set => SetFloat(306, value, "OrthographicFarClip"); }
	/// <summary>Perspective vertical field of view in radians.</summary>
	public float PerspectiveVerticalFov { get => GetFloat(307, "PerspectiveVerticalFov"); set => SetFloat(307, value, "PerspectiveVerticalFov"); }
	public float PerspectiveNearClip { get => GetFloat(308, "PerspectiveNearClip"); set => SetFloat(308, value, "PerspectiveNearClip"); }
	public float PerspectiveFarClip { get => GetFloat(309, "PerspectiveFarClip"); set => SetFloat(309, value, "PerspectiveFarClip"); }
	private bool GetBool(uint id, string name) => NativeBridge.GetGameplayBool(Entity, NativeComponentTypeV1.Camera, id, $"Camera.{name}");
	private void SetBool(uint id, bool value, string name) => NativeBridge.SetGameplayBool(Entity, NativeComponentTypeV1.Camera, id, value, $"Camera.{name}");
	private int GetInt(uint id, string name) => NativeBridge.GetGameplayInt32(Entity, NativeComponentTypeV1.Camera, id, $"Camera.{name}");
	private void SetInt(uint id, int value, string name) => NativeBridge.SetGameplayInt32(Entity, NativeComponentTypeV1.Camera, id, value, $"Camera.{name}");
	private float GetFloat(uint id, string name) => NativeBridge.GetGameplayFloat(Entity, NativeComponentTypeV1.Camera, id, $"Camera.{name}");
	private void SetFloat(uint id, float value, string name) => NativeBridge.SetGameplayFloat(Entity, NativeComponentTypeV1.Camera, id, value, $"Camera.{name}");
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
