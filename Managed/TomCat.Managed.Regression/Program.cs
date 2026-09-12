using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using TomCat;
using TomCat.Interop;
using TomCat.ScriptHost;

namespace TomCat.Managed.Regression;

internal static unsafe class Program
{
    private const ulong SceneSession = 11;
    private const ulong RuntimeGeneration = 7;
	private const ulong ActiveSceneHandle = 7001;
	private const int ActiveSceneBuildIndex = 2;
	private const ulong MetadataReceiverSentinel = 0xC0DEC0DE5A17UL;
	private const string ConstructorGuardMessage =
		"TomCat engine APIs cannot be used from a script constructor or field initializer. Use OnCreate or another lifecycle callback.";
	private const string WrongThreadMessage =
		"WrongThread: TomCat engine APIs may only be used from the main thread.";
	private static int s_diagnostics;
	private static ulong s_removedAttachment;
	private static ulong s_metadataReceiverToken;
	private static string? s_metadataReceiverJson;
	private static int s_entityTextSetterCalls;
	private static int s_entityLayerSetterCalls;
	private static NativeVector3 s_lastTransformPosition;
	private static NativeVector2 s_lastRigidbodyVelocity;
	private static ulong s_requestedSceneHandle;
	private static int s_requestedSceneIndex;
	private static int s_sceneReloadRequests;
	private static ulong s_requestedPrefabHandle;
	private static NativeEntityHandleV1 s_requestedPrefabParent;
	private static NativeVector3 s_requestedPrefabPosition;
	private static bool s_extendedInputPressed;
	private static float s_extendedLeftTriggerRaw = -1.0f;
	private static bool s_healthPresent;
	private static int s_healthMaximum;
	private static int s_healthCurrent;
	private static bool s_healthInvulnerable;
	private static int s_componentHasCalls;
	private static int s_componentAddCalls;
	private static int s_componentRemoveCalls;
	private static int s_componentGetCalls;
	private static int s_componentSetCalls;
	private static NativeEntityHandleV1 s_reservedGameplayEntity;
	private static NativeEntityHandleV1 s_gameplayParent;
	private static NativeVector3 s_localTransformPosition;
	private static bool s_gameplayActive = true;
	private static bool s_usePerEntityGameplayActivation;
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), bool> s_gameplayActiveByEntity = [];
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), NativeEntityHandleV1> s_gameplayParentByEntity = [];
	private static int s_gameplayQueryCalls;
	private static int s_lastGameplayQueryComponent;
	private static int s_spriteAnimatorHasCalls;
	private static int s_spriteAnimatorAddCalls;
	private static int s_spriteAnimatorRemoveCalls;
	private static int s_spriteAnimatorPlayCalls;
	private static int s_spriteAnimatorStopCalls;
	private static string? s_lastSpriteAnimatorClip;
	private static bool s_lastSpriteAnimatorRestart;
	private static int s_spriteAnimatorParameterCalls;
	private static readonly Dictionary<string, object> s_animatorParameters = [];
	private static bool s_audioStreaming;
	private static float s_audioSpatialBlend;
	private static float s_audioMinDistance = 1.0f;
	private static float s_audioMaxDistance = 25.0f;
	private static string s_runtimeUIText = "Ready";
	private static bool s_runtimeUIButtonFocused;
	private static bool s_runtimeUICaptured;
	private const ulong RuntimeUIButtonClickSerial = 41;
	private static readonly Dictionary<(ulong Component, ulong Property),
		NativePropertyValueV1> s_registeredRuntimeUIProperties = [];
	private static readonly Dictionary<(int Component, uint Property),
		NativePropertyValueV1> s_gameplayProperties = [];
	private static readonly UTF8Encoding s_strictUtf8 = new(false, true);
	private static readonly int s_mainManagedThread = Environment.CurrentManagedThreadId;

    private static int Main()
    {
        try
		{
			VerifyDescriptorConstructorFactory();
			VerifyConstructorGuard();

			string fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixture");
			string fixtureAssemblyPath = Path.Combine(fixtureDirectory, "Assembly-CSharp.dll");
			byte[] assembly = File.ReadAllBytes(fixtureAssemblyPath);
            string pdbPath = Path.Combine(fixtureDirectory, "Assembly-CSharp.pdb");
            byte[] pdb = File.Exists(pdbPath) ? File.ReadAllBytes(pdbPath) : [];
			string fixtureBDirectory = Path.Combine(AppContext.BaseDirectory, "FixtureB");
			byte[] assemblyB = File.ReadAllBytes(
				Path.Combine(fixtureBDirectory, "Assembly-CSharp.dll"));
			string pdbBPath = Path.Combine(fixtureBDirectory, "Assembly-CSharp.pdb");
			byte[] pdbB = File.Exists(pdbBPath) ? File.ReadAllBytes(pdbBPath) : [];

			VerifyBootstrapAbi(assembly, pdb);
			ScriptDomain primary = BeginRuntimeUnload(assembly, pdb);
			Check(PollUntilUnloaded(primary), "primary collectible ALC leaked");
			ScriptDomain projectB = BeginProjectBSwitchUnload(assemblyB, pdbB);
			Check(PollUntilUnloaded(projectB), "project B collectible ALC leaked");
			ScriptDomain staticReset = BeginRuntimeUnload(assembly, pdb);
			Check(PollUntilUnloaded(staticReset),
				"second Play Domain collectible ALC leaked");
			Entity? previousCycleEntity = null;
			for (int index = 0; index < 100; ++index)
			{
				ScriptDomain play = BeginPlayCycleUnload(assembly, pdb, index,
					out Entity cycleEntity);
				if (previousCycleEntity is not null)
				{
					Check(previousCycleEntity != cycleEntity &&
						previousCycleEntity.RuntimeGeneration != cycleEntity.RuntimeGeneration,
						$"Play Domain cycle {index + 1} reused an old Entity identity");
				}
				previousCycleEntity = cycleEntity;
				Check(PollUntilUnloaded(play),
					$"Play Domain collectible ALC cycle {index + 1} leaked");
				VerifyFileUnlocked(fixtureAssemblyPath,
					$"Play Domain cycle {index + 1} kept Assembly-CSharp.dll locked");
			}

            Console.WriteLine("TomCat.Managed regression suite passed.");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine(exception);
            return 1;
        }
    }

    private static void VerifyConstructorGuard() =>
        Throws<InvalidOperationException>(static () => _ = new ConstructorProbe(),
            "TomCat API use in a script constructor must be rejected");

	private static void VerifyDescriptorConstructorFactory()
	{
		PropertyInfo[] properties = typeof(ScriptDescriptor).GetProperties(
			BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
		PropertyInfo? factory = properties.SingleOrDefault(property =>
			property.Name == nameof(ScriptDescriptor.ConstructorFactory));
		Check(factory?.PropertyType == typeof(Func<TomCatBehaviour>),
			"ScriptDescriptor must cache a strongly typed constructor factory");
		Check(properties.All(property => property.PropertyType != typeof(ConstructorInfo)),
			"ScriptDescriptor must not retain ConstructorInfo for runtime instantiation");
	}

	private static NativeApiV1 CreateCompleteNativeApi() => new()
	{
		Version = ManagedAbi.NativeApiVersion,
		Size = (uint)sizeof(NativeApiV1),
		Log = &StubLog,
		EmitDiagnostic = &CaptureDiagnostic,
		IsMainThread = &IsMainThread,
		EntityIsAlive = &StubEntityAlive,
		EntityGetName = &StubGetEntityText,
		EntitySetName = &StubSetEntityText,
		EntityGetTag = &StubGetEntityText,
		EntitySetTag = &StubSetEntityText,
		EntityGetLayer = &StubGetEntityLayer,
		EntitySetLayer = &StubSetEntityLayer,
		DestroyEntityDeferred = &StubEntityStatus,
		HasComponent = &StubHasComponent,
		AddComponentDeferred = &StubAddEntityComponent,
		RemoveComponentDeferred = &StubRemoveEntityComponent,
		TransformGetPosition = &StubGetTransformVector,
		TransformSetPosition = &StubSetTransformVector,
		TransformGetRotationEuler = &StubGetTransformVector,
		TransformSetRotationEuler = &StubSetTransformVector,
		TransformGetScale = &StubGetTransformVector,
		TransformSetScale = &StubSetTransformVector,
		TransformGetWorldMatrix = &StubGetWorldMatrix,
		InputIsKeyHeld = &StubInputKey,
		InputWasKeyPressed = &StubInputKey,
		InputWasKeyReleased = &StubInputKey,
		InputGetMousePosition = &StubInputVector,
		InputGetMouseDelta = &StubInputVector,
		InputGetModifiers = &StubInputModifiers,
		RigidbodyGetLinearVelocity = &StubGetRigidbodyVector,
		RigidbodySetLinearVelocity = &StubSetRigidbodyVector,
		RigidbodyApplyForce = &StubSetRigidbodyVector,
		RigidbodyApplyLinearImpulse = &StubSetRigidbodyVector,
		PhysicsRaycast = &StubPhysicsRaycast,
		PhysicsQueryAabb = &StubPhysicsQueryAabb,
		AssetIsValid = &StubAssetIsValid,
		AssetGetType = &StubAssetGetType,
		BehaviourGetEnabled = &StubBehaviourGetEnabled,
		BehaviourSetEnabledDeferred = &SetBehaviourEnabled,
		BehaviourRemoveDeferred = &RemoveBehaviour,
		SceneGetActiveHandle = &StubSceneGetActiveHandle,
		SceneGetActiveBuildIndex = &StubSceneGetActiveBuildIndex,
		SceneRequestLoadHandle = &StubSceneRequestLoadHandle,
		SceneRequestLoadIndex = &StubSceneRequestLoadIndex,
		SceneRequestReload = &StubSceneRequestReload,
		PrefabInstantiateDeferred = &StubPrefabInstantiate
	};

    private static void VerifyBootstrapAbi(byte[] assembly, byte[] pdb)
    {
		NativeApiV1 native = CreateCompleteNativeApi();
		ManagedApiV1 managed = new()
		{
			Version = ManagedAbi.ManagedApiVersion,
			Size = (uint)sizeof(ManagedApiV1)
		};
        delegate* unmanaged[Cdecl]<NativeApiV1*, ManagedApiV1*, int> bootstrap =
            &EntryPoint.GetManagedApi;
        int status = bootstrap(&native, &managed);

        Equal(0, status, "GetManagedApi status");
        Equal(ManagedAbi.ManagedApiVersion, managed.Version, "Managed API version");
        Equal((uint)sizeof(ManagedApiV1), managed.Size, "Managed API size");
		Check(managed.CreateDomain != null && managed.LoadProjectAssembly != null &&
            managed.ReadScriptMetadata != null && managed.CreateSceneRuntime != null &&
            managed.InstantiateAll != null && managed.ApplySerializedFields != null &&
            managed.InvokeCreateAll != null && managed.SetEnabled != null &&
            managed.UpdateAll != null && managed.FixedUpdateAll != null &&
			managed.DispatchPhysicsEvents != null && managed.DestroyAll != null &&
			managed.BeginUnloadDomain != null && managed.PollUnload != null &&
			managed.DestroyAttachments != null && managed.InstantiateAttachments != null,
			"GetManagedApi must populate every V1 export");
		VerifyOptionalInputCapability(native, bootstrap);
		VerifyNaturalProxySyntax();
		VerifyMetadataReceiverToken(managed, assembly, pdb);

		NativeApiV1 missingRequiredCallback = native;
		missingRequiredCallback.PhysicsRaycast = null;
		missingRequiredCallback.BehaviourRemoveDeferred = &PoisonRemoveBehaviour;
		ManagedApiV1 rejectedOutput = new()
		{
			Version = ManagedAbi.ManagedApiVersion,
			Size = (uint)sizeof(ManagedApiV1)
		};
		Equal(-8, bootstrap(&missingRequiredCallback, &rejectedOutput),
			"GetManagedApi must reject a missing required NativeApiV1 callback");
		Check(rejectedOutput.CreateDomain == null,
			"a rejected NativeApiV1 table must not partially populate ManagedApiV1");

		s_removedAttachment = 0;
		var removalProbe = new RemovalProbe();
		removalProbe.__Bind(new Entity(SceneSession, 9, RuntimeGeneration),
			new ScriptInstanceHandle(777));
		removalProbe.__Create();
		Equal(777UL, s_removedAttachment, "deferred behaviour removal handle");

		ManagedApiV1 undersized = new()
		{
			Version = ManagedAbi.ManagedApiVersion,
			Size = (uint)sizeof(ManagedApiV1) - (uint)sizeof(nint)
		};
		Equal(-4, bootstrap(&native, &undersized),
			"GetManagedApi must reject an undersized output table without overwriting it");
		int diagnosticsBeforeBackgroundFailure = s_diagnostics;
		nint loadProjectAssembly = (nint)managed.LoadProjectAssembly;
		int backgroundStatus = Task.Run(() => InvokeMissingDomain(
			loadProjectAssembly)).GetAwaiter().GetResult();
		Equal(-3, backgroundStatus,
			"background managed export failure status");
		Equal(diagnosticsBeforeBackgroundFailure + 1, s_diagnostics,
			"background managed export failure diagnostic");
		ThrowsWithMessage<InvalidOperationException>(
			static () => _ = new ConstructorInputProbe(), ConstructorGuardMessage,
			"Input use in a script constructor must retain the constructor guard");
		Throws<InvalidOperationException>(static () => _ = new ConstructorLogProbe(),
			"Log use in a script constructor must be rejected");
		VerifyBackgroundThreadApiGuards();
    }

	private static void VerifyOptionalInputCapability(NativeApiV1 native,
		delegate* unmanaged[Cdecl]<NativeApiV1*, ManagedApiV1*, int> bootstrap)
	{
		NativeApiV2 envelope = new()
		{
			V1 = native,
			QueryCapability = &QueryTestCapability
		};
		envelope.V1.Size = (uint)sizeof(NativeApiV2);
		ManagedApiV1 managed = new()
		{
			Version = ManagedAbi.ManagedApiVersion,
			Size = (uint)sizeof(ManagedApiV1)
		};
		Equal(0, bootstrap(&envelope.V1, &managed),
			"GetManagedApi with NativeApiV2 envelope");

		byte[] missingName = Encoding.UTF8.GetBytes("TomCat.MissingApiV1");
		byte[] inputName = Encoding.UTF8.GetBytes("TomCat.InputApiV1");
		byte[] componentName = Encoding.UTF8.GetBytes("TomCat.ComponentApiV1");
		byte[] gameplayName = Encoding.UTF8.GetBytes("TomCat.GameplayApiV1");
		byte[] audioSpatialName = Encoding.UTF8.GetBytes("TomCat.AudioSpatialApiV1");
		byte[] runtimeUIName = Encoding.UTF8.GetBytes("TomCat.RuntimeUIApiV1");
		fixed (byte* missingPointer = missingName)
		fixed (byte* inputPointer = inputName)
		fixed (byte* componentPointer = componentName)
		fixed (byte* gameplayPointer = gameplayName)
		fixed (byte* audioSpatialPointer = audioSpatialName)
		fixed (byte* runtimeUIPointer = runtimeUIName)
		{
			uint required = 123;
			Equal(-3, envelope.QueryCapability(
				new NativeUtf8View(missingPointer, (ulong)missingName.Length), 1,
				null, 0, &required), "unknown capability status");
			Equal(0U, required, "unknown capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(inputPointer, (ulong)inputName.Length), 2,
				null, 0, &required), "newer input capability version rejection");
			Equal((uint)sizeof(NativeInputApiV1), required,
				"input capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(componentPointer, (ulong)componentName.Length), 2,
				null, 0, &required), "newer component capability version rejection");
			Equal((uint)sizeof(NativeComponentApiV1), required,
				"component capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(gameplayPointer, (ulong)gameplayName.Length), 2,
				null, 0, &required), "newer gameplay capability version rejection");
			Equal((uint)sizeof(NativeGameplayApiV1), required,
				"gameplay capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(audioSpatialPointer,
					(ulong)audioSpatialName.Length), 2, null, 0, &required),
				"newer audio spatial capability version rejection");
			Equal((uint)sizeof(NativeAudioSpatialApiV1), required,
				"audio spatial capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(runtimeUIPointer,
					(ulong)runtimeUIName.Length), 2, null, 0, &required),
				"newer runtime UI capability version rejection");
			Equal((uint)sizeof(NativeRuntimeUIApiV1), required,
				"runtime UI capability required size");
		}

		var audioProbe = new AudioSpatialProxyProbe();
		audioProbe.__Bind(new Entity(SceneSession, 92, RuntimeGeneration),
			new ScriptInstanceHandle(780), CancellationToken.None);
		audioProbe.__Create();
		Check(audioProbe.Passed,
			"AudioSpatialApiV1 C# property proxy round-trip");

		s_runtimeUIText = "Ready";
		s_runtimeUIButtonFocused = false;
		s_runtimeUICaptured = true;
		s_registeredRuntimeUIProperties.Clear();
		var runtimeUIProbe = new RuntimeUIProxyProbe();
		runtimeUIProbe.__Bind(new Entity(SceneSession, 93, RuntimeGeneration),
			new ScriptInstanceHandle(781), CancellationToken.None);
		runtimeUIProbe.__Create();
		Check(runtimeUIProbe.Passed && s_runtimeUIButtonFocused,
			"RuntimeUIApiV1 text, button, focus and rect proxy round-trip");
		s_runtimeUICaptured = false;

		var probe = new ExtendedInputProbe();
		probe.__Bind(new Entity(SceneSession, 91, RuntimeGeneration),
			new ScriptInstanceHandle(779), CancellationToken.None);
		s_extendedInputPressed = false;
		s_extendedLeftTriggerRaw = -1.0f;
		probe.__Create();
		s_extendedInputPressed = true;
		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.StartedCount, "InputAction Started count");
		Equal(1, probe.PerformedCount, "InputAction Performed count");
		Check(probe.Action.IsPressed && probe.Action.WasPressedThisFrame,
			"InputAction press state");
		Check(!probe.AlternativeButton.IsPressed
			&& MathF.Abs(probe.AlternativeButton.Value - 0.3f) < 1.0e-6f,
			"Button alternatives use strongest binding without summing");
		Check(MathF.Abs(probe.Axis.Value - 0.7f) < 1.0e-6f
			&& MathF.Abs(probe.ClampedAxis.Value - 1.0f) < 1.0e-6f,
			"Axis1D bindings aggregate and clamp");
		Equal(1, probe.AxisStartedCount, "Axis1D Started count");
		Equal(1, probe.AxisPerformedCount, "Axis1D initial Performed count");

		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.PerformedCount,
			"Button Performed only fires on the press threshold crossing");
		Equal(2, probe.AxisPerformedCount,
			"actuated Axis1D performs continuously");

		s_runtimeUICaptured = true;
		probe.RequestRuntimeCaptureTest = true;
		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.CanceledCount,
			"runtime UI capture cancels an active Gameplay action");
		Equal(1, probe.AxisCanceledCount,
			"runtime UI capture cancels an active Gameplay axis");
		Equal(1, probe.UiStartedCount,
			"UI context continues while runtime UI captures Gameplay");

		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.StartedCount,
			"held UI-owned input must not restart its Gameplay action");
		Equal(1, probe.PerformedCount,
			"held UI-owned input must not perform in Gameplay");
		Equal(1, probe.CanceledCount,
			"held UI-owned input must not repeatedly cancel Gameplay");

		s_extendedInputPressed = false;
		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.StartedCount,
			"UI-owned release frame must not restart Gameplay");
		Equal(1, probe.PerformedCount,
			"UI-owned release frame must not perform in Gameplay");
		Equal(1, probe.UiCanceledCount,
			"UI context must observe the owned control release");

		s_runtimeUICaptured = false;
		probe.RequestRuntimeCaptureRestore = true;
		probe.__Update(1.0f / 60.0f);
		Equal(1, probe.StartedCount,
			"Gameplay must remain idle after Runtime UI releases ownership");
		Equal(1, probe.PerformedCount,
			"released Runtime UI ownership must not synthesize a Gameplay press");
		Equal(2, probe.AxisStartedCount,
			"Gameplay axis restarts after runtime UI releases capture");
		Equal(1, probe.UiCanceledCount,
			"runtime capture test disables its temporary UI context");

		s_extendedInputPressed = true;
		probe.__Update(1.0f / 60.0f);
		Equal(2, probe.StartedCount,
			"next independent press starts Gameplay after UI ownership ended");
		Equal(2, probe.PerformedCount,
			"next independent press performs Gameplay exactly once");

		probe.RequestUiExclusive = true;
		probe.__Update(1.0f / 60.0f);
		Equal(2, probe.CanceledCount,
			"exclusive UI context cancels Gameplay action");
		Equal(2, probe.UiStartedCount, "exclusive UI context starts UI action");
		Equal(2, probe.AxisCanceledCount,
			"exclusive UI context cancels Gameplay axis");

		probe.RequestClearExclusive = true;
		probe.__Update(1.0f / 60.0f);
		Equal(2, probe.StartedCount,
			"higher-priority UI consumes shared Gameplay control");
		Equal(3, probe.AxisStartedCount,
			"clearing exclusive context re-enables unconsumed Gameplay axis");

		probe.RequestGameplayResume = true;
		probe.__Update(1.0f / 60.0f);
		Equal(3, probe.StartedCount,
			"explicit Gameplay re-enable restarts held action");
		Equal(3, probe.PerformedCount,
			"re-enabled Button performs once on its new threshold crossing");
		Equal(2, probe.UiCanceledCount,
			"disabling UI context cancels its held action");

		s_extendedInputPressed = false;
		probe.__Update(1.0f / 60.0f);
		Equal(3, probe.CanceledCount, "InputAction release Canceled count");
		Check(!probe.Action.IsPressed && probe.Action.WasReleasedThisFrame,
			"InputAction release state");
		probe.__Destroy();
	}

	private static void VerifyNaturalProxySyntax()
	{
		s_entityTextSetterCalls = 0;
		s_entityLayerSetterCalls = 0;
		s_lastTransformPosition = default;
		s_lastRigidbodyVelocity = default;
		s_requestedSceneHandle = 0;
		s_requestedSceneIndex = -1;
		s_sceneReloadRequests = 0;
		s_requestedPrefabHandle = 0;
		s_requestedPrefabParent = default;
		s_requestedPrefabPosition = default;
		s_healthPresent = true;
		s_healthMaximum = 100;
		s_healthCurrent = 100;
		s_healthInvulnerable = false;
		s_componentHasCalls = 0;
		s_componentAddCalls = 0;
		s_componentRemoveCalls = 0;
		s_componentGetCalls = 0;
		s_componentSetCalls = 0;
		s_reservedGameplayEntity = default;
		s_gameplayParent = new NativeEntityHandleV1(SceneSession, 42, RuntimeGeneration);
		s_localTransformPosition = default;
		s_gameplayActive = true;
		s_gameplayQueryCalls = 0;
		s_lastGameplayQueryComponent = 0;
		s_spriteAnimatorHasCalls = 0;
		s_spriteAnimatorAddCalls = 0;
		s_spriteAnimatorRemoveCalls = 0;
		s_spriteAnimatorPlayCalls = 0;
		s_spriteAnimatorStopCalls = 0;
		s_lastSpriteAnimatorClip = null;
		s_lastSpriteAnimatorRestart = true;
		s_spriteAnimatorParameterCalls = 0;
		s_animatorParameters.Clear();
		s_gameplayProperties.Clear();

		var probe = new NaturalProxySyntaxProbe();
		probe.__Bind(new Entity(SceneSession, 8, RuntimeGeneration),
			new ScriptInstanceHandle(776));
		probe.__Create();

		Equal(2, s_entityTextSetterCalls,
			"Entity.Name and Entity.Tag direct setters");
		Equal(1, s_entityLayerSetterCalls, "Entity.Layer direct setter");
		Equal(1.0f, s_lastTransformPosition.X, "Transform.Position direct setter X");
		Equal(2.0f, s_lastTransformPosition.Y, "Transform.Position direct setter Y");
		Equal(3.0f, s_lastTransformPosition.Z, "Transform.Position direct setter Z");
		Equal(4.0f, s_lastRigidbodyVelocity.X,
			"GetComponent<Rigidbody2D>().LinearVelocity direct setter X");
		Equal(5.0f, s_lastRigidbodyVelocity.Y,
			"GetComponent<Rigidbody2D>().LinearVelocity direct setter Y");
		Equal(8001UL, s_requestedSceneHandle,
			"SceneManager.LoadScene(SceneAsset) request");
		Equal(4, s_requestedSceneIndex, "SceneManager.LoadScene(int) request");
		Equal(1, s_sceneReloadRequests, "SceneManager.ReloadActiveScene request");
		Equal(9001UL, s_requestedPrefabHandle,
			"TomCatBehaviour.Instantiate Prefab handle");
		Equal(8UL, s_requestedPrefabParent.EntityId,
			"TomCatBehaviour.Instantiate parent entity");
		Equal(6.0f, s_requestedPrefabPosition.X,
			"TomCatBehaviour.Instantiate world position X");
		Equal(4, s_componentHasCalls,
			"registry component generic Has/Get proxy checks");
		Equal(1, s_componentAddCalls, "registry component Add call");
		Equal(1, s_componentRemoveCalls, "registry component Remove call");
		Equal(3, s_componentGetCalls, "registry component property reads");
		Equal(3, s_componentSetCalls, "registry component property writes");
		Equal(150, s_healthMaximum, "HealthComponent.Maximum round trip");
		Equal(75, s_healthCurrent, "HealthComponent.Current round trip");
		Check(s_healthInvulnerable, "HealthComponent.Invulnerable round trip");
		Equal(1200UL, s_reservedGameplayEntity.EntityId,
			"World.CreateEntity reserved handle");
		Equal(44UL, s_gameplayParent.EntityId, "Entity.Parent deferred setter");
		Equal(9.0f, s_localTransformPosition.X,
			"Transform.LocalPosition direct setter X");
		Equal(3, s_gameplayQueryCalls,
			"World.All and both registered/native component queries");
		Equal((int)NativeComponentTypeV1.SpriteAnimator,
			s_lastGameplayQueryComponent, "World.Query<SpriteAnimator> component type");
		Equal(2, s_spriteAnimatorHasCalls, "SpriteAnimator Has/Get component calls");
		Equal(1, s_spriteAnimatorAddCalls, "SpriteAnimator Add component call");
		Equal(1, s_spriteAnimatorRemoveCalls, "SpriteAnimator Remove component call");
		Equal(1, s_spriteAnimatorPlayCalls, "SpriteAnimator Play call");
		Equal(1, s_spriteAnimatorStopCalls, "SpriteAnimator Stop call");
		Equal("运行😀", s_lastSpriteAnimatorClip,
			"SpriteAnimator UTF-8 clip name round trip");
		Check(!s_lastSpriteAnimatorRestart,
			"SpriteAnimator restart argument round trip");
		Equal(5, s_spriteAnimatorParameterCalls,
			"SpriteAnimator parameter API calls");
		Equal(7, s_animatorParameters["Lives"], "SpriteAnimator Int parameter");
		Equal(1.25f, s_animatorParameters["Speed"], "SpriteAnimator Float parameter");
		Equal(false, s_animatorParameters["Grounded"], "SpriteAnimator Bool parameter");
		Equal(false, s_animatorParameters["Jump"], "SpriteAnimator reset Trigger");
	}

	private static void VerifyMetadataReceiverToken(ManagedApiV1 managed, byte[] assembly,
		byte[] pdb)
	{
		ulong domainId = 0;
		Equal(0, managed.CreateDomain((int)ScriptDomainKind.Metadata, &domainId),
			"metadata ABI CreateDomain status");
		fixed (byte* assemblyPointer = assembly)
		fixed (byte* pdbPointer = pdb)
		{
			Equal(0, managed.LoadProjectAssembly(domainId,
				new NativeByteView(assemblyPointer, (ulong)assembly.Length),
				new NativeByteView(pdbPointer, (ulong)pdb.Length)),
				"metadata ABI LoadProjectAssembly status");
		}

		s_metadataReceiverToken = 0;
		s_metadataReceiverJson = null;
		Equal(0, managed.ReadScriptMetadata(domainId, &CaptureMetadata,
			MetadataReceiverSentinel), "metadata ABI ReadScriptMetadata status");
		Equal(MetadataReceiverSentinel, s_metadataReceiverToken,
			"metadata receiver token round trip");
		Check(!string.IsNullOrWhiteSpace(s_metadataReceiverJson),
			"metadata receiver must receive non-empty JSON");
		using (JsonDocument manifest = JsonDocument.Parse(s_metadataReceiverJson!))
		{
			Equal((int)ManagedAbi.ScriptManifestVersion,
				manifest.RootElement.GetProperty("version").GetInt32(),
				"metadata receiver manifest version");
			foreach (JsonElement script in manifest.RootElement
				.GetProperty("scripts").EnumerateArray())
			{
				JsonElement lifecycle = script.GetProperty("lifecycle");
				Equal(JsonValueKind.Number, lifecycle.ValueKind,
					"metadata receiver lifecycle must be a native ABI bitmask");
				Check(lifecycle.TryGetUInt32(out uint bits) && (bits & ~0x7ffU) == 0,
					"metadata receiver lifecycle contains invalid ABI bits");
				if (script.GetProperty("typeName").GetString() == "Game.GoodBehaviour")
				{
					JsonElement fields = script.GetProperty("fields");
					JsonElement target = script.GetProperty("fields").EnumerateArray()
						.Single(field => field.GetProperty("name").GetString() == "Target")
						.GetProperty("defaultValue");
					Equal(JsonValueKind.Number, target.ValueKind,
						"default Entity field must retain its UInt64 wire representation");
					Equal(0UL, target.GetUInt64(),
						"default Entity field must remain the invalid zero handle");
					JsonElement texture = fields.EnumerateArray().Single(field =>
						field.GetProperty("name").GetString() == "Texture");
					Equal("AssetRef", texture.GetProperty("type").GetString(),
						"Texture2D AssetRef generator field token");
					Equal("TomCat.AssetRef<TomCat.Texture2DAsset>",
						texture.GetProperty("typeName").GetString(),
						"Texture2D AssetRef generator type name");
					JsonElement nextScene = fields.EnumerateArray().Single(field =>
						field.GetProperty("name").GetString() == "NextScene");
					Equal("AssetRef", nextScene.GetProperty("type").GetString(),
						"SceneAsset generator field token");
					Equal("TomCat.SceneAsset", nextScene.GetProperty("typeName").GetString(),
						"SceneAsset generator type name");
					JsonElement bulletPrefab = fields.EnumerateArray().Single(field =>
						field.GetProperty("name").GetString() == "BulletPrefab");
					Equal("AssetRef", bulletPrefab.GetProperty("type").GetString(),
						"PrefabAsset generator field token");
					Equal("TomCat.PrefabAsset", bulletPrefab.GetProperty("typeName").GetString(),
						"PrefabAsset generator type name");
				}
			}
		}

		Equal(0, managed.BeginUnloadDomain(domainId),
			"metadata ABI BeginUnloadDomain status");
		bool unloaded = false;
		for (int attempt = 0; attempt < 100 && !unloaded; ++attempt)
		{
			int completed = 0;
			Equal(0, managed.PollUnload(domainId, &completed),
				"metadata ABI PollUnload status");
			unloaded = completed != 0;
		}
		Check(unloaded, "metadata ABI domain must unload");
	}

	private static void VerifyBackgroundThreadApiGuards()
	{
		var probe = new BackgroundThreadApiProbe();
		probe.__Bind(new Entity(SceneSession, 10, RuntimeGeneration),
			new ScriptInstanceHandle(778));
		probe.__Create();
	}

	private static void AssertWrongThread(Action action, string operation)
	{
		Exception? failure = null;
		try
		{
			Task.Run(action).GetAwaiter().GetResult();
		}
		catch (Exception exception)
		{
			failure = exception;
		}

		if (failure is not InvalidOperationException invalidOperation)
			throw new InvalidOperationException(
				$"{operation} from Task.Run must throw InvalidOperationException; got {failure?.GetType().FullName ?? "no exception"}.");
		Equal(WrongThreadMessage, invalidOperation.Message,
			$"{operation} wrong-thread diagnostic");
	}

    [MethodImpl(MethodImplOptions.NoInlining)]
	private static ScriptDomain BeginRuntimeUnload(byte[] assembly, byte[] pdb)
	{
		var domain = new ScriptDomain(ScriptDomainKind.Play);
		CancellationToken domainCancellation = domain.DomainCancellationToken;
        domain.LoadProjectAssembly(assembly, pdb);

        Equal(1u, domain.Manifest.Version, "manifest version");
        Equal(2, domain.Manifest.Scripts.Count, "script count");
        ScriptTypeManifest good = domain.Manifest.Scripts.Single(script => script.AssetHandle == 1001);
        ScriptTypeManifest faulty = domain.Manifest.Scripts.Single(script => script.AssetHandle == 1002);
        Equal("Game.GoodBehaviour", good.TypeName, "good type name");
        Equal(-100, good.ExecutionOrder, "execution order");
        Check(good.DisallowMultiple, "DisallowMultipleComponent metadata");
		Equal(ScriptLifecycle.Create | ScriptLifecycle.Enable |
			ScriptLifecycle.Update | ScriptLifecycle.FixedUpdate |
			ScriptLifecycle.CollisionEnter2D | ScriptLifecycle.TriggerExit2D |
			ScriptLifecycle.Disable | ScriptLifecycle.Destroy | ScriptLifecycle.LateUpdate,
			good.Lifecycle, "lifecycle metadata");
        Check((faulty.Lifecycle & ScriptLifecycle.Update) != 0, "faulty lifecycle metadata");

        ScriptFieldManifest speed = good.Fields.Single(field => field.Name == "_speed");
        Equal(ScriptFieldType.Float, speed.Type, "speed field type");
        Equal("Movement", speed.Header, "header metadata");
        Equal("Units moved per second.", speed.Tooltip, "tooltip metadata");
        Equal(0.0f, speed.RangeMinimum, "range minimum");
        Equal(100.0f, speed.RangeMaximum, "range maximum");
        Check(speed.Id.Length == 32 && speed.Id.All(character =>
            character is >= '0' and <= '9' or >= 'a' and <= 'f'), "stable lower-case FieldID");
        ScriptFieldManifest count = good.Fields.Single(field => field.Name == "Count");
        Check(count.FormerNames.SequenceEqual(["_oldCount"]), "former field-name metadata");
        Check(good.Fields.All(field => field.Name != "Speed"), "properties are not serialized");

        ScriptSceneRuntime scene = domain.CreateSceneRuntime(SceneSession, RuntimeGeneration);
		scene.EnableCallbackTraceForTesting();
        Entity first = new(SceneSession, 1, RuntimeGeneration);
        Entity second = new(SceneSession, 2, RuntimeGeneration);
        Entity failing = new(SceneSession, 3, RuntimeGeneration);
        Entity other = new(SceneSession, 4, RuntimeGeneration);
        scene.InstantiateAll([
            new ScriptAttachment(failing, 300, 1002, true),
            new ScriptAttachment(first, 100, 1001, true),
            new ScriptAttachment(second, 200, 1001, true)
        ]);

        string fieldsJson = $$"""
            {"attachments":[{"attachmentId":100,"fields":[
              {"fieldId":"{{speed.Id}}","name":"_speed","type":"Float","value":21.5},
              {"fieldId":"00000000000000000000000000000000","name":"_oldCount","type":"Int32","value":42},
              {"fieldId":"","name":"Spawn","type":"Vector2","value":[8.0,9.0]},
              {"fieldId":"","name":"Direction","type":"Vector3","value":[1.0,2.0,3.0]},
              {"fieldId":"","name":"Mask","type":"Vector4","value":[4.0,5.0,6.0,7.0]},
              {"fieldId":"","name":"Tint","type":"Color","value":[0.1,0.2,0.3,0.4]},
              {"fieldId":"","name":"Target","type":"Entity","value":4},
              {"fieldId":"","name":"Texture","type":"AssetRef","value":987654321},
              {"fieldId":"","name":"Mode","type":"Enum","value":2},
              {"fieldId":"","name":"WideMode","type":"Enum","value":-2},
              {"fieldId":"","name":"Label","type":"String","value":"restored"},
              {"fieldId":"","name":"Armed","type":"Bool","value":false}
            ]}]}
            """;
        scene.ApplySerializedFields(fieldsJson);

        Equal(21.5f, scene.ReadFieldValue(100, "_speed"), "float restore by FieldID");
        Equal(42, scene.ReadFieldValue(100, "Count"), "restore through FormerlySerializedAs");
        Equal(new Vector2(8, 9), scene.ReadFieldValue(100, "Spawn"), "Vector2 restore");
        Equal(new Vector3(1, 2, 3), scene.ReadFieldValue(100, "Direction"), "Vector3 restore");
        Equal(new Vector4(4, 5, 6, 7), scene.ReadFieldValue(100, "Mask"), "Vector4 restore");
        Equal(new Color(0.1f, 0.2f, 0.3f, 0.4f), scene.ReadFieldValue(100, "Tint"), "Color restore");
        Equal(other, scene.ReadFieldValue(100, "Target"), "Entity restore");
        Equal(987654321UL, ReadUlongProperty(scene.ReadFieldValue(100, "Texture"), "Handle"),
            "AssetRef restore");
		Equal(2L, Convert.ToInt64(scene.ReadFieldValue(100, "Mode")), "Enum restore");
		Equal(0xfffffffffffffffeUL,
			Convert.ToUInt64(scene.ReadFieldValue(100, "WideMode")),
			"UInt64 enum bit-preserving restore");
        Equal("restored", scene.ReadFieldValue(100, "Label"), "String restore");
        Equal(false, scene.ReadFieldValue(100, "Armed"), "Bool restore");

		scene.InvokeCreateAll();
		Equal(true, scene.ReadFieldValue(100, "DomainCancellationCanBeCanceled"),
			"script-visible Play Domain cancellation token");
		Equal(1, scene.ReadFieldValue(100, "ObservedStaticCreateSequence"),
			"first script static state in a fresh Play Domain");
		Equal(2, scene.ReadFieldValue(200, "ObservedStaticCreateSequence"),
			"second script static state in a fresh Play Domain");
        AssertPrefix(scene.CallbackTrace,
            "100:OnCreate", "200:OnCreate", "300:OnCreate",
            "100:OnEnable", "200:OnEnable", "300:OnEnable");

		Entity spawned = new(SceneSession, 5, RuntimeGeneration);
		scene.InstantiateAttachments([
			new ScriptAttachment(spawned, 250, 1001, true)
		], $$"""
			{"attachments":[{"attachmentId":250,"fields":[
			  {"fieldId":"{{speed.Id}}","name":"_speed","type":"Float","value":33.25}
			]}]}
			""");
		Equal(33.25f, scene.ReadFieldValue(250, "_speed"),
			"dynamic attachment field restore before OnCreate");
		Equal(3, scene.ReadFieldValue(250, "ObservedStaticCreateSequence"),
			"dynamic attachment OnCreate sequence");
		AssertSuffix(scene.CallbackTrace, "250:OnCreate", "250:OnEnable");
		Throws<InvalidDataException>(() => scene.InstantiateAttachments([
			new ScriptAttachment(spawned, 250, 1001, true)
		], "{\"attachments\":[]}"),
			"dynamic attachment IDs must remain unique for the scene runtime");

        int diagnosticsBefore = s_diagnostics;
		ulong frameBefore = Time.FrameCount;
        scene.UpdateAll(1.0f / 60.0f);
        Equal(ScriptInstanceState.Faulted, scene.GetInstanceState(300), "fault isolation state");
        Equal(1, scene.ReadFieldValue(300, "Updates"), "faulting callback runs once");
        Equal(diagnosticsBefore + 1, s_diagnostics, "fault diagnostic count");
        scene.UpdateAll(1.0f / 60.0f);
        Equal(1, scene.ReadFieldValue(300, "Updates"), "faulted instance is quarantined");
        Equal(2, scene.ReadFieldValue(100, "Updates"), "healthy instance continues");
        Equal(2, scene.ReadFieldValue(200, "Updates"), "second healthy instance continues");
		Equal(2, scene.ReadFieldValue(100, "LateUpdates"), "late update dispatch");
		Equal(2, scene.ReadFieldValue(200, "LateUpdates"), "late update execution order");
		Equal(frameBefore + 2, Time.FrameCount, "frame clock advances once per UpdateAll");
		Equal(1.0f / 60.0f, Time.DeltaTime, "frame delta clock");

        ulong fixedFrameBefore = Time.FixedFrameCount;
		scene.FixedUpdateAll(1.0f / 50.0f);
        Equal(1, scene.ReadFieldValue(100, "FixedUpdates"), "fixed update dispatch");
		Equal(fixedFrameBefore + 1, Time.FixedFrameCount, "fixed clock advances once per step");
		Equal(1.0f / 50.0f, Time.FixedDeltaTime, "fixed delta clock");
		Equal(false, Time.InFixedUpdate, "fixed-update clock scope is restored");
        scene.DispatchPhysicsEvents([
            new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, first, other),
            new ScriptPhysicsEvent(NativePhysicsEventKindV1.TriggerExit, second, other)
        ]);
        Equal(1, scene.ReadFieldValue(100, "CollisionEnters"), "collision dispatch");
        Equal(1, scene.ReadFieldValue(200, "TriggerExits"), "trigger dispatch");

        scene.SetEnabled(100, false);
        Equal(1, scene.ReadFieldValue(100, "Disables"), "disable callback");
        scene.UpdateAll(0.01f);
        Equal(2, scene.ReadFieldValue(100, "Updates"), "disabled instance is skipped");
        Equal(3, scene.ReadFieldValue(200, "Updates"), "enabled instance still updates");
		scene.SetEnabled(100, true);
		Equal(2, scene.ReadFieldValue(100, "Enables"), "re-enable callback");

		scene.DestroyAttachments([200]);
		AssertSuffix(scene.CallbackTrace, "200:OnDisable", "200:OnDestroy");
		Throws<KeyNotFoundException>(() => scene.GetInstanceState(200),
			"selectively destroyed attachment remained addressable");
		scene.UpdateAll(0.01f);
		Equal(3, scene.ReadFieldValue(100, "Updates"),
			"remaining attachment stopped after selective teardown");

		VerifySameBatchMutations(domain);
		VerifyHierarchyActivationConvergence(domain);

		ScriptSceneRuntime duplicateScene = domain.CreateSceneRuntime(SceneSession, RuntimeGeneration);
        Throws<InvalidDataException>(() => duplicateScene.InstantiateAll([
            new ScriptAttachment(first, 900, 1001, true),
            new ScriptAttachment(second, 900, 1001, true)
        ]), "duplicate AttachmentIDs must be rejected within a scene runtime");
		duplicateScene.DestroyAll();

		ScriptSceneRuntime missingScene = domain.CreateSceneRuntime(14, 10);
		int diagnosticsBeforeMissing = s_diagnostics;
		missingScene.InstantiateAll([
			new ScriptAttachment(new Entity(14, 1, 10), 600, 999999, true),
			new ScriptAttachment(new Entity(14, 2, 10), 601, 1001, true)
		]);
		missingScene.ApplySerializedFields("{\"attachments\":[]}");
		missingScene.InvokeCreateAll();
		missingScene.UpdateAll(0.01f);
		Equal(diagnosticsBeforeMissing + 1, s_diagnostics,
			"missing script diagnostic count");
		Throws<KeyNotFoundException>(() => missingScene.GetInstanceState(600),
			"missing script attachment incorrectly created a managed instance");
		Equal(1, missingScene.ReadFieldValue(601, "Updates"),
			"missing script prevented a healthy attachment from running");
		missingScene.DestroyAll();

		ScriptSceneRuntime reentrantDestroyScene = domain.CreateSceneRuntime(15, 11);
		reentrantDestroyScene.EnableCallbackTraceForTesting();
		reentrantDestroyScene.InstantiateAll([
			new ScriptAttachment(new Entity(15, 1, 11), 700, 1001, true)
		]);
		reentrantDestroyScene.ApplySerializedFields(
			"{\"attachments\":[{\"attachmentId\":700,\"fields\":[" +
			"{\"fieldId\":\"\",\"name\":\"RemoveOnDestroy\",\"type\":\"Bool\",\"value\":true}]}]}");
		reentrantDestroyScene.InvokeCreateAll();
		reentrantDestroyScene.DestroyAttachments([700]);
		Equal(1, reentrantDestroyScene.CallbackTrace.Count(
			value => value == "700:OnDestroy"),
			"OnDestroy must be exactly-once when it requests its own removal");
		reentrantDestroyScene.DestroyAll();

		scene.DestroyAll();
		AssertSuffix(scene.CallbackTrace,
			"250:OnDisable", "100:OnDisable", "300:OnDestroy",
			"250:OnDestroy", "100:OnDestroy");

		domain.BeginUnload();
		Check(domainCancellation.IsCancellationRequested,
			"Play Domain cancellation token was not cancelled before ALC unload");
		return domain;
    }

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static ScriptDomain BeginProjectBSwitchUnload(byte[] assembly, byte[] pdb)
	{
		var domain = new ScriptDomain(ScriptDomainKind.Play);
		domain.LoadProjectAssembly(assembly, pdb);
		Equal(1, domain.Manifest.Scripts.Count, "project B script count");
		ScriptTypeManifest script = domain.Manifest.Scripts.Single();
		Equal(2001UL, script.AssetHandle, "project B script asset");
		Equal("GameB.ProjectBBehaviour", script.TypeName, "project B type name");

		ScriptSceneRuntime scene = domain.CreateSceneRuntime(16, 12);
		scene.InstantiateAll([
			new ScriptAttachment(new Entity(16, 1, 12), 800, 2001, true)
		]);
		scene.ApplySerializedFields("{\"attachments\":[]}");
		scene.InvokeCreateAll();
		Equal(1, scene.ReadFieldValue(800, "ObservedStaticCreateSequence"),
			"project B static state in a fresh Play Domain");
		scene.DestroyAll();
		domain.BeginUnload();
		return domain;
	}

	private static void VerifySameBatchMutations(ScriptDomain domain)
	{
		Entity other = new(12, 99, 8);
		ScriptSceneRuntime disableScene = domain.CreateSceneRuntime(12, 8);
		disableScene.InstantiateAll([
			new ScriptAttachment(new Entity(12, 1, 8), 400, 1001, true)
		]);
		disableScene.ApplySerializedFields(
			"{\"attachments\":[{\"attachmentId\":400,\"fields\":[" +
			"{\"fieldId\":\"\",\"name\":\"DisableOnCollisionEnter\",\"type\":\"Bool\",\"value\":true}]}]}");
		disableScene.InvokeCreateAll();
		Entity disabledEntity = new(12, 1, 8);
		disableScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, disabledEntity, other),
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, disabledEntity, other)
		]);
		Equal(1, disableScene.ReadFieldValue(400, "CollisionEnters"),
			"self-disable must suppress later events in the same native batch");
		Equal(1, disableScene.ReadFieldValue(400, "Disables"),
			"self-disable callback count");
		disableScene.DestroyAll();

		ScriptSceneRuntime removeScene = domain.CreateSceneRuntime(13, 9);
		removeScene.EnableCallbackTraceForTesting();
		Entity removedEntity = new(13, 1, 9);
		Entity removeOther = new(13, 2, 9);
		removeScene.InstantiateAll([
			new ScriptAttachment(removedEntity, 500, 1001, true)
		]);
		removeScene.ApplySerializedFields(
			"{\"attachments\":[{\"attachmentId\":500,\"fields\":[" +
			"{\"fieldId\":\"\",\"name\":\"RemoveOnCollisionEnter\",\"type\":\"Bool\",\"value\":true}]}]}");
		removeScene.InvokeCreateAll();
		removeScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, removedEntity, removeOther),
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, removedEntity, removeOther)
		]);
		Equal(ScriptInstanceState.Destroyed, removeScene.GetInstanceState(500),
			"self-removal must become locally visible after its callback");
		Equal(1, removeScene.CallbackTrace.Count(value => value == "500:OnCollisionEnter2D"),
			"self-removal must suppress later events in the same native batch");
		removeScene.DestroyAll();
	}

	private static void VerifyHierarchyActivationConvergence(ScriptDomain domain)
	{
		s_usePerEntityGameplayActivation = true;
		try
		{
			VerifyHierarchyUpdateSuppression(domain);
			VerifyHierarchyFixedUpdateSuppression(domain);
			VerifyHierarchyPhysicsSuppression(domain);
			VerifyLifecycleCallbackReversal(domain);
			VerifyLifecycleOscillationIsBounded(domain);
		}
		finally
		{
			s_gameplayActiveByEntity.Clear();
			s_gameplayParentByEntity.Clear();
			s_gameplayActive = true;
			s_usePerEntityGameplayActivation = false;
		}
	}

	private static void VerifyHierarchyUpdateSuppression(ScriptDomain domain)
	{
		const ulong sceneSession = 21;
		const ulong generation = 17;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 1, 2);
		Entity first = new(sceneSession, 1, generation);
		Entity second = new(sceneSession, 2, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(first, 910, 1001, true),
			new ScriptAttachment(second, 911, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":910,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnUpdate","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();

		scene.UpdateAll(0.01f);
		Equal(1, scene.ReadFieldValue(910, "Updates"),
			"parent-disabling Update executes its initiating callback");
		Equal(0, scene.ReadFieldValue(911, "Updates"),
			"parent disable suppresses the next Update in the same batch");
		Equal(0, scene.ReadFieldValue(910, "LateUpdates"),
			"parent disable suppresses initiating script LateUpdate");
		Equal(0, scene.ReadFieldValue(911, "LateUpdates"),
			"parent disable suppresses sibling LateUpdate");
		Equal(1, scene.ReadFieldValue(910, "Disables"),
			"parent disable transitions initiating script once");
		Equal(1, scene.ReadFieldValue(911, "Disables"),
			"parent disable transitions sibling script once");
		scene.DestroyAll();
	}

	private static void VerifyHierarchyFixedUpdateSuppression(ScriptDomain domain)
	{
		const ulong sceneSession = 22;
		const ulong generation = 18;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 1, 2);
		Entity first = new(sceneSession, 1, generation);
		Entity second = new(sceneSession, 2, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(first, 920, 1001, true),
			new ScriptAttachment(second, 921, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":920,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnFixedUpdate","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();

		scene.FixedUpdateAll(1.0f / 60.0f);
		Equal(1, scene.ReadFieldValue(920, "FixedUpdates"),
			"parent-disabling FixedUpdate executes its initiating callback");
		Equal(0, scene.ReadFieldValue(921, "FixedUpdates"),
			"parent disable suppresses the next FixedUpdate in the same batch");
		Equal(1, scene.ReadFieldValue(920, "Disables"),
			"FixedUpdate parent disable transitions initiating script once");
		Equal(1, scene.ReadFieldValue(921, "Disables"),
			"FixedUpdate parent disable transitions sibling script once");
		scene.DestroyAll();
	}

	private static void VerifyHierarchyPhysicsSuppression(ScriptDomain domain)
	{
		const ulong sceneSession = 23;
		const ulong generation = 19;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 1, 2);
		Entity first = new(sceneSession, 1, generation);
		Entity second = new(sceneSession, 2, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(first, 930, 1001, true),
			new ScriptAttachment(second, 931, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":930,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnCollisionEnter","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();

		scene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, first, second)
		]);
		Equal(1, scene.ReadFieldValue(930, "CollisionEnters"),
			"parent-disabling collision executes its initiating callback");
		Equal(0, scene.ReadFieldValue(931, "CollisionEnters"),
			"parent disable suppresses the next collision callback in the same batch");
		Equal(1, scene.ReadFieldValue(930, "Disables"),
			"collision parent disable transitions initiating script once");
		Equal(1, scene.ReadFieldValue(931, "Disables"),
			"collision parent disable transitions sibling script once");
		scene.DestroyAll();
	}

	private static void VerifyLifecycleCallbackReversal(ScriptDomain domain)
	{
		const ulong enableSceneSession = 24;
		const ulong enableGeneration = 20;
		ResetGameplayActivationGraph();
		Entity enableEntity = new(enableSceneSession, 1, enableGeneration);
		ScriptSceneRuntime enableScene = domain.CreateSceneRuntime(
			enableSceneSession, enableGeneration);
		enableScene.InstantiateAll([
			new ScriptAttachment(enableEntity, 940, 1001, true)
		]);
		enableScene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":940,"fields":[
			  {"fieldId":"","name":"DisableSelfOnEnable","type":"Bool","value":true}
			]}]}
			""");
		enableScene.InvokeCreateAll();
		enableScene.UpdateAll(0.01f);
		Equal(1, enableScene.ReadFieldValue(940, "Enables"),
			"OnEnable self-disable enable count");
		Equal(1, enableScene.ReadFieldValue(940, "Disables"),
			"OnEnable self-disable converges through OnDisable");
		Equal(0, enableScene.ReadFieldValue(940, "Updates"),
			"OnEnable self-disable suppresses Update");
		enableScene.DestroyAll();

		const ulong disableSceneSession = 25;
		const ulong disableGeneration = 21;
		ResetGameplayActivationGraph();
		Entity disableEntity = new(disableSceneSession, 1, disableGeneration);
		ScriptSceneRuntime disableScene = domain.CreateSceneRuntime(
			disableSceneSession, disableGeneration);
		disableScene.InstantiateAll([
			new ScriptAttachment(disableEntity, 950, 1001, true)
		]);
		disableScene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":950,"fields":[
			  {"fieldId":"","name":"EnableSelfOnDisable","type":"Bool","value":true}
			]}]}
			""");
		disableScene.InvokeCreateAll();
		var disableHandle = new NativeEntityHandleV1(
			disableSceneSession, 1, disableGeneration);
		s_gameplayActiveByEntity[GameplayEntityKey(disableHandle)] = false;
		disableScene.UpdateAll(0.01f);
		Equal(2, disableScene.ReadFieldValue(950, "Enables"),
			"OnDisable self-enable converges through OnEnable");
		Equal(1, disableScene.ReadFieldValue(950, "Disables"),
			"OnDisable self-enable disable count");
		Equal(1, disableScene.ReadFieldValue(950, "Updates"),
			"OnDisable self-enable resumes Update after convergence");
		disableScene.DestroyAll();
	}

	private static void VerifyLifecycleOscillationIsBounded(ScriptDomain domain)
	{
		const ulong sceneSession = 26;
		const ulong generation = 22;
		ResetGameplayActivationGraph();
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(new Entity(sceneSession, 1, generation),
				960, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":960,"fields":[
			  {"fieldId":"","name":"DisableSelfOnEnable","type":"Bool","value":true},
			  {"fieldId":"","name":"EnableSelfOnDisable","type":"Bool","value":true}
			]}]}
			""");
		int diagnosticsBefore = s_diagnostics;
		scene.InvokeCreateAll();

		Equal(ScriptInstanceState.Faulted, scene.GetInstanceState(960),
			"non-convergent lifecycle callbacks are quarantined");
		int transitionCallbacks =
			(int)scene.ReadFieldValue(960, "Enables")!
			+ (int)scene.ReadFieldValue(960, "Disables")!;
		Check(transitionCallbacks > 1 && transitionCallbacks <= 8,
			"lifecycle oscillation exceeded its bounded transition budget");
		Equal(diagnosticsBefore + 1, s_diagnostics,
			"lifecycle oscillation reports one managed diagnostic");
		scene.UpdateAll(0.01f);
		Equal(0, scene.ReadFieldValue(960, "Updates"),
			"quarantined lifecycle oscillator does not receive Update");
		scene.DestroyAll();
	}

	private static void ResetGameplayActivationGraph()
	{
		s_gameplayActiveByEntity.Clear();
		s_gameplayParentByEntity.Clear();
		s_gameplayActive = true;
	}

	private static void ConfigureGameplayParent(ulong sceneSession,
		ulong runtimeGeneration, ulong parentId, params ulong[] children)
	{
		var parent = new NativeEntityHandleV1(sceneSession, parentId,
			runtimeGeneration);
		foreach (ulong childId in children)
		{
			var child = new NativeEntityHandleV1(sceneSession, childId,
				runtimeGeneration);
			s_gameplayParentByEntity[GameplayEntityKey(child)] = parent;
		}
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static ScriptDomain BeginPlayCycleUnload(byte[] assembly, byte[] pdb,
		int cycleIndex, out Entity instantiatedEntity)
	{
		var domain = new ScriptDomain(ScriptDomainKind.Play);
		domain.LoadProjectAssembly(assembly, pdb);
		Equal(2, domain.Manifest.Scripts.Count,
			$"Play Domain cycle {cycleIndex + 1} manifest");

		ulong sceneSession = 1000UL + (ulong)cycleIndex;
		ulong runtimeGeneration = 2000UL + (ulong)cycleIndex;
		ulong attachmentId = 3000UL + (ulong)cycleIndex;
		instantiatedEntity = new Entity(sceneSession, 1, runtimeGeneration);
		Entity target = new(sceneSession, 2, runtimeGeneration);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession,
			runtimeGeneration);
		scene.InstantiateAll([
			new ScriptAttachment(instantiatedEntity, attachmentId, 1001, true)
		]);
		int restoredCount = 10000 + cycleIndex;
		scene.ApplySerializedFields($$"""
			{"attachments":[{"attachmentId":{{attachmentId}},"fields":[
			  {"fieldId":"","name":"Count","type":"Int32","value":{{restoredCount}}},
			  {"fieldId":"","name":"Target","type":"Entity","value":{{target.Id}}}
			]}]}
			""");
		Equal(restoredCount, scene.ReadFieldValue(attachmentId, "Count"),
			$"Play Domain cycle {cycleIndex + 1} field restore");
		Equal(target, scene.ReadFieldValue(attachmentId, "Target"),
			$"Play Domain cycle {cycleIndex + 1} Entity restore");

		scene.InvokeCreateAll();
		Equal(1, scene.ReadFieldValue(attachmentId, "Creates"),
			$"Play Domain cycle {cycleIndex + 1} OnCreate");
		Equal(1, scene.ReadFieldValue(attachmentId, "Enables"),
			$"Play Domain cycle {cycleIndex + 1} OnEnable");
		Equal(1, scene.ReadFieldValue(attachmentId,
			"ObservedStaticCreateSequence"),
			$"Play Domain cycle {cycleIndex + 1} static reset");

		scene.DestroyAll();
		Throws<KeyNotFoundException>(() => scene.GetInstanceState(attachmentId),
			$"Play Domain cycle {cycleIndex + 1} retained a destroyed instance");
		domain.BeginUnload();
		return domain;
	}

	private static void VerifyFileUnlocked(string path, string message)
	{
		try
		{
			using FileStream stream = new(path, FileMode.Open, FileAccess.ReadWrite,
				FileShare.None);
			Check(stream.Length > 0, $"{message}: fixture DLL is empty");
		}
		catch (IOException exception)
		{
			throw new InvalidOperationException(message, exception);
		}
	}

    private static bool PollUntilUnloaded(ScriptDomain domain)
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            if (domain.PollUnload())
                return true;
        }
        return false;
    }

    private static ulong ReadUlongProperty(object? value, string name)
    {
        Check(value is not null, $"{name} owner is null");
        object? property = value!.GetType().GetProperty(name)?.GetValue(value);
        return property is ulong result ? result : throw new InvalidOperationException(
            $"Property {name} is not a UInt64.");
    }

    private static void AssertPrefix(IReadOnlyList<string> actual, params string[] expected)
    {
        Check(actual.Take(expected.Length).SequenceEqual(expected),
            $"callback prefix mismatch: {string.Join(", ", actual)}");
    }

    private static void AssertSuffix(IReadOnlyList<string> actual, params string[] expected)
    {
        Check(actual.TakeLast(expected.Length).SequenceEqual(expected),
            $"callback suffix mismatch: {string.Join(", ", actual)}");
    }

    private static void Throws<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }

	private static void ThrowsWithMessage<TException>(Action action, string expectedMessage,
		string message) where TException : Exception
	{
		try
		{
			action();
		}
		catch (TException exception)
		{
			Equal(expectedMessage, exception.Message, message);
			return;
		}
		throw new InvalidOperationException(message);
	}

    private static void Equal<T>(T expected, object? actual, string message)
    {
        if (actual is T typed && EqualityComparer<T>.Default.Equals(expected, typed))
            return;
        throw new InvalidOperationException($"{message}: expected {expected}, got {actual ?? "<null>"}");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CaptureDiagnostic(NativeDiagnosticV1* diagnostic)
    {
        if (diagnostic is null)
            return -1;
        Interlocked.Increment(ref s_diagnostics);
        return 0;
    }

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int CaptureMetadata(NativeByteView metadata, ulong receiverToken)
	{
		if (metadata.Data is null || metadata.Length == 0 || metadata.Length > int.MaxValue)
			return -1;
		try
		{
			s_metadataReceiverToken = receiverToken;
			s_metadataReceiverJson = Encoding.UTF8.GetString(
				new ReadOnlySpan<byte>(metadata.Data, (int)metadata.Length));
			return 0;
		}
		catch
		{
			return -1;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int IsMainThread() =>
		Environment.CurrentManagedThreadId == s_mainManagedThread ? 1 : 0;

	private static int InvokeMissingDomain(nint loadProjectAssembly)
	{
		byte invalidAssembly = 0;
		var callback = (delegate* unmanaged[Cdecl]<ulong, NativeByteView,
			NativeByteView, int>)loadProjectAssembly;
		return callback(ulong.MaxValue, new NativeByteView(&invalidAssembly, 1), default);
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int RemoveBehaviour(ulong attachmentId)
	{
		s_removedAttachment = attachmentId;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int SetBehaviourEnabled(ulong attachmentId, int enabled) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int PoisonRemoveBehaviour(ulong attachmentId)
	{
		s_removedAttachment = ulong.MaxValue;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubLog(int level, NativeUtf8View message) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubEntityAlive(NativeEntityHandleV1 entity) => 1;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetEntityText(NativeEntityHandleV1 entity, byte* buffer,
		uint capacity, uint* required)
	{
		if (required is null)
			return -1;
		*required = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetEntityText(NativeEntityHandleV1 entity, NativeUtf8View value)
	{
		++s_entityTextSetterCalls;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetEntityLayer(NativeEntityHandleV1 entity, uint* layer)
	{
		if (layer is null)
			return -1;
		*layer = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetEntityLayer(NativeEntityHandleV1 entity, uint layer)
	{
		++s_entityLayerSetterCalls;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubEntityStatus(NativeEntityHandleV1 entity) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubHasComponent(NativeEntityHandleV1 entity, int componentType)
	{
		if (componentType == (int)NativeComponentTypeV1.SpriteAnimator)
			++s_spriteAnimatorHasCalls;
		return 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAddEntityComponent(NativeEntityHandleV1 entity,
		int componentType)
	{
		if (componentType == (int)NativeComponentTypeV1.SpriteAnimator)
			++s_spriteAnimatorAddCalls;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRemoveEntityComponent(NativeEntityHandleV1 entity,
		int componentType)
	{
		if (componentType == (int)NativeComponentTypeV1.SpriteAnimator)
			++s_spriteAnimatorRemoveCalls;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetTransformVector(NativeEntityHandleV1 entity, NativeVector3* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetTransformVector(NativeEntityHandleV1 entity, NativeVector3 value)
	{
		s_lastTransformPosition = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetWorldMatrix(NativeEntityHandleV1 entity, NativeMatrix4* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubInputKey(uint key) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubInputVector(NativeVector2* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubInputModifiers(uint* value)
	{
		if (value is null)
			return -1;
		*value = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int QueryTestCapability(NativeUtf8View name, uint minimumVersion,
		void* output, uint capacity, uint* required)
	{
		if (required is null || (name.Data is null && name.Length != 0)
			|| name.Length > int.MaxValue)
			return -1;
		string capability = Encoding.UTF8.GetString(
			new ReadOnlySpan<byte>(name.Data, (int)name.Length));
		if (capability == "TomCat.InputApiV1")
		{
			*required = (uint)sizeof(NativeInputApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeInputApiV1))
				return -6;
			*(NativeInputApiV1*)output = new NativeInputApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeInputApiV1),
				MaximumGamepads = 16,
				GamepadButtonCount = 15,
				GamepadAxisCount = 6,
				IsMouseButtonHeld = &StubMouseButtonHeld,
				WasMouseButtonPressed = &StubMouseButtonHeld,
				WasMouseButtonReleased = &StubMouseButtonReleased,
				GetScrollDelta = &StubScrollDelta,
				IsWindowFocused = &StubWindowFocused,
				IsGamepadConnected = &StubGamepadConnected,
				WasGamepadConnected = &StubGamepadConnected,
				WasGamepadDisconnected = &StubGamepadDisconnected,
				IsGamepadButtonHeld = &StubGamepadButtonHeld,
				WasGamepadButtonPressed = &StubGamepadButtonHeld,
				WasGamepadButtonReleased = &StubGamepadButtonReleased,
				GetGamepadAxis = &StubGamepadAxis,
				GetGamepadName = &StubGamepadName
			};
			return 0;
		}
		if (capability == "TomCat.ComponentApiV1")
		{
			*required = (uint)sizeof(NativeComponentApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeComponentApiV1))
				return -6;
			*(NativeComponentApiV1*)output = new NativeComponentApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeComponentApiV1),
				Has = &StubRegisteredComponentHas,
				Add = &StubRegisteredComponentAdd,
				Remove = &StubRegisteredComponentRemove,
				GetProperty = &StubRegisteredComponentGetProperty,
				SetProperty = &StubRegisteredComponentSetProperty
			};
			return 0;
		}
		if (capability == "TomCat.GameplayApiV1")
		{
			*required = (uint)sizeof(NativeGameplayApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeGameplayApiV1))
				return -6;
			*(NativeGameplayApiV1*)output = new NativeGameplayApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeGameplayApiV1),
				CreateEntityDeferred = &StubGameplayCreateEntity,
				FindEntityByName = &StubGameplayFindEntity,
				QueryEntities = &StubGameplayQueryEntities,
				GetParent = &StubGameplayGetParent,
				SetParentDeferred = &StubGameplaySetParent,
				GetChildren = &StubGameplayGetChildren,
				GetActiveSelf = &StubGameplayGetActiveSelf,
				SetActiveSelf = &StubGameplaySetActiveSelf,
				GetActiveInHierarchy = &StubGameplayGetActiveInHierarchy,
				TransformGetLocalPosition = &StubGameplayGetLocalPosition,
				TransformSetLocalPosition = &StubGameplaySetLocalPosition,
				TransformGetLocalRotationEuler = &StubGetTransformVector,
				TransformSetLocalRotationEuler = &StubSetTransformVector,
				TransformGetLocalScale = &StubGetTransformVector,
				TransformSetLocalScale = &StubSetTransformVector,
				GetComponentProperty = &StubGameplayGetComponentProperty,
				SetComponentProperty = &StubGameplaySetComponentProperty,
				SpriteAnimatorPlay = &StubGameplaySpriteAnimatorPlay,
				SpriteAnimatorStop = &StubGameplaySpriteAnimatorStop,
				SpriteAnimatorSetBool = &StubGameplaySpriteAnimatorSetBool,
				SpriteAnimatorSetInt = &StubGameplaySpriteAnimatorSetInt,
				SpriteAnimatorSetFloat = &StubGameplaySpriteAnimatorSetFloat,
				SpriteAnimatorSetTrigger = &StubGameplaySpriteAnimatorSetTrigger,
				SpriteAnimatorResetTrigger = &StubGameplaySpriteAnimatorResetTrigger,
				SpriteAnimatorGetCurrentState = &StubGameplaySpriteAnimatorGetCurrentState
			};
			return 0;
		}
		if (capability == "TomCat.AudioSpatialApiV1")
		{
			*required = (uint)sizeof(NativeAudioSpatialApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeAudioSpatialApiV1))
				return -6;
			*(NativeAudioSpatialApiV1*)output = new NativeAudioSpatialApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeAudioSpatialApiV1),
				GetStreaming = &StubAudioGetStreaming,
				SetStreaming = &StubAudioSetStreaming,
				GetSpatialBlend = &StubAudioGetSpatialBlend,
				SetSpatialBlend = &StubAudioSetSpatialBlend,
				GetMinDistance = &StubAudioGetMinDistance,
				SetMinDistance = &StubAudioSetMinDistance,
				GetMaxDistance = &StubAudioGetMaxDistance,
				SetMaxDistance = &StubAudioSetMaxDistance
			};
			return 0;
		}
		if (capability == "TomCat.RuntimeUIApiV1")
		{
			*required = (uint)sizeof(NativeRuntimeUIApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeRuntimeUIApiV1))
				return -6;
			*(NativeRuntimeUIApiV1*)output = new NativeRuntimeUIApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeRuntimeUIApiV1),
				GetText = &StubRuntimeUIGetText,
				SetText = &StubRuntimeUISetText,
				WasButtonClicked = &StubRuntimeUIButtonClicked,
				GetButtonClickSerial = &StubRuntimeUIButtonClickSerial,
				FocusButton = &StubRuntimeUIFocusButton,
				GetRect = &StubRuntimeUIGetRect,
				IsGameplayInputCaptured = &StubRuntimeUIInputCaptured
			};
			return 0;
		}
		*required = 0;
		return -3;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIGetText(NativeEntityHandleV1 entity,
		ulong typeId, byte* buffer, uint capacity, uint* required)
	{
		if (required is null || typeId != UIText.TypeId)
			return -1;
		byte[] bytes = Encoding.UTF8.GetBytes(s_runtimeUIText);
		*required = (uint)bytes.Length;
		if (capacity < bytes.Length || (buffer is null && bytes.Length != 0))
			return -6;
		bytes.CopyTo(new Span<byte>(buffer, bytes.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUISetText(NativeEntityHandleV1 entity,
		ulong typeId, NativeUtf8View value)
	{
		if (typeId != UIText.TypeId || (value.Data is null && value.Length != 0)
			|| value.Length > 65536)
			return -1;
		try
		{
			s_runtimeUIText = s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(value.Data, (int)value.Length));
			return 0;
		}
		catch (DecoderFallbackException) { return -1; }
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIButtonClicked(NativeEntityHandleV1 entity) => 1;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIButtonClickSerial(NativeEntityHandleV1 entity,
		ulong* value)
	{
		if (value is null) return -1;
		*value = RuntimeUIButtonClickSerial;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIFocusButton(NativeEntityHandleV1 entity)
	{
		s_runtimeUIButtonFocused = true;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIGetRect(NativeEntityHandleV1 entity,
		NativeVector4* value)
	{
		if (value is null) return -1;
		*value = new NativeVector4 { X = 10.0f, Y = 20.0f, Z = 300.0f, W = 80.0f };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRuntimeUIInputCaptured() => s_runtimeUICaptured ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioGetStreaming(NativeEntityHandleV1 entity) =>
		s_audioStreaming ? 1 : 0;
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioSetStreaming(NativeEntityHandleV1 entity, int value)
	{ s_audioStreaming = value != 0; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioGetSpatialBlend(NativeEntityHandleV1 entity, float* value)
	{ if (value is null) return -1; *value = s_audioSpatialBlend; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioSetSpatialBlend(NativeEntityHandleV1 entity, float value)
	{ s_audioSpatialBlend = value; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioGetMinDistance(NativeEntityHandleV1 entity, float* value)
	{ if (value is null) return -1; *value = s_audioMinDistance; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioSetMinDistance(NativeEntityHandleV1 entity, float value)
	{ s_audioMinDistance = value; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioGetMaxDistance(NativeEntityHandleV1 entity, float* value)
	{ if (value is null) return -1; *value = s_audioMaxDistance; return 0; }
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAudioSetMaxDistance(NativeEntityHandleV1 entity, float value)
	{ s_audioMaxDistance = value; return 0; }

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayCreateEntity(NativeEntityHandleV1 context,
		NativeUtf8View name, NativeVector3 position, NativeEntityHandleV1 parent,
		NativeEntityHandleV1* output)
	{
		if (output is null || context.SceneSessionId != SceneSession
			|| context.RuntimeGeneration != RuntimeGeneration)
			return -1;
		s_reservedGameplayEntity = new NativeEntityHandleV1(
			context.SceneSessionId, 1200, context.RuntimeGeneration);
		*output = s_reservedGameplayEntity;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayFindEntity(NativeEntityHandleV1 context,
		NativeUtf8View name, NativeEntityHandleV1* output)
	{
		if (output is null || name.Data is null || name.Length > int.MaxValue)
			return -1;
		string value = Encoding.UTF8.GetString(
			new ReadOnlySpan<byte>(name.Data, (int)name.Length));
		if (value != "Target")
			return -3;
		*output = new NativeEntityHandleV1(context.SceneSessionId, 44,
			context.RuntimeGeneration);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayQueryEntities(NativeEntityHandleV1 context,
		int componentType, ulong registeredTypeId, NativeEntityHandleV1* output,
		uint capacity, uint* required)
	{
		if (required is null || (componentType != 0 && registeredTypeId != 0))
			return -1;
		*required = 2;
		if (capacity < 2 || output is null)
			return -6;
		++s_gameplayQueryCalls;
		s_lastGameplayQueryComponent = componentType;
		output[0] = new NativeEntityHandleV1(context.SceneSessionId, 8,
			context.RuntimeGeneration);
		output[1] = new NativeEntityHandleV1(context.SceneSessionId, 44,
			context.RuntimeGeneration);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetParent(NativeEntityHandleV1 entity,
		NativeEntityHandleV1* output)
	{
		if (output is null) return -1;
		*output = s_gameplayParent;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySetParent(NativeEntityHandleV1 entity,
		NativeEntityHandleV1 parent)
	{
		s_gameplayParent = parent;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetChildren(NativeEntityHandleV1 entity,
		NativeEntityHandleV1* output, uint capacity, uint* required)
	{
		if (required is null) return -1;
		*required = 2;
		if (capacity < 2 || output is null) return -6;
		output[0] = new NativeEntityHandleV1(entity.SceneSessionId, 45,
			entity.RuntimeGeneration);
		output[1] = new NativeEntityHandleV1(entity.SceneSessionId, 46,
			entity.RuntimeGeneration);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetActiveSelf(NativeEntityHandleV1 entity) =>
		StubGameplayActiveSelf(entity) ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetActiveInHierarchy(NativeEntityHandleV1 entity)
	{
		if (!s_usePerEntityGameplayActivation)
			return s_gameplayActive ? 1 : 0;

		var visited = new HashSet<(ulong SceneSessionId, ulong EntityId,
			ulong RuntimeGeneration)>();
		NativeEntityHandleV1 cursor = entity;
		while (cursor.EntityId != 0)
		{
			var key = GameplayEntityKey(cursor);
			if (!visited.Add(key) || !StubGameplayActiveSelf(cursor))
				return 0;
			if (!s_gameplayParentByEntity.TryGetValue(key,
				out NativeEntityHandleV1 parent))
				return 1;
			cursor = parent;
		}
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySetActiveSelf(NativeEntityHandleV1 entity,
		int active)
	{
		if (active is not (0 or 1)) return -1;
		if (s_usePerEntityGameplayActivation)
			s_gameplayActiveByEntity[GameplayEntityKey(entity)] = active != 0;
		else
			s_gameplayActive = active != 0;
		return 0;
	}

	private static bool StubGameplayActiveSelf(NativeEntityHandleV1 entity) =>
		s_gameplayActiveByEntity.TryGetValue(GameplayEntityKey(entity),
			out bool active) ? active : s_gameplayActive;

	private static (ulong SceneSessionId, ulong EntityId, ulong RuntimeGeneration)
		GameplayEntityKey(NativeEntityHandleV1 entity) =>
		(entity.SceneSessionId, entity.EntityId, entity.RuntimeGeneration);

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetLocalPosition(NativeEntityHandleV1 entity,
		NativeVector3* value)
	{
		if (value is null) return -1;
		*value = s_localTransformPosition;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySetLocalPosition(NativeEntityHandleV1 entity,
		NativeVector3 value)
	{
		s_localTransformPosition = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplayGetComponentProperty(NativeEntityHandleV1 entity,
		int componentType, uint propertyId, NativePropertyValueV1* value)
	{
		if (value is null || !s_gameplayProperties.TryGetValue(
			(componentType, propertyId), out NativePropertyValueV1 stored))
			return -3;
		*value = stored;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySetComponentProperty(NativeEntityHandleV1 entity,
		int componentType, uint propertyId, NativePropertyValueV1 value)
	{
		s_gameplayProperties[(componentType, propertyId)] = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorPlay(NativeEntityHandleV1 entity,
		NativeUtf8View clipName, int restart)
	{
		if ((clipName.Data is null && clipName.Length != 0)
			|| clipName.Length > int.MaxValue || restart is not (0 or 1))
			return -1;
		try
		{
			s_lastSpriteAnimatorClip = s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(clipName.Data, (int)clipName.Length));
		}
		catch (DecoderFallbackException)
		{
			return -1;
		}
		if (string.IsNullOrEmpty(s_lastSpriteAnimatorClip)
			|| s_lastSpriteAnimatorClip.Contains('\0'))
			return -1;
		++s_spriteAnimatorPlayCalls;
		s_lastSpriteAnimatorRestart = restart != 0;
		s_gameplayProperties[((int)NativeComponentTypeV1.SpriteAnimator, 702)] =
			new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Bool,
				Integer = 1
			};
		s_gameplayProperties[((int)NativeComponentTypeV1.SpriteAnimator, 703)] =
			new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.UInt32,
				Integer = 3
			};
		return 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorStop(NativeEntityHandleV1 entity)
	{
		++s_spriteAnimatorStopCalls;
		s_gameplayProperties[((int)NativeComponentTypeV1.SpriteAnimator, 702)] =
			new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Bool,
				Integer = 0
			};
		return 0;
	}

	private static bool TryReadAnimatorParameter(NativeUtf8View view, out string value)
	{
		value = string.Empty;
		if ((view.Data is null && view.Length != 0) || view.Length > int.MaxValue)
			return false;
		try
		{
			value = s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(view.Data, (int)view.Length));
			return value.Length != 0 && !value.Contains('\0');
		}
		catch (DecoderFallbackException)
		{
			return false;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorSetBool(NativeEntityHandleV1 entity,
		NativeUtf8View parameter, int value)
	{
		if (!TryReadAnimatorParameter(parameter, out string name)
			|| value is not (0 or 1)) return -1;
		++s_spriteAnimatorParameterCalls;
		s_animatorParameters[name] = value != 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorSetInt(NativeEntityHandleV1 entity,
		NativeUtf8View parameter, int value)
	{
		if (!TryReadAnimatorParameter(parameter, out string name)) return -1;
		++s_spriteAnimatorParameterCalls;
		s_animatorParameters[name] = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorSetFloat(NativeEntityHandleV1 entity,
		NativeUtf8View parameter, float value)
	{
		if (!TryReadAnimatorParameter(parameter, out string name)
			|| !float.IsFinite(value)) return -1;
		++s_spriteAnimatorParameterCalls;
		s_animatorParameters[name] = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorSetTrigger(NativeEntityHandleV1 entity,
		NativeUtf8View parameter)
	{
		if (!TryReadAnimatorParameter(parameter, out string name)) return -1;
		++s_spriteAnimatorParameterCalls;
		s_animatorParameters[name] = true;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorResetTrigger(NativeEntityHandleV1 entity,
		NativeUtf8View parameter)
	{
		if (!TryReadAnimatorParameter(parameter, out string name)) return -1;
		++s_spriteAnimatorParameterCalls;
		s_animatorParameters[name] = false;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySpriteAnimatorGetCurrentState(
		NativeEntityHandleV1 entity, byte* buffer, uint capacity, uint* required)
	{
		if (required is null) return -1;
		ReadOnlySpan<byte> state = "Running"u8;
		*required = (uint)state.Length;
		if (capacity < state.Length || (buffer is null && state.Length != 0)) return -6;
		state.CopyTo(new Span<byte>(buffer, state.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentHas(NativeEntityHandleV1 entity,
		ulong typeId)
	{
		if (typeId != HealthComponent.TypeId)
			return -1;
		++s_componentHasCalls;
		return s_healthPresent ? 1 : 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentAdd(NativeEntityHandleV1 entity,
		ulong typeId)
	{
		if (typeId != HealthComponent.TypeId)
			return -1;
		++s_componentAddCalls;
		s_healthPresent = true;
		s_healthMaximum = 100;
		s_healthCurrent = 100;
		s_healthInvulnerable = false;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentRemove(NativeEntityHandleV1 entity,
		ulong typeId)
	{
		if (typeId != HealthComponent.TypeId)
			return -1;
		++s_componentRemoveCalls;
		s_healthPresent = false;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentGetProperty(NativeEntityHandleV1 entity,
		ulong typeId, ulong propertyId, NativePropertyValueV1* value)
	{
		if (value is null)
			return -3;
		if (typeId == UIText.TypeId || typeId == TextRenderer.TypeId)
		{
			if (!s_registeredRuntimeUIProperties.TryGetValue(
				(typeId, propertyId), out NativePropertyValueV1 property))
				return -3;
			*value = property;
			return 0;
		}
		if (typeId != HealthComponent.TypeId || !s_healthPresent || value is null)
			return -3;
		++s_componentGetCalls;
		*value = propertyId switch
		{
			HealthComponent.MaximumPropertyId => new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Int32,
				Integer = s_healthMaximum
			},
			HealthComponent.CurrentPropertyId => new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Int32,
				Integer = s_healthCurrent
			},
			HealthComponent.InvulnerablePropertyId => new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Bool,
				Integer = s_healthInvulnerable ? 1 : 0
			},
			_ => default
		};
		return propertyId == HealthComponent.MaximumPropertyId
			|| propertyId == HealthComponent.CurrentPropertyId
			|| propertyId == HealthComponent.InvulnerablePropertyId ? 0 : -1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentSetProperty(NativeEntityHandleV1 entity,
		ulong typeId, ulong propertyId, NativePropertyValueV1 value)
	{
		if (typeId == UIText.TypeId || typeId == TextRenderer.TypeId)
		{
			if (value.Kind != NativePropertyKindV1.UInt64)
				return -1;
			s_registeredRuntimeUIProperties[(typeId, propertyId)] = value;
			return 0;
		}
		if (typeId != HealthComponent.TypeId || !s_healthPresent)
			return -3;
		if (propertyId == HealthComponent.MaximumPropertyId
			&& value.Kind == NativePropertyKindV1.Int32
			&& value.Integer is > 0 and <= int.MaxValue
			&& value.Integer >= s_healthCurrent)
			s_healthMaximum = (int)value.Integer;
		else if (propertyId == HealthComponent.CurrentPropertyId
			&& value.Kind == NativePropertyKindV1.Int32
			&& value.Integer >= 0 && value.Integer <= s_healthMaximum)
			s_healthCurrent = (int)value.Integer;
		else if (propertyId == HealthComponent.InvulnerablePropertyId
			&& value.Kind == NativePropertyKindV1.Bool
			&& (value.Integer == 0 || value.Integer == 1))
			s_healthInvulnerable = value.Integer != 0;
		else
			return -1;
		++s_componentSetCalls;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubMouseButtonHeld(uint button) =>
		button == (uint)MouseButton.Left && s_extendedInputPressed ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubMouseButtonReleased(uint button) =>
		button == (uint)MouseButton.Left && !s_extendedInputPressed ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubScrollDelta(NativeVector2* value)
	{
		if (value is null) return -1;
		*value = new NativeVector2 { X = 1.25f, Y = -2.5f };
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubWindowFocused() => 1;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadConnected(uint gamepad) => gamepad == 0 ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadDisconnected(uint gamepad) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadButtonHeld(uint gamepad, uint button) =>
		gamepad == 0 && button == (uint)GamepadButton.South ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadButtonReleased(uint gamepad, uint button) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadAxis(uint gamepad, uint axis, float* value)
	{
		if (value is null || gamepad != 0 || axis > (uint)GamepadAxis.RightTrigger)
			return -1;
		*value = (GamepadAxis)axis switch
		{
			GamepadAxis.LeftX => 0.575f,
			GamepadAxis.LeftTrigger => s_extendedLeftTriggerRaw,
			GamepadAxis.RightTrigger => -1.0f,
			_ => 0.0f
		};
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGamepadName(uint gamepad, byte* buffer, uint capacity,
		uint* required)
	{
		if (required is null || gamepad >= Input.MaximumGamepads)
			return -1;
		ReadOnlySpan<byte> name = "Regression Pad"u8;
		*required = (uint)name.Length;
		if (capacity < name.Length || (buffer is null && name.Length != 0))
			return -6;
		name.CopyTo(new Span<byte>(buffer, name.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetRigidbodyVector(NativeEntityHandleV1 entity, NativeVector2* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetRigidbodyVector(NativeEntityHandleV1 entity, NativeVector2 value)
	{
		s_lastRigidbodyVelocity = value;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubPhysicsRaycast(NativeEntityHandleV1 context, NativeVector2 start,
		NativeVector2 end, uint layerMask, int includeTriggers, NativeRaycastHit2D* result) => -3;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubPhysicsQueryAabb(NativeEntityHandleV1 context, NativeVector2 minimum,
		NativeVector2 maximum, uint layerMask, int includeTriggers,
		NativePhysicsQueryHit2D* hits, uint capacity, uint* required)
	{
		if (required is null)
			return -1;
		*required = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAssetIsValid(ulong assetHandle) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAssetGetType(ulong assetHandle, int* type)
	{
		if (type is null)
			return -1;
		*type = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubBehaviourGetEnabled(ulong attachmentId) => 1;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSceneGetActiveHandle(ulong* sceneHandle)
	{
		if (sceneHandle is null)
			return -1;
		*sceneHandle = ActiveSceneHandle;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSceneGetActiveBuildIndex(int* buildIndex)
	{
		if (buildIndex is null)
			return -1;
		*buildIndex = ActiveSceneBuildIndex;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSceneRequestLoadHandle(ulong sceneHandle)
	{
		s_requestedSceneHandle = sceneHandle;
		return sceneHandle == 0 ? 0 : 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSceneRequestLoadIndex(int buildIndex)
	{
		s_requestedSceneIndex = buildIndex;
		return buildIndex < 0 ? 0 : 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSceneRequestReload()
	{
		++s_sceneReloadRequests;
		return 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubPrefabInstantiate(NativeEntityHandleV1 context,
		ulong prefabHandle, NativeVector3 worldPosition, NativeEntityHandleV1 parent)
	{
		if (context.SceneSessionId != SceneSession
			|| context.RuntimeGeneration != RuntimeGeneration)
			return -1;
		s_requestedPrefabHandle = prefabHandle;
		s_requestedPrefabPosition = worldPosition;
		s_requestedPrefabParent = parent;
		return prefabHandle == 0 ? 0 : 1;
	}

	private sealed class ConstructorProbe : TomCatBehaviour
    {
        internal ConstructorProbe() => _ = Entity;
	}

	private sealed class RemovalProbe : TomCatBehaviour
	{
		protected override void OnCreate() => RemoveFromEntity();
	}

	private sealed class NaturalProxySyntaxProbe : TomCatBehaviour
	{
		protected override void OnCreate()
		{
			Entity.Name = "natural-name";
			Entity.Tag = "natural-tag";
			Entity.Layer = 5;
			Transform.Position = new Vector3(1.0f, 2.0f, 3.0f);
			Transform.LocalPosition = new Vector3(9.0f, 8.0f, 7.0f);
			if (!Transform.LocalPosition.Equals(new Vector3(9.0f, 8.0f, 7.0f)))
				throw new InvalidOperationException("Transform local-space round trip failed.");
			Entity? target = World.Find("Target");
			if (target is null || target.Id != 44)
				throw new InvalidOperationException("World.Find returned an unexpected entity.");
			Entity.Parent = target;
			if (Entity.Parent != target || Entity.Children.Count != 2)
				throw new InvalidOperationException("Entity hierarchy bridge failed.");
			Entity.ActiveSelf = false;
			if (Entity.ActiveSelf || Entity.ActiveInHierarchy)
				throw new InvalidOperationException("Entity active-state bridge failed.");
			Entity.ActiveSelf = true;
			if (!Entity.ActiveInHierarchy)
				throw new InvalidOperationException("Entity active hierarchy did not recover.");
			Entity reserved = World.CreateEntity("Runtime child",
				new Vector3(2.0f, 3.0f, 4.0f), target);
			if (reserved.Id != 1200 || World.All.Count != 2
				|| World.Query<HealthComponent>().Count != 2
				|| World.Query<SpriteAnimator>().Count != 2)
				throw new InvalidOperationException("World create/query bridge failed.");
			GetComponent<Rigidbody2D>().LinearVelocity = new Vector2(4.0f, 5.0f);
			Rigidbody2D body = GetComponent<Rigidbody2D>();
			body.Enabled = false;
			body.BodyType = RigidbodyBodyType.Dynamic;
			body.FixedRotation = true;
			if (body.Enabled || body.BodyType != RigidbodyBodyType.Dynamic
				|| !body.FixedRotation)
				throw new InvalidOperationException("Rigidbody2D property round trip failed.");
			BoxCollider2D box = GetComponent<BoxCollider2D>();
			box.Size = new Vector2(2.0f, 3.0f);
			box.Density = 2.5f;
			if (!box.Size.Equals(new Vector2(2.0f, 3.0f)) || box.Density != 2.5f)
				throw new InvalidOperationException("BoxCollider2D property round trip failed.");
			CircleCollider2D circle = GetComponent<CircleCollider2D>();
			circle.Radius = 1.25f;
			if (circle.Radius != 1.25f)
				throw new InvalidOperationException("CircleCollider2D property round trip failed.");
			DistanceJoint2D joint = GetComponent<DistanceJoint2D>();
			joint.Distance = 4.5f;
			joint.ConnectedEntity = target;
			if (joint.Distance != 4.5f || joint.ConnectedEntity != target)
				throw new InvalidOperationException("DistanceJoint2D property round trip failed.");
			SpriteRenderer sprite = GetComponent<SpriteRenderer>();
			sprite.Color = new Color(0.1f, 0.2f, 0.3f, 0.4f);
			sprite.SortingLayer = -7;
			sprite.OrderInLayer = 42;
			if (!sprite.Color.Equals(new Color(0.1f, 0.2f, 0.3f, 0.4f)) ||
				sprite.SortingLayer != -7 || sprite.OrderInLayer != 42)
				throw new InvalidOperationException("SpriteRenderer property round trip failed.");
			if (!Entity.HasComponent<SpriteAnimator>())
				throw new InvalidOperationException("SpriteAnimator should be queryable.");
			Entity.RemoveComponent<SpriteAnimator>();
			Entity.AddComponent<SpriteAnimator>();
			SpriteAnimator animator = GetComponent<SpriteAnimator>();
			animator.Enabled = false;
			animator.Speed = 0.0f;
			if (animator.Enabled || animator.Speed != 0.0f)
				throw new InvalidOperationException("SpriteAnimator property round trip failed.");
			if (!animator.Play("运行😀", restart: false)
				|| !animator.IsPlaying || animator.CurrentFrame != 3)
				throw new InvalidOperationException("SpriteAnimator Play bridge failed.");
			animator.Stop();
			if (animator.IsPlaying)
				throw new InvalidOperationException("SpriteAnimator Stop bridge failed.");
			animator.SetBool("Grounded", false);
			animator.SetInt("Lives", 7);
			animator.SetFloat("Speed", 1.25f);
			animator.SetTrigger("Jump");
			animator.ResetTrigger("Jump");
			if (animator.CurrentState != "Running")
				throw new InvalidOperationException("SpriteAnimator state bridge failed.");
			Camera camera = GetComponent<Camera>();
			camera.Primary = true;
			camera.OrthographicSize = 12.0f;
			if (!camera.Primary || camera.OrthographicSize != 12.0f)
				throw new InvalidOperationException("Camera property round trip failed.");
			if (!Entity.HasComponent<HealthComponent>())
				throw new InvalidOperationException("HealthComponent should initially exist.");
			Entity.RemoveComponent<HealthComponent>();
			if (Entity.HasComponent<HealthComponent>())
				throw new InvalidOperationException("HealthComponent remove did not apply.");
			HealthComponent health = Entity.AddComponent<HealthComponent>();
			if (!Entity.HasComponent<HealthComponent>())
				throw new InvalidOperationException("HealthComponent add did not apply.");
			health.Maximum = 150;
			health.Current = 75;
			health.Invulnerable = true;
			HealthComponent roundTrip = Entity.GetComponent<HealthComponent>();
			if (roundTrip.Maximum != 150 || roundTrip.Current != 75
				|| !roundTrip.Invulnerable)
				throw new InvalidOperationException("HealthComponent property round trip failed.");
			if (SceneManager.ActiveScene.Handle != ActiveSceneHandle ||
				SceneManager.ActiveBuildIndex != ActiveSceneBuildIndex ||
				!SceneManager.LoadScene(new SceneAsset(8001)) ||
				!SceneManager.LoadScene(4) || !SceneManager.ReloadActiveScene())
				throw new InvalidOperationException("SceneManager bridge returned an unexpected value.");
			if (!Instantiate(new PrefabAsset(9001), new Vector3(6.0f, 7.0f, 8.0f),
				Entity))
				throw new InvalidOperationException("Prefab instantiate bridge returned false.");
		}
	}

	private sealed class BackgroundThreadApiProbe : TomCatBehaviour
	{
		protected override void OnCreate()
		{
			AssertWrongThread(() => _ = Entity.IsValid, "Entity.IsValid");
			AssertWrongThread(() => _ = Input.IsKeyHeld(KeyCode.Space), "Input.IsKeyHeld");
			AssertWrongThread(() => InputContext.Enable(InputContext.UI),
				"InputContext.Enable");
			AssertWrongThread(() => _ = Physics2D.Raycast(Vector2.Zero, Vector2.One),
				"Physics2D.Raycast");
		}
	}

	private sealed class ConstructorInputProbe : TomCatBehaviour
	{
		internal ConstructorInputProbe() => _ = Input.IsKeyHeld(KeyCode.Space);
	}

	private sealed class ExtendedInputProbe : TomCatBehaviour
	{
		private InputActionMap _gameplayMap = null!;
		private InputActionMap _uiMap = null!;
		internal InputAction Action { get; private set; } = null!;
		internal InputAction AlternativeButton { get; private set; } = null!;
		internal InputAction Axis { get; private set; } = null!;
		internal InputAction ClampedAxis { get; private set; } = null!;
		internal bool RequestUiExclusive { get; set; }
		internal bool RequestClearExclusive { get; set; }
		internal bool RequestGameplayResume { get; set; }
		internal bool RequestRuntimeCaptureTest { get; set; }
		internal bool RequestRuntimeCaptureRestore { get; set; }
		internal int StartedCount { get; private set; }
		internal int PerformedCount { get; private set; }
		internal int CanceledCount { get; private set; }
		internal int UiStartedCount { get; private set; }
		internal int UiCanceledCount { get; private set; }
		internal int AxisStartedCount { get; private set; }
		internal int AxisPerformedCount { get; private set; }
		internal int AxisCanceledCount { get; private set; }

		protected override void OnCreate()
		{
			Check(Input.IsWindowFocused, "V2 input focus state");
			Equal(new Vector2(1.25f, -2.5f), Input.ScrollDelta,
				"V2 input scroll delta");
			Check(Input.IsGamepadConnected() && Input.WasGamepadConnected()
				&& !Input.WasGamepadDisconnected(), "V2 gamepad hot-plug state");
			Equal("Regression Pad", Input.GetGamepadName(), "V2 gamepad name");
			Check(Input.IsGamepadButtonHeld(GamepadButton.South),
				"V2 gamepad standard button");
			Check(MathF.Abs(Input.GetGamepadAxis(GamepadAxis.LeftX) - 0.5f) < 1.0e-6f,
				"V2 gamepad dead-zone normalization");
			Equal(-1.0f, Input.GetGamepadAxisRaw(GamepadAxis.LeftTrigger),
				"V2 idle trigger raw value");
			Equal(0.0f, Input.GetGamepadAxis(GamepadAxis.LeftTrigger),
				"V2 idle trigger normalized value");
			s_extendedLeftTriggerRaw = 1.0f;
			Equal(1.0f, Input.GetGamepadAxisRaw(GamepadAxis.LeftTrigger),
				"V2 fully pressed trigger raw value");
			Equal(1.0f, Input.GetGamepadAxis(GamepadAxis.LeftTrigger),
				"V2 fully pressed trigger normalized value");
			s_extendedLeftTriggerRaw = -1.0f;

			InputContext.Reset();
			InputContext.Disable(InputContext.UI);
			_gameplayMap = new InputActionMap("Regression.Gameplay",
				InputContext.Gameplay);
			Action = _gameplayMap.AddAction("Jump")
				.AddBinding(InputBinding.Mouse(MouseButton.Left));
			Action.Started += _ => ++StartedCount;
			Action.Performed += _ => ++PerformedCount;
			Action.Canceled += _ => ++CanceledCount;
			AlternativeButton = _gameplayMap.AddAction("AlternativeButton")
				.AddBinding(InputBinding.Mouse(MouseButton.Left, 0.3f))
				.AddBinding(InputBinding.Mouse(MouseButton.Left, 0.3f));
			Axis = _gameplayMap.AddAction("Axis", InputActionType.Axis1D)
				.AddBinding(InputBinding.GamepadButton(GamepadButton.South, scale: 0.3f))
				.AddBinding(InputBinding.GamepadButton(GamepadButton.South, scale: 0.4f));
			Axis.Started += _ => ++AxisStartedCount;
			Axis.Performed += _ => ++AxisPerformedCount;
			Axis.Canceled += _ => ++AxisCanceledCount;
			ClampedAxis = _gameplayMap.AddAction("ClampedAxis", InputActionType.Axis1D)
				.AddBinding(InputBinding.GamepadButton(GamepadButton.South, scale: 0.8f))
				.AddBinding(InputBinding.GamepadButton(GamepadButton.South, scale: 0.7f));

			string saved = _gameplayMap.ExportRebinds();
			Action.Rebind(0, InputBinding.Key(KeyCode.Enter));
			_gameplayMap.ImportRebinds(saved);
			Equal(InputBindingKind.MouseButton, Action.Bindings[0].Kind,
				"InputAction rebind JSON round trip");

			_uiMap = new InputActionMap("Regression.UI", InputContext.UI, priority: 100);
			InputAction uiAction = _uiMap.AddAction("Submit")
				.AddBinding(InputBinding.Mouse(MouseButton.Left));
			uiAction.Started += _ => ++UiStartedCount;
			uiAction.Canceled += _ => ++UiCanceledCount;
			_gameplayMap.Enable();
			_uiMap.Enable();
		}

		protected override void OnUpdate(float deltaTime)
		{
			if (RequestRuntimeCaptureTest)
			{
				RequestRuntimeCaptureTest = false;
				_uiMap.ConsumesInput = false;
				InputContext.Enable(InputContext.UI);
			}
			if (RequestRuntimeCaptureRestore)
			{
				RequestRuntimeCaptureRestore = false;
				InputContext.Disable(InputContext.UI);
				_uiMap.ConsumesInput = true;
			}
			if (RequestUiExclusive)
			{
				RequestUiExclusive = false;
				InputContext.ActivateExclusive(InputContext.UI);
			}
			if (RequestClearExclusive)
			{
				RequestClearExclusive = false;
				InputContext.ClearExclusive();
			}
			if (RequestGameplayResume)
			{
				RequestGameplayResume = false;
				InputContext.Disable(InputContext.UI);
				InputContext.Enable(InputContext.Gameplay);
			}
			InputActionRuntime.UpdateEnabled(ScriptRuntime.DomainCancellationToken);
		}

		protected override void OnDestroy()
		{
			_uiMap.Disable();
			_gameplayMap.Disable();
			InputContext.Reset();
		}
	}

	private sealed class RuntimeUIProxyProbe : TomCatBehaviour
	{
		internal bool Passed { get; private set; }

		protected override void OnCreate()
		{
			var text = new UIText(Entity) { Text = "开始 TomCat 😀" };
			text.FallbackFont = new AssetRef<FontAsset>(9102);
			text.EmojiFont = new AssetRef<FontAsset>(9103);
			var worldText = new TextRenderer(Entity);
			worldText.FallbackFont = new AssetRef<FontAsset>(9202);
			worldText.EmojiFont = new AssetRef<FontAsset>(9203);
			var button = new UIButton(Entity);
			button.Focus();
			UIRect rect = new RectTransform(Entity).RuntimeRect;
			Passed = text.Text == "开始 TomCat 😀"
				&& text.FallbackFont.Handle == 9102
				&& text.EmojiFont.Handle == 9103
				&& worldText.FallbackFont.Handle == 9202
				&& worldText.EmojiFont.Handle == 9203
				&& button.WasClickedThisFrame
				&& button.ClickSerial == RuntimeUIButtonClickSerial
				&& rect.Equals(new UIRect(10.0f, 20.0f, 300.0f, 80.0f))
				&& UIEventSystem.IsGameplayInputCaptured;
		}
	}

	private sealed class AudioSpatialProxyProbe : TomCatBehaviour
	{
		public bool Passed { get; private set; }
		protected override void OnCreate()
		{
			var audio = new AudioSource(Entity);
			audio.Streaming = true;
			audio.SpatialBlend = 0.75f;
			audio.MaxDistance = 80.0f;
			audio.MinDistance = 2.0f;
			Passed = audio.Streaming
				&& MathF.Abs(audio.SpatialBlend - 0.75f) < 1.0e-6f
				&& MathF.Abs(audio.MinDistance - 2.0f) < 1.0e-6f
				&& MathF.Abs(audio.MaxDistance - 80.0f) < 1.0e-6f;
		}
	}

	private sealed class ConstructorLogProbe : TomCatBehaviour
	{
		internal ConstructorLogProbe() => Log.Info("constructor");
	}
}
