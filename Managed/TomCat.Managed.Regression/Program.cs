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
	private const ulong MetadataReceiverSentinel = 0xC0DEC0DE5A17UL;
	private const string ConstructorGuardMessage =
		"TomCat engine APIs cannot be used from a script constructor or field initializer. Use OnCreate or another lifecycle callback.";
	private const string WrongThreadMessage =
		"WrongThread: TomCat engine APIs may only be used from the main thread.";
	private static int s_diagnostics;
	private static ulong s_removedAttachment;
	private static ulong s_metadataReceiverToken;
	private static string? s_metadataReceiverJson;
	private static readonly int s_mainManagedThread = Environment.CurrentManagedThreadId;

    private static int Main()
    {
        try
		{
			VerifyDescriptorConstructorFactory();
            VerifyConstructorGuard();

            string fixtureDirectory = Path.Combine(AppContext.BaseDirectory, "Fixture");
            byte[] assembly = File.ReadAllBytes(Path.Combine(fixtureDirectory, "Assembly-CSharp.dll"));
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
            for (int index = 0; index < 100; ++index)
            {
                ScriptDomain metadata = BeginMetadataUnload(assembly, pdb);
                Check(PollUntilUnloaded(metadata), $"collectible ALC cycle {index + 1} leaked");
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
		HasComponent = &StubEntityComponent,
		AddComponentDeferred = &StubEntityComponent,
		RemoveComponentDeferred = &StubEntityComponent,
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
		BehaviourRemoveDeferred = &RemoveBehaviour
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
			managed.DestroyAttachments != null,
			"GetManagedApi must populate every V1 export");
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
			ScriptLifecycle.Disable | ScriptLifecycle.Destroy,
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

        int diagnosticsBefore = s_diagnostics;
        scene.UpdateAll(1.0f / 60.0f);
        Equal(ScriptInstanceState.Faulted, scene.GetInstanceState(300), "fault isolation state");
        Equal(1, scene.ReadFieldValue(300, "Updates"), "faulting callback runs once");
        Equal(diagnosticsBefore + 1, s_diagnostics, "fault diagnostic count");
        scene.UpdateAll(1.0f / 60.0f);
        Equal(1, scene.ReadFieldValue(300, "Updates"), "faulted instance is quarantined");
        Equal(2, scene.ReadFieldValue(100, "Updates"), "healthy instance continues");
        Equal(2, scene.ReadFieldValue(200, "Updates"), "second healthy instance continues");

        scene.FixedUpdateAll(1.0f / 50.0f);
        Equal(1, scene.ReadFieldValue(100, "FixedUpdates"), "fixed update dispatch");
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
			"100:OnDisable", "300:OnDestroy", "100:OnDestroy");

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

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static ScriptDomain BeginMetadataUnload(byte[] assembly, byte[] pdb)
    {
        var domain = new ScriptDomain(ScriptDomainKind.Metadata);
        domain.LoadProjectAssembly(assembly, pdb);
        Equal(2, domain.Manifest.Scripts.Count, "metadata-domain manifest");
        Throws<InvalidOperationException>(() => domain.CreateSceneRuntime(1, 1),
            "metadata domains cannot instantiate scripts");
        domain.BeginUnload();
        return domain;
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
	private static int StubSetEntityText(NativeEntityHandleV1 entity, NativeUtf8View value) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetEntityLayer(NativeEntityHandleV1 entity, uint* layer)
	{
		if (layer is null)
			return -1;
		*layer = 0;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetEntityLayer(NativeEntityHandleV1 entity, uint layer) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubEntityStatus(NativeEntityHandleV1 entity) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubEntityComponent(NativeEntityHandleV1 entity, int componentType) => 0;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubGetTransformVector(NativeEntityHandleV1 entity, NativeVector3* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetTransformVector(NativeEntityHandleV1 entity, NativeVector3 value) => 0;

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
	private static int StubGetRigidbodyVector(NativeEntityHandleV1 entity, NativeVector2* value)
	{
		if (value is null)
			return -1;
		*value = default;
		return 0;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static int StubSetRigidbodyVector(NativeEntityHandleV1 entity, NativeVector2 value) => 0;

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

	private sealed class ConstructorProbe : TomCatBehaviour
    {
        internal ConstructorProbe() => _ = Entity;
	}

	private sealed class RemovalProbe : TomCatBehaviour
	{
		protected override void OnCreate() => RemoveFromEntity();
	}

	private sealed class BackgroundThreadApiProbe : TomCatBehaviour
	{
		protected override void OnCreate()
		{
			AssertWrongThread(() => _ = Entity.IsValid, "Entity.IsValid");
			AssertWrongThread(() => _ = Input.IsKeyHeld(KeyCode.Space), "Input.IsKeyHeld");
			AssertWrongThread(() => _ = Physics2D.Raycast(Vector2.Zero, Vector2.One),
				"Physics2D.Raycast");
		}
	}

	private sealed class ConstructorInputProbe : TomCatBehaviour
	{
		internal ConstructorInputProbe() => _ = Input.IsKeyHeld(KeyCode.Space);
	}

	private sealed class ConstructorLogProbe : TomCatBehaviour
	{
		internal ConstructorLogProbe() => Log.Info("constructor");
	}
}
