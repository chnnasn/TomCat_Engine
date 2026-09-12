using System.Reflection;
using System.Text.Json;
using TomCat.Interop;

namespace TomCat.ScriptHost;

public readonly record struct ScriptPhysicsEvent(NativePhysicsEventKindV1 Kind, Entity EntityA,
    Entity EntityB);

public sealed class ScriptSceneRuntime : IScriptMutationSink
{
    private readonly Dictionary<ulong, ScriptDescriptor> _descriptors;
    private readonly Action _onDestroyed;
	private readonly CancellationToken _domainCancellation;
    private readonly List<ScriptInstance> _instances = [];
    private readonly Dictionary<ulong, ScriptInstance> _instancesByAttachment = [];
	private readonly Dictionary<ulong, bool> _pendingEnableChanges = [];
	private readonly HashSet<ulong> _pendingAttachmentRemovals = [];
	private readonly HashSet<Entity> _pendingEntityDestructions = [];
	private readonly List<string> _callbackTrace = [];
	private int _callbackDepth;
	private bool _flushingDeferredChanges;
	private bool _traceCallbacks;
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

		int sequence = 0;
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

            var instance = new ScriptInstance(attachment, descriptor, sequence++);
            try
            {
                instance.Behaviour = descriptor.ConstructorFactory();
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

        _instances.Sort(static (left, right) =>
        {
            int order = left.Descriptor.Manifest.ExecutionOrder.CompareTo(
                right.Descriptor.Manifest.ExecutionOrder);
            return order != 0 ? order : left.Sequence.CompareTo(right.Sequence);
        });
    }

    public void ApplySerializedFields(string json)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_instantiated || _createInvoked)
            throw new InvalidOperationException("Serialized fields must be applied after InstantiateAll and before InvokeCreateAll.");
        ArgumentException.ThrowIfNullOrWhiteSpace(json);

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

        foreach (ScriptInstance instance in _instances)
        {
            if (instance.State == ScriptInstanceState.Ready)
            {
                Invoke(instance, "OnCreate", static behaviour => behaviour.__Create());
                instance.Created = instance.State == ScriptInstanceState.Ready;
            }
        }
        foreach (ScriptInstance instance in _instances)
        {
            if (instance.Created && instance.Enabled && instance.State == ScriptInstanceState.Ready)
                Invoke(instance, "OnEnable", static behaviour => behaviour.__Enable());
        }
        FlushDeferredChanges();
    }

	public void SetEnabled(ulong attachmentId, bool enabled)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_instancesByAttachment.ContainsKey(attachmentId))
            throw new KeyNotFoundException($"Attachment {attachmentId} does not exist.");
        if (_callbackDepth != 0)
        {
            _pendingEnableChanges[attachmentId] = enabled;
            return;
        }
		ApplyEnabled(attachmentId, enabled);
	}

	void IScriptMutationSink.SetBehaviourEnabled(ulong attachmentId, bool enabled)
	{
		if (!_destroyed && _instancesByAttachment.ContainsKey(attachmentId))
			_pendingEnableChanges[attachmentId] = enabled;
	}

	void IScriptMutationSink.RemoveBehaviour(ulong attachmentId)
	{
		if (!_destroyed && _instancesByAttachment.ContainsKey(attachmentId))
			_pendingAttachmentRemovals.Add(attachmentId);
	}

	void IScriptMutationSink.DestroyEntity(Entity entity)
	{
		if (!_destroyed && entity.SceneSessionId == SceneSessionId
			&& entity.RuntimeGeneration == RuntimeGeneration && entity.Id != 0)
			_pendingEntityDestructions.Add(entity);
	}

    public void UpdateAll(float deltaTime)
    {
        ValidateDispatch(deltaTime, nameof(deltaTime));
        foreach (ScriptInstance instance in _instances)
        {
            if (CanDispatch(instance))
                Invoke(instance, "OnUpdate", behaviour => behaviour.__Update(deltaTime));
        }
        FlushDeferredChanges();
    }

    public void FixedUpdateAll(float fixedDeltaTime)
    {
        ValidateDispatch(fixedDeltaTime, nameof(fixedDeltaTime));
        foreach (ScriptInstance instance in _instances)
        {
            if (CanDispatch(instance))
                Invoke(instance, "OnFixedUpdate", behaviour => behaviour.__FixedUpdate(fixedDeltaTime));
        }
        FlushDeferredChanges();
    }

    public void DispatchPhysicsEvents(IEnumerable<ScriptPhysicsEvent> events)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_createInvoked)
            throw new InvalidOperationException("Create callbacks must run before physics events.");
        ArgumentNullException.ThrowIfNull(events);

        foreach (ScriptPhysicsEvent physicsEvent in events)
        {
            if (physicsEvent.EntityA.SceneSessionId != SceneSessionId ||
                physicsEvent.EntityB.SceneSessionId != SceneSessionId ||
                physicsEvent.EntityA.RuntimeGeneration != RuntimeGeneration ||
                physicsEvent.EntityB.RuntimeGeneration != RuntimeGeneration)
                continue;

            foreach (ScriptInstance instance in _instances)
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
                        throw new InvalidDataException($"Unknown physics event kind {physicsEvent.Kind}.");
                }
            }
        }
        FlushDeferredChanges();
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

		for (int index = _instances.Count - 1; index >= 0; --index)
			DisableInstanceForDestroy(_instances[index]);
		for (int index = _instances.Count - 1; index >= 0; --index)
			DestroyInstance(_instances[index]);
		_instances.Clear();
        _instancesByAttachment.Clear();
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
		_pendingEnableChanges.Remove(instance.Attachment.AttachmentId);
		_pendingAttachmentRemovals.Remove(instance.Attachment.AttachmentId);
		_instances.RemoveAt(index);
		}
	}

	private void DisableInstanceForDestroy(ScriptInstance instance)
	{
		if (instance.Behaviour is null || instance.State == ScriptInstanceState.Destroyed
			|| instance.Destroying || !instance.Created || !instance.Enabled)
			return;
		if (instance.State == ScriptInstanceState.Ready)
		{
			instance.Enabled = false;
			Invoke(instance, "OnDisable", static behaviour => behaviour.__Disable());
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
		if (instance.Enabled == enabled || instance.State == ScriptInstanceState.Destroyed
			|| instance.Destroying)
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

        if (enabled)
        {
            instance.Enabled = true;
            Invoke(instance, "OnEnable", static behaviour => behaviour.__Enable());
        }
		else
		{
			instance.Enabled = false;
			Invoke(instance, "OnDisable", static behaviour => behaviour.__Disable());
		}
	}

	private void FlushDeferredChanges()
	{
		if (_callbackDepth != 0 || _flushingDeferredChanges)
			return;

		_flushingDeferredChanges = true;
		try
		{
			while (_pendingEnableChanges.Count != 0
				|| _pendingAttachmentRemovals.Count != 0
				|| _pendingEntityDestructions.Count != 0)
			{
				var destroyAttachments = new HashSet<ulong>(_pendingAttachmentRemovals);
				_pendingAttachmentRemovals.Clear();
				if (_pendingEntityDestructions.Count != 0)
				{
					foreach (ScriptInstance instance in _instances)
					{
						if (_pendingEntityDestructions.Contains(instance.Attachment.Entity))
							destroyAttachments.Add(instance.Attachment.AttachmentId);
					}
					_pendingEntityDestructions.Clear();
				}

				if (destroyAttachments.Count != 0)
				{
					foreach (ulong attachmentId in destroyAttachments)
						_pendingEnableChanges.Remove(attachmentId);
					for (int index = _instances.Count - 1; index >= 0; --index)
					{
						if (destroyAttachments.Contains(_instances[index].Attachment.AttachmentId))
							DisableInstanceForDestroy(_instances[index]);
					}
					for (int index = _instances.Count - 1; index >= 0; --index)
					{
						if (destroyAttachments.Contains(_instances[index].Attachment.AttachmentId))
							DestroyInstance(_instances[index]);
					}
				}

				KeyValuePair<ulong, bool>[] pending = _pendingEnableChanges.ToArray();
				_pendingEnableChanges.Clear();
				foreach ((ulong attachmentId, bool enabled) in pending)
				{
					if (_instancesByAttachment.ContainsKey(attachmentId))
						ApplyEnabled(attachmentId, enabled);
				}
			}
		}
		finally
		{
			_flushingDeferredChanges = false;
		}
	}

    private void ValidateDispatch(float deltaTime, string parameterName)
    {
        ObjectDisposedException.ThrowIf(_destroyed, this);
        if (!_createInvoked)
            throw new InvalidOperationException("Create callbacks must run before updates.");
        if (!float.IsFinite(deltaTime) || deltaTime < 0.0f)
            throw new ArgumentOutOfRangeException(parameterName);
    }

    private static bool CanDispatch(ScriptInstance instance) => instance.Created && instance.Enabled &&
		instance.State == ScriptInstanceState.Ready && !instance.Destroying
		&& instance.Behaviour is not null;

    private void Invoke(ScriptInstance instance, string callback, Action<TomCatBehaviour> invoke,
        bool allowFaulted = false)
    {
        if (instance.Behaviour is null || instance.State == ScriptInstanceState.Destroyed ||
            (!allowFaulted && instance.State == ScriptInstanceState.Faulted))
            return;

		++_callbackDepth;
		if (_traceCallbacks)
			_callbackTrace.Add($"{instance.Attachment.AttachmentId}:{callback}");
		using ScriptExecutionContext.MutationScope mutationScope =
			ScriptExecutionContext.EnterMutationSink(this);
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
			FlushDeferredChanges();
		}
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
        NativeBridge.ReportManagedException(message);
    }

    private sealed class ScriptInstance(ScriptAttachment attachment, ScriptDescriptor descriptor,
        int sequence)
    {
        internal ScriptAttachment Attachment { get; } = attachment;
        internal ScriptDescriptor Descriptor { get; } = descriptor;
        internal int Sequence { get; } = sequence;
        internal TomCatBehaviour? Behaviour { get; set; }
        internal bool Enabled { get; set; } = attachment.Enabled;
		internal bool Created { get; set; }
		internal bool Destroying { get; set; }
		internal ScriptInstanceState State { get; set; } = ScriptInstanceState.Ready;
    }
}
