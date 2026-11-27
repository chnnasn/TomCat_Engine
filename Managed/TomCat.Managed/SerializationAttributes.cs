namespace TomCat;

[AttributeUsage(AttributeTargets.Field)]
public sealed class SerializeFieldAttribute : Attribute;

[AttributeUsage(AttributeTargets.Field)]
public sealed class HideInInspectorAttribute : Attribute;

/// <summary>Checks an Entity or GameObject field against the current scene.
/// Any listed script type satisfies the target constraint.</summary>
[AttributeUsage(AttributeTargets.Field)]
public sealed class SceneReferenceAttribute(params Type[] scripts) : Attribute
{
    public Type[] Scripts { get; } = scripts;
    public bool Required { get; set; }
}

[AttributeUsage(AttributeTargets.Field)]
public sealed class HeaderAttribute(string text) : Attribute
{
    public string Text { get; } = text ?? throw new ArgumentNullException(nameof(text));
}
[AttributeUsage(AttributeTargets.Field)]
public sealed class TooltipAttribute(string text) : Attribute
{
    public string Text { get; } = text ?? throw new ArgumentNullException(nameof(text));
}

[AttributeUsage(AttributeTargets.Field)]
public sealed class RangeAttribute(float minimum, float maximum) : Attribute
{
    public float Minimum { get; } = minimum;
    public float Maximum { get; } = maximum;
}

[AttributeUsage(AttributeTargets.Field, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute(string oldName) : Attribute
{
    public string OldName { get; } = oldName ?? throw new ArgumentNullException(nameof(oldName));
}

[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class DisallowMultipleComponentAttribute : Attribute;

[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class DefaultExecutionOrderAttribute(int order) : Attribute
{
    public int Order { get; } = order;
}
