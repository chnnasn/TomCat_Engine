using TomCat;

namespace Game;

public sealed class TextureAsset;

[DefaultExecutionOrder(-100)]
[DisallowMultipleComponent]
public sealed class GoodBehaviour : TomCatBehaviour
{
	// The host supports non-public parameterless constructors and compiles the
	// access once while loading the collectible project assembly.
	internal GoodBehaviour()
	{
	}

	public static int StaticCreates;
    [SerializeField]
    [Header("Movement")]
    [Tooltip("Units moved per second.")]
    [Range(0.0f, 100.0f)]
    private float _speed = 12.0f;

    [FormerlySerializedAs("_oldCount")]
    public int Count = 3;
    public Vector2 Spawn = new(1.0f, 2.0f);
    public Vector3 Direction = new(3.0f, 4.0f, 5.0f);
    public Vector4 Mask = new(6.0f, 7.0f, 8.0f, 9.0f);
    public Color Tint = Color.White;
    public Entity Target;
    public AssetRef<TextureAsset> Texture;
	public TestMode Mode = TestMode.One;
	public WideMode WideMode = WideMode.HighBit;

    [HideInInspector] public long HiddenValue = 4;
    public double Weight = 1.5;
    public string Label = "initial";
    public bool Armed = true;
    public bool DisableOnCollisionEnter;
    public bool RemoveOnCollisionEnter;
	public bool RemoveOnDestroy;
	public bool DomainCancellationCanBeCanceled;
	public int ObservedStaticCreateSequence;

    public int Creates;
    public int Enables;
    public int Updates;
    public int FixedUpdates;
    public int CollisionEnters;
    public int TriggerExits;
    public int Disables;
    public int Destroys;

    public float Speed => _speed;

	protected override void OnCreate()
	{
		Creates++;
		ObservedStaticCreateSequence = ++StaticCreates;
		DomainCancellationCanBeCanceled =
			ScriptRuntime.DomainCancellationToken.CanBeCanceled;
	}
    protected override void OnEnable() => Enables++;
    protected override void OnUpdate(float deltaTime) => Updates++;
    protected override void OnFixedUpdate(float fixedDeltaTime) => FixedUpdates++;
    protected override void OnCollisionEnter2D(Collision2D collision)
    {
        CollisionEnters++;
        if (DisableOnCollisionEnter)
            Enabled = false;
        if (RemoveOnCollisionEnter)
            RemoveFromEntity();
    }
    protected override void OnTriggerExit2D(Trigger2D trigger) => TriggerExits++;
    protected override void OnDisable() => Disables++;
	protected override void OnDestroy()
	{
		Destroys++;
		if (RemoveOnDestroy)
			RemoveFromEntity();
	}
}

public enum TestMode : long
{
    Zero = 0,
    One = 1,
    Two = 2
}

public enum WideMode : ulong
{
	Zero = 0,
	HighBit = 0xffffffffffffffffUL
}
