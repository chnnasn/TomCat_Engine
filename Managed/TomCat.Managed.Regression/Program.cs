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
	private static int s_abortBatchCalls;
	private static NativeEntityHandleV1 s_abortBatchContext;
	private static string? s_abortBatchReason;
	private static bool s_exposeDeferredCallbackTransactions;
	private static ScriptSceneRuntime? s_callbackTransactionScene;
	private static ulong s_openCallbackTransactionToken;
	private static bool s_openCallbackTransactionAborted;
	private static ulong s_nextCallbackTransactionToken = 1;
	private static bool s_drainingCallbackTransactions;
	private static bool s_failNextCallbackTransactionBegin;
	private static bool s_failNextCallbackTransactionComplete;
	private static bool s_failNextAbortBatch;
	private static int s_callbackTransactionBeginCalls;
	private static int s_callbackTransactionCompleteCalls;
	private static readonly Queue<(ulong Token, bool Committed)>
		s_callbackTransactionAcks = [];
	private static readonly List<ulong> s_callbackTransactionBeginTokens = [];
	private static readonly List<ulong> s_callbackTransactionCompleteTokens = [];
	private static readonly List<ulong> s_callbackTransactionAbortTokens = [];
	private static readonly List<(ulong Token, bool Committed)>
		s_callbackTransactionResolvedAcks = [];
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
	private static bool s_extendedInputPulse;
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
	private static bool s_extensionPresent = true;
	private static int s_extensionCount;
	private static string s_extensionLabel = string.Empty;
	private static int s_extensionPropertyGetCalls;
	private static int s_extensionPropertySetCalls;
	private static int s_extensionStringGetCalls;
	private static int s_extensionStringSetCalls;
	private static bool s_returnMalformedExtensionUtf8;
	private static NativeEntityHandleV1 s_reservedGameplayEntity;
	private static NativeEntityHandleV1 s_gameplayParent;
	private static NativeEntityHandleV1 s_gameplayParentOwner;
	private static NativeVector3 s_localTransformPosition;
	private static bool s_gameplayActive = true;
	private static bool s_usePerEntityGameplayActivation;
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), bool> s_gameplayActiveByEntity = [];
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), bool> s_committedGameplayActiveByEntity = [];
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), NativeEntityHandleV1> s_gameplayParentByEntity = [];
	private static readonly Dictionary<(ulong SceneSessionId, ulong EntityId,
		ulong RuntimeGeneration), NativeEntityHandleV1> s_committedGameplayParentByEntity = [];
	private static int s_gameplayQueryCalls;
	private static int s_lastGameplayQueryComponent;
	private static ulong s_lastGameplayQueryRegisteredComponent;
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
	private static string s_worldText = "World";
	private static bool s_runtimeUIButtonFocused;
	private static bool s_runtimeUICaptured;
	private const ulong RuntimeUIButtonClickSerial = 41;
	private const ulong SchemaProviderId = 0x7a11000000000001UL;
	private const ulong SchemaAssetPropertyId = 0x7a11000000000002UL;
	private const string SchemaComponentStableName = "TomCat.HealthComponent";
	private const string SchemaComponentDisplayName = "Health";
	private const string SchemaMaximumStableName = "Maximum";
	private const string SchemaMaximumDisplayName = "Maximum Health";
	private const string SchemaAssetStableName = "Portrait";
	private const string SchemaAssetDisplayName = "Portrait Texture";
	private static readonly nint s_schemaComponentStable =
		Marshal.StringToCoTaskMemUTF8(SchemaComponentStableName);
	private static readonly nint s_schemaComponentDisplay =
		Marshal.StringToCoTaskMemUTF8(SchemaComponentDisplayName);
	private static readonly nint s_schemaMaximumStable =
		Marshal.StringToCoTaskMemUTF8(SchemaMaximumStableName);
	private static readonly nint s_schemaMaximumDisplay =
		Marshal.StringToCoTaskMemUTF8(SchemaMaximumDisplayName);
	private static readonly nint s_schemaAssetStable =
		Marshal.StringToCoTaskMemUTF8(SchemaAssetStableName);
	private static readonly nint s_schemaAssetDisplay =
		Marshal.StringToCoTaskMemUTF8(SchemaAssetDisplayName);
	private static readonly Dictionary<(ulong Component, ulong Property),
		NativePropertyValueV1> s_registeredRuntimeUIProperties = [];
	private static readonly Dictionary<(ulong Component, ulong Property),
		NativePropertyValueV1> s_registeredBuiltInProperties = [];
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
			VerifyDeferredCallbackTransactions(assembly, pdb);

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
			managed.DestroyAttachments != null && managed.InstantiateAttachments != null &&
			managed.ResolveDeferredCommandBatch != null && managed.InvokeMethod != null,
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
		byte[] inputEventsName = Encoding.UTF8.GetBytes("TomCat.InputEventsApiV1");
		byte[] applicationPathsName =
			Encoding.UTF8.GetBytes("TomCat.ApplicationPathsApiV1");
		byte[] componentName = Encoding.UTF8.GetBytes("TomCat.ComponentApiV1");
		byte[] componentStringName =
			Encoding.UTF8.GetBytes("TomCat.ComponentStringApiV1");
		byte[] deferredCommandsName =
			Encoding.UTF8.GetBytes("TomCat.DeferredCommandsApiV1");
		byte[] componentSchemaName =
			Encoding.UTF8.GetBytes("TomCat.ComponentSchemaApiV1");
		byte[] gameplayName = Encoding.UTF8.GetBytes("TomCat.GameplayApiV1");
		byte[] audioSpatialName = Encoding.UTF8.GetBytes("TomCat.AudioSpatialApiV1");
		byte[] runtimeUIName = Encoding.UTF8.GetBytes("TomCat.RuntimeUIApiV1");
		fixed (byte* missingPointer = missingName)
		fixed (byte* inputPointer = inputName)
		fixed (byte* inputEventsPointer = inputEventsName)
		fixed (byte* applicationPathsPointer = applicationPathsName)
		fixed (byte* componentPointer = componentName)
		fixed (byte* componentStringPointer = componentStringName)
		fixed (byte* deferredCommandsPointer = deferredCommandsName)
		fixed (byte* componentSchemaPointer = componentSchemaName)
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
				new NativeUtf8View(inputEventsPointer,
					(ulong)inputEventsName.Length), 2, null, 0, &required),
				"newer input events capability version rejection");
			Equal((uint)sizeof(NativeInputEventsApiV1), required,
				"input events capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(applicationPathsPointer,
					(ulong)applicationPathsName.Length), 2, null, 0, &required),
				"newer application paths capability version rejection");
			Equal((uint)sizeof(NativeApplicationPathsApiV1), required,
				"application paths capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(componentPointer, (ulong)componentName.Length), 2,
				null, 0, &required), "newer component capability version rejection");
			Equal((uint)sizeof(NativeComponentApiV1), required,
				"component capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(componentStringPointer,
					(ulong)componentStringName.Length), 2, null, 0, &required),
				"newer component string capability version rejection");
			Equal((uint)sizeof(NativeComponentStringApiV1), required,
				"component string capability required size");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(deferredCommandsPointer,
					(ulong)deferredCommandsName.Length), 2, null, 0, &required),
				"newer deferred commands capability version rejection");
			Equal((uint)sizeof(NativeDeferredCommandsApiV1), required,
				"deferred commands capability required size");
			NativeDeferredCommandsApiV1 deferredCommands = default;
			Equal(0, envelope.QueryCapability(
				new NativeUtf8View(deferredCommandsPointer,
					(ulong)deferredCommandsName.Length), 1, &deferredCommands,
				(uint)sizeof(NativeDeferredCommandsApiV1), &required),
				"deferred commands capability query");
			Equal(1U, deferredCommands.Version,
				"deferred commands capability version");
			Equal((uint)sizeof(NativeDeferredCommandsApiV1), deferredCommands.Size,
				"deferred commands capability size");
			Check(deferredCommands.AbortBatch != null,
				"deferred commands capability abort callback");
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(componentSchemaPointer,
					(ulong)componentSchemaName.Length), 2, null, 0, &required),
				"newer component schema capability version rejection");
			Equal((uint)sizeof(NativeComponentSchemaApiV1), required,
				"component schema capability required size");
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

		Check(ComponentSchema.IsAvailable,
			"managed component schema capability was not bound");
		Check(RegisteredComponentProperties.IsStringTransportAvailable,
			"managed component string capability was not bound");
		IReadOnlyList<ComponentSchemaInfo> schemas = ComponentSchema.GetComponents();
		Equal(1, schemas.Count, "managed component schema count");
		ComponentSchemaInfo healthSchema = schemas[0];
		Check(healthSchema.TypeId == HealthComponent.TypeId
			&& healthSchema.ProviderId == SchemaProviderId
			&& healthSchema.SchemaVersion == 3
			&& healthSchema.StableName == SchemaComponentStableName
			&& healthSchema.DisplayName == SchemaComponentDisplayName
			&& healthSchema.IsScriptAccessible && healthSchema.IsInspectorVisible,
			"managed component schema metadata round-trip");
		Equal(2, healthSchema.Properties.Count,
			"managed component property schema count");
		Check(healthSchema.Properties[0].PropertyId
				== HealthComponent.MaximumPropertyId
			&& healthSchema.Properties[0].Kind == ComponentPropertyKind.Int32
			&& !healthSchema.Properties[0].IsAssetReference
			&& healthSchema.Properties[0].StableName == SchemaMaximumStableName
			&& healthSchema.Properties[1].PropertyId == SchemaAssetPropertyId
			&& healthSchema.Properties[1].Kind == ComponentPropertyKind.UInt64
			&& healthSchema.Properties[1].IsAssetReference,
			"managed component property schema metadata round-trip");
		byte originalNameByte = *(byte*)s_schemaComponentStable;
		try
		{
			*(byte*)s_schemaComponentStable = (byte)'X';
			Check(healthSchema.StableName == SchemaComponentStableName,
				"managed schema snapshot retained a native UTF-8 pointer");
		}
		finally
		{
			*(byte*)s_schemaComponentStable = originalNameByte;
		}
		Check(ComponentSchema.TryGetComponent(HealthComponent.TypeId,
			out ComponentSchemaInfo? foundSchema)
			&& foundSchema?.SchemaVersion == 3,
			"managed component schema lookup");

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

		// The native frozen input snapshot can legitimately expose both edges
		// while the final held state is neutral. The action layer must preserve
		// that complete tap instead of rebuilding transitions from IsPressed.
		s_extendedInputPulse = true;
		probe.__Update(1.0f / 60.0f);
		Equal(4, probe.StartedCount, "fast tap InputAction Started count");
		Equal(4, probe.PerformedCount, "fast tap InputAction Performed count");
		Equal(4, probe.CanceledCount, "fast tap InputAction Canceled count");
		Check(!probe.Action.IsPressed && probe.Action.WasPressedThisFrame
			&& probe.Action.WasReleasedThisFrame,
			"InputAction preserves a complete same-frame press and release");
		s_extendedInputPulse = false;
		probe.__Destroy();
		VerifyInputActionResetReentrancy();
	}

	private static void VerifyInputActionResetReentrancy()
	{
		using var scope = ScriptExecutionContext.Enter(
			new Entity(SceneSession, 94, RuntimeGeneration), CancellationToken.None);
		s_extendedInputPulse = false;
		s_runtimeUICaptured = false;
		s_extendedInputPressed = true;

		var activeMap = new InputActionMap("Regression.ReentrantActive");
		InputAction activeFirst = activeMap.AddAction("First")
			.AddBinding(InputBinding.Mouse(MouseButton.Left));
		InputAction activeSecond = activeMap.AddAction("Second")
			.AddBinding(InputBinding.Mouse(MouseButton.Left));
		int activeFirstStarted = 0;
		int activeSecondStarted = 0;
		int activeFirstCanceled = 0;
		int activeSecondCanceled = 0;
		bool reactivateOnCancel = false;
		activeFirst.Started += _ => ++activeFirstStarted;
		activeSecond.Started += _ => ++activeSecondStarted;
		activeFirst.Canceled += _ =>
		{
			++activeFirstCanceled;
			if (reactivateOnCancel)
			{
				reactivateOnCancel = false;
				activeMap.Active = true;
			}
		};
		activeSecond.Canceled += _ => ++activeSecondCanceled;
		activeMap.Enable();
		try
		{
			activeMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Check(activeFirst.IsPressed && activeSecond.IsPressed,
				"two-action Active reentrancy setup");
			activeMap.Active = false;
			reactivateOnCancel = true;
			activeMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Check(activeMap.Active && !activeFirst.IsPressed && !activeSecond.IsPressed,
				"Canceled reactivation left a later action actuated");
			Equal(1, activeFirstCanceled,
				"Active reset cancels the first action once");
			Equal(1, activeSecondCanceled,
				"Active reset cancels every action despite first-handler reactivation");
			activeMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Equal(2, activeFirstStarted,
				"reactivated first action restarts on held input");
			Equal(2, activeSecondStarted,
				"reactivated later action restarts after pre-cleared cancellation");
		}
		finally
		{
			reactivateOnCancel = false;
			s_extendedInputPressed = false;
			activeMap.Disable();
		}

		var captureMap = new InputActionMap("Regression.ReentrantCapture");
		InputAction captureFirst = captureMap.AddAction("First")
			.AddBinding(InputBinding.Mouse(MouseButton.Left));
		InputAction captureSecond = captureMap.AddAction("Second")
			.AddBinding(InputBinding.Mouse(MouseButton.Left));
		int captureFirstStarted = 0;
		int captureSecondStarted = 0;
		int captureFirstCanceled = 0;
		int captureSecondCanceled = 0;
		bool reenableOnCancel = false;
		captureFirst.Started += _ => ++captureFirstStarted;
		captureSecond.Started += _ => ++captureSecondStarted;
		captureFirst.Canceled += _ =>
		{
			++captureFirstCanceled;
			if (reenableOnCancel)
			{
				reenableOnCancel = false;
				captureMap.Enable();
			}
		};
		captureSecond.Canceled += _ => ++captureSecondCanceled;
		captureMap.Enable();
		try
		{
			s_extendedInputPressed = true;
			captureMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Check(captureFirst.IsPressed && captureSecond.IsPressed,
				"two-action Runtime UI capture setup");
			s_runtimeUICaptured = true;
			reenableOnCancel = true;
			captureMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Check(captureMap.Enabled && !captureFirst.IsPressed && !captureSecond.IsPressed,
				"Canceled re-enable left a later UI-captured action actuated");
			Equal(1, captureFirstCanceled,
				"Runtime UI capture cancels the first action once");
			Equal(1, captureSecondCanceled,
				"Runtime UI capture cancels every action despite first-handler re-enable");
			s_runtimeUICaptured = false;
			captureMap.Update(InputActionUpdatePhase.DisplayFrame, [], true);
			Equal(2, captureFirstStarted,
				"re-enabled first action restarts after Runtime UI capture");
			Equal(2, captureSecondStarted,
				"re-enabled later action restarts after Runtime UI capture");
		}
		finally
		{
			reenableOnCancel = false;
			s_runtimeUICaptured = false;
			s_extendedInputPressed = false;
			captureMap.Disable();
		}
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
		s_extensionPresent = true;
		s_extensionCount = 0;
		s_extensionLabel = string.Empty;
		s_extensionPropertyGetCalls = 0;
		s_extensionPropertySetCalls = 0;
		s_extensionStringGetCalls = 0;
		s_extensionStringSetCalls = 0;
		s_returnMalformedExtensionUtf8 = false;
		s_reservedGameplayEntity = default;
		s_gameplayParent = new NativeEntityHandleV1(SceneSession, 42, RuntimeGeneration);
		s_gameplayParentOwner = default;
		s_localTransformPosition = default;
		s_gameplayActive = true;
		s_gameplayQueryCalls = 0;
		s_lastGameplayQueryComponent = 0;
		s_lastGameplayQueryRegisteredComponent = 0;
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
		s_registeredBuiltInProperties.Clear();

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
		Equal(1, s_extensionPropertyGetCalls,
			"public plugin numeric property read");
		Equal(1, s_extensionPropertySetCalls,
			"public plugin numeric property write");
		Equal(4, s_extensionStringGetCalls,
			"public plugin UTF-8 probe/copy and malformed read calls");
		Equal(1, s_extensionStringSetCalls,
			"public plugin UTF-8 property write");
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
		Equal(0, s_lastGameplayQueryComponent,
			"World.Query<SpriteAnimator> legacy component type");
		Equal(0x9f00000000000007UL, s_lastGameplayQueryRegisteredComponent,
			"World.Query<SpriteAnimator> registered component type");
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
		// The simple non-hierarchy stub stores one global Parent for this proxy test.
		// Do not leak it into later ScriptSceneRuntime hierarchy walks.
		s_gameplayParent = default;
		s_gameplayParentOwner = default;
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
		Check(good.Methods.SequenceEqual(["HandleButtonClick", "ThrowButtonClick"]),
			"public parameterless void event-method metadata");

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
		scene.InvokeMethod(100, "HandleButtonClick");
		Equal(1, scene.ReadFieldValue(100, "ButtonClicks"),
			"persistent event method invocation");
		Equal(0, scene.ReadFieldValue(200, "ButtonClicks"),
			"event invocation did not select the exact attachment");
		AssertSuffix(scene.CallbackTrace, "100:Event:HandleButtonClick");
		scene.SetEnabled(100, false);
		scene.InvokeMethod(100, "HandleButtonClick");
		Equal(2, scene.ReadFieldValue(100, "ButtonClicks"),
			"disabled behaviour did not receive a UnityEvent-style callback");
		scene.SetEnabled(100, true);
		scene.InvokeMethod(100, "HandleButtonClick");
		Equal(3, scene.ReadFieldValue(100, "ButtonClicks"),
			"reenabled event target did not remain callable");
		Throws<KeyNotFoundException>(() => scene.InvokeMethod(100, "MissingMethod"),
			"event invocation must reject methods outside generated metadata");

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
		Entity throwingEventEntity = new(SceneSession, 6, RuntimeGeneration);
		scene.InstantiateAttachments([
			new ScriptAttachment(throwingEventEntity, 275, 1001, true)
		], "{\"attachments\":[{\"attachmentId\":275,\"fields\":[]}]}");
		int eventDiagnostics = s_diagnostics;
		scene.InvokeMethod(275, "ThrowButtonClick");
		Equal(ScriptInstanceState.Faulted, scene.GetInstanceState(275),
			"throwing event callback did not fault only its target attachment");
		Equal(eventDiagnostics + 1, s_diagnostics,
			"throwing event callback did not emit one diagnostic");
		scene.InvokeMethod(100, "HandleButtonClick");
		Equal(4, scene.ReadFieldValue(100, "ButtonClicks"),
			"one throwing listener prevented another attachment callback");
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
		Equal(9001L, scene.ReadFieldValue(100, "FixedInputFirstSequence"),
			"managed FixedUpdate reads the active ordered input batch");
		Equal(2, scene.ReadFieldValue(100, "FixedInputEventCount"),
			"managed FixedUpdate reads every active input event");
		Equal(fixedFrameBefore + 1, Time.FixedFrameCount, "fixed clock advances once per step");
		Equal(1.0f / 50.0f, Time.FixedDeltaTime, "fixed delta clock");
		Equal(false, Time.InFixedUpdate, "fixed-update clock scope is restored");
        scene.DispatchPhysicsEvents([
            new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, first, other),
            new ScriptPhysicsEvent(NativePhysicsEventKindV1.TriggerExit, second, other)
        ]);
        Equal(1, scene.ReadFieldValue(100, "CollisionEnters"), "collision dispatch");
		Equal(9001L, scene.ReadFieldValue(100, "CollisionInputFirstSequence"),
			"managed collision callback reads the enclosing fixed-step input batch");
		Equal(2, scene.ReadFieldValue(100, "CollisionInputEventCount"),
			"managed collision callback reads every fixed-step input event");
        Equal(1, scene.ReadFieldValue(200, "TriggerExits"), "trigger dispatch");
		VerifyFixedInputActionEvaluation(domain);

        int disablesBeforeDisable = Convert.ToInt32(
            scene.ReadFieldValue(100, "Disables"));
        scene.SetEnabled(100, false);
        Equal(disablesBeforeDisable + 1, scene.ReadFieldValue(100, "Disables"),
            "disable callback");
        scene.UpdateAll(0.01f);
        Equal(2, scene.ReadFieldValue(100, "Updates"), "disabled instance is skipped");
        Equal(3, scene.ReadFieldValue(200, "Updates"), "enabled instance still updates");
		int enablesBeforeEnable = Convert.ToInt32(
			scene.ReadFieldValue(100, "Enables"));
		scene.SetEnabled(100, true);
		Equal(enablesBeforeEnable + 1, scene.ReadFieldValue(100, "Enables"),
			"re-enable callback");

		scene.DestroyAttachments([200]);
		AssertSuffix(scene.CallbackTrace, "200:OnDisable", "200:OnDestroy");
		Throws<KeyNotFoundException>(() => scene.GetInstanceState(200),
			"selectively destroyed attachment remained addressable");
		scene.UpdateAll(0.01f);
		Equal(3, scene.ReadFieldValue(100, "Updates"),
			"remaining attachment stopped after selective teardown");

		VerifySameBatchMutations(domain);
		VerifyHierarchyActivationConvergence(domain);
		VerifyCallbackBatchAbort(domain);

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
			"275:OnDestroy", "250:OnDestroy", "100:OnDestroy");

		domain.BeginUnload();
		Check(domainCancellation.IsCancellationRequested,
			"Play Domain cancellation token was not cancelled before ALC unload");
		return domain;
    }

	private static void VerifyCallbackBatchAbort(ScriptDomain domain)
	{
		const ulong throwingSceneSession = 31;
		const ulong throwingGeneration = 27;
		const ulong throwingAttachment = 1300;
		Entity throwingEntity = new(throwingSceneSession, 5, throwingGeneration);
		ScriptSceneRuntime throwingScene = domain.CreateSceneRuntime(
			throwingSceneSession, throwingGeneration);
		throwingScene.InstantiateAll([
			new ScriptAttachment(throwingEntity, throwingAttachment, 1001, true)
		]);
		throwingScene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":1300,"fields":[
			 {"fieldId":"","name":"QueueNameThenThrowOnUpdate","type":"Bool","value":true}
			]}]}
			""");
		throwingScene.InvokeCreateAll();
		ResetAbortBatchCapture();
		s_entityTextSetterCalls = 0;
		int diagnosticsBefore = s_diagnostics;

		throwingScene.UpdateAll(0.01f);

		Equal(1, s_entityTextSetterCalls,
			"faulting callback queued its mutation before throwing");
		Equal(1, s_abortBatchCalls,
			"faulting callback aborts its deferred batch exactly once");
		AssertAbortBatchContext(throwingEntity,
			"faulting callback abort context");
		Check(s_abortBatchReason?.Contains(
			"intentional managed callback transaction failure",
			StringComparison.Ordinal) == true,
			"faulting callback abort reason omitted the managed exception");
		Equal(ScriptInstanceState.Faulted,
			throwingScene.GetInstanceState(throwingAttachment),
			"faulting callback must remain quarantined");
		Equal(diagnosticsBefore + 1, s_diagnostics,
			"faulting callback reports one diagnostic");
		throwingScene.DestroyAll();

		const ulong validationSceneSession = 32;
		const ulong validationGeneration = 28;
		const ulong validationAttachment = 1301;
		Entity validationEntity = new(validationSceneSession, 6,
			validationGeneration);
		ScriptSceneRuntime validationScene = domain.CreateSceneRuntime(
			validationSceneSession, validationGeneration);
		validationScene.InstantiateAll([
			new ScriptAttachment(validationEntity, validationAttachment, 1001, true)
		]);
		validationScene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":1301,"fields":[
			 {"fieldId":"","name":"QueueNameThenCatchNullTagOnUpdate","type":"Bool","value":true}
			]}]}
			""");
		validationScene.InvokeCreateAll();
		ResetAbortBatchCapture();
		s_entityTextSetterCalls = 0;
		diagnosticsBefore = s_diagnostics;

		validationScene.UpdateAll(0.01f);

		Equal(1, s_entityTextSetterCalls,
			"caught validation callback queued its first mutation");
		Equal(1, validationScene.ReadFieldValue(validationAttachment,
			"CaughtMutationValidationExceptions"),
			"managed mutation validation exception was caught by the script");
		Equal(ScriptInstanceState.Ready,
			validationScene.GetInstanceState(validationAttachment),
			"caught mutation validation must not fault the script");
		Equal(diagnosticsBefore, s_diagnostics,
			"caught mutation validation emitted a managed diagnostic");
		Equal(1, s_abortBatchCalls,
			"caught mutation validation poisons its deferred batch exactly once");
		AssertAbortBatchContext(validationEntity,
			"caught mutation validation abort context");
		Equal("Entity.Tag value cannot be null", s_abortBatchReason,
			"caught mutation validation abort reason");

		validationScene.UpdateAll(0.01f);
		Equal(2, validationScene.ReadFieldValue(validationAttachment, "Updates"),
			"caught validation script remains dispatchable");
		Equal(1, s_abortBatchCalls,
			"one-shot caught validation does not re-abort later callbacks");
		validationScene.DestroyAll();
		VerifyDistanceJointValidationAbort(domain);
	}

	private static void VerifyDistanceJointValidationAbort(ScriptDomain domain)
	{
		const ulong capturedSceneSession = 33;
		const ulong capturedGeneration = 29;
		ScriptSceneRuntime captureScene = domain.CreateSceneRuntime(
			capturedSceneSession, capturedGeneration);
		captureScene.InstantiateAll([
			new ScriptAttachment(
				new Entity(capturedSceneSession, 7, capturedGeneration),
				1302, 1001, true)
		]);
		captureScene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":1302,"fields":[
			 {"fieldId":"","name":"CaptureCrossSceneJointTargetOnCreate","type":"Bool","value":true}
			]}]}
			""");
		captureScene.InvokeCreateAll();

		VerifyDistanceJointValidationAbortCase(domain,
			capturedSceneSession, capturedGeneration + 1, 1303,
			"stale runtime generation");
		VerifyDistanceJointValidationAbortCase(domain,
			capturedSceneSession + 1, capturedGeneration, 1304,
			"cross-scene entity");
		captureScene.DestroyAll();
	}

	private static void VerifyDistanceJointValidationAbortCase(
		ScriptDomain domain, ulong sceneSession, ulong runtimeGeneration,
		ulong attachmentId, string label)
	{
		Entity entity = new(sceneSession, 8, runtimeGeneration);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, runtimeGeneration);
		scene.InstantiateAll([
			new ScriptAttachment(entity, attachmentId, 1001, true)
		]);
		scene.ApplySerializedFields($$"""
			{"attachments":[{"attachmentId":{{attachmentId}},"fields":[
			 {"fieldId":"","name":"QueueNameThenCatchCrossSceneJointOnUpdate","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();
		ResetAbortBatchCapture();
		s_entityTextSetterCalls = 0;
		int diagnosticsBefore = s_diagnostics;

		scene.UpdateAll(0.01f);

		Equal(1, s_entityTextSetterCalls,
			$"{label} queued its earlier deferred write");
		Equal(1, scene.ReadFieldValue(attachmentId,
			"CaughtMutationValidationExceptions"),
			$"{label} validation exception was caught by the script");
		Equal(ScriptInstanceState.Ready, scene.GetInstanceState(attachmentId),
			$"{label} caught validation keeps the script Ready");
		Equal(diagnosticsBefore, s_diagnostics,
			$"{label} caught validation emits no managed diagnostic");
		Equal(1, s_abortBatchCalls,
			$"{label} poisons the deferred batch exactly once");
		AssertAbortBatchContext(entity, $"{label} abort context");
		Equal(
			"DistanceJoint2D.ConnectedEntity must belong to the same scene runtime",
			s_abortBatchReason, $"{label} abort reason");

		scene.UpdateAll(0.01f);
		Equal(2, scene.ReadFieldValue(attachmentId, "Updates"),
			$"{label} script remains dispatchable");
		Equal(1, s_abortBatchCalls,
			$"{label} one-shot validation does not re-abort");
		scene.DestroyAll();
	}

	private static void ResetAbortBatchCapture()
	{
		s_abortBatchCalls = 0;
		s_abortBatchContext = default;
		s_abortBatchReason = null;
	}

	private static void AssertAbortBatchContext(Entity expected, string message)
	{
		Check(s_abortBatchContext.SceneSessionId == expected.SceneSessionId
			&& s_abortBatchContext.EntityId == expected.Id
			&& s_abortBatchContext.RuntimeGeneration == expected.RuntimeGeneration,
			message);
	}

	private static void VerifyFixedInputActionEvaluation(ScriptDomain domain)
	{
		const ulong sceneSession = 22;
		const ulong generation = 18;
		const ulong attachment = 970;
		s_extendedInputPressed = false;
		s_extendedInputPulse = false;
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		try
		{
			scene.InstantiateAll([
				new ScriptAttachment(new Entity(sceneSession, 1, generation),
					attachment, 1001, true)
			]);
			scene.ApplySerializedFields(
				"{\"attachments\":[{\"attachmentId\":970,\"fields\":[" +
				"{\"fieldId\":\"\",\"name\":\"EnableInputActionProbe\"," +
				"\"type\":\"Bool\",\"value\":true}]}]}");
			scene.InvokeCreateAll();

			// A display frame with no physics step may consume the display action
			// state. The next fixed step must still observe its own queued edge.
			scene.UpdateAll(1.0f / 144.0f);
			s_extendedInputPressed = true;
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(attachment,
				"UpdateActionPressedObservations"),
				"display action observes the press");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateActionStartedEvents"),
				"display action raises Started outside fixed update");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateActionPerformedEvents"),
				"display action raises Performed outside fixed update");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateAxisPerformedEvents"),
				"display Axis1D raises Performed on the display timeline");
			Equal(0, scene.ReadFieldValue(attachment,
				"FixedActionPressedObservations"),
				"display evaluation does not mutate fixed action state");

			scene.FixedUpdateAll(1.0f / 60.0f);
			Equal(1, scene.ReadFieldValue(attachment,
				"FixedActionPressedObservations"),
				"first fixed step observes a press already seen by Update");
			scene.DispatchPhysicsEvents([
				new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter,
					new Entity(sceneSession, 1, generation),
					new Entity(sceneSession, 2, generation))
			]);
			Equal(true, scene.ReadFieldValue(attachment,
				"CollisionObservedFixedTimeline"),
				"collision callback did not retain the native fixed-step timeline");
			Equal(1, scene.ReadFieldValue(attachment,
				"CollisionActionPressedObservations"),
				"collision callback did not share the current fixed action edge");
			Equal(false, Time.InFixedUpdate,
				"physics callback leaked its managed fixed timeline scope");
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);
			Equal(1, scene.ReadFieldValue(attachment,
				"FixedActionHeldObservations"),
				"first fixed step observes the held action");

			scene.FixedUpdateAll(1.0f / 60.0f);
			scene.DispatchPhysicsEvents([
				new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter,
					new Entity(sceneSession, 1, generation),
					new Entity(sceneSession, 2, generation))
			]);
			Equal(1, scene.ReadFieldValue(attachment,
				"CollisionActionPressedObservations"),
				"catch-up collision callback replayed the fixed action edge");
			Equal(1, scene.ReadFieldValue(attachment,
				"FixedActionPressedObservations"),
				"catch-up fixed step does not replay the press");
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);
			Equal(2, scene.ReadFieldValue(attachment,
				"FixedActionHeldObservations"),
				"catch-up fixed step retains the held action");

			// Release polling state remains independent across display and fixed
			// timelines. Public events continue to run only on the display timeline.
			s_extendedInputPressed = false;
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(attachment,
				"UpdateActionReleasedObservations"),
				"display action observes the release");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateActionCanceledEvents"),
				"display action raises Canceled outside fixed update");
			scene.FixedUpdateAll(1.0f / 60.0f);
			scene.FixedUpdateAll(1.0f / 60.0f);
			Equal(1, scene.ReadFieldValue(attachment,
				"FixedActionReleasedObservations"),
				"fixed action consumes the release exactly once");
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);

			// A complete press/release between polls is one pulse in the first
			// fixed step and is neutral in the catch-up step.
			s_extendedInputPulse = true;
			scene.FixedUpdateAll(1.0f / 60.0f);
			s_extendedInputPulse = false;
			VerifyFixedInputActionCounts(scene, attachment, 2, 2);
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);
			scene.FixedUpdateAll(1.0f / 60.0f);
			VerifyFixedInputActionCounts(scene, attachment, 2, 2);
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);
		}
		finally
		{
			s_extendedInputPressed = false;
			s_extendedInputPulse = false;
			scene.DestroyAll();
		}

		VerifyFixedDisableCancellation(domain);
		VerifyDisplayDisableReentrancy(domain);
	}

	private static void VerifyFixedDisableCancellation(ScriptDomain domain)
	{
		const ulong sceneSession = 23;
		const ulong generation = 19;
		const ulong attachment = 971;
		s_extendedInputPressed = false;
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		try
		{
			scene.InstantiateAll([
				new ScriptAttachment(new Entity(sceneSession, 1, generation),
					attachment, 1001, true)
			]);
			scene.ApplySerializedFields(
				"{\"attachments\":[{\"attachmentId\":971,\"fields\":[" +
				"{\"fieldId\":\"\",\"name\":\"EnableInputActionProbe\"," +
				"\"type\":\"Bool\",\"value\":true}," +
				"{\"fieldId\":\"\",\"name\":\"DisableInputActionMapOnFixedUpdate\"," +
				"\"type\":\"Bool\",\"value\":true}," +
				"{\"fieldId\":\"\",\"name\":\"ThrowOnInputActionCanceled\"," +
				"\"type\":\"Bool\",\"value\":true}]}]}");
			scene.InvokeCreateAll();
			scene.UpdateAll(1.0f / 144.0f);
			s_extendedInputPressed = true;
			scene.UpdateAll(1.0f / 144.0f);
			Equal(0, scene.ReadFieldValue(attachment, "UpdateActionCanceledEvents"),
				"actuated display action starts without cancellation");

			scene.FixedUpdateAll(1.0f / 60.0f);
			Equal(0, scene.ReadFieldValue(attachment, "UpdateActionCanceledEvents"),
				"FixedUpdate map disable must not raise a fixed-timeline event");
			s_extendedInputPressed = false;
			int diagnosticsBefore = s_diagnostics;
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(attachment, "UpdateActionCanceledEvents"),
				"FixedUpdate map disable defers one Canceled event to display update");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateAxisCanceledEvents"),
				"a throwing deferred Canceled handler must not drop later actions");
			Equal("Jump>Axis", scene.ReadFieldValue(attachment,
				"InputCancellationTrace"),
				"deferred Canceled handlers preserve action insertion order");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"deferred Canceled handler exception is reported exactly once");
			Equal(0, scene.ReadFieldValue(attachment,
				"UpdateActionReleasedObservations"),
				"map disable must clear display polling release state");

			scene.FixedUpdateAll(1.0f / 60.0f);
			Equal(0, scene.ReadFieldValue(attachment,
				"FixedActionReleasedObservations"),
				"map disable must clear fixed polling release state");
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(attachment, "UpdateActionCanceledEvents"),
				"deferred map cancellation is delivered exactly once");
			Equal(1, scene.ReadFieldValue(attachment, "UpdateAxisCanceledEvents"),
				"later display updates do not replay deferred cancellation");
			Equal(0, scene.ReadFieldValue(attachment,
				"UpdateActionReleasedObservations"),
				"disabled display polling state remains neutral");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"later updates do not replay the cancellation exception");
			VerifyFixedActionEventsRemainDisplayOnly(scene, attachment);
		}
		finally
		{
			s_extendedInputPressed = false;
			scene.DestroyAll();
		}
	}

	private static void VerifyDisplayDisableReentrancy(ScriptDomain domain)
	{
		const ulong sceneSession = 24;
		const ulong generation = 20;
		s_extendedInputPressed = false;
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(sceneSession, generation);
		try
		{
			scene.InstantiateAll([
				new ScriptAttachment(new Entity(sceneSession, 1, generation),
					972, 1001, true)
			]);
			scene.ApplySerializedFields(
				"{\"attachments\":[{\"attachmentId\":972,\"fields\":[" +
				"{\"fieldId\":\"\",\"name\":\"EnableInputActionProbe\"," +
				"\"type\":\"Bool\",\"value\":true}," +
				"{\"fieldId\":\"\",\"name\":\"DisableInputActionMapOnStarted\"," +
				"\"type\":\"Bool\",\"value\":true}]}]}");
			scene.InvokeCreateAll();
			scene.UpdateAll(1.0f / 144.0f);
			s_extendedInputPressed = true;
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(972, "UpdateActionStartedEvents"),
				"Started handler runs before its reentrant map disable");
			Equal(0, scene.ReadFieldValue(972, "UpdateActionPerformedEvents"),
				"reentrant disable suppresses the remaining action phase");
			Equal(1, scene.ReadFieldValue(972, "UpdateActionCanceledEvents"),
				"reentrant disable cancels the newly actuated action once");
			Equal(0, scene.ReadFieldValue(972, "UpdateAxisPerformedEvents"),
				"reentrant disable suppresses later actions in the map");
			Equal(0, scene.ReadFieldValue(972, "UpdateActionHeldObservations"),
				"reentrant disable clears display held state before OnUpdate");
			Equal(0, scene.ReadFieldValue(972, "UpdateActionPressedObservations"),
				"reentrant disable clears display press state before OnUpdate");
			scene.UpdateAll(1.0f / 144.0f);
			Equal(1, scene.ReadFieldValue(972, "UpdateActionCanceledEvents"),
				"disabled map does not replay a reentrant cancellation");
		}
		finally
		{
			s_extendedInputPressed = false;
			scene.DestroyAll();
		}

		const ulong throwingSceneSession = 25;
		ScriptSceneRuntime throwingScene = domain.CreateSceneRuntime(
			throwingSceneSession, generation + 1);
		try
		{
			throwingScene.InstantiateAll([
				new ScriptAttachment(new Entity(throwingSceneSession, 1, generation + 1),
					973, 1001, true)
			]);
			throwingScene.ApplySerializedFields(
				"{\"attachments\":[{\"attachmentId\":973,\"fields\":[" +
				"{\"fieldId\":\"\",\"name\":\"EnableInputActionProbe\"," +
				"\"type\":\"Bool\",\"value\":true}," +
				"{\"fieldId\":\"\",\"name\":\"DisableInputActionMapWhenPressedInUpdate\"," +
				"\"type\":\"Bool\",\"value\":true}," +
				"{\"fieldId\":\"\",\"name\":\"ThrowOnInputActionCanceled\"," +
				"\"type\":\"Bool\",\"value\":true}]}]}");
			throwingScene.InvokeCreateAll();
			throwingScene.UpdateAll(1.0f / 144.0f);
			s_extendedInputPressed = true;
			int diagnosticsBefore = s_diagnostics;
			throwingScene.UpdateAll(1.0f / 144.0f);
			Equal(1, throwingScene.ReadFieldValue(973,
				"UpdateActionCanceledEvents"),
				"synchronous disable dispatches the first cancellation once");
			Equal(1, throwingScene.ReadFieldValue(973,
				"UpdateAxisCanceledEvents"),
				"throwing synchronous cancellation does not drop later actions");
			Equal("Jump>Axis", throwingScene.ReadFieldValue(973,
				"InputCancellationTrace"),
				"synchronous cancellations preserve insertion order");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"throwing synchronous cancellation is diagnosed exactly once");
			int held = (int)throwingScene.ReadFieldValue(973,
				"UpdateActionHeldObservations")!;
			int pressed = (int)throwingScene.ReadFieldValue(973,
				"UpdateActionPressedObservations")!;
			int started = (int)throwingScene.ReadFieldValue(973,
				"UpdateActionStartedEvents")!;
			int performed = (int)throwingScene.ReadFieldValue(973,
				"UpdateActionPerformedEvents")!;
			throwingScene.UpdateAll(1.0f / 144.0f);
			Equal(held, throwingScene.ReadFieldValue(973,
				"UpdateActionHeldObservations"),
				"disabled map does not retain a held polling state");
			Equal(pressed, throwingScene.ReadFieldValue(973,
				"UpdateActionPressedObservations"),
				"disabled map does not retain a press edge");
			Equal(started, throwingScene.ReadFieldValue(973,
				"UpdateActionStartedEvents"),
				"disabled map does not replay Started");
			Equal(performed, throwingScene.ReadFieldValue(973,
				"UpdateActionPerformedEvents"),
				"disabled map does not replay Performed");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"disabled map does not replay the cancellation exception");
		}
		finally
		{
			s_extendedInputPressed = false;
			throwingScene.DestroyAll();
		}
	}

	private static void VerifyFixedInputActionCounts(ScriptSceneRuntime scene,
		ulong attachment, int presses, int releases)
	{
		Equal(presses, scene.ReadFieldValue(attachment,
			"FixedActionPressedObservations"), "fixed action press count");
		Equal(releases, scene.ReadFieldValue(attachment,
			"FixedActionReleasedObservations"), "fixed action release count");
	}

	private static void VerifyFixedActionEventsRemainDisplayOnly(
		ScriptSceneRuntime scene, ulong attachment)
	{
		Equal(0, scene.ReadFieldValue(attachment, "FixedActionStartedEvents"),
			"fixed action must not raise Started");
		Equal(0, scene.ReadFieldValue(attachment, "FixedActionPerformedEvents"),
			"fixed action must not raise Performed");
		Equal(0, scene.ReadFieldValue(attachment, "FixedActionCanceledEvents"),
			"fixed action must not raise Canceled");
		Equal(0, scene.ReadFieldValue(attachment, "FixedAxisPerformedEvents"),
			"held fixed Axis1D must not raise Performed per substep");
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
		Equal(0, disableScene.ReadFieldValue(400, "Disables"),
			"self-disable lifecycle must wait for native transaction commit");
		disableScene.SetEnabled(400, false);
		disableScene.ResolveDeferredCommandBatch(true);
		Equal(1, disableScene.ReadFieldValue(400, "Disables"),
			"native commit applies self-disable lifecycle exactly once");
		disableScene.UpdateAll(0.01f);
		Equal(0, disableScene.ReadFieldValue(400, "Updates"),
			"committed self-disable suppresses subsequent Update");
		disableScene.DestroyAll();

		ScriptSceneRuntime reentrantEnableScene = domain.CreateSceneRuntime(28, 24);
		reentrantEnableScene.InstantiateAll([
			new ScriptAttachment(new Entity(28, 1, 24), 990, 1001, true)
		]);
		reentrantEnableScene.ApplySerializedFields(
			"{\"attachments\":[{\"attachmentId\":990,\"fields\":[" +
			"{\"fieldId\":\"\",\"name\":\"EnableBehaviourOnDisable\",\"type\":\"Bool\",\"value\":true}]}]}");
		reentrantEnableScene.InvokeCreateAll();
		reentrantEnableScene.SetEnabled(990, false);
		reentrantEnableScene.ResolveDeferredCommandBatch(true);
		reentrantEnableScene.UpdateAll(0.01f);
		reentrantEnableScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter,
				new Entity(28, 1, 24), new Entity(28, 2, 24))
		]);
		Equal(1, reentrantEnableScene.ReadFieldValue(990, "Disables"),
			"authoritative disable invokes OnDisable exactly once");
		Equal(0, reentrantEnableScene.ReadFieldValue(990, "Updates"),
			"reentrant next-batch enable must wait for authoritative OnEnable");
		Equal(0, reentrantEnableScene.ReadFieldValue(990, "CollisionEnters"),
			"reentrant next-batch enable must not receive physics callbacks");
		reentrantEnableScene.SetEnabled(990, true);
		reentrantEnableScene.ResolveDeferredCommandBatch(true);
		reentrantEnableScene.UpdateAll(0.01f);
		reentrantEnableScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter,
				new Entity(28, 1, 24), new Entity(28, 2, 24))
		]);
		Equal(2, reentrantEnableScene.ReadFieldValue(990, "Enables"),
			"authoritative next-batch enable invokes OnEnable exactly once");
		Equal(1, reentrantEnableScene.ReadFieldValue(990, "Updates"),
			"authoritatively enabled instance resumes Update");
		Equal(1, reentrantEnableScene.ReadFieldValue(990, "CollisionEnters"),
			"authoritatively enabled instance resumes physics callbacks");
		reentrantEnableScene.DestroyAll();

		ScriptSceneRuntime abortScene = domain.CreateSceneRuntime(27, 23);
		abortScene.InstantiateAll([
			new ScriptAttachment(new Entity(27, 1, 23), 980, 1001, true)
		]);
		abortScene.ApplySerializedFields(
			"{\"attachments\":[{\"attachmentId\":980,\"fields\":[" +
			"{\"fieldId\":\"\",\"name\":\"DisableOnCollisionEnter\",\"type\":\"Bool\",\"value\":true}]}]}");
		abortScene.InvokeCreateAll();
		Entity abortEntity = new(27, 1, 23);
		Entity abortOther = new(27, 2, 23);
		abortScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, abortEntity, abortOther),
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, abortEntity, abortOther)
		]);
		Equal(1, abortScene.ReadFieldValue(980, "CollisionEnters"),
			"pending self-disable projects within the callback batch");
		Equal(0, abortScene.ReadFieldValue(980, "Disables"),
			"pending self-disable has no lifecycle before transaction resolution");
		abortScene.ResolveDeferredCommandBatch(false);
		Equal(0, abortScene.ReadFieldValue(980, "Disables"),
			"native abort clears the projection without invoking OnDisable");
		abortScene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(NativePhysicsEventKindV1.CollisionEnter, abortEntity, abortOther)
		]);
		Equal(2, abortScene.ReadFieldValue(980, "CollisionEnters"),
			"aborted self-disable no longer suppresses later callbacks");
		abortScene.ResolveDeferredCommandBatch(false);
		abortScene.DestroyAll();

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
		Equal(ScriptInstanceState.Ready, removeScene.GetInstanceState(500),
			"self-removal lifecycle must wait for native transaction commit");
		Equal(1, removeScene.CallbackTrace.Count(value => value == "500:OnCollisionEnter2D"),
			"self-removal must suppress later events in the same native batch");
		Equal(0, removeScene.CallbackTrace.Count(value => value == "500:OnDisable"),
			"pending self-removal must not invoke OnDisable");
		Equal(0, removeScene.CallbackTrace.Count(value => value == "500:OnDestroy"),
			"pending self-removal must not invoke OnDestroy");
		removeScene.DestroyAttachments([500]);
		removeScene.ResolveDeferredCommandBatch(true);
		AssertSuffix(removeScene.CallbackTrace, "500:OnDisable", "500:OnDestroy");
		Throws<KeyNotFoundException>(() => removeScene.GetInstanceState(500),
			"committed self-removal remained addressable");
		removeScene.DestroyAll();
	}

	private static void VerifyHierarchyActivationConvergence(ScriptDomain domain)
	{
		s_usePerEntityGameplayActivation = true;
		try
		{
			VerifyCreateLifecycleProjection(domain);
			VerifyHierarchyUpdateSuppression(domain);
			VerifyHierarchyFixedUpdateSuppression(domain);
			VerifyHierarchyPhysicsSuppression(domain);
			VerifyProjectedReparentSuppression(domain);
			VerifyHierarchyAbortDoesNotConverge(domain);
			VerifyLifecycleCallbackReversal(domain);
			VerifyLifecycleOscillationAdvancesAcrossCommits(domain);
		}
		finally
		{
			s_gameplayActiveByEntity.Clear();
			s_committedGameplayActiveByEntity.Clear();
			s_gameplayParentByEntity.Clear();
			s_committedGameplayParentByEntity.Clear();
			s_gameplayActive = true;
			s_usePerEntityGameplayActivation = false;
		}
	}

	private static void VerifyCreateLifecycleProjection(ScriptDomain domain)
	{
		VerifyCreateLifecycleProjectionAbort(domain);
		VerifyCreateLifecycleProjectionCommit(domain);
	}

	private static void VerifyCreateLifecycleProjectionAbort(ScriptDomain domain)
	{
		const ulong sceneSession = 31;
		const ulong generation = 27;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 6);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, generation);
		scene.EnableCallbackTraceForTesting();
		scene.InstantiateAll([
			new ScriptAttachment(new Entity(sceneSession, 1, generation),
				1100, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 2, generation),
				1101, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 3, generation),
				1102, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 4, generation),
				1103, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 5, generation),
				1104, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 6, generation),
				1105, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[
			 {"attachmentId":1100,"fields":[
			  {"fieldId":"","name":"DisableBehaviourOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1101,"fields":[
			  {"fieldId":"","name":"DisableEntityOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1102,"fields":[
			  {"fieldId":"","name":"RemoveOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1103,"fields":[
			  {"fieldId":"","name":"DestroyEntityOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1104,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnCreate","type":"Bool","value":true}]}
			]}
			""");
		scene.InvokeCreateAll();

		foreach (ulong attachmentId in new ulong[] { 1100, 1101, 1102, 1103, 1105 })
			Equal(0, scene.ReadFieldValue(attachmentId, "Enables"),
				$"create projection invoked premature OnEnable for {attachmentId}");
		Equal(1, scene.ReadFieldValue(1104, "Enables"),
			"unaffected create-time parent-disabling behaviour did not enable");

		AbortGameplayHierarchyProjection(scene);
		foreach (ulong attachmentId in new ulong[] { 1100, 1101, 1102, 1103, 1104, 1105 })
			Equal(1, scene.ReadFieldValue(attachmentId, "Enables"),
				$"aborted create projection did not converge OnEnable for {attachmentId}");

		scene.DestroyAll();
		foreach (ulong attachmentId in new ulong[] { 1100, 1101, 1102, 1103, 1104, 1105 })
		{
			Equal(1, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDisable"),
				$"aborted create projection did not pair OnDisable for {attachmentId}");
			Equal(1, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDestroy"),
				$"aborted create projection did not destroy {attachmentId} exactly once");
		}
	}

	private static void VerifyCreateLifecycleProjectionCommit(ScriptDomain domain)
	{
		const ulong sceneSession = 32;
		const ulong generation = 28;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 6);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, generation);
		scene.EnableCallbackTraceForTesting();
		scene.InstantiateAll([
			new ScriptAttachment(new Entity(sceneSession, 1, generation),
				1200, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 2, generation),
				1201, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 3, generation),
				1202, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 4, generation),
				1203, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 5, generation),
				1204, 1001, true),
			new ScriptAttachment(new Entity(sceneSession, 6, generation),
				1205, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[
			 {"attachmentId":1200,"fields":[
			  {"fieldId":"","name":"DisableBehaviourOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1201,"fields":[
			  {"fieldId":"","name":"DisableEntityOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1202,"fields":[
			  {"fieldId":"","name":"RemoveOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1203,"fields":[
			  {"fieldId":"","name":"DestroyEntityOnCreate","type":"Bool","value":true}]},
			 {"attachmentId":1204,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnCreate","type":"Bool","value":true}]}
			]}
			""");
		scene.InvokeCreateAll();

		foreach (ulong attachmentId in new ulong[] { 1200, 1201, 1202, 1203, 1205 })
			Equal(0, scene.ReadFieldValue(attachmentId, "Enables"),
				$"committed create projection invoked premature OnEnable for {attachmentId}");
		Equal(1, scene.ReadFieldValue(1204, "Enables"),
			"unaffected committed create-time behaviour did not enable");

		// Model the authoritative callbacks native emits while replaying the
		// validated batch, then acknowledge the transaction.
		scene.SetEnabled(1200, false);
		scene.DestroyAttachments([1202, 1203]);
		CommitGameplayHierarchyProjection(scene);

		foreach (ulong attachmentId in new ulong[] { 1200, 1201, 1205 })
		{
			Equal(0, scene.ReadFieldValue(attachmentId, "Enables"),
				$"inactive create commit enabled {attachmentId}");
			Equal(0, scene.ReadFieldValue(attachmentId, "Disables"),
				$"inactive create commit emitted unpaired OnDisable for {attachmentId}");
		}
		foreach (ulong attachmentId in new ulong[] { 1202, 1203 })
		{
			Equal(0, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnEnable"),
				$"removed create commit enabled {attachmentId}");
			Equal(0, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDisable"),
				$"removed create commit emitted unpaired OnDisable for {attachmentId}");
			Equal(1, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDestroy"),
				$"removed create commit did not destroy {attachmentId} exactly once");
		}

		// Later authoritative reactivation must begin each surviving lifecycle
		// exactly once; teardown must then produce its matching OnDisable.
		scene.SetEnabled(1200, true);
		var entityHandle = new NativeEntityHandleV1(
			sceneSession, 2, generation);
		var parentHandle = new NativeEntityHandleV1(
			sceneSession, 90, generation);
		s_gameplayActiveByEntity[GameplayEntityKey(entityHandle)] = true;
		s_gameplayActiveByEntity[GameplayEntityKey(parentHandle)] = true;
		CommitGameplayHierarchyProjection(scene);
		foreach (ulong attachmentId in new ulong[] { 1200, 1201, 1204, 1205 })
			Equal(1, scene.ReadFieldValue(attachmentId, "Enables"),
				$"authoritative reactivation did not enable {attachmentId} exactly once");

		scene.DestroyAll();
		foreach (ulong attachmentId in new ulong[] { 1200, 1201, 1204, 1205 })
			Equal(1, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDisable"),
				$"reactivated create lifecycle did not pair OnDisable for {attachmentId}");
		foreach (ulong attachmentId in new ulong[] { 1202, 1203 })
			Equal(0, scene.CallbackTrace.Count(
				value => value == $"{attachmentId}:OnDisable"),
				$"never-enabled removed lifecycle emitted OnDisable for {attachmentId}");
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
		Equal(false, scene.ReadFieldValue(910,
			"TargetActiveInHierarchyAfterMutation"),
			"public ActiveInHierarchy missed the pending parent disable");
		Equal(0, scene.ReadFieldValue(910, "LateUpdates"),
			"parent disable suppresses initiating script LateUpdate");
		Equal(0, scene.ReadFieldValue(911, "LateUpdates"),
			"parent disable suppresses sibling LateUpdate");
		Equal(0, scene.ReadFieldValue(910, "Disables"),
			"projected parent disable does not run initiating lifecycle");
		Equal(0, scene.ReadFieldValue(911, "Disables"),
			"projected parent disable does not run sibling lifecycle");
		CommitGameplayHierarchyProjection(scene);
		Equal(1, scene.ReadFieldValue(910, "Disables"),
			"committed parent disable transitions initiating script once");
		Equal(1, scene.ReadFieldValue(911, "Disables"),
			"committed parent disable transitions sibling script once");
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
		Equal(0, scene.ReadFieldValue(920, "Disables"),
			"projected FixedUpdate parent disable has no lifecycle");
		Equal(0, scene.ReadFieldValue(921, "Disables"),
			"projected FixedUpdate sibling disable has no lifecycle");
		CommitGameplayHierarchyProjection(scene);
		Equal(1, scene.ReadFieldValue(920, "Disables"),
			"committed FixedUpdate parent disable transitions initiating script");
		Equal(1, scene.ReadFieldValue(921, "Disables"),
			"committed FixedUpdate parent disable transitions sibling script");
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
		Equal(0, scene.ReadFieldValue(930, "Disables"),
			"projected collision parent disable has no lifecycle");
		Equal(0, scene.ReadFieldValue(931, "Disables"),
			"projected collision sibling disable has no lifecycle");
		CommitGameplayHierarchyProjection(scene);
		Equal(1, scene.ReadFieldValue(930, "Disables"),
			"committed collision parent disable transitions initiating script");
		Equal(1, scene.ReadFieldValue(931, "Disables"),
			"committed collision parent disable transitions sibling script");
		scene.DestroyAll();
	}

	private static void VerifyProjectedReparentSuppression(ScriptDomain domain)
	{
		const ulong sceneSession = 29;
		const ulong generation = 25;
		ResetGameplayActivationGraph();
		var inactiveParent = new NativeEntityHandleV1(
			sceneSession, 90, generation);
		s_gameplayActiveByEntity[GameplayEntityKey(inactiveParent)] = false;
		s_committedGameplayActiveByEntity[GameplayEntityKey(inactiveParent)] = false;
		Entity initiator = new(sceneSession, 1, generation);
		Entity target = new(sceneSession, 2, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(initiator, 1000, 1001, true),
			new ScriptAttachment(target, 1001, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":1000,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":2},
			  {"fieldId":"","name":"ReparentParent","type":"Entity","value":90},
			  {"fieldId":"","name":"ReparentTargetOnUpdate","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();

		scene.UpdateAll(0.01f);
		Equal(1, scene.ReadFieldValue(1000, "Updates"),
			"reparenting callback executes before its projected mutation");
		Equal(0, scene.ReadFieldValue(1001, "Updates"),
			"projected Parent under an inactive ancestor suppresses later Update");
		Equal(false, scene.ReadFieldValue(1000,
			"TargetActiveInHierarchyAfterMutation"),
			"public ActiveInHierarchy missed the projected reparent");
		Equal(1, scene.ReadFieldValue(1000, "LateUpdates"),
			"unrelated initiator remains active after projected reparent");
		Equal(0, scene.ReadFieldValue(1001, "LateUpdates"),
			"projected reparent suppresses target LateUpdate");
		Equal(0, scene.ReadFieldValue(1001, "Disables"),
			"projected reparent does not run lifecycle before commit");

		CommitGameplayHierarchyProjection(scene);
		Equal(0, scene.ReadFieldValue(1000, "Disables"),
			"reparent commit leaves the initiator hierarchy active");
		Equal(1, scene.ReadFieldValue(1001, "Disables"),
			"reparent commit converges target OnDisable exactly once");
		scene.DestroyAll();
	}

	private static void VerifyHierarchyAbortDoesNotConverge(ScriptDomain domain)
	{
		const ulong sceneSession = 30;
		const ulong generation = 26;
		ResetGameplayActivationGraph();
		ConfigureGameplayParent(sceneSession, generation, 90, 1, 2);
		Entity first = new(sceneSession, 1, generation);
		Entity second = new(sceneSession, 2, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, generation);
		scene.InstantiateAll([
			new ScriptAttachment(first, 1010, 1001, true),
			new ScriptAttachment(second, 1011, 1001, true)
		]);
		scene.ApplySerializedFields("""
			{"attachments":[{"attachmentId":1010,"fields":[
			  {"fieldId":"","name":"Target","type":"Entity","value":90},
			  {"fieldId":"","name":"DisableTargetOnCollisionEnter","type":"Bool","value":true}
			]}]}
			""");
		scene.InvokeCreateAll();

		scene.DispatchPhysicsEvents([
			new ScriptPhysicsEvent(
				NativePhysicsEventKindV1.CollisionEnter, first, second)
		]);
		Equal(1, scene.ReadFieldValue(1010, "CollisionEnters"),
			"aborted hierarchy regression queues one parent disable");
		Equal(0, scene.ReadFieldValue(1011, "CollisionEnters"),
			"projected parent disable suppresses the sibling in the same batch");
		Equal(0, scene.ReadFieldValue(1010, "Disables"),
			"pending parent disable does not run initiating lifecycle");
		Equal(0, scene.ReadFieldValue(1011, "Disables"),
			"pending parent disable does not run sibling lifecycle");

		AbortGameplayHierarchyProjection(scene);
		Equal(0, scene.ReadFieldValue(1010, "Disables"),
			"hierarchy abort does not invoke initiating OnDisable");
		Equal(0, scene.ReadFieldValue(1011, "Disables"),
			"hierarchy abort does not invoke sibling OnDisable");
		scene.UpdateAll(0.01f);
		Equal(1, scene.ReadFieldValue(1010, "Updates"),
			"aborted parent disable restores initiating Update");
		Equal(1, scene.ReadFieldValue(1011, "Updates"),
			"aborted parent disable restores sibling Update");
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
		Equal(1, enableScene.ReadFieldValue(940, "Enables"),
			"OnEnable self-disable enable count");
		Equal(0, enableScene.ReadFieldValue(940, "Disables"),
			"OnEnable projected self-disable waits for commit");
		enableScene.UpdateAll(0.01f);
		Equal(0, enableScene.ReadFieldValue(940, "Updates"),
			"OnEnable projected self-disable suppresses Update");
		CommitGameplayHierarchyProjection(enableScene);
		Equal(1, enableScene.ReadFieldValue(940, "Disables"),
			"committed OnEnable self-disable invokes OnDisable");
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
		Equal(1, disableScene.ReadFieldValue(950, "Enables"),
			"projected disable does not invoke OnEnable");
		Equal(0, disableScene.ReadFieldValue(950, "Disables"),
			"projected disable waits for commit");
		Equal(0, disableScene.ReadFieldValue(950, "Updates"),
			"projected disable suppresses Update");
		CommitGameplayHierarchyProjection(disableScene);
		Equal(1, disableScene.ReadFieldValue(950, "Disables"),
			"disable commit invokes OnDisable once");
		disableScene.UpdateAll(0.01f);
		Equal(0, disableScene.ReadFieldValue(950, "Updates"),
			"OnDisable reactivation waits for its next commit");
		CommitGameplayHierarchyProjection(disableScene);
		Equal(2, disableScene.ReadFieldValue(950, "Enables"),
			"next commit invokes OnEnable for reentrant activation");
		disableScene.UpdateAll(0.01f);
		Equal(1, disableScene.ReadFieldValue(950, "Updates"),
			"committed reactivation resumes Update");
		disableScene.DestroyAll();
	}

	private static void VerifyLifecycleOscillationAdvancesAcrossCommits(
		ScriptDomain domain)
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

		Equal(ScriptInstanceState.Ready, scene.GetInstanceState(960),
			"projected lifecycle reversal remains healthy before commit");
		Equal(1, scene.ReadFieldValue(960, "Enables"),
			"initial activation invokes OnEnable once");
		Equal(0, scene.ReadFieldValue(960, "Disables"),
			"initial projected disable does not converge early");
		CommitGameplayHierarchyProjection(scene);
		Equal(1, scene.ReadFieldValue(960, "Disables"),
			"first commit advances oscillation through OnDisable once");
		CommitGameplayHierarchyProjection(scene);
		Equal(2, scene.ReadFieldValue(960, "Enables"),
			"second commit advances oscillation through OnEnable once");
		Equal(ScriptInstanceState.Ready, scene.GetInstanceState(960),
			"cross-batch lifecycle reversal is not a same-commit oscillation");
		Equal(diagnosticsBefore, s_diagnostics,
			"cross-batch lifecycle reversal emits no false diagnostic");
		scene.UpdateAll(0.01f);
		Equal(0, scene.ReadFieldValue(960, "Updates"),
			"next projected disable suppresses Update until another commit");
		scene.DestroyAll();
	}

	private static void ResetGameplayActivationGraph()
	{
		s_gameplayActiveByEntity.Clear();
		s_committedGameplayActiveByEntity.Clear();
		s_gameplayParentByEntity.Clear();
		s_committedGameplayParentByEntity.Clear();
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
			s_committedGameplayParentByEntity[GameplayEntityKey(child)] = parent;
		}
	}

	private static void CommitGameplayHierarchyProjection(
		ScriptSceneRuntime scene)
	{
		s_committedGameplayActiveByEntity.Clear();
		foreach (var pair in s_gameplayActiveByEntity)
			s_committedGameplayActiveByEntity[pair.Key] = pair.Value;
		s_committedGameplayParentByEntity.Clear();
		foreach (var pair in s_gameplayParentByEntity)
			s_committedGameplayParentByEntity[pair.Key] = pair.Value;
		scene.ResolveDeferredCommandBatch(true);
	}

	private static void AbortGameplayHierarchyProjection(
		ScriptSceneRuntime scene)
	{
		s_gameplayActiveByEntity.Clear();
		foreach (var pair in s_committedGameplayActiveByEntity)
			s_gameplayActiveByEntity[pair.Key] = pair.Value;
		s_gameplayParentByEntity.Clear();
		foreach (var pair in s_committedGameplayParentByEntity)
			s_gameplayParentByEntity[pair.Key] = pair.Value;
		scene.ResolveDeferredCommandBatch(false);
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


	private static void VerifyDeferredCallbackTransactions(byte[] assembly,
		byte[] pdb)
	{
		s_exposeDeferredCallbackTransactions = true;
		NativeApiV1 native = CreateCompleteNativeApi();
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
		delegate* unmanaged[Cdecl]<NativeApiV1*, ManagedApiV1*, int> bootstrap =
			&EntryPoint.GetManagedApi;

		byte[] capabilityName = Encoding.UTF8.GetBytes(
			"TomCat.DeferredCallbackTransactionsApiV1");
		fixed (byte* name = capabilityName)
		{
			uint required = 0;
			Equal(-4, envelope.QueryCapability(
				new NativeUtf8View(name, (ulong)capabilityName.Length), 2,
				null, 0, &required),
				"newer deferred callback transaction capability rejection");
			Equal((uint)sizeof(NativeDeferredCallbackTransactionsApiV1), required,
				"deferred callback transaction capability required size");
			NativeDeferredCallbackTransactionsApiV1 transactions = default;
			Equal(0, envelope.QueryCapability(
				new NativeUtf8View(name, (ulong)capabilityName.Length), 1,
				&transactions,
				(uint)sizeof(NativeDeferredCallbackTransactionsApiV1), &required),
				"deferred callback transaction capability query");
			Check(transactions.Version == 1
				&& transactions.Size
					== (uint)sizeof(NativeDeferredCallbackTransactionsApiV1)
				&& transactions.BeginCallback != null
				&& transactions.CompleteCallback != null,
				"deferred callback transaction capability table");
		}
		Equal(0, bootstrap(&envelope.V1, &managed),
			"GetManagedApi with deferred callback transactions");
		Check(NativeBridge.SupportsDeferredCallbackTransactions,
			"deferred callback transaction capability was not bound");

		var domain = new ScriptDomain(ScriptDomainKind.Play);
		domain.LoadProjectAssembly(assembly, pdb);
		try
		{
			s_usePerEntityGameplayActivation = true;
			ResetGameplayActivationGraph();
			ResetCallbackTransactionHarness();

			const ulong oscillatingSceneSession = 41;
			const ulong oscillatingGeneration = 37;
			const ulong oscillatingAttachment = 1400;
			ScriptSceneRuntime oscillatingScene = domain.CreateSceneRuntime(
				oscillatingSceneSession, oscillatingGeneration);
			s_callbackTransactionScene = oscillatingScene;
			oscillatingScene.InstantiateAll([
				new ScriptAttachment(
					new Entity(oscillatingSceneSession, 1, oscillatingGeneration),
					oscillatingAttachment, 1001, true)
			]);
			oscillatingScene.ApplySerializedFields("""
				{"attachments":[{"attachmentId":1400,"fields":[
				  {"fieldId":"","name":"DisableSelfOnEnable","type":"Bool","value":true},
				  {"fieldId":"","name":"EnableSelfOnDisable","type":"Bool","value":true}
				]}]}
				""");
			int diagnosticsBefore = s_diagnostics;
			oscillatingScene.InvokeCreateAll();
			Equal(ScriptInstanceState.Faulted,
				oscillatingScene.GetInstanceState(oscillatingAttachment),
				"callback FIFO lifecycle oscillation was not quarantined");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"callback FIFO lifecycle oscillation diagnostic count");
			Equal(4, oscillatingScene.ReadFieldValue(
				oscillatingAttachment, "Enables"),
				"callback FIFO lifecycle oscillation enable budget");
			Equal(4, oscillatingScene.ReadFieldValue(
				oscillatingAttachment, "Disables"),
				"callback FIFO lifecycle oscillation disable budget");
			Check(s_callbackTransactionBeginCalls < 32
				&& s_callbackTransactionBeginCalls
					== s_callbackTransactionCompleteCalls,
				"callback FIFO lifecycle oscillation did not terminate");
			int beginsBeforeTeardown = s_callbackTransactionBeginCalls;
			int completesBeforeTeardown = s_callbackTransactionCompleteCalls;
			oscillatingScene.DestroyAll();
			Equal(beginsBeforeTeardown, s_callbackTransactionBeginCalls,
				"DestroyAll opened a callback transaction after Scene removal");
			Equal(completesBeforeTeardown, s_callbackTransactionCompleteCalls,
				"DestroyAll completed a callback transaction after Scene removal");

			VerifyInputActionCallbackTransactions(domain);

			ResetGameplayActivationGraph();
			ResetCallbackTransactionHarness();
			const ulong failingSceneSession = 42;
			const ulong failingGeneration = 38;
			const ulong failingAttachment = 1401;
			ScriptSceneRuntime failingScene = domain.CreateSceneRuntime(
				failingSceneSession, failingGeneration);
			s_callbackTransactionScene = failingScene;
			failingScene.InstantiateAll([
				new ScriptAttachment(
					new Entity(failingSceneSession, 1, failingGeneration),
					failingAttachment, 1001, true)
			]);
			failingScene.ApplySerializedFields("{\"attachments\":[]}");
			failingScene.InvokeCreateAll();
			s_failNextCallbackTransactionComplete = true;
			Throws<InvalidOperationException>(
				() => failingScene.UpdateAll(0.01f),
				"failed native Complete must surface a protocol fault");
			FieldInfo awaitingField = typeof(ScriptSceneRuntime).GetField(
				"_awaitingCallbackProjections",
				BindingFlags.Instance | BindingFlags.NonPublic)
				?? throw new MissingFieldException(
					nameof(ScriptSceneRuntime),
					"_awaitingCallbackProjections");
			object awaiting = awaitingField.GetValue(failingScene)
				?? throw new InvalidOperationException(
					"callback projection FIFO was null");
			int awaitingCount = (int)(awaiting.GetType().GetProperty("Count")
				?.GetValue(awaiting)
				?? throw new MissingMemberException(
					"callback projection FIFO Count"));
			Equal(0, awaitingCount,
				"failed native Complete left a stale managed FIFO frame");
			int failureBeginsBeforeTeardown = s_callbackTransactionBeginCalls;
			int failureCompletesBeforeTeardown =
				s_callbackTransactionCompleteCalls;
			failingScene.DestroyAll();
			Equal(failureBeginsBeforeTeardown,
				s_callbackTransactionBeginCalls,
				"failed callback Scene teardown reopened a transaction");
			Equal(failureCompletesBeforeTeardown,
				s_callbackTransactionCompleteCalls,
				"failed callback Scene teardown completed a transaction");
		}
		finally
		{
			s_callbackTransactionScene = null;
			s_exposeDeferredCallbackTransactions = false;
			s_usePerEntityGameplayActivation = false;
			s_openCallbackTransactionToken = 0;
			s_drainingCallbackTransactions = false;
			s_callbackTransactionAcks.Clear();
			domain.BeginUnload();
		}
		Check(PollUntilUnloaded(domain),
			"deferred callback transaction test Domain leaked");
	}

	private static void VerifyInputActionCallbackTransactions(
		ScriptDomain domain)
	{
		VerifyInputActionSubscriberIsolation(domain);
		VerifyNestedInputActionCancellationTransaction(domain);
		VerifyInputActionCallbackProtocolFailures(domain);
	}

	private static void VerifyInputActionSubscriberIsolation(ScriptDomain domain)
	{
		const ulong sceneSession = 43;
		const ulong generation = 39;
		const ulong attachment = 1402;
		var fixture = CreateInputActionTransactionFixture(domain,
			sceneSession, generation, attachment, "Regression.CallbackSubscribers");
		Entity firstTarget = new(sceneSession, 2, generation);
		Entity secondTarget = new(sceneSession, 3, generation);
		Entity thirdTarget = new(sceneSession, 4, generation);
		SetFakeGameplayActive(firstTarget, true);
		SetFakeGameplayActive(secondTarget, true);
		SetFakeGameplayActive(thirdTarget, true);
		int firstRuns = 0;
		int secondRuns = 0;
		int thirdRuns = 0;
		ulong firstToken = 0;
		ulong secondToken = 0;
		ulong thirdToken = 0;
		bool secondSawFirstCommit = false;
		bool secondSawOwnProjection = false;
		bool thirdSawSecondRollback = false;
		fixture.Action.Started += _ =>
		{
			++firstRuns;
			firstToken = s_openCallbackTransactionToken;
			firstTarget.ActiveSelf = false;
		};
		fixture.Action.Started += _ =>
		{
			++secondRuns;
			secondToken = s_openCallbackTransactionToken;
			secondSawFirstCommit = !firstTarget.ActiveSelf;
			secondTarget.ActiveSelf = false;
			secondSawOwnProjection = !secondTarget.ActiveSelf;
			throw new InvalidOperationException(
				"intentional InputAction subscriber failure");
		};
		fixture.Action.Started += _ =>
		{
			++thirdRuns;
			thirdToken = s_openCallbackTransactionToken;
			thirdSawSecondRollback = secondTarget.ActiveSelf;
			thirdTarget.ActiveSelf = false;
		};

		try
		{
			int diagnosticsBefore = s_diagnostics;
			s_extendedInputPressed = true;
			fixture.Scene.UpdateAll(1.0f / 60.0f);

			Equal(1, firstRuns,
				"first healthy InputAction subscriber runs once");
			Equal(1, secondRuns,
				"throwing InputAction subscriber runs once");
			Equal(1, thirdRuns,
				"later healthy InputAction subscriber survives an earlier failure");
			Check(firstToken != 0 && secondToken != 0 && thirdToken != 0
				&& firstToken != secondToken && secondToken != thirdToken,
				"InputAction subscribers did not receive distinct transactions");
			Check(secondSawFirstCommit,
				"second subscriber did not observe the first subscriber commit");
			Check(secondSawOwnProjection,
				"throwing subscriber did not observe its staged mutation");
			Check(thirdSawSecondRollback,
				"later subscriber did not observe the throwing subscriber rollback");
			Equal(ScriptInstanceState.Ready,
				fixture.Scene.GetInstanceState(attachment),
				"one failing InputAction subscriber faulted the script instance");
			Equal(diagnosticsBefore + 1, s_diagnostics,
				"throwing InputAction subscriber diagnostic count");
			Equal(1, s_abortBatchCalls,
				"throwing InputAction subscriber abort count");
			AssertAbortBatchContext(fixture.Owner,
				"throwing InputAction subscriber abort context");
			Check(s_abortBatchReason?.Contains(
				"intentional InputAction subscriber failure",
				StringComparison.Ordinal) == true,
				"throwing InputAction subscriber abort reason");
			Check(s_callbackTransactionAbortTokens.SequenceEqual([secondToken]),
				"throwing InputAction subscriber aborted the wrong transaction");

			Equal(false, ReadFakeGameplayActive(firstTarget, committed: false),
				"first healthy InputAction subscriber working state");
			Equal(false, ReadFakeGameplayActive(firstTarget, committed: true),
				"first healthy InputAction subscriber commit");
			Equal(true, ReadFakeGameplayActive(secondTarget, committed: false),
				"throwing InputAction subscriber working-state rollback");
			Equal(true, ReadFakeGameplayActive(secondTarget, committed: true),
				"throwing InputAction subscriber authoritative rollback");
			Equal(false, ReadFakeGameplayActive(thirdTarget, committed: false),
				"later healthy InputAction subscriber working state");
			Equal(false, ReadFakeGameplayActive(thirdTarget, committed: true),
				"later healthy InputAction subscriber commit");

			Check(s_callbackTransactionBeginTokens.Take(3).SequenceEqual(
				[firstToken, secondToken, thirdToken]),
				"InputAction subscriber Begin order");
			Check(s_callbackTransactionCompleteTokens.Take(3).SequenceEqual(
				[firstToken, secondToken, thirdToken]),
				"InputAction subscriber Complete order");
			var handlerAcks =
				s_callbackTransactionResolvedAcks.Take(3).ToArray();
			Equal(3, handlerAcks.Length,
				"InputAction subscriber acknowledgement count");
			Check(handlerAcks[0] == (firstToken, true)
				&& handlerAcks[1] == (secondToken, false)
				&& handlerAcks[2] == (thirdToken, true),
				"InputAction subscriber commit/abort acknowledgement order");
			AssertSuccessfulCallbackTransactionFifo(
				"InputAction subscriber transaction FIFO");
		}
		finally
		{
			DestroyInputActionTransactionFixture(domain, fixture.Scene,
				fixture.Owner, fixture.Map);
		}
	}

	private static void VerifyNestedInputActionCancellationTransaction(
		ScriptDomain domain)
	{
		const ulong sceneSession = 44;
		const ulong generation = 40;
		const ulong attachment = 1403;
		var fixture = CreateInputActionTransactionFixture(domain,
			sceneSession, generation, attachment, "Regression.NestedCancellation");
		Entity target = new(sceneSession, 2, generation);
		SetFakeGameplayActive(target, true);
		ulong startedToken = 0;
		ulong canceledToken = 0;
		int beginsBeforeDisable = -1;
		int beginsAfterDisable = -1;
		fixture.Action.Canceled += _ =>
		{
			canceledToken = s_openCallbackTransactionToken;
			target.ActiveSelf = false;
		};
		fixture.Action.Started += _ =>
		{
			startedToken = s_openCallbackTransactionToken;
			beginsBeforeDisable = s_callbackTransactionBeginCalls;
			fixture.Map.Disable();
			beginsAfterDisable = s_callbackTransactionBeginCalls;
		};

		try
		{
			int diagnosticsBefore = s_diagnostics;
			s_extendedInputPressed = true;
			fixture.Scene.UpdateAll(1.0f / 60.0f);

			Check(startedToken != 0 && canceledToken == startedToken,
				"nested Disable/Canceled did not share the Started transaction");
			Equal(beginsBeforeDisable, beginsAfterDisable,
				"nested Canceled opened another callback transaction");
			Check(!fixture.Map.Enabled,
				"Started handler did not disable its InputAction map");
			Equal(false, ReadFakeGameplayActive(target, committed: false),
				"nested Canceled working state");
			Equal(false, ReadFakeGameplayActive(target, committed: true),
				"nested Canceled mutation did not commit with Started");
			Equal(diagnosticsBefore, s_diagnostics,
				"healthy nested Canceled emitted a diagnostic");
			Equal(0, s_abortBatchCalls,
				"healthy nested Canceled aborted its outer transaction");
			Equal((startedToken, true),
				s_callbackTransactionResolvedAcks.First(
					ack => ack.Token == startedToken),
				"nested Disable/Canceled transaction acknowledgement");
			AssertSuccessfulCallbackTransactionFifo(
				"nested Disable/Canceled transaction FIFO");
		}
		finally
		{
			DestroyInputActionTransactionFixture(domain, fixture.Scene,
				fixture.Owner, fixture.Map);
		}
	}

	private static void VerifyInputActionCallbackProtocolFailures(
		ScriptDomain domain)
	{
		VerifyInputActionCallbackBeginFailure(domain);
		VerifyInputActionCallbackCompleteFailure(domain);
		VerifyInputActionCallbackAbortFailure(domain);
	}

	private static void VerifyInputActionCallbackBeginFailure(ScriptDomain domain)
	{
		const ulong sceneSession = 45;
		const ulong generation = 41;
		const ulong attachment = 1404;
		var fixture = CreateInputActionTransactionFixture(domain,
			sceneSession, generation, attachment, "Regression.BeginFailure");
		int handlerRuns = 0;
		fixture.Action.Started += _ => ++handlerRuns;
		try
		{
			s_failNextCallbackTransactionBegin = true;
			s_extendedInputPressed = true;
			Throws<DeferredCallbackProtocolException>(
				() => fixture.Scene.UpdateAll(1.0f / 60.0f),
				"InputAction Begin protocol failure was swallowed");
			Equal(0, handlerRuns,
				"InputAction handler ran after Begin protocol failure");
			Check(!s_failNextCallbackTransactionBegin,
				"InputAction Begin failure injection was not consumed");
			Equal(0, s_callbackTransactionBeginCalls,
				"failed InputAction Begin counted as an open transaction");
			Equal(0, s_callbackTransactionCompleteCalls,
				"failed InputAction Begin attempted Complete");
			Equal(0, GetAwaitingCallbackProjectionCount(fixture.Scene),
				"failed InputAction Begin left an awaiting projection");
		}
		finally
		{
			DestroyInputActionTransactionFixture(domain, fixture.Scene,
				fixture.Owner, fixture.Map);
		}
	}

	private static void VerifyInputActionCallbackCompleteFailure(
		ScriptDomain domain)
	{
		const ulong sceneSession = 46;
		const ulong generation = 42;
		const ulong attachment = 1405;
		var fixture = CreateInputActionTransactionFixture(domain,
			sceneSession, generation, attachment, "Regression.CompleteFailure");
		int handlerRuns = 0;
		ulong handlerToken = 0;
		fixture.Action.Started += _ =>
		{
			++handlerRuns;
			handlerToken = s_openCallbackTransactionToken;
		};
		try
		{
			s_failNextCallbackTransactionComplete = true;
			s_extendedInputPressed = true;
			Throws<DeferredCallbackProtocolException>(
				() => fixture.Scene.UpdateAll(1.0f / 60.0f),
				"InputAction Complete protocol failure was swallowed");
			Equal(1, handlerRuns,
				"InputAction handler did not run before Complete failure");
			Check(handlerToken != 0,
				"InputAction Complete failure handler had no transaction token");
			Check(!s_failNextCallbackTransactionComplete,
				"InputAction Complete failure injection was not consumed");
			Check(s_callbackTransactionBeginTokens.SequenceEqual([handlerToken]),
				"InputAction Complete failure Begin sequence");
			Check(s_callbackTransactionCompleteTokens.SequenceEqual([handlerToken]),
				"InputAction Complete failure token sequence");
			Equal(0, s_callbackTransactionResolvedAcks.Count,
				"failed InputAction Complete produced an acknowledgement");
			Equal(0, GetAwaitingCallbackProjectionCount(fixture.Scene),
				"failed InputAction Complete left a stale managed FIFO frame");
		}
		finally
		{
			DestroyInputActionTransactionFixture(domain, fixture.Scene,
				fixture.Owner, fixture.Map);
		}
	}

	private static void VerifyInputActionCallbackAbortFailure(
		ScriptDomain domain)
	{
		const ulong sceneSession = 47;
		const ulong generation = 43;
		const ulong attachment = 1406;
		var fixture = CreateInputActionTransactionFixture(domain,
			sceneSession, generation, attachment, "Regression.AbortFailure");
		Entity target = new(sceneSession, 2, generation);
		SetFakeGameplayActive(target, true);
		int handlerRuns = 0;
		ulong handlerToken = 0;
		fixture.Action.Started += _ =>
		{
			++handlerRuns;
			handlerToken = s_openCallbackTransactionToken;
			target.ActiveSelf = false;
			throw new InvalidOperationException(
				"intentional abort protocol failure");
		};
		try
		{
			s_failNextAbortBatch = true;
			s_extendedInputPressed = true;
			Throws<DeferredCallbackProtocolException>(
				() => fixture.Scene.UpdateAll(1.0f / 60.0f),
				"InputAction Abort protocol failure was swallowed");
			Equal(1, handlerRuns,
				"InputAction Abort failure handler run count");
			Check(handlerToken != 0,
				"InputAction Abort failure handler had no transaction token");
			Check(!s_failNextAbortBatch,
				"InputAction Abort failure injection was not consumed");
			Equal(1, s_abortBatchCalls,
				"InputAction Abort failure native call count");
			AssertAbortBatchContext(fixture.Owner,
				"InputAction Abort failure context");
			Check(s_abortBatchReason?.Contains(
				"intentional abort protocol failure",
				StringComparison.Ordinal) == true,
				"InputAction Abort failure reason");
			Check(s_callbackTransactionBeginTokens.SequenceEqual([handlerToken]),
				"InputAction Abort failure Begin sequence");
			Equal(0, s_callbackTransactionCompleteCalls,
				"failed Abort must suppress native Complete");
			Equal(0, s_callbackTransactionCompleteTokens.Count,
				"failed Abort recorded a Complete token");
			Equal(0, s_callbackTransactionResolvedAcks.Count,
				"failed Abort produced a commit acknowledgement");
			Equal(0, s_callbackTransactionAbortTokens.Count,
				"rejected Abort incorrectly poisoned the native batch");
			Equal(0, GetAwaitingCallbackProjectionCount(fixture.Scene),
				"failed Abort left a stale managed FIFO frame");
			Equal(true, ReadFakeGameplayActive(target, committed: true),
				"failed Abort committed the callback mutation");
			Equal(false, ReadFakeGameplayActive(target, committed: false),
				"Abort failure test did not stage its callback mutation");
			Equal(handlerToken, s_openCallbackTransactionToken,
				"failed Abort unexpectedly closed the native transaction");
		}
		finally
		{
			// The real host fail-stops and destroys the Scene after the surfaced
			// protocol fault. Reset the in-process native stub before managed cleanup.
			ResetCallbackTransactionHarness();
			DestroyInputActionTransactionFixture(domain, fixture.Scene,
				fixture.Owner, fixture.Map);
		}
	}

	private static (ScriptSceneRuntime Scene, Entity Owner, InputActionMap Map,
		InputAction Action) CreateInputActionTransactionFixture(
		ScriptDomain domain, ulong sceneSession, ulong generation,
		ulong attachment, string mapName)
	{
		ResetGameplayActivationGraph();
		ResetCallbackTransactionHarness();
		ResetAbortBatchCapture();
		s_extendedInputPressed = false;
		s_extendedInputPulse = false;
		Entity owner = new(sceneSession, 1, generation);
		ScriptSceneRuntime scene = domain.CreateSceneRuntime(
			sceneSession, generation);
		s_callbackTransactionScene = scene;
		scene.InstantiateAll([
			new ScriptAttachment(owner, attachment, 1001, true)
		]);
		scene.ApplySerializedFields("{\"attachments\":[]}");
		scene.InvokeCreateAll();

		InputActionMap map;
		InputAction action;
		using (ScriptExecutionContext.Enter(owner,
			domain.DomainCancellationToken))
		{
			map = new InputActionMap(mapName);
			action = map.AddAction("Fire")
				.AddBinding(InputBinding.Mouse(MouseButton.Left));
			map.Enable();
		}

		// Discard setup callback tokens; the next display update must begin with the
		// first InputAction subscriber transaction.
		ResetCallbackTransactionHarness();
		ResetAbortBatchCapture();
		s_callbackTransactionScene = scene;
		return (scene, owner, map, action);
	}

	private static void DestroyInputActionTransactionFixture(
		ScriptDomain domain, ScriptSceneRuntime scene, Entity owner,
		InputActionMap map)
	{
		s_extendedInputPressed = false;
		s_extendedInputPulse = false;
		using (ScriptExecutionContext.Enter(owner,
			domain.DomainCancellationToken))
			map.Disable();
		scene.DestroyAll();
		s_callbackTransactionScene = null;
	}

	private static void SetFakeGameplayActive(Entity entity, bool active)
	{
		var handle = new NativeEntityHandleV1(entity.SceneSessionId,
			entity.Id, entity.RuntimeGeneration);
		var key = GameplayEntityKey(handle);
		s_gameplayActiveByEntity[key] = active;
		s_committedGameplayActiveByEntity[key] = active;
	}

	private static bool ReadFakeGameplayActive(Entity entity, bool committed)
	{
		var handle = new NativeEntityHandleV1(entity.SceneSessionId,
			entity.Id, entity.RuntimeGeneration);
		var key = GameplayEntityKey(handle);
		Dictionary<(ulong SceneSessionId, ulong EntityId,
			ulong RuntimeGeneration), bool> states = committed
				? s_committedGameplayActiveByEntity
				: s_gameplayActiveByEntity;
		return states.TryGetValue(key, out bool active)
			? active : s_gameplayActive;
	}

	private static void AssertSuccessfulCallbackTransactionFifo(string message)
	{
		Check(s_callbackTransactionBeginTokens.SequenceEqual(
			s_callbackTransactionCompleteTokens),
			$"{message}: Begin/Complete tokens differ");
		Check(s_callbackTransactionCompleteTokens.SequenceEqual(
			s_callbackTransactionResolvedAcks.Select(ack => ack.Token)),
			$"{message}: Complete/resolve tokens differ");
	}

	private static int GetAwaitingCallbackProjectionCount(
		ScriptSceneRuntime scene)
	{
		FieldInfo awaitingField = typeof(ScriptSceneRuntime).GetField(
			"_awaitingCallbackProjections",
			BindingFlags.Instance | BindingFlags.NonPublic)
			?? throw new MissingFieldException(nameof(ScriptSceneRuntime),
				"_awaitingCallbackProjections");
		object awaiting = awaitingField.GetValue(scene)
			?? throw new InvalidOperationException(
				"callback projection FIFO was null");
		return (int)(awaiting.GetType().GetProperty("Count")?.GetValue(awaiting)
			?? throw new MissingMemberException(
				"callback projection FIFO Count"));
	}
	private static void ResetCallbackTransactionHarness()
	{
		s_callbackTransactionScene = null;
		s_openCallbackTransactionToken = 0;
		s_openCallbackTransactionAborted = false;
		s_drainingCallbackTransactions = false;
		s_failNextCallbackTransactionBegin = false;
		s_failNextCallbackTransactionComplete = false;
		s_failNextAbortBatch = false;
		s_callbackTransactionBeginCalls = 0;
		s_callbackTransactionCompleteCalls = 0;
		s_callbackTransactionAcks.Clear();
		s_callbackTransactionBeginTokens.Clear();
		s_callbackTransactionCompleteTokens.Clear();
		s_callbackTransactionAbortTokens.Clear();
		s_callbackTransactionResolvedAcks.Clear();
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
				WasMouseButtonPressed = &StubMouseButtonPressed,
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
		if (capability == "TomCat.DeferredCommandsApiV1")
		{
			*required = (uint)sizeof(NativeDeferredCommandsApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeDeferredCommandsApiV1))
				return -6;
			*(NativeDeferredCommandsApiV1*)output = new NativeDeferredCommandsApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeDeferredCommandsApiV1),
				AbortBatch = &StubAbortBatch
			};
			return 0;
		}
		if (capability == "TomCat.DeferredCallbackTransactionsApiV1"
			&& s_exposeDeferredCallbackTransactions)
		{
			*required =
				(uint)sizeof(NativeDeferredCallbackTransactionsApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity
				< sizeof(NativeDeferredCallbackTransactionsApiV1))
				return -6;
			*(NativeDeferredCallbackTransactionsApiV1*)output =
				new NativeDeferredCallbackTransactionsApiV1
				{
					Version = 1,
					Size =
						(uint)sizeof(NativeDeferredCallbackTransactionsApiV1),
					BeginCallback = &StubBeginCallbackTransaction,
					CompleteCallback = &StubCompleteCallbackTransaction
				};
			return 0;
		}
		if (capability == "TomCat.InputEventsApiV1")
		{
			*required = (uint)sizeof(NativeInputEventsApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeInputEventsApiV1))
				return -6;
			*(NativeInputEventsApiV1*)output = new NativeInputEventsApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeInputEventsApiV1),
				GetBatchInfo = &StubInputEventBatchInfo,
				CopyEvents = &StubInputEvents
			};
			return 0;
		}
		if (capability == "TomCat.ApplicationPathsApiV1")
		{
			*required = (uint)sizeof(NativeApplicationPathsApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeApplicationPathsApiV1))
				return -6;
			*(NativeApplicationPathsApiV1*)output = new NativeApplicationPathsApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeApplicationPathsApiV1),
				GetSaveDirectory = &StubSaveDirectory,
				GetLogDirectory = &StubLogDirectory,
				GetCrashDirectory = &StubCrashDirectory
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
		if (capability == "TomCat.ComponentStringApiV1")
		{
			*required = (uint)sizeof(NativeComponentStringApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeComponentStringApiV1))
				return -6;
			*(NativeComponentStringApiV1*)output = new NativeComponentStringApiV1
			{
				Version = 1,
				Size = (uint)sizeof(NativeComponentStringApiV1),
				GetProperty = &StubRegisteredComponentGetString,
				SetProperty = &StubRegisteredComponentSetString
			};
			return 0;
		}
		if (capability == "TomCat.ComponentSchemaApiV1")
		{
			*required = (uint)sizeof(NativeComponentSchemaApiV1);
			if (minimumVersion > 1)
				return -4;
			if (output is null || capacity < sizeof(NativeComponentSchemaApiV1))
				return -6;
			*(NativeComponentSchemaApiV1*)output =
				new NativeComponentSchemaApiV1
				{
					Version = 1,
					Size = (uint)sizeof(NativeComponentSchemaApiV1),
					GetComponentCount = &StubSchemaGetComponentCount,
					GetComponent = &StubSchemaGetComponent,
					GetPropertyCount = &StubSchemaGetPropertyCount,
					GetProperty = &StubSchemaGetProperty
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
	private static int StubBeginCallbackTransaction(
		NativeEntityHandleV1 context, ulong* token)
	{
		if (token is null || s_callbackTransactionScene is null
			|| context.SceneSessionId
				!= s_callbackTransactionScene.SceneSessionId
			|| context.RuntimeGeneration
				!= s_callbackTransactionScene.RuntimeGeneration
			|| context.EntityId == 0 || s_openCallbackTransactionToken != 0)
		{
			if (token is not null)
				*token = 0;
			return -2;
		}
		if (s_failNextCallbackTransactionBegin)
		{
			s_failNextCallbackTransactionBegin = false;
			*token = 0;
			return -2;
		}
		do
		{
			s_openCallbackTransactionToken =
				s_nextCallbackTransactionToken++;
		}
		while (s_openCallbackTransactionToken == 0);
		s_openCallbackTransactionAborted = false;
		*token = s_openCallbackTransactionToken;
		++s_callbackTransactionBeginCalls;
		s_callbackTransactionBeginTokens.Add(
			s_openCallbackTransactionToken);
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubCompleteCallbackTransaction(ulong token)
	{
		if (token == 0 || token != s_openCallbackTransactionToken
			|| s_callbackTransactionScene is null)
			return -2;
		bool committed = !s_openCallbackTransactionAborted;
		s_openCallbackTransactionToken = 0;
		s_openCallbackTransactionAborted = false;
		++s_callbackTransactionCompleteCalls;
		s_callbackTransactionCompleteTokens.Add(token);
		if (s_failNextCallbackTransactionComplete)
		{
			s_failNextCallbackTransactionComplete = false;
			RestoreFakeGameplayWorkingState();
			return -2;
		}

		s_callbackTransactionAcks.Enqueue((token, committed));
		if (s_drainingCallbackTransactions)
			return 0;
		s_drainingCallbackTransactions = true;
		try
		{
			while (s_callbackTransactionAcks.Count != 0)
			{
				(ulong completedToken, bool callbackCommitted) =
					s_callbackTransactionAcks.Dequeue();
				s_callbackTransactionResolvedAcks.Add(
					(completedToken, callbackCommitted));
				if (callbackCommitted)
					CommitFakeGameplayWorkingState();
				else
					RestoreFakeGameplayWorkingState();
				s_callbackTransactionScene.ResolveDeferredCommandBatch(
					callbackCommitted);
			}
		}
		finally
		{
			s_drainingCallbackTransactions = false;
		}
		return 0;
	}

	private static void CommitFakeGameplayWorkingState()
	{
		s_committedGameplayActiveByEntity.Clear();
		foreach (var pair in s_gameplayActiveByEntity)
			s_committedGameplayActiveByEntity[pair.Key] = pair.Value;
		s_committedGameplayParentByEntity.Clear();
		foreach (var pair in s_gameplayParentByEntity)
			s_committedGameplayParentByEntity[pair.Key] = pair.Value;
	}

	private static void RestoreFakeGameplayWorkingState()
	{
		s_gameplayActiveByEntity.Clear();
		foreach (var pair in s_committedGameplayActiveByEntity)
			s_gameplayActiveByEntity[pair.Key] = pair.Value;
		s_gameplayParentByEntity.Clear();
		foreach (var pair in s_committedGameplayParentByEntity)
			s_gameplayParentByEntity[pair.Key] = pair.Value;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubAbortBatch(NativeEntityHandleV1 context,
		NativeUtf8View reason)
	{
		if ((reason.Data is null && reason.Length != 0)
			|| reason.Length > int.MaxValue)
			return -1;
		try
		{
			s_abortBatchContext = context;
			s_abortBatchReason = s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(reason.Data, (int)reason.Length));
			++s_abortBatchCalls;
			if (s_failNextAbortBatch)
			{
				s_failNextAbortBatch = false;
				return -2;
			}
			if (s_openCallbackTransactionToken != 0)
			{
				s_openCallbackTransactionAborted = true;
				s_callbackTransactionAbortTokens.Add(
					s_openCallbackTransactionToken);
			}
			return 0;
		}
		catch (DecoderFallbackException)
		{
			return -1;
		}
	}

	private static NativeUtf8View SchemaView(nint pointer, string value) =>
		new((byte*)pointer, (ulong)Encoding.UTF8.GetByteCount(value));

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSchemaGetComponentCount(uint* count)
	{
		if (count is null)
			return -1;
		*count = 1;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSchemaGetComponent(uint index,
		NativeComponentSchemaInfoV1* component)
	{
		if (component is null)
			return -1;
		if (index != 0)
			return -3;
		*component = new NativeComponentSchemaInfoV1
		{
			TypeId = HealthComponent.TypeId,
			ProviderId = SchemaProviderId,
			SchemaVersion = 3,
			PropertyCount = 2,
			Flags = NativeComponentSchemaFlagsV1.ScriptAccessible
				| NativeComponentSchemaFlagsV1.InspectorVisible,
			StableName = SchemaView(s_schemaComponentStable,
				SchemaComponentStableName),
			DisplayName = SchemaView(s_schemaComponentDisplay,
				SchemaComponentDisplayName)
		};
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSchemaGetPropertyCount(ulong componentTypeId,
		uint* count)
	{
		if (count is null)
			return -1;
		if (componentTypeId != HealthComponent.TypeId)
			return -3;
		*count = 2;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSchemaGetProperty(ulong componentTypeId, uint index,
		NativeComponentPropertySchemaInfoV1* property)
	{
		if (property is null)
			return -1;
		if (componentTypeId != HealthComponent.TypeId || index >= 2)
			return -3;
		if (index == 0)
		{
			*property = new NativeComponentPropertySchemaInfoV1
			{
				ComponentTypeId = componentTypeId,
				PropertyId = HealthComponent.MaximumPropertyId,
				Kind = NativePropertyKindV1.Int32,
				StableName = SchemaView(s_schemaMaximumStable,
					SchemaMaximumStableName),
				DisplayName = SchemaView(s_schemaMaximumDisplay,
					SchemaMaximumDisplayName)
			};
		}
		else
		{
			*property = new NativeComponentPropertySchemaInfoV1
			{
				ComponentTypeId = componentTypeId,
				PropertyId = SchemaAssetPropertyId,
				Kind = NativePropertyKindV1.UInt64,
				Flags = NativeComponentPropertyFlagsV1.AssetReference,
				StableName = SchemaView(s_schemaAssetStable,
					SchemaAssetStableName),
				DisplayName = SchemaView(s_schemaAssetDisplay,
					SchemaAssetDisplayName)
			};
		}
		return 0;
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
		s_lastGameplayQueryRegisteredComponent = registeredTypeId;
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
		if (s_usePerEntityGameplayActivation)
		{
			*output = s_gameplayParentByEntity.TryGetValue(
				GameplayEntityKey(entity), out NativeEntityHandleV1 parent)
				? parent : default;
		}
		else
			*output = entity.Equals(s_gameplayParentOwner)
				? s_gameplayParent : default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGameplaySetParent(NativeEntityHandleV1 entity,
		NativeEntityHandleV1 parent)
	{
		if (s_usePerEntityGameplayActivation)
		{
			var key = GameplayEntityKey(entity);
			if (parent.EntityId == 0)
				s_gameplayParentByEntity.Remove(key);
			else
				s_gameplayParentByEntity[key] = parent;
		}
		else
		{
			s_gameplayParentOwner = entity;
			s_gameplayParent = parent;
		}
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
			if (!visited.Add(key) || !StubGameplayCommittedActiveSelf(cursor))
				return 0;
			if (!s_committedGameplayParentByEntity.TryGetValue(key,
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

	private static bool StubGameplayCommittedActiveSelf(
		NativeEntityHandleV1 entity) =>
		s_committedGameplayActiveByEntity.TryGetValue(GameplayEntityKey(entity),
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
		if (typeId == ExtensionProxy.TypeId)
			return s_extensionPresent ? 1 : 0;
		if (typeId != HealthComponent.TypeId)
		{
			if (typeId == 0x9f00000000000007UL)
				++s_spriteAnimatorHasCalls;
			return IsTraditionalRegisteredType(typeId) ? 1 : -1;
		}
		++s_componentHasCalls;
		return s_healthPresent ? 1 : 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentAdd(NativeEntityHandleV1 entity,
		ulong typeId)
	{
		if (typeId == ExtensionProxy.TypeId)
		{
			if (s_extensionPresent) return -2;
			s_extensionPresent = true;
			s_extensionCount = 0;
			s_extensionLabel = string.Empty;
			return 0;
		}
		if (typeId != HealthComponent.TypeId)
		{
			if (typeId == 0x9f00000000000007UL)
				++s_spriteAnimatorAddCalls;
			return IsTraditionalRegisteredType(typeId) ? 0 : -1;
		}
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
		if (typeId == ExtensionProxy.TypeId)
		{
			if (!s_extensionPresent) return -3;
			s_extensionPresent = false;
			return 0;
		}
		if (typeId != HealthComponent.TypeId)
		{
			if (typeId == 0x9f00000000000007UL)
				++s_spriteAnimatorRemoveCalls;
			return IsTraditionalRegisteredType(typeId) ? 0 : -1;
		}
		++s_componentRemoveCalls;
		s_healthPresent = false;
		return 0;
	}

	private static bool IsTraditionalRegisteredType(ulong typeId) =>
		typeId is >= 0x9f00000000000004UL and <= 0x9f0000000000000fUL
			|| typeId == ExtensionProxy.TypeId;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentGetProperty(NativeEntityHandleV1 entity,
		ulong typeId, ulong propertyId, NativePropertyValueV1* value)
	{
		if (value is null)
			return -3;
		if (typeId == ExtensionProxy.TypeId)
		{
			if (!s_extensionPresent || propertyId != ExtensionProxy.CountPropertyId)
				return -3;
			++s_extensionPropertyGetCalls;
			*value = new NativePropertyValueV1
			{
				Kind = NativePropertyKindV1.Int32,
				Integer = s_extensionCount
			};
			return 0;
		}
		if (typeId is >= 0x9f00000000000004UL and <= 0x9f0000000000000fUL)
		{
			if (!s_registeredBuiltInProperties.TryGetValue(
				(typeId, propertyId), out NativePropertyValueV1 property))
				return -3;
			*value = property;
			return 0;
		}
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
		if (typeId == ExtensionProxy.TypeId)
		{
			if (!s_extensionPresent || propertyId != ExtensionProxy.CountPropertyId)
				return -3;
			if (value.Kind != NativePropertyKindV1.Int32
				|| value.Integer < int.MinValue || value.Integer > int.MaxValue)
				return -1;
			s_extensionCount = (int)value.Integer;
			++s_extensionPropertySetCalls;
			return 0;
		}
		if (typeId is >= 0x9f00000000000004UL and <= 0x9f0000000000000fUL)
		{
			s_registeredBuiltInProperties[(typeId, propertyId)] = value;
			return 0;
		}
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
	private static int StubRegisteredComponentGetString(NativeEntityHandleV1 entity,
		ulong typeId, ulong propertyId, byte* buffer, uint capacity, uint* required)
	{
		if (required is null)
			return -1;
		*required = 0;
		string value;
		if (typeId == ExtensionProxy.TypeId
			&& propertyId == ExtensionProxy.LabelPropertyId)
		{
			if (!s_extensionPresent) return -3;
			++s_extensionStringGetCalls;
			if (s_returnMalformedExtensionUtf8)
			{
				ReadOnlySpan<byte> malformed = [0xc0, 0xaf];
				*required = (uint)malformed.Length;
				if (capacity < malformed.Length || buffer is null) return -6;
				malformed.CopyTo(new Span<byte>(buffer, malformed.Length));
				return 0;
			}
			value = s_extensionLabel;
		}
		else if (typeId == UIText.TypeId
			&& propertyId == 0x9f01500000000003UL)
			value = s_runtimeUIText;
		else if (typeId == TextRenderer.TypeId
			&& propertyId == 0x9f01100000000003UL)
			value = s_worldText;
		else return -1;

		byte[] bytes = s_strictUtf8.GetBytes(value);
		*required = (uint)bytes.Length;
		if (capacity < bytes.Length || (buffer is null && bytes.Length != 0))
			return -6;
		bytes.CopyTo(new Span<byte>(buffer, bytes.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubRegisteredComponentSetString(NativeEntityHandleV1 entity,
		ulong typeId, ulong propertyId, NativeUtf8View value)
	{
		if ((value.Data is null && value.Length != 0)
			|| value.Length > RegisteredComponentProperties.MaximumStringUtf8Bytes)
			return -1;
		if (typeId == ExtensionProxy.TypeId)
		{
			if (!s_extensionPresent) return -3;
			if (propertyId != ExtensionProxy.LabelPropertyId) return -1;
		}
		else if (!((typeId == UIText.TypeId
				&& propertyId == 0x9f01500000000003UL)
			|| (typeId == TextRenderer.TypeId
				&& propertyId == 0x9f01100000000003UL)))
			return -1;
		try
		{
			string decoded = s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(value.Data, checked((int)value.Length)));
			if (typeId == ExtensionProxy.TypeId)
			{
				s_extensionLabel = decoded;
				++s_extensionStringSetCalls;
			}
			else if (typeId == UIText.TypeId)
				s_runtimeUIText = decoded;
			else s_worldText = decoded;
			return 0;
		}
		catch (Exception error) when (error is DecoderFallbackException
			or OverflowException)
		{
			return -1;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubMouseButtonHeld(uint button) =>
		button == (uint)MouseButton.Left && s_extendedInputPressed ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubInputEventBatchInfo(NativeInputEventBatchInfoV1* value)
	{
		if (value is null)
			return -1;
		*value = new NativeInputEventBatchInfoV1
		{
			FirstFrameNumber = 41,
			LastFrameNumber = 42,
			FirstSequence = 9001,
			LastSequence = 9002,
			DroppedEventCount = 3,
			EventCount = 2
		};
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubInputEvents(NativeInputEventV1* events, uint capacity,
		uint* required)
	{
		if (required is null)
			return -1;
		*required = 2;
		if (events is null || capacity < 2)
			return -6;
		events[0] = new NativeInputEventV1
		{
			Sequence = 9001,
			TimestampSeconds = 12.5,
			FrameNumber = 41,
			Device = NativeInputDeviceV1.Keyboard,
			Action = NativeInputActionV1.Pressed,
			Code = (uint)KeyCode.A
		};
		events[1] = new NativeInputEventV1
		{
			Sequence = 9002,
			TimestampSeconds = 12.75,
			FrameNumber = 42,
			Device = NativeInputDeviceV1.MouseButton,
			Action = NativeInputActionV1.Released,
			Code = (uint)MouseButton.Left
		};
		return 0;
	}

	private static int WriteStubUtf8(ReadOnlySpan<byte> value, byte* buffer,
		uint capacity, uint* required)
	{
		if (required is null)
			return -1;
		*required = (uint)value.Length;
		if (capacity < value.Length || (buffer is null && value.Length != 0))
			return -6;
		value.CopyTo(new Span<byte>(buffer, value.Length));
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSaveDirectory(byte* buffer, uint capacity,
		uint* required) => WriteStubUtf8(
			"C:/TomCat/Games/Regression/Saves"u8, buffer, capacity, required);

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubLogDirectory(byte* buffer, uint capacity,
		uint* required) => WriteStubUtf8(
			"C:/TomCat/Games/Regression/Logs"u8, buffer, capacity, required);

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubCrashDirectory(byte* buffer, uint capacity,
		uint* required) => WriteStubUtf8(
			"C:/TomCat/Games/Regression/Crashes"u8, buffer, capacity, required);

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubMouseButtonPressed(uint button) =>
		button == (uint)MouseButton.Left
			&& (s_extendedInputPressed || s_extendedInputPulse) ? 1 : 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubMouseButtonReleased(uint button) =>
		button == (uint)MouseButton.Left
			&& (!s_extendedInputPressed || s_extendedInputPulse) ? 1 : 0;

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
			ExtensionProxy extension = Entity.GetComponent<ExtensionProxy>();
			if (extension.Entity != Entity)
				throw new InvalidOperationException("attribute-discovered plugin component proxy failed.");
			extension.Count = 73;
			extension.Label = "插件属性 😀";
			if (extension.Count != 73 || extension.Label != "插件属性 😀")
				throw new InvalidOperationException(
					"public plugin component property round trip failed.");
			s_returnMalformedExtensionUtf8 = true;
			bool malformedRejected = false;
			try { _ = extension.Label; }
			catch (TomCatException) { malformedRejected = true; }
			finally { s_returnMalformedExtensionUtf8 = false; }
			if (!malformedRejected)
				throw new InvalidOperationException(
					"malformed native plugin UTF-8 was accepted.");
			bool invalidManagedStringRejected = false;
			try { extension.Label = "\ud800"; }
			catch (ArgumentException) { invalidManagedStringRejected = true; }
			if (!invalidManagedStringRejected)
				throw new InvalidOperationException(
					"invalid managed UTF-16 was accepted by plugin transport.");
			s_extensionPresent = false;
			bool unloadedRejected = false;
			try { _ = extension.Label; }
			catch (TomCatException) { unloadedRejected = true; }
			finally { s_extensionPresent = true; }
			if (!unloadedRejected)
				throw new InvalidOperationException(
					"unloaded plugin property remained accessible.");
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
			camera.Enabled = false;
			camera.Primary = true;
			camera.OrthographicSize = 12.0f;
			if (camera.Enabled || !camera.Primary || camera.OrthographicSize != 12.0f)
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

	[RegisteredComponent(ExtensionProxy.TypeId)]
	private sealed class ExtensionProxy : IEntityComponent
	{
		internal const ulong TypeId = 0xd20a1f7c73e04e11UL;
		internal const ulong CountPropertyId = 0xd20a1f7c73e04e12UL;
		internal const ulong LabelPropertyId = 0xd20a1f7c73e04e13UL;
		private ExtensionProxy(Entity entity) => Entity = entity;
		public Entity Entity { get; }
		public int Count
		{
			get => RegisteredComponentProperties.GetInt32(Entity, TypeId,
				CountPropertyId);
			set => RegisteredComponentProperties.SetInt32(Entity, TypeId,
				CountPropertyId, value);
		}
		public string Label
		{
			get => RegisteredComponentProperties.GetString(Entity, TypeId,
				LabelPropertyId);
			set => RegisteredComponentProperties.SetString(Entity, TypeId,
				LabelPropertyId, value);
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
			InputEventBatch inputBatch = Input.EventBatch;
			Equal(2, inputBatch.Events.Count, "managed ordered input event count");
			Check(inputBatch.FirstFrameNumber == 41 && inputBatch.LastFrameNumber == 42
				&& inputBatch.FirstSequence == 9001 && inputBatch.LastSequence == 9002
				&& inputBatch.DroppedEventCount == 3
				&& inputBatch.Events[0].Device == InputEventDevice.Keyboard
				&& inputBatch.Events[0].Action == InputEventAction.Pressed
				&& inputBatch.Events[1].Device == InputEventDevice.MouseButton
				&& inputBatch.Events[1].Action == InputEventAction.Released
				&& inputBatch.Events[0].TimestampSeconds == 12.5
				&& inputBatch.Events[1].TimestampSeconds == 12.75,
				"managed ordered input metadata round-trip");
			Equal("C:/TomCat/Games/Regression/Saves",
				TomCat.ApplicationPaths.SaveDirectory, "managed save directory");
			Equal("C:/TomCat/Games/Regression/Logs",
				TomCat.ApplicationPaths.LogDirectory, "managed log directory");
			Equal("C:/TomCat/Games/Regression/Crashes",
				TomCat.ApplicationPaths.CrashDirectory, "managed crash directory");
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
			InputActionRuntime.UpdateEnabled(ScriptRuntime.DomainCancellationToken,
				InputActionUpdatePhase.DisplayFrame);
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
			worldText.Text = "World 文本 😀";
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
				&& worldText.Text == "World 文本 😀"
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
