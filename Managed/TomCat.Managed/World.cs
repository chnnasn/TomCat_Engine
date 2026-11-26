namespace TomCat;

/// <summary>Operations on the scene that owns the currently executing behaviour.</summary>
public static class World
{
	/// <summary>
	/// Reserves an entity identity immediately and creates it at the callback safe point.
	/// The returned handle may be retained or used as a parent by later queued commands;
	/// <see cref="Entity.IsValid"/> becomes true after the callback returns.
	/// </summary>
	public static Entity CreateEntity(string name = "Entity",
		Vector3 worldPosition = default, Entity? parent = null) =>
		NativeBridge.CreateEntity(ScriptExecutionContext.CurrentEntity, name,
			worldPosition, parent);

	public static Entity? Find(string name) => NativeBridge.FindEntityByName(
		ScriptExecutionContext.CurrentEntity, name);

	/// <summary>
	/// First entity, in scene order, whose name and gameplay tag match. Null
	/// <paramref name="name"/> or <paramref name="tag"/> matches any value;
	/// <paramref name="within"/> limits the search to that entity's
	/// descendants (not the entity itself). This scans the scene, so look up
	/// once in Start and keep the result rather than calling it every frame.
	/// </summary>
	public static Entity? Find(string? name, string? tag, Entity? within = null)
	{
		foreach (Entity entity in Candidates(within))
			if (Matches(entity, name, tag))
				return entity;
		return null;
	}

	/// <summary>Every entity, in scene order, that <see cref="Find(string?, string?, Entity?)"/> would accept.</summary>
	public static IReadOnlyList<Entity> FindAll(string? name = null, string? tag = null,
		Entity? within = null)
	{
		var result = new List<Entity>();
		foreach (Entity entity in Candidates(within))
			if (Matches(entity, name, tag))
				result.Add(entity);
		return result;
	}

	/// <summary>Every entity whose gameplay tag is <paramref name="tag"/>.</summary>
	public static IReadOnlyList<Entity> FindWithTag(string tag)
	{
		ArgumentException.ThrowIfNullOrEmpty(tag);
		return FindAll(null, tag);
	}

	/// <summary>
	/// The <typeparamref name="T"/> component of the first matching entity that
	/// has one, replacing <c>World.Find(name)?.GetComponent&lt;T&gt;()</c>.
	/// Only entities with <typeparamref name="T"/> are considered.
	/// </summary>
	public static T? Find<T>(string? name = null, string? tag = null, Entity? within = null)
		where T : class, IEntityComponent
	{
		foreach (Entity entity in Query<T>())
			if (Matches(entity, name, tag) && (within is null || IsDescendant(entity, within))
				&& entity.TryGetComponent(out T component))
				return component;
		return null;
	}

	/// <summary>The <typeparamref name="T"/> components of every matching entity.</summary>
	public static IReadOnlyList<T> FindAll<T>(string? name = null, string? tag = null,
		Entity? within = null) where T : class, IEntityComponent
	{
		var result = new List<T>();
		foreach (Entity entity in Query<T>())
			if (Matches(entity, name, tag) && (within is null || IsDescendant(entity, within))
				&& entity.TryGetComponent(out T component))
				result.Add(component);
		return result;
	}

	private static IEnumerable<Entity> Candidates(Entity? within)
	{
		if (within is null)
			return All;
		if (!within.IsValid)
			throw new ArgumentException("The search root is not a live entity.", nameof(within));
		var descendants = new List<Entity>();
		var pending = new Stack<Entity>();
		pending.Push(within);
		while (pending.Count != 0)
		{
			IReadOnlyList<Entity> children = pending.Pop().Children;
			for (int index = children.Count - 1; index >= 0; --index)
			{
				descendants.Add(children[index]);
				pending.Push(children[index]);
			}
		}
		return descendants;
	}

	private static bool Matches(Entity entity, string? name, string? tag) =>
		(name is null || string.Equals(entity.Name, name, StringComparison.Ordinal))
		&& (tag is null || string.Equals(entity.Tag, tag, StringComparison.Ordinal));

	private static bool IsDescendant(Entity entity, Entity ancestor)
	{
		for (Entity? cursor = entity.Parent; cursor is not null; cursor = cursor.Parent)
			if (cursor.Equals(ancestor))
				return true;
		return false;
	}

	public static IReadOnlyList<Entity> All => NativeBridge.QueryEntities(
		ScriptExecutionContext.CurrentEntity, 0, 0);

	public static IReadOnlyList<Entity> Query<T>() where T : class, IEntityComponent
	{
		Entity context = ScriptExecutionContext.CurrentEntity;
		if (ComponentProxy<T>.TryGetRegisteredTypeId(out ulong registeredTypeId))
			return NativeBridge.QueryEntities(context, 0, registeredTypeId);
		throw new TomCatException($"{typeof(T).FullName} is not a queryable TomCat component.");
	}

	public static bool Instantiate(PrefabAsset prefab, Vector3 worldPosition,
		Entity? parent = null) => NativeBridge.InstantiatePrefab(
			ScriptExecutionContext.CurrentEntity, prefab, worldPosition, parent);
}
