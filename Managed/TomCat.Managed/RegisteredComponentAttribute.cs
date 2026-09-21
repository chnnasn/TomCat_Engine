namespace TomCat;

/// <summary>
/// Declares the stable native ComponentRegistry type ID implemented by a managed
/// entity-component proxy. Plugin-generated proxies can carry this attribute and
/// an instance constructor accepting <see cref="Entity"/>; no engine manifest or
/// edit to Entity is required.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class RegisteredComponentAttribute : Attribute
{
	public RegisteredComponentAttribute(ulong typeId)
	{
		if (typeId == 0)
			throw new ArgumentOutOfRangeException(nameof(typeId));
		ComponentTypeId = typeId;
	}

	public ulong ComponentTypeId { get; }
}
