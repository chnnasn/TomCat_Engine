using TomCat;

namespace Game;

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
	private static Entity? s_crossSceneJointTarget;
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
    // The scene host replaces this null-forgiving CLR default with the invalid
    // zero Entity handle before applying serialized values.
    public Entity Target = null!;
	public Entity ReparentParent = null!;
    public AssetRef<Texture2DAsset> Texture;
	public SceneAsset NextScene;
	public PrefabAsset BulletPrefab;
	public TestMode Mode = TestMode.One;
	public WideMode WideMode = WideMode.HighBit;

    [HideInInspector] public long HiddenValue = 4;
    public double Weight = 1.5;
    public string Label = "initial";
    public bool Armed = true;
    public bool DisableOnCollisionEnter;
    public bool RemoveOnCollisionEnter;
	public bool DisableTargetOnUpdate;
	public bool DisableTargetOnFixedUpdate;
	public bool DisableTargetOnCollisionEnter;
	public bool ReparentTargetOnUpdate;
	public bool DisableBehaviourOnCreate;
	public bool DisableEntityOnCreate;
	public bool RemoveOnCreate;
	public bool DestroyEntityOnCreate;
	public bool DisableTargetOnCreate;
	public bool DisableSelfOnEnable;
	public bool EnableSelfOnDisable;
	public bool EnableBehaviourOnDisable;
	public bool RemoveOnDestroy;
	public bool EnableInputActionProbe;
	public bool DisableInputActionMapOnFixedUpdate;
	public bool DisableInputActionMapOnStarted;
	public bool DisableInputActionMapWhenPressedInUpdate;
	public bool ThrowOnInputActionCanceled;
	public bool QueueNameThenThrowOnUpdate;
	public bool QueueNameThenCatchNullTagOnUpdate;
	public bool CaptureCrossSceneJointTargetOnCreate;
	public bool QueueNameThenCatchCrossSceneJointOnUpdate;
	public bool DomainCancellationCanBeCanceled;
	public int ObservedStaticCreateSequence;

    public int Creates;
    public int Enables;
    public int Updates;
    public int LateUpdates;
    public int FixedUpdates;
    public int CollisionEnters;
    public int TriggerExits;
	[HideInInspector] public long FixedInputFirstSequence;
	[HideInInspector] public int FixedInputEventCount;
	[HideInInspector] public long CollisionInputFirstSequence;
	[HideInInspector] public int CollisionInputEventCount;
	[HideInInspector] public bool CollisionObservedFixedTimeline;
	[HideInInspector] public int CollisionActionPressedObservations;
	[HideInInspector] public int CollisionActionReleasedObservations;
	[HideInInspector] public int UpdateActionHeldObservations;
	[HideInInspector] public int UpdateActionPressedObservations;
	[HideInInspector] public int UpdateActionReleasedObservations;
	[HideInInspector] public int UpdateActionStartedEvents;
	[HideInInspector] public int UpdateActionPerformedEvents;
	[HideInInspector] public int UpdateActionCanceledEvents;
	[HideInInspector] public int UpdateAxisCanceledEvents;
	[HideInInspector] public string InputCancellationTrace = string.Empty;
	[HideInInspector] public int FixedActionHeldObservations;
	[HideInInspector] public int FixedActionPressedObservations;
	[HideInInspector] public int FixedActionReleasedObservations;
	[HideInInspector] public int FixedActionStartedEvents;
	[HideInInspector] public int FixedActionPerformedEvents;
	[HideInInspector] public int FixedActionCanceledEvents;
	[HideInInspector] public int UpdateAxisPerformedEvents;
	[HideInInspector] public int FixedAxisPerformedEvents;
	[HideInInspector] public bool TargetActiveInHierarchyAfterMutation = true;
	[HideInInspector] public int CaughtMutationValidationExceptions;
    public int Disables;
    public int Destroys;
	private InputActionMap? _inputActionMap;
	private InputAction? _inputAction;
	private InputAction? _inputAxis;

    public float Speed => _speed;

	protected override void OnCreate()
	{
		Creates++;
		ObservedStaticCreateSequence = ++StaticCreates;
		DomainCancellationCanBeCanceled =
			ScriptRuntime.DomainCancellationToken.CanBeCanceled;
		if (EnableInputActionProbe)
		{
			_inputActionMap = new InputActionMap("Regression.FixedInput");
			_inputAction = _inputActionMap.AddAction("Jump")
				.AddBinding(InputBinding.Mouse(MouseButton.Left));
			_inputAction.Started += _ =>
			{
				if (Time.InFixedUpdate)
					FixedActionStartedEvents++;
				else
					UpdateActionStartedEvents++;
				if (DisableInputActionMapOnStarted)
				{
					DisableInputActionMapOnStarted = false;
					_inputActionMap?.Disable();
				}
			};
			_inputAction.Performed += _ =>
			{
				if (Time.InFixedUpdate)
					FixedActionPerformedEvents++;
				else
					UpdateActionPerformedEvents++;
			};
			_inputAction.Canceled += _ =>
			{
				if (Time.InFixedUpdate)
					FixedActionCanceledEvents++;
				else
					UpdateActionCanceledEvents++;
				InputCancellationTrace += "Jump>";
				if (ThrowOnInputActionCanceled)
					throw new InvalidOperationException(
						"intentional deferred cancellation regression failure");
			};
			_inputAxis = _inputActionMap.AddAction("Move", InputActionType.Axis1D)
				.AddBinding(InputBinding.Mouse(MouseButton.Left));
			_inputAxis.Performed += _ =>
			{
				if (Time.InFixedUpdate)
					FixedAxisPerformedEvents++;
				else
					UpdateAxisPerformedEvents++;
			};
			_inputAxis.Canceled += _ =>
			{
				UpdateAxisCanceledEvents++;
				InputCancellationTrace += "Axis";
			};
			_inputActionMap.Enable();
		}
		if (DisableBehaviourOnCreate)
			Enabled = false;
		if (DisableEntityOnCreate)
			Entity.ActiveSelf = false;
		if (RemoveOnCreate)
			RemoveFromEntity();
		if (DestroyEntityOnCreate)
			Entity.Destroy();
		if (DisableTargetOnCreate)
			Target.ActiveSelf = false;
		if (CaptureCrossSceneJointTargetOnCreate)
			s_crossSceneJointTarget = Entity;
	}
    protected override void OnEnable()
	{
		Enables++;
		if (DisableSelfOnEnable)
			Entity.ActiveSelf = false;
	}
    protected override void OnUpdate(float deltaTime)
	{
		Updates++;
		ObserveInputAction(false);
		if (QueueNameThenThrowOnUpdate)
		{
			QueueNameThenThrowOnUpdate = false;
			Entity.Name = "pending-name-before-managed-failure";
			throw new InvalidOperationException(
				"intentional managed callback transaction failure");
		}
		if (QueueNameThenCatchNullTagOnUpdate)
		{
			QueueNameThenCatchNullTagOnUpdate = false;
			Entity.Name = "pending-name-before-validation-failure";
			try
			{
				Entity.Tag = null!;
			}
			catch (ArgumentNullException)
			{
				CaughtMutationValidationExceptions++;
			}
		}
		if (QueueNameThenCatchCrossSceneJointOnUpdate)
		{
			QueueNameThenCatchCrossSceneJointOnUpdate = false;
			Entity.Name = "pending-name-before-joint-validation-failure";
			try
			{
				Entity.GetComponent<DistanceJoint2D>().ConnectedEntity =
					s_crossSceneJointTarget
					?? throw new InvalidOperationException(
						"Cross-scene joint target was not captured.");
			}
			catch (ArgumentException)
			{
				CaughtMutationValidationExceptions++;
			}
		}
		if (DisableInputActionMapWhenPressedInUpdate
			&& _inputAction?.IsPressed == true)
		{
			DisableInputActionMapWhenPressedInUpdate = false;
			_inputActionMap?.Disable();
		}
		if (DisableTargetOnUpdate)
		{
			Target.ActiveSelf = false;
			TargetActiveInHierarchyAfterMutation = Target.ActiveInHierarchy;
		}
		if (ReparentTargetOnUpdate)
		{
			Target.Parent = ReparentParent;
			TargetActiveInHierarchyAfterMutation = Target.ActiveInHierarchy;
		}
	}
    protected override void OnLateUpdate(float deltaTime) => LateUpdates++;
	protected override void OnFixedUpdate(float fixedDeltaTime)
	{
		FixedUpdates++;
		InputEventBatch input = Input.EventBatch;
		FixedInputFirstSequence = unchecked((long)input.FirstSequence);
		FixedInputEventCount = input.Events.Count;
		ObserveInputAction(true);
		if (DisableInputActionMapOnFixedUpdate)
		{
			DisableInputActionMapOnFixedUpdate = false;
			_inputActionMap?.Disable();
		}
		if (DisableTargetOnFixedUpdate)
			Target.ActiveSelf = false;
	}
    protected override void OnCollisionEnter2D(Collision2D collision)
    {
        CollisionEnters++;
		InputEventBatch input = Input.EventBatch;
		CollisionInputFirstSequence = unchecked((long)input.FirstSequence);
		CollisionInputEventCount = input.Events.Count;
		CollisionObservedFixedTimeline = Time.InFixedUpdate;
		if (_inputAction?.WasPressedThisFrame == true)
			CollisionActionPressedObservations++;
		if (_inputAction?.WasReleasedThisFrame == true)
			CollisionActionReleasedObservations++;
		if (DisableTargetOnCollisionEnter)
			Target.ActiveSelf = false;
        if (DisableOnCollisionEnter)
            Enabled = false;
        if (RemoveOnCollisionEnter)
            RemoveFromEntity();
    }
    protected override void OnTriggerExit2D(Trigger2D trigger) => TriggerExits++;
	protected override void OnDisable()
	{
		Disables++;
		if (EnableSelfOnDisable)
			Entity.ActiveSelf = true;
		if (EnableBehaviourOnDisable)
			Enabled = true;
	}
	protected override void OnDestroy()
	{
		_inputActionMap?.Disable();
		Destroys++;
		if (RemoveOnDestroy)
			RemoveFromEntity();
	}

	private void ObserveInputAction(bool fixedStep)
	{
		if (_inputAction is null)
			return;
		if (_inputAction.IsPressed)
		{
			if (fixedStep)
				FixedActionHeldObservations++;
			else
				UpdateActionHeldObservations++;
		}
		if (_inputAction.WasPressedThisFrame)
		{
			if (fixedStep)
				FixedActionPressedObservations++;
			else
				UpdateActionPressedObservations++;
		}
		if (_inputAction.WasReleasedThisFrame)
		{
			if (fixedStep)
				FixedActionReleasedObservations++;
			else
				UpdateActionReleasedObservations++;
		}
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
