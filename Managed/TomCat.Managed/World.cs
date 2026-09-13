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
