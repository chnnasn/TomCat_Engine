namespace TomCat;

public enum TextAlignment
{
	Left = 0,
	Center = 1,
	Right = 2
}

public enum CanvasScaleMode
{
	ConstantPixelSize = 0,
	ScaleWithScreenSize = 1
}

public enum UILayoutDirection
{
	Horizontal = 0,
	Vertical = 1
}

public readonly struct UIRect : IEquatable<UIRect>
{
	public UIRect(float x, float y, float width, float height) =>
		(X, Y, Width, Height) = (x, y, width, height);
	public float X { get; }
	public float Y { get; }
	public float Width { get; }
	public float Height { get; }
	public bool IsEmpty => Width <= 0.0f || Height <= 0.0f;
	public bool Equals(UIRect other) => X.Equals(other.X) && Y.Equals(other.Y)
		&& Width.Equals(other.Width) && Height.Equals(other.Height);
	public override bool Equals(object? obj) => obj is UIRect other && Equals(other);
	public override int GetHashCode() => HashCode.Combine(X, Y, Width, Height);
	public override string ToString() => $"({X}, {Y}, {Width}, {Height})";
}

public sealed partial class TextRenderer
{
	public const ulong TypeId = RegisteredTypeId;
}

public sealed class Canvas : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000002UL;
	private const ulong EnabledId = 0x9f01200000000001UL;
	private const ulong ScaleModeId = 0x9f01200000000002UL;
	private const ulong ReferenceResolutionId = 0x9f01200000000003UL;
	private const ulong MatchId = 0x9f01200000000004UL;
	private const ulong ScaleFactorId = 0x9f01200000000005UL;
	private const ulong ReferenceDpiId = 0x9f01200000000006UL;
	private const ulong SortingOrderId = 0x9f01200000000007UL;

	internal Canvas(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => GetBool(EnabledId, "Enabled"); set => SetBool(EnabledId, value, "Enabled"); }
	public CanvasScaleMode ScaleMode { get => (CanvasScaleMode)GetInt(ScaleModeId, "ScaleMode"); set => SetInt(ScaleModeId, (int)value, "ScaleMode"); }
	public Vector2 ReferenceResolution { get => GetVector(ReferenceResolutionId, "ReferenceResolution"); set => SetVector(ReferenceResolutionId, value, "ReferenceResolution"); }
	public float MatchWidthOrHeight { get => GetFloat(MatchId, "MatchWidthOrHeight"); set => SetFloat(MatchId, value, "MatchWidthOrHeight"); }
	public float ScaleFactor { get => GetFloat(ScaleFactorId, "ScaleFactor"); set => SetFloat(ScaleFactorId, value, "ScaleFactor"); }
	public float ReferenceDPI { get => GetFloat(ReferenceDpiId, "ReferenceDPI"); set => SetFloat(ReferenceDpiId, value, "ReferenceDPI"); }
	public int SortingOrder { get => GetInt(SortingOrderId, "SortingOrder"); set => SetInt(SortingOrderId, value, "SortingOrder"); }
	private bool GetBool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"Canvas.{name}");
	private void SetBool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"Canvas.{name}");
	private int GetInt(ulong id, string name) => NativeBridge.GetRegisteredInt32(Entity, TypeId, id, $"Canvas.{name}");
	private void SetInt(ulong id, int value, string name) => NativeBridge.SetRegisteredInt32(Entity, TypeId, id, value, $"Canvas.{name}");
	private float GetFloat(ulong id, string name) => NativeBridge.GetRegisteredFloat(Entity, TypeId, id, $"Canvas.{name}");
	private void SetFloat(ulong id, float value, string name) => NativeBridge.SetRegisteredFloat(Entity, TypeId, id, value, $"Canvas.{name}");
	private Vector2 GetVector(ulong id, string name) => NativeBridge.GetRegisteredVector2(Entity, TypeId, id, $"Canvas.{name}");
	private void SetVector(ulong id, Vector2 value, string name) => NativeBridge.SetRegisteredVector2(Entity, TypeId, id, value, $"Canvas.{name}");
}

public sealed class RectTransform : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000003UL;
	private const ulong AnchorMinId = 0x9f01300000000001UL;
	private const ulong AnchorMaxId = 0x9f01300000000002UL;
	private const ulong PivotId = 0x9f01300000000003UL;
	private const ulong AnchoredPositionId = 0x9f01300000000004UL;
	private const ulong SizeDeltaId = 0x9f01300000000005UL;
	private const ulong ClipChildrenId = 0x9f01300000000006UL;

	internal RectTransform(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public Vector2 AnchorMin { get => Vector(AnchorMinId, "AnchorMin"); set => Vector(AnchorMinId, value, "AnchorMin"); }
	public Vector2 AnchorMax { get => Vector(AnchorMaxId, "AnchorMax"); set => Vector(AnchorMaxId, value, "AnchorMax"); }
	public Vector2 Pivot { get => Vector(PivotId, "Pivot"); set => Vector(PivotId, value, "Pivot"); }
	public Vector2 AnchoredPosition { get => Vector(AnchoredPositionId, "AnchoredPosition"); set => Vector(AnchoredPositionId, value, "AnchoredPosition"); }
	public Vector2 SizeDelta { get => Vector(SizeDeltaId, "SizeDelta"); set => Vector(SizeDeltaId, value, "SizeDelta"); }
	public bool ClipChildren { get => NativeBridge.GetRegisteredBool(Entity, TypeId, ClipChildrenId, "RectTransform.ClipChildren"); set => NativeBridge.SetRegisteredBool(Entity, TypeId, ClipChildrenId, value, "RectTransform.ClipChildren"); }
	public UIRect RuntimeRect { get { Vector4 value = NativeBridge.GetRuntimeUIRect(Entity); return new(value.X, value.Y, value.Z, value.W); } }
	private Vector2 Vector(ulong id, string name) => NativeBridge.GetRegisteredVector2(Entity, TypeId, id, $"RectTransform.{name}");
	private void Vector(ulong id, Vector2 value, string name) => NativeBridge.SetRegisteredVector2(Entity, TypeId, id, value, $"RectTransform.{name}");
}

public sealed class UIImage : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000004UL;
	private const ulong EnabledId = 0x9f01400000000001UL;
	private const ulong ImageId = 0x9f01400000000002UL;
	private const ulong ColorId = 0x9f01400000000003UL;
	private const ulong RaycastTargetId = 0x9f01400000000004UL;
	private const ulong PreserveAspectId = 0x9f01400000000005UL;

	internal UIImage(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => Bool(EnabledId, "Enabled"); set => Bool(EnabledId, value, "Enabled"); }
	public AssetRef<Texture2DAsset> Image { get => new(NativeBridge.GetRegisteredUInt64(Entity, TypeId, ImageId, "UIImage.Image")); set => NativeBridge.SetRegisteredUInt64(Entity, TypeId, ImageId, value.Handle, "UIImage.Image"); }
	public Color Color { get => NativeBridge.GetRegisteredColor(Entity, TypeId, ColorId, "UIImage.Color"); set => NativeBridge.SetRegisteredColor(Entity, TypeId, ColorId, value, "UIImage.Color"); }
	public bool RaycastTarget { get => Bool(RaycastTargetId, "RaycastTarget"); set => Bool(RaycastTargetId, value, "RaycastTarget"); }
	public bool PreserveAspect { get => Bool(PreserveAspectId, "PreserveAspect"); set => Bool(PreserveAspectId, value, "PreserveAspect"); }
	private bool Bool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"UIImage.{name}");
	private void Bool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"UIImage.{name}");
}

public sealed class UIText : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000005UL;
	private const ulong EnabledId = 0x9f01500000000001UL;
	private const ulong FontId = 0x9f01500000000002UL;
	private const ulong FontSizeId = 0x9f01500000000004UL;
	private const ulong ColorId = 0x9f01500000000005UL;
	private const ulong AlignmentId = 0x9f01500000000006UL;
	private const ulong WrapId = 0x9f01500000000007UL;
	private const ulong LineSpacingId = 0x9f01500000000008UL;
	private const ulong RaycastTargetId = 0x9f01500000000009UL;
	private const ulong FallbackFontId = 0x9f0150000000000aUL;
	private const ulong EmojiFontId = 0x9f0150000000000bUL;

	internal UIText(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => Bool(EnabledId, "Enabled"); set => Bool(EnabledId, value, "Enabled"); }
	public AssetRef<FontAsset> Font { get => new(NativeBridge.GetRegisteredUInt64(Entity, TypeId, FontId, "UIText.Font")); set => NativeBridge.SetRegisteredUInt64(Entity, TypeId, FontId, value.Handle, "UIText.Font"); }
	public AssetRef<FontAsset> FallbackFont { get => new(NativeBridge.GetRegisteredUInt64(Entity, TypeId, FallbackFontId, "UIText.FallbackFont")); set => NativeBridge.SetRegisteredUInt64(Entity, TypeId, FallbackFontId, value.Handle, "UIText.FallbackFont"); }
	public AssetRef<FontAsset> EmojiFont { get => new(NativeBridge.GetRegisteredUInt64(Entity, TypeId, EmojiFontId, "UIText.EmojiFont")); set => NativeBridge.SetRegisteredUInt64(Entity, TypeId, EmojiFontId, value.Handle, "UIText.EmojiFont"); }
	public string Text { get => NativeBridge.GetRuntimeUIText(Entity, TypeId, "UIText.Text"); set => NativeBridge.SetRuntimeUIText(Entity, TypeId, value, "UIText.Text"); }
	public float FontSize { get => Float(FontSizeId, "FontSize"); set => Float(FontSizeId, value, "FontSize"); }
	public Color Color { get => NativeBridge.GetRegisteredColor(Entity, TypeId, ColorId, "UIText.Color"); set => NativeBridge.SetRegisteredColor(Entity, TypeId, ColorId, value, "UIText.Color"); }
	public TextAlignment Alignment { get => (TextAlignment)NativeBridge.GetRegisteredInt32(Entity, TypeId, AlignmentId, "UIText.Alignment"); set => NativeBridge.SetRegisteredInt32(Entity, TypeId, AlignmentId, (int)value, "UIText.Alignment"); }
	public bool Wrap { get => Bool(WrapId, "Wrap"); set => Bool(WrapId, value, "Wrap"); }
	public float LineSpacing { get => Float(LineSpacingId, "LineSpacing"); set => Float(LineSpacingId, value, "LineSpacing"); }
	public bool RaycastTarget { get => Bool(RaycastTargetId, "RaycastTarget"); set => Bool(RaycastTargetId, value, "RaycastTarget"); }
	private bool Bool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"UIText.{name}");
	private void Bool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"UIText.{name}");
	private float Float(ulong id, string name) => NativeBridge.GetRegisteredFloat(Entity, TypeId, id, $"UIText.{name}");
	private void Float(ulong id, float value, string name) => NativeBridge.SetRegisteredFloat(Entity, TypeId, id, value, $"UIText.{name}");
}

public sealed class UIButton : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000006UL;
	private const ulong EnabledId = 0x9f01600000000001UL;
	private const ulong InteractableId = 0x9f01600000000002UL;
	private const ulong NormalColorId = 0x9f01600000000003UL;
	private const ulong HoverColorId = 0x9f01600000000004UL;
	private const ulong PressedColorId = 0x9f01600000000005UL;
	private const ulong SelectedColorId = 0x9f01600000000006UL;

	internal UIButton(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => Bool(EnabledId, "Enabled"); set => Bool(EnabledId, value, "Enabled"); }
	public bool Interactable { get => Bool(InteractableId, "Interactable"); set => Bool(InteractableId, value, "Interactable"); }
	public Color NormalColor { get => Color(NormalColorId, "NormalColor"); set => Color(NormalColorId, value, "NormalColor"); }
	public Color HoverColor { get => Color(HoverColorId, "HoverColor"); set => Color(HoverColorId, value, "HoverColor"); }
	public Color PressedColor { get => Color(PressedColorId, "PressedColor"); set => Color(PressedColorId, value, "PressedColor"); }
	public Color SelectedColor { get => Color(SelectedColorId, "SelectedColor"); set => Color(SelectedColorId, value, "SelectedColor"); }
	public bool WasClickedThisFrame => NativeBridge.RuntimeUIButtonWasClicked(Entity);
	public ulong ClickSerial => NativeBridge.GetRuntimeUIButtonClickSerial(Entity);
	public void Focus() => NativeBridge.FocusRuntimeUIButton(Entity);
	private bool Bool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"UIButton.{name}");
	private void Bool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"UIButton.{name}");
	private Color Color(ulong id, string name) => NativeBridge.GetRegisteredColor(Entity, TypeId, id, $"UIButton.{name}");
	private void Color(ulong id, Color value, string name) => NativeBridge.SetRegisteredColor(Entity, TypeId, id, value, $"UIButton.{name}");
}

public sealed class UIEventSystem : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000007UL;
	private const ulong EnabledId = 0x9f01700000000001UL;
	private const ulong ConsumeId = 0x9f01700000000002UL;
	private const ulong WrapId = 0x9f01700000000003UL;

	internal UIEventSystem(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => Bool(EnabledId, "Enabled"); set => Bool(EnabledId, value, "Enabled"); }
	public bool ConsumeGameplayInput { get => Bool(ConsumeId, "ConsumeGameplayInput"); set => Bool(ConsumeId, value, "ConsumeGameplayInput"); }
	public bool WrapNavigation { get => Bool(WrapId, "WrapNavigation"); set => Bool(WrapId, value, "WrapNavigation"); }
	public static bool IsGameplayInputCaptured => NativeBridge.IsRuntimeUIInputCaptured();
	private bool Bool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"UIEventSystem.{name}");
	private void Bool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"UIEventSystem.{name}");
}

public sealed class UILayoutGroup : IEntityComponent
{
	public const ulong TypeId = 0x9f01000000000008UL;
	private const ulong EnabledId = 0x9f01800000000001UL;
	private const ulong DirectionId = 0x9f01800000000002UL;
	private const ulong SpacingId = 0x9f01800000000003UL;
	private const ulong PaddingId = 0x9f01800000000004UL;
	private const ulong ControlChildSizeId = 0x9f01800000000005UL;
	private const ulong ChildSizeId = 0x9f01800000000006UL;

	internal UILayoutGroup(Entity entity) => Entity = entity;
	public Entity Entity { get; }
	public bool Enabled { get => Bool(EnabledId, "Enabled"); set => Bool(EnabledId, value, "Enabled"); }
	public UILayoutDirection Direction { get => (UILayoutDirection)NativeBridge.GetRegisteredInt32(Entity, TypeId, DirectionId, "UILayoutGroup.Direction"); set => NativeBridge.SetRegisteredInt32(Entity, TypeId, DirectionId, (int)value, "UILayoutGroup.Direction"); }
	public float Spacing { get => NativeBridge.GetRegisteredFloat(Entity, TypeId, SpacingId, "UILayoutGroup.Spacing"); set => NativeBridge.SetRegisteredFloat(Entity, TypeId, SpacingId, value, "UILayoutGroup.Spacing"); }
	/// <summary>Left, bottom, right and top padding.</summary>
	public Vector4 Padding { get => NativeBridge.GetRegisteredVector4(Entity, TypeId, PaddingId, "UILayoutGroup.Padding"); set => NativeBridge.SetRegisteredVector4(Entity, TypeId, PaddingId, value, "UILayoutGroup.Padding"); }
	public bool ControlChildSize { get => Bool(ControlChildSizeId, "ControlChildSize"); set => Bool(ControlChildSizeId, value, "ControlChildSize"); }
	public Vector2 ChildSize { get => NativeBridge.GetRegisteredVector2(Entity, TypeId, ChildSizeId, "UILayoutGroup.ChildSize"); set => NativeBridge.SetRegisteredVector2(Entity, TypeId, ChildSizeId, value, "UILayoutGroup.ChildSize"); }
	private bool Bool(ulong id, string name) => NativeBridge.GetRegisteredBool(Entity, TypeId, id, $"UILayoutGroup.{name}");
	private void Bool(ulong id, bool value, string name) => NativeBridge.SetRegisteredBool(Entity, TypeId, id, value, $"UILayoutGroup.{name}");
}
