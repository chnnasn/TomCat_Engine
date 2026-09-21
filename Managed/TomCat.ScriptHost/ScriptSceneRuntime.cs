using System.Reflection;
using System.Text.Json;
using TomCat.Interop;

namespace TomCat.ScriptHost;

public readonly record struct ScriptPhysicsEvent(NativePhysicsEventKindV1 Kind, Entity EntityA,
    Entity EntityB);

public sealed class ScriptSceneRuntime : IScriptMutationSink
{
	private const int MaximumLifecycleActivationTransitionsPerInstance = 8;

    private readonly Dictionary<ulong, ScriptDescriptor> _descriptors;
    private readonly Action _onDestroyed;
	private readonly CancellationToken _domainCancellation;
    private readonly List<ScriptInstance> _instances = [];
    private readonly Dictionary<ulong, ScriptInstance> _instancesByAttachment = [];
	// The legacy dictionaries preserve compatibility with a native host that does
	// not expose per-callback transactions. A current host uses one projection
	// frame per user callback, plus a FIFO matching native commit acknowledgements.
	private readonly Dictionary<ulong, bool> _pendingEnableChanges = [];
	private readonly HashSet<ulong> _pendingAttachmentRemovals = [];
	private readonly HashSet<Entity> _pendingEntityDestructions = [];
	private readonly Dictionary<ulong, bool> _nextBatchEnableChanges = [];
	private readonly HashSet<ulong> _nextBatchAttachmentRemovals = [];
	private readonly HashSet<Entity> _nextBatchEntityDestructions = [];
	private readonly Queue<DeferredProjectionFrame> _awaitingCallbackProjections = [];
	private readonly Dictionary<ScriptInstance, int>
		_callbackLifecycleActivationTransitions = [];
	private DeferredProjectionFrame? _activeCallbackProjection;
	private readonly List<string> _callbackTrace = [];
	private int _callbackDepth;
	private int _authoritativeMutationDepth;
	private int _deferLifecycleConvergenceDepth;
	private int _lifecycleConvergenceDepth;
	private bool _traceCallbacks;
	private int _nextSequence;
    private bool _instantiated;
    private bool _createInvoked;
    private bool _destroyed;

	internal ScriptSceneRuntime(ulong id, ulong sceneSessionId, ulong runtimeGeneration,
		Dictionary<ulong, ScriptDescriptor> descriptors,
		CancellationToken domainCancellation, Action onDestroyed)
    {
        Id = id;
        SceneSessionId = sceneSessionId;
        RuntimeGeneration = runtimeGeneration;
        _descriptors = descriptors;
		_domainCancellation = domainCancellation;
        _onDestroyed = onDestroyed;
    }

    public ulong Id { get; }
    public ulong SceneSessionId { get; }
    public ulong RuntimeGeneration { get; }
	internal IReadOnlyList<string> CallbackTrace => _callbackTrace;
	internal void EnableCallbackTraceForTesting() => _traceCallbacks = true;

    public void InstantiateAll(IEnumerable<ScriptAttachment> attachments)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (_instantiated)
            throw new InvalidOperationException("InstantiateAll may only be called once per scene runtime.");
        ArgumentNullException.ThrowIfNull(attachments);
		_instantiated = true;

		var seenAttachmentIds = new HashSet<ulong>();
		foreach (ScriptAttachment attachment in attachments)
		{
			if (attachment.AttachmentId == 0)
				throw new InvalidDataException("AttachmentID 0 is reserved.");
			if (!seenAttachmentIds.Add(attachment.AttachmentId))
				throw new InvalidDataException(
					$"Duplicate AttachmentID {attachment.AttachmentId} in one scene runtime.");
			if (attachment.Entity.SceneSessionId != SceneSessionId ||
				attachment.Entity.RuntimeGeneration != RuntimeGeneration || attachment.Entity.Id == 0)
				throw new InvalidDataException($"Attachment {attachment.AttachmentId} has an invalid scene Entity handle.");
			if (!_descriptors.TryGetValue(attachment.ScriptAsset, out ScriptDescriptor? descriptor))
			{
				NativeBridge.ReportManagedException(
					$"Missing C# script asset {attachment.ScriptAsset} on entity " +
					$"{attachment.Entity.Id}, attachment {attachment.AttachmentId}; the attachment was skipped.");
				continue;
			}
            if (descriptor.Manifest.DisallowMultiple && _instances.Any(value =>
                    value.Attachment.Entity == attachment.Entity &&
                    value.Attachment.ScriptAsset == attachment.ScriptAsset))
                throw new InvalidDataException(
                    $"Script '{descriptor.Manifest.TypeName}' disallows multiple attachments on entity {attachment.Entity.Id}.");

			var instance = new ScriptInstance(attachment, descriptor, _nextSequence++);
            try
            {
                instance.Behaviour = descriptor.ConstructorFactory();
				InitializeEntityFieldDefaults(instance);
                // V1 deliberately uses the globally unique AttachmentID as the
                // ScriptInstanceHandle understood by the native behaviour bridge.
				instance.Behaviour.__Bind(attachment.Entity,
					new ScriptInstanceHandle(attachment.AttachmentId),
					_domainCancellation);
            }
            catch (Exception exception)
            {
                MarkFaulted(instance, "Constructor", Unwrap(exception));
            }

            _instances.Add(instance);
            _instancesByAttachment.Add(attachment.AttachmentId, instance);
        }

		_instances.Sort(CompareInstances);
	}

	public void InstantiateAttachments(IEnumerable<ScriptAttachment> attachments,
		string fieldsJson)
	{
		ObjectDisposedException.ThrowIf(_destroyed, this);
		if (!_instantiated || !_createInvoked)
			throw new InvalidOperationException(
				"Dynamic attachments require a running scene runtime.");
		if (_callbackDepth != 0)
			throw new InvalidOperationException(
				"Dynamic attachments can only be instantiated at a native safe point.");
		ArgumentNullException.ThrowIfNull(attachments);
		ArgumentException.ThrowIfNullOrWhiteSpace(fieldsJson);

		var newInstances = new List<ScriptInstance>();
		var seenAttachmentIds = new HashSet<ulong>();
		foreach (ScriptAttachment attachment in attachments)
		{
			if (attachment.AttachmentId == 0)
				throw new InvalidDataException("AttachmentID 0 is reserved.");
			if (!seenAttachmentIds.Add(attachment.AttachmentId)
				|| _instancesByAttachment.ContainsKey(attachment.AttachmentId))
				throw new InvalidDataException(
					$"Duplicate AttachmentID {attachment.AttachmentId} in one scene runtime.");
			if (attachment.Entity.SceneSessionId != SceneSessionId
				|| attachment.Entity.RuntimeGeneration != RuntimeGeneration
				|| attachment.Entity.Id == 0)
				throw new InvalidDataException(
					$"Attachment {attachment.AttachmentId} has an invalid scene Entity handle.");
			if (!_descriptors.TryGetValue(attachment.ScriptAsset,
				out ScriptDescriptor? descriptor))
			{
				NativeBridge.ReportManagedException(
					$"Missing C# script asset {attachment.ScriptAsset} on entity "
					+ $"{attachment.Entity.Id}, attachment {attachment.AttachmentId}; "
					+ "the attachment was skipped.");
				continue;
			}
			if (descriptor.Manifest.DisallowMultiple
				&& (_instances.Any(value => value.Attachment.Entity == attachment.Entity
						&& value.Attachment.ScriptAsset == attachment.ScriptAsset)
					|| newInstances.Any(value => value.Attachment.Entity == attachment.Entity
						&& value.Attachment.ScriptAsset == attachment.ScriptAsset)))
				throw new InvalidDataException(
					$"Script '{descriptor.Manifest.TypeName}' disallows multiple attachments "
					+ $"on entity {attachment.Entity.Id}.");

			var instance = new ScriptInstance(attachment, descriptor, _nextSequence++);
			try
			{
				instance.Behaviour = descriptor.ConstructorFactory();
				InitializeEntityFieldDefaults(instance);
				instance.Behaviour.__Bind(attachment.Entity,
					new ScriptInstanceHandle(attachment.AttachmentId),
					_domainCancellation);
			}
			catch (Exception exception)
			{
				MarkFaulted(instance, "Constructor", Unwrap(exception));
			}
			newInstances.Add(instance);
		}

		try
		{
			foreach (ScriptInstance instance in newInstances)
			{
				_instances.Add(instance);
				_instancesByAttachment.Add(instance.Attachment.AttachmentId, instance);
			}
			ApplySerializedFieldsCore(fieldsJson, seenAttachmentIds);
		}
		catch
		{
			foreach (ScriptInstance instance in newInstances)
			{
				_instances.Remove(instance);
				_instancesByAttachment.Remove(instance.Attachment.AttachmentId);
				instance.Behaviour = null;
				instance.State = ScriptInstanceState.Destroyed;
			}
			throw;
		}

		_instances.Sort(CompareInstances);
		newInstances.Sort(CompareInstances);
		// Scene invokes this only after the preceding native transaction has
		// resolved, then flushes mutations from these callbacks as their own batch.
		InvokeCreateBatch(newInstances);
		FlushDeferredChanges();
	}

	private static int CompareInstances(ScriptInstance left, ScriptInstance right)
	{
		int order = left.Descriptor.Manifest.ExecutionOrder.CompareTo(
			right.Descriptor.Manifest.ExecutionOrder);
		return order != 0 ? order : left.Sequence.CompareTo(right.Sequence);
	}

	private void InitializeEntityFieldDefaults(ScriptInstance instance)
	{
		// Entity used to be a value proxy, so an uninitialized serialized field
		// naturally contained an invalid zero handle. Keep that observable value
		// after moving the public proxy to a class for natural chained setters.
		foreach (FieldDescriptor descriptor in instance.Descriptor.FieldsById.Values)
		{
			if (descriptor.Manifest.Type == ScriptFieldType.Entity &&
				descriptor.Field.GetValue(instance.Behaviour) is null)
			{
				descriptor.Field.SetValue(instance.Behaviour,
					new Entity(SceneSessionId, 0, RuntimeGeneration));
			}
		}
	}

    public void ApplySerializedFields(string json)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_instantiated || _createInvoked)
            throw new InvalidOperationException("Serialized fields must be applied after InstantiateAll and before InvokeCreateAll.");
        ArgumentException.ThrowIfNullOrWhiteSpace(json);
		ApplySerializedFieldsCore(json, null);
	}

	private void ApplySerializedFieldsCore(string json,
		IReadOnlySet<ulong>? allowedAttachments)
	{
        using JsonDocument document = JsonDocument.Parse(json, new JsonDocumentOptions
        {
            AllowTrailingCommas = false,
            CommentHandling = JsonCommentHandling.Disallow,
            MaxDepth = 64
        });
        JsonElement root = document.RootElement;
        if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("attachments", out JsonElement attachments) ||
            attachments.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Serialized script fields require an 'attachments' array.");

        var seenAttachments = new HashSet<ulong>();
        foreach (JsonElement attachmentElement in attachments.EnumerateArray())
        {
            ulong attachmentId = attachmentElement.GetProperty("attachmentId").GetUInt64();
            if (!seenAttachments.Add(attachmentId))
                throw new InvalidDataException($"Serialized fields contain duplicate AttachmentID {attachmentId}.");
			if (allowedAttachments is not null
				&& !allowedAttachments.Contains(attachmentId))
				throw new InvalidDataException(
					$"Serialized fields contain AttachmentID {attachmentId} outside the dynamic batch.");
            if (!_instancesByAttachment.TryGetValue(attachmentId, out ScriptInstance? instance) ||
                instance.Behaviour is null)
                continue;
            JsonElement fields = attachmentElement.GetProperty("fields");
            if (fields.ValueKind != JsonValueKind.Array)
                throw new InvalidDataException($"Attachment {attachmentId} fields must be an array.");

            foreach (JsonElement fieldElement in fields.EnumerateArray())
                ApplyField(instance, fieldElement);
        }
    }

    public void InvokeCreateAll()
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_instantiated || _createInvoked)
            throw new InvalidOperationException("InvokeCreateAll requires one completed InstantiateAll call.");
        _createInvoked = true;
		InvokeCreateBatch(_instances);
        FlushDeferredChanges();
    }

	public void InvokeMethod(ulong attachmentId, string methodName)
	{
		ObjectDisposedException.ThrowIf(_destroyed, this);
		if (!_createInvoked)
			throw new InvalidOperationException(
				"Create callbacks must run before event methods.");
		if (string.IsNullOrWhiteSpace(methodName) || methodName.Length > 512
			|| methodName.IndexOf('\0') >= 0)
			throw new ArgumentException("Event method name is invalid.", nameof(methodName));
		if (!_instancesByAttachment.TryGetValue(attachmentId,
			out ScriptInstance? instance))
			throw new KeyNotFoundException(
				$"Attachment {attachmentId} does not exist.");
		if (!CanInvokeEvent(instance))
			throw new InvalidOperationException(
				$"Attachment {attachmentId} is not callable.");
		if (!instance.Descriptor.EventMethods.TryGetValue(methodName,
			out System.Reflection.MethodInfo? method))
			throw new KeyNotFoundException(
				$"Public parameterless void method '{methodName}' is not exposed by "
				+ $"'{instance.Descriptor.Manifest.TypeName}'.");

		Invoke(instance, $"Event:{methodName}", behaviour =>
			method.Invoke(behaviour, parameters: null));
		FlushDeferredChanges();
	}

	private void InvokeCreateBatch(IEnumerable<ScriptInstance> instances)
	{
		ScriptInstance[] batch = instances.ToArray();
		++_deferLifecycleConvergenceDepth;
		try
		{
			foreach (ScriptInstance instance in batch)
			{
				if (instance.State == ScriptInstanceState.Ready)
				{
					Invoke(instance, "OnCreate",
						static behaviour => behaviour.__Create());
					instance.Created = instance.State == ScriptInstanceState.Ready;
				}
			}
		}
		finally
		{
			--_deferLifecycleConvergenceDepth;
		}

		// All OnCreate callbacks finish before any initial OnEnable. Per-callback
		// transactions are already authoritative here, so one convergence pass can
		// reconcile both new and existing instances. A legacy host retains the
		// projected batch view until its phase-level acknowledgement.
		if (NativeBridge.SupportsDeferredCallbackTransactions)
		{
			ApplyAuthoritativeMutation(ConvergeLifecycleActivation);
			return;
		}
		foreach (ScriptInstance instance in batch)
		{
			if (ShouldEnterInitialLifecycle(instance))
				SetLifecycleActive(instance, true);
		}
	}

	public void SetEnabled(ulong attachmentId, bool enabled)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_instancesByAttachment.ContainsKey(attachmentId))
            throw new KeyNotFoundException($"Attachment {attachmentId} does not exist.");
        if (_callbackDepth != 0)
        {
            RecordPendingEnable(attachmentId, enabled);
            return;
        }

		_pendingEnableChanges.Remove(attachmentId);
		ApplyAuthoritativeMutation(() =>
			ApplyEnabled(attachmentId, enabled));
	}

	void IScriptMutationSink.SetBehaviourEnabled(ulong attachmentId, bool enabled)
	{
		if (!_destroyed && _instancesByAttachment.ContainsKey(attachmentId))
			RecordPendingEnable(attachmentId, enabled);
	}

	void IScriptMutationSink.RemoveBehaviour(ulong attachmentId)
	{
		if (!_destroyed && _instancesByAttachment.ContainsKey(attachmentId))
			RecordPendingAttachmentRemoval(attachmentId);
	}

	void IScriptMutationSink.DestroyEntity(Entity entity)
	{
		if (!_destroyed && entity.SceneSessionId == SceneSessionId
			&& entity.RuntimeGeneration == RuntimeGeneration && entity.Id != 0)
			RecordPendingEntityDestruction(entity);
	}

    public void UpdateAll(float deltaTime)
    {
        ValidateDispatch(deltaTime, nameof(deltaTime));
		TimeRuntime.BeginFrame(deltaTime);
		if (_instances.Count != 0)
		{
			using var inputScope = ScriptExecutionContext.Enter(
				_instances[0].Attachment.Entity, _domainCancellation);
			InputActionRuntime.UpdateEnabled(_domainCancellation,
				InputActionUpdatePhase.DisplayFrame, InvokeInputActionCallback);
		}
		DispatchActiveCallbacks("OnUpdate",
			behaviour => behaviour.__Update(deltaTime));
		DispatchActiveCallbacks("OnLateUpdate",
			behaviour => behaviour.__LateUpdate(deltaTime));
        FlushDeferredChanges();
    }

    public void FixedUpdateAll(float fixedDeltaTime)
    {
        ValidateDispatch(fixedDeltaTime, nameof(fixedDeltaTime));
		TimeRuntime.BeginFixedStep(fixedDeltaTime);
		try
		{
			// Native code enters the scene's fixed-input scope before this call.
			// Evaluate actions now so the first substep sees accumulated edges and
			// catch-up substeps see the same held state without replaying them.
			if (_instances.Count != 0)
			{
				using var inputScope = ScriptExecutionContext.Enter(
					_instances[0].Attachment.Entity, _domainCancellation);
				InputActionRuntime.UpdateEnabled(_domainCancellation,
					InputActionUpdatePhase.FixedStep);
			}
			DispatchActiveCallbacks("OnFixedUpdate",
				behaviour => behaviour.__FixedUpdate(fixedDeltaTime));
			FlushDeferredChanges();
		}
		finally
		{
			TimeRuntime.EndFixedStep();
		}
    }

    public void DispatchPhysicsEvents(IEnumerable<ScriptPhysicsEvent> events)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_createInvoked)
            throw new InvalidOperationException("Create callbacks must run before physics events.");
        ArgumentNullException.ThrowIfNull(events);

        bool previousFixedScope = TimeRuntime.BeginFixedCallbackBatch();
        try
        {
            foreach (ScriptPhysicsEvent physicsEvent in events)
            {
                if (physicsEvent.EntityA.SceneSessionId != SceneSessionId ||
                    physicsEvent.EntityB.SceneSessionId != SceneSessionId ||
                    physicsEvent.EntityA.RuntimeGeneration != RuntimeGeneration ||
                    physicsEvent.EntityB.RuntimeGeneration != RuntimeGeneration)
                    continue;

                foreach (ScriptInstance instance in _instances.ToArray())
                {
                    if (!CanDispatch(instance))
                        continue;
                    Entity self = instance.Attachment.Entity;
                    Entity other;
                    if (self == physicsEvent.EntityA)
                        other = physicsEvent.EntityB;
                    else if (self == physicsEvent.EntityB)
                        other = physicsEvent.EntityA;
                    else
                        continue;

                    switch (physicsEvent.Kind)
                    {
                        case NativePhysicsEventKindV1.CollisionEnter:
                            Invoke(instance, "OnCollisionEnter2D",
                                behaviour => behaviour.__CollisionEnter(new Collision2D(self, other)));
                            break;
                        case NativePhysicsEventKindV1.CollisionExit:
                            Invoke(instance, "OnCollisionExit2D",
                                behaviour => behaviour.__CollisionExit(new Collision2D(self, other)));
                            break;
                        case NativePhysicsEventKindV1.TriggerEnter:
                            Invoke(instance, "OnTriggerEnter2D",
                                behaviour => behaviour.__TriggerEnter(new Trigger2D(self, other)));
                            break;
                        case NativePhysicsEventKindV1.TriggerExit:
                            Invoke(instance, "OnTriggerExit2D",
                                behaviour => behaviour.__TriggerExit(new Trigger2D(self, other)));
                            break;
                        default:
                            throw new InvalidDataException(
                                $"Unknown physics event kind {physicsEvent.Kind}.");
                    }
                }
            }
            FlushDeferredChanges();
        }
        finally
        {
            TimeRuntime.EndFixedCallbackBatch(previousFixedScope);
        }
    }

    public ScriptInstanceState GetInstanceState(ulong attachmentId) =>
        _instancesByAttachment.TryGetValue(attachmentId, out ScriptInstance? instance)
            ? instance.State
            : throw new KeyNotFoundException($"Attachment {attachmentId} does not exist.");

    public object? ReadFieldValue(ulong attachmentId, string fieldName)
    {
        if (!_instancesByAttachment.TryGetValue(attachmentId, out ScriptInstance? instance) ||
            instance.Behaviour is null)
            throw new KeyNotFoundException($"Attachment {attachmentId} has no live instance.");
        FieldInfo field = instance.Descriptor.Type.GetField(fieldName,
            BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic) ??
            throw new MissingFieldException(instance.Descriptor.Type.FullName, fieldName);
        return field.GetValue(instance.Behaviour);
    }

	public void DestroyAll()
    {
        if (_destroyed)
            return;
		_destroyed = true;
		_pendingEnableChanges.Clear();
		_pendingAttachmentRemovals.Clear();
		_pendingEntityDestructions.Clear();
		_nextBatchEnableChanges.Clear();
		_nextBatchAttachmentRemovals.Clear();
		_nextBatchEntityDestructions.Clear();
		_activeCallbackProjection = null;
		_awaitingCallbackProjections.Clear();
		_callbackLifecycleActivationTransitions.Clear();

		for (int index = _instances.Count - 1; index >= 0; --index)
			DisableInstanceForDestroy(_instances[index]);
		for (int index = _instances.Count - 1; index >= 0; --index)
			DestroyInstance(_instances[index]);
		_instances.Clear();
        _instancesByAttachment.Clear();
		InputActionRuntime.DisableDomain(_domainCancellation);
		_onDestroyed();
	}

	public void DestroyAttachments(IEnumerable<ulong> attachmentIds)
	{
		ObjectDisposedException.ThrowIf(_destroyed, this);
		ArgumentNullException.ThrowIfNull(attachmentIds);
		if (_callbackDepth != 0)
			throw new InvalidOperationException("Attachments can only be destroyed at a native safe point.");
		HashSet<ulong> requested = attachmentIds.Where(static id => id != 0).ToHashSet();
		if (requested.Count == 0)
			return;

		var destroyedEntities = new HashSet<Entity>();
		foreach (ulong attachmentId in requested)
		{
			_pendingEnableChanges.Remove(attachmentId);
			_pendingAttachmentRemovals.Remove(attachmentId);
			if (_instancesByAttachment.TryGetValue(attachmentId,
				out ScriptInstance? instance))
				destroyedEntities.Add(instance.Attachment.Entity);
		}

		ApplyAuthoritativeMutation(() =>
		{
			for (int index = _instances.Count - 1; index >= 0; --index)
			{
				ScriptInstance instance = _instances[index];
				if (requested.Contains(instance.Attachment.AttachmentId))
					DisableInstanceForDestroy(instance);
			}
			for (int index = _instances.Count - 1; index >= 0; --index)
			{
				ScriptInstance instance = _instances[index];
				if (!requested.Contains(instance.Attachment.AttachmentId))
					continue;
				DestroyInstance(instance);
				_instancesByAttachment.Remove(instance.Attachment.AttachmentId);
				_instances.RemoveAt(index);
			}
		});

		// Ignore reentrant requests against attachments/entities that the
		// authoritative native mutation has just removed. Requests targeting other
		// objects remain projected for the next native batch.
		foreach (ulong attachmentId in requested)
		{
			_nextBatchEnableChanges.Remove(attachmentId);
			_nextBatchAttachmentRemovals.Remove(attachmentId);
		}
		_nextBatchEntityDestructions.RemoveWhere(destroyedEntities.Contains);
	}

	private void DisableInstanceForDestroy(ScriptInstance instance)
	{
		if (instance.Behaviour is null || instance.State == ScriptInstanceState.Destroyed
			|| instance.Destroying || !instance.Created || !instance.Enabled)
			return;
		if (instance.State == ScriptInstanceState.Ready)
		{
			instance.Enabled = false;
			SetLifecycleActive(instance, false);
		}
		else
			instance.Enabled = false;
	}

	private void DestroyInstance(ScriptInstance instance)
	{
		if (instance.Behaviour is null || instance.State == ScriptInstanceState.Destroyed
			|| instance.Destroying)
			return;
		instance.Destroying = true;
		// A faulted behaviour still receives exactly one best-effort OnDestroy.
		Invoke(instance, "OnDestroy", static behaviour => behaviour.__Destroy(), allowFaulted: true);
		instance.State = ScriptInstanceState.Destroyed;
		instance.Behaviour = null;
	}

    private void ApplyField(ScriptInstance instance, JsonElement fieldElement)
    {
        string fieldId = fieldElement.GetProperty("fieldId").GetString() ?? string.Empty;
        string name = fieldElement.GetProperty("name").GetString() ?? string.Empty;
        string token = fieldElement.GetProperty("type").GetString() ?? string.Empty;
        if (!Enum.TryParse(token, ignoreCase: false, out ScriptFieldType serializedType))
            return;

        FieldDescriptor? descriptor = null;
        if (!string.IsNullOrEmpty(fieldId))
            instance.Descriptor.FieldsById.TryGetValue(fieldId, out descriptor);
        if (descriptor is null && !string.IsNullOrEmpty(name))
            instance.Descriptor.FieldsByName.TryGetValue(name, out descriptor);
        if (descriptor is null || descriptor.Manifest.Type != serializedType)
            return; // Native data remains authoritative as an Inspector orphan.

        JsonElement value = fieldElement.GetProperty("value");
        object? converted = ConvertFieldValue(descriptor, value);
        descriptor.Field.SetValue(instance.Behaviour, converted);
    }

    private object? ConvertFieldValue(FieldDescriptor descriptor, JsonElement value)
    {
        return descriptor.Manifest.Type switch
        {
            ScriptFieldType.Bool => value.GetBoolean(),
            ScriptFieldType.Int32 => value.GetInt32(),
            ScriptFieldType.Int64 => value.GetInt64(),
            ScriptFieldType.Float => value.GetSingle(),
            ScriptFieldType.Double => value.GetDouble(),
            ScriptFieldType.String => value.GetString() ?? string.Empty,
            ScriptFieldType.Vector2 => ReadVector2(value),
            ScriptFieldType.Vector3 => ReadVector3(value),
            ScriptFieldType.Vector4 => ReadVector4(value),
            ScriptFieldType.Color => ReadColor(value),
			ScriptFieldType.Enum => ReadEnum(descriptor.Field.FieldType, value.GetInt64()),
            ScriptFieldType.Entity => new Entity(SceneSessionId, value.GetUInt64(), RuntimeGeneration),
            ScriptFieldType.AssetRef => Activator.CreateInstance(descriptor.Field.FieldType, value.GetUInt64()),
            _ => throw new InvalidDataException($"Unsupported field type {descriptor.Manifest.Type}.")
		};
	}

	private static object ReadEnum(Type enumType, long storageBits)
	{
		object underlying = Type.GetTypeCode(Enum.GetUnderlyingType(enumType)) switch
		{
			TypeCode.SByte => unchecked((sbyte)storageBits),
			TypeCode.Byte => unchecked((byte)storageBits),
			TypeCode.Int16 => unchecked((short)storageBits),
			TypeCode.UInt16 => unchecked((ushort)storageBits),
			TypeCode.Int32 => unchecked((int)storageBits),
			TypeCode.UInt32 => unchecked((uint)storageBits),
			TypeCode.Int64 => storageBits,
			TypeCode.UInt64 => unchecked((ulong)storageBits),
			_ => throw new InvalidDataException(
				$"Enum '{enumType.FullName}' has an unsupported underlying type.")
		};
		return Enum.ToObject(enumType, underlying);
	}

    private static Vector2 ReadVector2(JsonElement value)
    {
        float[] values = ReadFloatArray(value, 2);
        return new(values[0], values[1]);
    }

    private static Vector3 ReadVector3(JsonElement value)
    {
        float[] values = ReadFloatArray(value, 3);
        return new(values[0], values[1], values[2]);
    }

    private static Vector4 ReadVector4(JsonElement value)
    {
        float[] values = ReadFloatArray(value, 4);
        return new(values[0], values[1], values[2], values[3]);
    }

    private static Color ReadColor(JsonElement value)
    {
        float[] values = ReadFloatArray(value, 4);
        return new(values[0], values[1], values[2], values[3]);
    }

    private static float[] ReadFloatArray(JsonElement value, int count)
    {
        if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != count)
            throw new InvalidDataException($"Expected a numeric array with {count} values.");
        float[] result = new float[count];
        int index = 0;
        foreach (JsonElement element in value.EnumerateArray())
            result[index++] = element.GetSingle();
        return result;
    }

	private void ApplyEnabled(ulong attachmentId, bool enabled)
	{
		ScriptInstance instance = _instancesByAttachment[attachmentId];
		if (instance.State == ScriptInstanceState.Destroyed || instance.Destroying)
			return;
		if (!instance.Created)
		{
			instance.Enabled = enabled;
			return;
		}
		if (instance.State == ScriptInstanceState.Faulted)
		{
			instance.Enabled = enabled;
			return;
		}

		instance.Enabled = enabled;
		SetLifecycleActive(instance, enabled
			&& NativeBridge.IsActiveForScriptHost(instance.Attachment.Entity));
	}

	private void DispatchActiveCallbacks(string callback,
		Action<TomCatBehaviour> dispatch)
	{
		foreach (ScriptInstance instance in _instances.ToArray())
		{
			if (!CanDispatch(instance))
				continue;
			Invoke(instance, callback, dispatch);
		}
	}

	private void ConvergeLifecycleActivation()
	{
		// A callback transaction can enqueue another lifecycle callback while native
		// drains its FIFO. Share one transition budget across that entire drain chain;
		// legacy phase-level hosts keep their historical per-resolution budget.
		Dictionary<ScriptInstance, int> transitions =
			NativeBridge.SupportsDeferredCallbackTransactions
				? _callbackLifecycleActivationTransitions
				: new Dictionary<ScriptInstance, int>();
		++_lifecycleConvergenceDepth;
		try
		{
			while (true)
			{
				bool changed = false;
				foreach (ScriptInstance instance in _instances.ToArray())
				{
					if (!instance.Created || instance.State != ScriptInstanceState.Ready
						|| instance.Destroying || instance.Behaviour is null)
						continue;

					bool active = instance.Enabled
						&& NativeBridge.IsActiveForScriptHost(instance.Attachment.Entity);
					if (active == instance.LifecycleActive)
						continue;

					changed = true;
					int transitionCount = transitions.TryGetValue(instance, out int current)
						? current + 1 : 1;
					transitions[instance] = transitionCount;
					if (transitionCount > MaximumLifecycleActivationTransitionsPerInstance)
					{
						MarkFaulted(instance, "LifecycleActivation",
							new InvalidOperationException(
								$"OnEnable/OnDisable did not stabilize after "
								+ $"{MaximumLifecycleActivationTransitionsPerInstance} transitions."));
						continue;
					}

					SetLifecycleActive(instance, active);
				}
				if (!changed)
					return;
			}
		}
		finally
		{
			--_lifecycleConvergenceDepth;
			if (_lifecycleConvergenceDepth == 0
				&& _activeCallbackProjection is null
				&& _awaitingCallbackProjections.Count == 0)
				_callbackLifecycleActivationTransitions.Clear();
		}
	}

	private void FlushDeferredChanges()
	{
		// Native owns the transaction boundary. These dictionaries are the managed
		// projected view until native replays the validated batch and resolves it
		// through the explicit ManagedApiV2 transaction callback.
	}

	public void ResolveDeferredCommandBatch(bool committed)
	{
		if (_awaitingCallbackProjections.Count != 0)
		{
			// Native seals callback batches before replay and acknowledges them in FIFO
			// order. Discard only the projection owned by this callback; authoritative
			// runtime effects have already updated instances for a committed batch.
			_awaitingCallbackProjections.Dequeue();
			if (_deferLifecycleConvergenceDepth == 0)
				ApplyAuthoritativeMutation(ConvergeLifecycleActivation);
			return;
		}

		if (!committed)
		{
			_pendingEnableChanges.Clear();
			_pendingAttachmentRemovals.Clear();
			_pendingEntityDestructions.Clear();
			_nextBatchEnableChanges.Clear();
			_nextBatchAttachmentRemovals.Clear();
			_nextBatchEntityDestructions.Clear();
			// A projected mutation may have suppressed the initial OnEnable. Once the
			// native batch aborts, restore the lifecycle from unchanged authoritative
			// state and keep mutations queued by that callback for the next batch.
			ApplyAuthoritativeMutation(ConvergeLifecycleActivation);
			PromoteNextBatchMutations();
			return;
		}

		_pendingEnableChanges.Clear();
		_pendingAttachmentRemovals.Clear();
		_pendingEntityDestructions.Clear();

		// Runtime effects from the committed native batch may have queued commands
		// from OnDisable/OnDestroy. Promote those before lifecycle convergence, then
		// promote again for commands queued by the convergence callbacks themselves.
		PromoteNextBatchMutations();
		ApplyAuthoritativeMutation(ConvergeLifecycleActivation);
		PromoteNextBatchMutations();
	}

	private void PromoteNextBatchMutations()
	{
		foreach ((ulong attachmentId, bool enabled) in _nextBatchEnableChanges)
			_pendingEnableChanges[attachmentId] = enabled;
		_pendingAttachmentRemovals.UnionWith(_nextBatchAttachmentRemovals);
		_pendingEntityDestructions.UnionWith(_nextBatchEntityDestructions);
		_nextBatchEnableChanges.Clear();
		_nextBatchAttachmentRemovals.Clear();
		_nextBatchEntityDestructions.Clear();
	}

	private void ApplyAuthoritativeMutation(Action mutation)
	{
		++_authoritativeMutationDepth;
		try
		{
			mutation();
		}
		finally
		{
			--_authoritativeMutationDepth;
		}
	}

	private void RecordPendingEnable(ulong attachmentId, bool enabled)
	{
		if (_activeCallbackProjection is not null)
		{
			_activeCallbackProjection.EnableChanges[attachmentId] = enabled;
			return;
		}
		Dictionary<ulong, bool> changes = _authoritativeMutationDepth == 0
			? _pendingEnableChanges : _nextBatchEnableChanges;
		changes[attachmentId] = enabled;
	}

	private void RecordPendingAttachmentRemoval(ulong attachmentId)
	{
		if (_activeCallbackProjection is not null)
		{
			_activeCallbackProjection.AttachmentRemovals.Add(attachmentId);
			return;
		}
		HashSet<ulong> removals = _authoritativeMutationDepth == 0
			? _pendingAttachmentRemovals : _nextBatchAttachmentRemovals;
		removals.Add(attachmentId);
	}

	private void RecordPendingEntityDestruction(Entity entity)
	{
		if (_activeCallbackProjection is not null)
		{
			_activeCallbackProjection.EntityDestructions.Add(entity);
			return;
		}
		HashSet<Entity> destructions = _authoritativeMutationDepth == 0
			? _pendingEntityDestructions : _nextBatchEntityDestructions;
		destructions.Add(entity);
	}

    private void ValidateDispatch(float deltaTime, string parameterName)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_createInvoked)
            throw new InvalidOperationException("Create callbacks must run before updates.");
        if (!float.IsFinite(deltaTime) || deltaTime < 0.0f)
            throw new ArgumentOutOfRangeException(parameterName);
    }

	private bool CanDispatch(ScriptInstance instance) =>
		instance.Created && instance.Enabled && GetProjectedEnabled(instance)
		&& IsProjectedInstanceAvailable(instance)
		&& instance.LifecycleActive && IsProjectedActiveInHierarchy(instance)
		&& instance.State == ScriptInstanceState.Ready && !instance.Destroying
		&& instance.Behaviour is not null;

	// UnityEvent calls remain valid when a MonoBehaviour is disabled or its entity
	// is inactive. Only a missing, destroyed, removed, or faulted target is rejected.
	private bool CanInvokeEvent(ScriptInstance instance) =>
		instance.Created && IsProjectedInstanceAvailable(instance)
		&& instance.State == ScriptInstanceState.Ready && !instance.Destroying
		&& instance.Behaviour is not null;

	private bool ShouldEnterInitialLifecycle(ScriptInstance instance) =>
		instance.Created && instance.Enabled && GetProjectedEnabled(instance)
		&& IsProjectedInstanceAvailable(instance)
		&& NativeBridge.IsActiveForScriptHost(instance.Attachment.Entity)
		&& IsProjectedActiveInHierarchy(instance)
		&& instance.State == ScriptInstanceState.Ready && !instance.Destroying
		&& instance.Behaviour is not null;

	private void SetLifecycleActive(ScriptInstance instance, bool active)
	{
		if (active == instance.LifecycleActive)
			return;
		instance.LifecycleActive = active;
		Invoke(instance, active ? "OnEnable" : "OnDisable", active
			? static behaviour => behaviour.__Enable()
			: static behaviour => behaviour.__Disable());
	}

	private bool IsProjectedActiveInHierarchy(ScriptInstance instance)
	{
		using ScriptExecutionContext.Scope scope = ScriptExecutionContext.Enter(
			instance.Attachment.Entity, _domainCancellation);
		var visited = new HashSet<Entity>();
		Entity? current = instance.Attachment.Entity;
		while (current is not null)
		{
			if (!visited.Add(current) || !current.ActiveSelf)
				return false;
			current = current.Parent;
		}
		return true;
	}

	private bool GetProjectedEnabled(ScriptInstance instance)
	{
		ulong attachmentId = instance.Attachment.AttachmentId;
		bool enabled = instance.Enabled;
		if (_activeCallbackProjection is not null)
		{
			if (_activeCallbackProjection.EnableChanges.TryGetValue(
				attachmentId, out bool callbackValue))
				enabled = callbackValue;
			return enabled;
		}
		if (_pendingEnableChanges.TryGetValue(attachmentId, out bool pending))
			enabled = pending;
		if (_nextBatchEnableChanges.TryGetValue(attachmentId, out bool next))
			enabled = next;
		return enabled;
	}

	private bool IsProjectedInstanceAvailable(ScriptInstance instance)
	{
		ulong attachmentId = instance.Attachment.AttachmentId;
		if (_activeCallbackProjection is not null)
		{
			if (_activeCallbackProjection.AttachmentRemovals.Contains(attachmentId)
				|| _activeCallbackProjection.EntityDestructions.Contains(
					instance.Attachment.Entity))
				return false;
		}
		else if (_pendingAttachmentRemovals.Contains(attachmentId)
			|| _nextBatchAttachmentRemovals.Contains(attachmentId)
			|| _pendingEntityDestructions.Contains(instance.Attachment.Entity)
			|| _nextBatchEntityDestructions.Contains(instance.Attachment.Entity))
			return false;

		// Native EntityIsAlive includes deferred subtree destruction, so destroying a
		// parent suppresses callbacks for attachments on all projected-dead children.
		using ScriptExecutionContext.Scope scope = ScriptExecutionContext.Enter(
			instance.Attachment.Entity, _domainCancellation);
		return instance.Attachment.Entity.IsValid;
	}

    private void Invoke(ScriptInstance instance, string callback, Action<TomCatBehaviour> invoke,
        bool allowFaulted = false)
    {
        if (instance.Behaviour is null || instance.State == ScriptInstanceState.Destroyed ||
            (!allowFaulted && instance.State == ScriptInstanceState.Faulted))
            return;

		DeferredProjectionFrame? projection = BeginCallbackTransaction(
			instance.Attachment.Entity);
		++_callbackDepth;
		if (_traceCallbacks)
			_callbackTrace.Add($"{instance.Attachment.AttachmentId}:{callback}");
		using ScriptExecutionContext.MutationScope mutationScope =
			ScriptExecutionContext.EnterMutationSink(this);
		using InputActionRuntime.CallbackDispatcherScope inputActionCallbacks =
			InputActionRuntime.EnterCallbackDispatcher(InvokeInputActionCallback);
		try
        {
            invoke(instance.Behaviour);
        }
        catch (Exception exception)
        {
            MarkFaulted(instance, callback, Unwrap(exception));
        }
		finally
		{
			--_callbackDepth;
			CompleteCallbackTransaction(projection);
		}
    }

	private void InvokeInputActionCallback(Action invoke)
	{
		Entity context = ScriptExecutionContext.CurrentEntity;
		DeferredProjectionFrame? projection = BeginCallbackTransaction(context);
		++_callbackDepth;
		using ScriptExecutionContext.MutationScope mutationScope =
			ScriptExecutionContext.EnterMutationSink(this);
		try
		{
			invoke();
		}
		catch (Exception exception)
		{
			// CompleteCallback may synchronously commit the sealed suffix. Mark this
			// handler's transaction aborted before Complete so earlier healthy
			// subscribers stay committed while this handler's writes roll back.
			NativeBridge.AbortDeferredCommandBatch(context,
				$"Managed input action handler threw {exception.GetType().FullName}: " +
				$"{exception.Message}");
			throw;
		}
		finally
		{
			--_callbackDepth;
			CompleteCallbackTransaction(projection);
		}
	}

	private DeferredProjectionFrame? BeginCallbackTransaction(Entity context)
	{
		// HostRegistry removes a Scene before DestroyAll invokes OnDisable/OnDestroy.
		// Teardown mutations are discarded by native StopScene, so they must not
		// attempt to open a transaction against the already-unregistered Scene.
		if (_destroyed || _callbackDepth != 0
			|| _activeCallbackProjection is not null)
			return null;
		if (!NativeBridge.SupportsDeferredCallbackTransactions)
			return null;
		if (!NativeBridge.TryBeginDeferredCallbackTransaction(context,
			out ulong token))
			throw new DeferredCallbackProtocolException(
				"Native could not begin a managed callback transaction.");
		var projection = new DeferredProjectionFrame(token);
		_activeCallbackProjection = projection;
		return projection;
	}

	private void CompleteCallbackTransaction(DeferredProjectionFrame? projection)
	{
		if (projection is null)
			return;
		if (!ReferenceEquals(_activeCallbackProjection, projection))
			throw new DeferredCallbackProtocolException(
				"Managed callback projection stack became inconsistent.");
		_activeCallbackProjection = null;
		// CompleteCallback can synchronously replay native data and re-enter
		// ResolveDeferredCommandBatch, so publish the projection frame first.
		_awaitingCallbackProjections.Enqueue(projection);
		if (!NativeBridge.CompleteDeferredCallbackTransaction(projection.Token))
		{
			// A failed Complete produces no native acknowledgement. Remove this frame
			// before surfacing the protocol fault so a later acknowledgement cannot
			// consume the wrong callback projection.
			RemoveAwaitingCallbackProjection(projection);
			throw new DeferredCallbackProtocolException(
				$"Native callback transaction {projection.Token} could not be completed.");
		}
	}

	private void RemoveAwaitingCallbackProjection(
		DeferredProjectionFrame projection)
	{
		int count = _awaitingCallbackProjections.Count;
		bool removed = false;
		for (int index = 0; index < count; ++index)
		{
			DeferredProjectionFrame queued =
				_awaitingCallbackProjections.Dequeue();
			if (!removed && ReferenceEquals(queued, projection))
			{
				removed = true;
				continue;
			}
			_awaitingCallbackProjections.Enqueue(queued);
		}
	}

	private sealed class DeferredProjectionFrame(ulong token)
	{
		internal ulong Token { get; } = token;
		internal Dictionary<ulong, bool> EnableChanges { get; } = [];
		internal HashSet<ulong> AttachmentRemovals { get; } = [];
		internal HashSet<Entity> EntityDestructions { get; } = [];
	}

    private static Exception Unwrap(Exception exception) =>
        exception is TargetInvocationException { InnerException: not null } invocation
            ? invocation.InnerException!
            : exception;

    private static void MarkFaulted(ScriptInstance instance, string callback, Exception exception)
    {
        instance.State = ScriptInstanceState.Faulted;
        string message = $"Managed script '{instance.Descriptor.Manifest.TypeName}' on entity " +
            $"{instance.Attachment.Entity.Id}, attachment {instance.Attachment.AttachmentId}, " +
            $"callback {callback} threw {exception.GetType().FullName}: {exception.Message}\n{exception.StackTrace}";
		NativeBridge.AbortDeferredCommandBatch(instance.Attachment.Entity, message);
        NativeBridge.ReportManagedException(message, exception);
    }

    private sealed class ScriptInstance(ScriptAttachment attachment, ScriptDescriptor descriptor,
        int sequence)
    {
        internal ScriptAttachment Attachment { get; } = attachment;
        internal ScriptDescriptor Descriptor { get; } = descriptor;
        internal int Sequence { get; } = sequence;
        internal TomCatBehaviour? Behaviour { get; set; }
        internal bool Enabled { get; set; } = attachment.Enabled;
		internal bool LifecycleActive { get; set; }
		internal bool Created { get; set; }
		internal bool Destroying { get; set; }
		internal ScriptInstanceState State { get; set; } = ScriptInstanceState.Ready;
    }
}
