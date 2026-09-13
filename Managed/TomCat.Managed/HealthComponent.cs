namespace TomCat;

/// <summary>A registry-backed gameplay health component.</summary>
public sealed class HealthComponent : IEntityComponent
{
	// Explicit values shared with the native ComponentRegistry. They are public
	// so tooling and generated bindings can use the persisted identities directly.
	public const ulong TypeId = 0x8d0df196efd946a1UL;
	public const ulong MaximumPropertyId = 0x91bc0a20e4f64ed1UL;
	public const ulong CurrentPropertyId = 0xa43fdaf1b97042e7UL;
	public const ulong InvulnerablePropertyId = 0xcb63837a8a7a4c12UL;

	internal HealthComponent(Entity entity) => Entity = entity;
	public Entity Entity { get; }

	public int Maximum
	{
		get => NativeBridge.GetRegisteredInt32(Entity, TypeId, MaximumPropertyId,
			"HealthComponent.Maximum");
		set => NativeBridge.SetRegisteredInt32(Entity, TypeId, MaximumPropertyId,
			value, "HealthComponent.Maximum");
	}

	public int Current
	{
		get => NativeBridge.GetRegisteredInt32(Entity, TypeId, CurrentPropertyId,
			"HealthComponent.Current");
		set => NativeBridge.SetRegisteredInt32(Entity, TypeId, CurrentPropertyId,
			value, "HealthComponent.Current");
	}

	public bool Invulnerable
	{
		get => NativeBridge.GetRegisteredBool(Entity, TypeId, InvulnerablePropertyId,
			"HealthComponent.Invulnerable");
		set => NativeBridge.SetRegisteredBool(Entity, TypeId, InvulnerablePropertyId,
			value, "HealthComponent.Invulnerable");
	}
}
