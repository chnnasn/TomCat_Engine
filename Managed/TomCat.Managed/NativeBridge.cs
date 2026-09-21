using System.Runtime.InteropServices;
using System.Text;
using TomCat.Interop;

namespace TomCat;

public sealed class TomCatException : InvalidOperationException
{
    public TomCatException(string message) : base(message) { }
}

internal sealed class DeferredCallbackProtocolException : InvalidOperationException
{
    internal DeferredCallbackProtocolException(string message) : base(message) { }
	internal DeferredCallbackProtocolException(string message, Exception innerException)
		: base(message, innerException) { }
}

internal static unsafe partial class NativeBridge
{
	private const int NativeInvalidArgument = -1;
	private const int NativeVersionMismatch = -4;
	private const int NativeNotFound = -3;
	private const int NativeBufferTooSmall = -6;
	private const int NativeUnavailable = -8;

	private static readonly object s_bindGate = new();
    private static NativeApiV1 s_api;
	private static NativeInputApiV1 s_inputApi;
	private static NativeInputEventsApiV1 s_inputEventsApi;
	private static NativeApplicationPathsApiV1 s_applicationPathsApi;
	private static NativeComponentApiV1 s_componentApi;
	private static NativeComponentStringApiV1 s_componentStringApi;
	private static NativeDeferredCommandsApiV1 s_deferredCommandsApi;
	private static NativeDeferredCallbackTransactionsApiV1
		s_deferredCallbackTransactionsApi;
	private static NativeComponentSchemaApiV1 s_componentSchemaApi;
	private static NativeAudioApiV1 s_audioApi;
	private static NativeAudioSpatialApiV1 s_audioSpatialApi;
	private static NativeRuntimeUIApiV1 s_runtimeUIApi;
	private static NativeGameplayApiV1 s_gameplayApi;
	private static readonly UTF8Encoding s_strictUtf8 = new(false, true);
    private static bool s_bound;
	private static bool s_inputBound;
	private static bool s_inputEventsBound;
	private static bool s_applicationPathsBound;
	private static bool s_componentBound;
	private static bool s_componentStringBound;
	private static bool s_deferredCommandsBound;
	private static bool s_deferredCallbackTransactionsBound;
	private static bool s_componentSchemaBound;
	private static bool s_audioBound;
	private static bool s_audioSpatialBound;
	private static bool s_runtimeUIBound;
	private static bool s_gameplayBound;
	[ThreadStatic] private static ulong s_activeDeferredCallbackToken;
	[ThreadStatic] private static bool s_deferredAbortProtocolFailed;

    internal static bool IsBound => Volatile.Read(ref s_bound);
	internal static bool SupportsDeferredCallbackTransactions =>
		Volatile.Read(ref s_deferredCommandsBound)
		&& Volatile.Read(ref s_deferredCallbackTransactionsBound);

    internal static int Bind(NativeApiV1* api)
    {
		if (api is null)
			return NativeInvalidArgument;
		if (api->Version != ManagedAbi.NativeApiVersion
			|| api->Size < (uint)sizeof(NativeApiV1))
			return NativeVersionMismatch;

		NativeApiV1 candidate = *api;
		if (!HasRequiredCallbacks(candidate))
			return NativeUnavailable;
		NativeSceneApiV1 sceneCandidate = default;
		bool hasSceneCandidate = TryReadSceneCapability(api, out sceneCandidate);
		NativeInputApiV1 inputCandidate = default;
		bool hasInputCandidate = TryReadInputCapability(api, out inputCandidate);
		NativeInputEventsApiV1 inputEventsCandidate = default;
		bool hasInputEventsCandidate = TryReadInputEventsCapability(api,
			out inputEventsCandidate);
		NativeApplicationPathsApiV1 applicationPathsCandidate = default;
		bool hasApplicationPathsCandidate = TryReadApplicationPathsCapability(api,
			out applicationPathsCandidate);
		NativeComponentApiV1 componentCandidate = default;
		bool hasComponentCandidate = TryReadComponentCapability(api,
			out componentCandidate);
		NativeComponentStringApiV1 componentStringCandidate = default;
		bool hasComponentStringCandidate = TryReadComponentStringCapability(api,
			out componentStringCandidate);
		NativeDeferredCommandsApiV1 deferredCommandsCandidate = default;
		bool hasDeferredCommandsCandidate = TryReadDeferredCommandsCapability(api,
			out deferredCommandsCandidate);
		NativeDeferredCallbackTransactionsApiV1
			deferredCallbackTransactionsCandidate = default;
		bool hasDeferredCallbackTransactionsCandidate =
			TryReadDeferredCallbackTransactionsCapability(api,
				out deferredCallbackTransactionsCandidate);
		NativeComponentSchemaApiV1 componentSchemaCandidate = default;
		bool hasComponentSchemaCandidate = TryReadComponentSchemaCapability(api,
			out componentSchemaCandidate);
		NativeAudioApiV1 audioCandidate = default;
		bool hasAudioCandidate = TryReadAudioCapability(api, out audioCandidate);
		NativeAudioSpatialApiV1 audioSpatialCandidate = default;
		bool hasAudioSpatialCandidate = TryReadAudioSpatialCapability(api,
			out audioSpatialCandidate);
		NativeRuntimeUIApiV1 runtimeUICandidate = default;
		bool hasRuntimeUICandidate = TryReadRuntimeUICapability(api,
			out runtimeUICandidate);
		NativeGameplayApiV1 gameplayCandidate = default;
		bool hasGameplayCandidate = TryReadGameplayCapability(api,
			out gameplayCandidate);

		// Publish only a fully validated table. A rejected rebind leaves the last
		// complete process-lifetime table and its bound state untouched.
		lock (s_bindGate)
		{
			if (hasSceneCandidate && !s_sceneBound)
			{
				s_sceneApi = sceneCandidate;
				Volatile.Write(ref s_sceneBound, true);
			}
			if (!s_bound)
			{
				s_api = candidate;
				Volatile.Write(ref s_bound, true);
			}
			if (hasInputCandidate && !s_inputBound)
			{
				s_inputApi = inputCandidate;
				Volatile.Write(ref s_inputBound, true);
			}
			if (hasInputEventsCandidate && !s_inputEventsBound)
			{
				s_inputEventsApi = inputEventsCandidate;
				Volatile.Write(ref s_inputEventsBound, true);
			}
			if (hasApplicationPathsCandidate && !s_applicationPathsBound)
			{
				s_applicationPathsApi = applicationPathsCandidate;
				Volatile.Write(ref s_applicationPathsBound, true);
			}
			if (hasComponentCandidate && !s_componentBound)
			{
				s_componentApi = componentCandidate;
				Volatile.Write(ref s_componentBound, true);
			}
			if (hasComponentStringCandidate && !s_componentStringBound)
			{
				s_componentStringApi = componentStringCandidate;
				Volatile.Write(ref s_componentStringBound, true);
			}
			if (hasDeferredCommandsCandidate && !s_deferredCommandsBound)
			{
				s_deferredCommandsApi = deferredCommandsCandidate;
				Volatile.Write(ref s_deferredCommandsBound, true);
			}
			if (hasDeferredCallbackTransactionsCandidate
				&& !s_deferredCallbackTransactionsBound)
			{
				s_deferredCallbackTransactionsApi =
					deferredCallbackTransactionsCandidate;
				Volatile.Write(ref s_deferredCallbackTransactionsBound, true);
			}
			if (hasComponentSchemaCandidate && !s_componentSchemaBound)
			{
				s_componentSchemaApi = componentSchemaCandidate;
				Volatile.Write(ref s_componentSchemaBound, true);
			}
			if (hasAudioCandidate && !s_audioBound)
			{
				s_audioApi = audioCandidate;
				Volatile.Write(ref s_audioBound, true);
			}
			if (hasAudioSpatialCandidate && !s_audioSpatialBound)
			{
				s_audioSpatialApi = audioSpatialCandidate;
				Volatile.Write(ref s_audioSpatialBound, true);
			}
			if (hasRuntimeUICandidate && !s_runtimeUIBound)
			{
				s_runtimeUIApi = runtimeUICandidate;
				Volatile.Write(ref s_runtimeUIBound, true);
			}
			if (hasGameplayCandidate && !s_gameplayBound)
			{
				s_gameplayApi = gameplayCandidate;
				Volatile.Write(ref s_gameplayBound, true);
			}
		}
        return 0;
    }

	private static bool TryReadInputCapability(NativeApiV1* api,
		out NativeInputApiV1 input)
	{
		input = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes("TomCat.InputApiV1");
		NativeInputApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeInputApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeInputApiV1))
				return false;
		}
		input = candidate;
		return input.Version == 1 && input.Size >= (uint)sizeof(NativeInputApiV1)
			&& input.MaximumGamepads > 0 && input.MaximumGamepads <= 16
			&& input.GamepadButtonCount >= 15 && input.GamepadAxisCount >= 6
			&& input.IsMouseButtonHeld != null && input.WasMouseButtonPressed != null
			&& input.WasMouseButtonReleased != null && input.GetScrollDelta != null
			&& input.IsWindowFocused != null && input.IsGamepadConnected != null
			&& input.WasGamepadConnected != null && input.WasGamepadDisconnected != null
			&& input.IsGamepadButtonHeld != null && input.WasGamepadButtonPressed != null
			&& input.WasGamepadButtonReleased != null && input.GetGamepadAxis != null
			&& input.GetGamepadName != null;
	}

	private static bool TryReadInputEventsCapability(NativeApiV1* api,
		out NativeInputEventsApiV1 inputEvents)
	{
		inputEvents = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.InputEventsApiV1");
		NativeInputEventsApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeInputEventsApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeInputEventsApiV1))
				return false;
		}
		inputEvents = candidate;
		return inputEvents.Version == 1
			&& inputEvents.Size >= (uint)sizeof(NativeInputEventsApiV1)
			&& inputEvents.GetBatchInfo != null && inputEvents.CopyEvents != null;
	}

	private static bool TryReadApplicationPathsCapability(NativeApiV1* api,
		out NativeApplicationPathsApiV1 applicationPaths)
	{
		applicationPaths = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.ApplicationPathsApiV1");
		NativeApplicationPathsApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeApplicationPathsApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeApplicationPathsApiV1))
				return false;
		}
		applicationPaths = candidate;
		return applicationPaths.Version == 1
			&& applicationPaths.Size >= (uint)sizeof(NativeApplicationPathsApiV1)
			&& applicationPaths.GetSaveDirectory != null
			&& applicationPaths.GetLogDirectory != null
			&& applicationPaths.GetCrashDirectory != null;
	}

	private static bool TryReadComponentCapability(NativeApiV1* api,
		out NativeComponentApiV1 component)
	{
		component = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes("TomCat.ComponentApiV1");
		NativeComponentApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeComponentApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeComponentApiV1))
				return false;
		}
		component = candidate;
		return component.Version == 1
			&& component.Size >= (uint)sizeof(NativeComponentApiV1)
			&& component.Has != null && component.Add != null
			&& component.Remove != null && component.GetProperty != null
			&& component.SetProperty != null;
	}

	private static bool TryReadComponentStringCapability(NativeApiV1* api,
		out NativeComponentStringApiV1 componentString)
	{
		componentString = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes("TomCat.ComponentStringApiV1");
		NativeComponentStringApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeComponentStringApiV1), &required);
			if (status != 0
				|| required > (uint)sizeof(NativeComponentStringApiV1))
				return false;
		}
		componentString = candidate;
		return componentString.Version == 1
			&& componentString.Size >= (uint)sizeof(NativeComponentStringApiV1)
			&& componentString.GetProperty != null
			&& componentString.SetProperty != null;
	}

	private static bool TryReadDeferredCommandsCapability(NativeApiV1* api,
		out NativeDeferredCommandsApiV1 deferredCommands)
	{
		deferredCommands = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes("TomCat.DeferredCommandsApiV1");
		NativeDeferredCommandsApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeDeferredCommandsApiV1), &required);
			if (status != 0
				|| required > (uint)sizeof(NativeDeferredCommandsApiV1))
				return false;
		}
		deferredCommands = candidate;
		return deferredCommands.Version == 1
			&& deferredCommands.Size >= (uint)sizeof(NativeDeferredCommandsApiV1)
			&& deferredCommands.AbortBatch != null;
	}

	private static bool TryReadDeferredCallbackTransactionsCapability(
		NativeApiV1* api,
		out NativeDeferredCallbackTransactionsApiV1 transactions)
	{
		transactions = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes(
			"TomCat.DeferredCallbackTransactionsApiV1");
		NativeDeferredCallbackTransactionsApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate,
				(uint)sizeof(NativeDeferredCallbackTransactionsApiV1), &required);
			if (status != 0
				|| required
					> (uint)sizeof(NativeDeferredCallbackTransactionsApiV1))
				return false;
		}
		transactions = candidate;
		return transactions.Version == 1
			&& transactions.Size
				>= (uint)sizeof(NativeDeferredCallbackTransactionsApiV1)
			&& transactions.BeginCallback != null
			&& transactions.CompleteCallback != null;
	}

	private static bool TryReadComponentSchemaCapability(NativeApiV1* api,
		out NativeComponentSchemaApiV1 schema)
	{
		schema = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;

		byte[] name = Encoding.UTF8.GetBytes("TomCat.ComponentSchemaApiV1");
		NativeComponentSchemaApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeComponentSchemaApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeComponentSchemaApiV1))
				return false;
		}
		schema = candidate;
		return schema.Version == 1
			&& schema.Size >= (uint)sizeof(NativeComponentSchemaApiV1)
			&& schema.GetComponentCount != null && schema.GetComponent != null
			&& schema.GetPropertyCount != null && schema.GetProperty != null;
	}

	private static bool TryReadAudioCapability(NativeApiV1* api,
		out NativeAudioApiV1 audio)
	{
		audio = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.AudioApiV1");
		NativeAudioApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeAudioApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeAudioApiV1))
				return false;
		}
		audio = candidate;
		return audio.Version == 1 && audio.Size >= (uint)sizeof(NativeAudioApiV1)
			&& audio.IsHardwareAvailable != null && audio.GetBackendName != null
			&& audio.HasSource != null && audio.AddSource != null
			&& audio.RemoveSource != null && audio.GetClip != null
			&& audio.SetClip != null && audio.GetEnabled != null
			&& audio.SetEnabled != null && audio.GetPlayOnStart != null
			&& audio.SetPlayOnStart != null && audio.GetLoop != null
			&& audio.SetLoop != null && audio.GetVolume != null
			&& audio.SetVolume != null && audio.GetPitch != null
			&& audio.SetPitch != null && audio.GetMixerGroup != null
			&& audio.SetMixerGroup != null && audio.Play != null
			&& audio.Pause != null && audio.Stop != null
			&& audio.GetPlaybackState != null && audio.HasListener != null
			&& audio.AddListener != null && audio.RemoveListener != null
			&& audio.GetListenerEnabled != null && audio.SetListenerEnabled != null
			&& audio.GetListenerPrimary != null && audio.SetListenerPrimary != null
			&& audio.GetMixerVolume != null && audio.SetMixerVolume != null;
	}

	private static bool TryReadAudioSpatialCapability(NativeApiV1* api,
		out NativeAudioSpatialApiV1 audio)
	{
		audio = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.AudioSpatialApiV1");
		NativeAudioSpatialApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeAudioSpatialApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeAudioSpatialApiV1))
				return false;
		}
		audio = candidate;
		return audio.Version == 1
			&& audio.Size >= (uint)sizeof(NativeAudioSpatialApiV1)
			&& audio.GetStreaming != null && audio.SetStreaming != null
			&& audio.GetSpatialBlend != null && audio.SetSpatialBlend != null
			&& audio.GetMinDistance != null && audio.SetMinDistance != null
			&& audio.GetMaxDistance != null && audio.SetMaxDistance != null;
	}

	private static bool TryReadRuntimeUICapability(NativeApiV1* api,
		out NativeRuntimeUIApiV1 runtimeUI)
	{
		runtimeUI = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.RuntimeUIApiV1");
		NativeRuntimeUIApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeRuntimeUIApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeRuntimeUIApiV1))
				return false;
		}
		runtimeUI = candidate;
		return runtimeUI.Version == 1
			&& runtimeUI.Size >= (uint)sizeof(NativeRuntimeUIApiV1)
			&& runtimeUI.GetText != null && runtimeUI.SetText != null
			&& runtimeUI.WasButtonClicked != null
			&& runtimeUI.GetButtonClickSerial != null
			&& runtimeUI.FocusButton != null && runtimeUI.GetRect != null
			&& runtimeUI.IsGameplayInputCaptured != null;
	}

	private static bool TryReadGameplayCapability(NativeApiV1* api,
		out NativeGameplayApiV1 gameplay)
	{
		gameplay = default;
		if (api->Size < (uint)sizeof(NativeApiV2))
			return false;
		NativeApiV2* envelope = (NativeApiV2*)api;
		if (envelope->QueryCapability == null)
			return false;
		byte[] name = Encoding.UTF8.GetBytes("TomCat.GameplayApiV1");
		NativeGameplayApiV1 candidate = default;
		fixed (byte* namePointer = name)
		{
			uint required = 0;
			int status = envelope->QueryCapability(
				new NativeUtf8View(namePointer, (ulong)name.Length), 1,
				&candidate, (uint)sizeof(NativeGameplayApiV1), &required);
			if (status != 0 || required > (uint)sizeof(NativeGameplayApiV1))
				return false;
		}
		gameplay = candidate;
		uint requiredPrefix = (uint)Marshal.OffsetOf<NativeGameplayApiV1>(
			nameof(NativeGameplayApiV1.SpriteAnimatorSetBool));
		return gameplay.Version == 1
			&& gameplay.Size >= requiredPrefix
			&& gameplay.CreateEntityDeferred != null
			&& gameplay.FindEntityByName != null
			&& gameplay.QueryEntities != null && gameplay.GetParent != null
			&& gameplay.SetParentDeferred != null && gameplay.GetChildren != null
			&& gameplay.GetActiveSelf != null && gameplay.SetActiveSelf != null
			&& gameplay.GetActiveInHierarchy != null
			&& gameplay.TransformGetLocalPosition != null
			&& gameplay.TransformSetLocalPosition != null
			&& gameplay.TransformGetLocalRotationEuler != null
			&& gameplay.TransformSetLocalRotationEuler != null
			&& gameplay.TransformGetLocalScale != null
			&& gameplay.TransformSetLocalScale != null
			&& gameplay.GetComponentProperty != null
			&& gameplay.SetComponentProperty != null
			&& gameplay.SpriteAnimatorPlay != null
			&& gameplay.SpriteAnimatorStop != null;
	}

	private static bool HasRequiredCallbacks(NativeApiV1 api) =>
		api.Log != null && api.EmitDiagnostic != null && api.IsMainThread != null &&
		api.EntityIsAlive != null && api.EntityGetName != null && api.EntitySetName != null &&
		api.EntityGetTag != null && api.EntitySetTag != null && api.EntityGetLayer != null &&
		api.EntitySetLayer != null && api.DestroyEntityDeferred != null &&
		api.HasComponent != null && api.AddComponentDeferred != null &&
		api.RemoveComponentDeferred != null && api.TransformGetPosition != null &&
		api.TransformSetPosition != null && api.TransformGetRotationEuler != null &&
		api.TransformSetRotationEuler != null && api.TransformGetScale != null &&
		api.TransformSetScale != null && api.TransformGetWorldMatrix != null &&
		api.InputIsKeyHeld != null && api.InputWasKeyPressed != null &&
		api.InputWasKeyReleased != null && api.InputGetMousePosition != null &&
		api.InputGetMouseDelta != null && api.InputGetModifiers != null &&
		api.RigidbodyGetLinearVelocity != null && api.RigidbodySetLinearVelocity != null &&
		api.RigidbodyApplyForce != null && api.RigidbodyApplyLinearImpulse != null &&
		api.PhysicsRaycast != null && api.PhysicsQueryAabb != null &&
		api.AssetIsValid != null && api.AssetGetType != null &&
		api.BehaviourGetEnabled != null && api.BehaviourSetEnabledDeferred != null &&
		api.BehaviourRemoveDeferred != null && api.SceneGetActiveHandle != null &&
		api.SceneGetActiveBuildIndex != null && api.SceneRequestLoadHandle != null &&
		api.SceneRequestLoadIndex != null && api.SceneRequestReload != null &&
		api.PrefabInstantiateDeferred != null;

	internal static void EnsureMainThread(bool requireLifecycleContext = true)
	{
		if (!Volatile.Read(ref s_bound))
			throw new TomCatException("The TomCat native API is not initialized.");
		if (s_api.IsMainThread == null || s_api.IsMainThread() == 0)
			throw new TomCatException(
				"WrongThread: TomCat engine APIs may only be used from the main thread.");
		if (requireLifecycleContext && !ScriptExecutionContext.IsActive)
			throw new TomCatException(
				"TomCat engine APIs cannot be used from a script constructor or field initializer. Use OnCreate or another lifecycle callback.");
    }

    internal static NativeEntityHandleV1 ToNative(Entity entity) =>
        new(entity.SceneSessionId, entity.Id, entity.RuntimeGeneration);

    internal static Entity FromNative(NativeEntityHandleV1 entity) =>
        new(entity.SceneSessionId, entity.EntityId, entity.RuntimeGeneration);

    internal static bool EntityIsAlive(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.EntityIsAlive != null, "Entity.IsAlive");
        return ReadBoolean(s_api.EntityIsAlive(ToNative(entity)), "Entity.IsAlive");
    }

    internal static string GetEntityName(Entity entity) => ReadEntityString(entity, s_api.EntityGetName, "Entity.Name");
    internal static string GetEntityTag(Entity entity) => ReadEntityString(entity, s_api.EntityGetTag, "Entity.Tag");

    private static string ReadEntityString(Entity entity,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, byte*, uint, uint*, int> callback,
        string operation)
    {
        EnsureMainThread();
		Require(callback != null, operation);
		uint required = 0;
		int probeStatus = callback(ToNative(entity), null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, operation);
        if (required == 0)
            return string.Empty;
        if (required > 16 * 1024 * 1024)
            throw new TomCatException($"{operation} returned an invalid UTF-8 length.");

        byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
        fixed (byte* buffer = bytes)
        {
            uint actual = required;
            Check(callback(ToNative(entity), buffer, required, &actual), operation);
            if (actual > required)
                throw new TomCatException($"{operation} changed length while being read.");
            return Encoding.UTF8.GetString(bytes, 0, (int)actual);
        }
    }

    internal static void SetEntityName(Entity entity, string value) =>
        WithEntityUtf8(entity, value, "Entity.Name",
			view => s_api.EntitySetName(ToNative(entity), view),
			s_api.EntitySetName != null);

    internal static void SetEntityTag(Entity entity, string value) =>
        WithEntityUtf8(entity, value, "Entity.Tag",
			view => s_api.EntitySetTag(ToNative(entity), view),
			s_api.EntitySetTag != null);

    internal static uint GetEntityLayer(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.EntityGetLayer != null, "Entity.Layer");
        uint value = 0;
        Check(s_api.EntityGetLayer(ToNative(entity), &value), "Entity.Layer");
        return value;
    }

    internal static void SetEntityLayer(Entity entity, uint value)
    {
        EnsureMainThread();
        Require(s_api.EntitySetLayer != null, "Entity.Layer");
        Check(s_api.EntitySetLayer(ToNative(entity), value), "Entity.Layer");
    }

    internal static void DestroyEntity(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.DestroyEntityDeferred != null, "Entity.Destroy");
        Check(s_api.DestroyEntityDeferred(ToNative(entity)), "Entity.Destroy");
    }

	internal static Entity CreateEntity(Entity context, string name,
		Vector3 worldPosition, Entity? parent)
	{
		EnsureMainThread();
		if (name is null)
		{
			AbortDeferredCommandBatch(context,
				"World.CreateEntity name cannot be null");
			throw new ArgumentNullException(nameof(name));
		}
		RequireGameplay(s_gameplayApi.CreateEntityDeferred != null,
			"World.CreateEntity");
		byte[] bytes;
		try
		{
			bytes = s_strictUtf8.GetBytes(name);
		}
		catch (EncoderFallbackException error)
		{
			AbortDeferredCommandBatch(context,
				"World.CreateEntity name is not valid Unicode");
			throw new ArgumentException("Name is not valid Unicode.", nameof(name),
				error);
		}
		NativeEntityHandleV1 created = default;
		NativeEntityHandleV1 parentHandle = parent is null ? default : ToNative(parent);
		fixed (byte* pointer = bytes)
		{
			Check(s_gameplayApi.CreateEntityDeferred(ToNative(context),
				new NativeUtf8View(pointer, (ulong)bytes.Length), ToNative(worldPosition),
				parentHandle, &created), "World.CreateEntity");
		}
		if (created.EntityId == 0)
			throw new TomCatException("World.CreateEntity returned an invalid reservation.");
		return FromNative(created);
	}

	internal static Entity? FindEntityByName(Entity context, string name)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(name);
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.FindEntityByName != null,
			"World.Find");
		byte[] bytes = Encoding.UTF8.GetBytes(name);
		NativeEntityHandleV1 found = default;
		fixed (byte* pointer = bytes)
		{
			int status = s_gameplayApi.FindEntityByName(ToNative(context),
				new NativeUtf8View(pointer, (ulong)bytes.Length), &found);
			if (status == NativeNotFound)
				return null;
			Check(status, "World.Find");
		}
		return found.EntityId == 0 ? null : FromNative(found);
	}

	internal static Entity[] QueryEntities(Entity context, int componentType,
		ulong registeredTypeId)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.QueryEntities != null, "World.Query");
		uint required = 0;
		int probeStatus = s_gameplayApi.QueryEntities(ToNative(context), componentType,
			registeredTypeId, null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, "World.Query");
		if (required == 0)
			return [];
		if (required > 1_000_000)
			throw new TomCatException("World.Query returned an invalid entity count.");
		NativeEntityHandleV1[] native = GC.AllocateUninitializedArray<NativeEntityHandleV1>(
			checked((int)required));
		fixed (NativeEntityHandleV1* values = native)
		{
			uint actual = required;
			Check(s_gameplayApi.QueryEntities(ToNative(context), componentType,
				registeredTypeId, values, required, &actual), "World.Query");
			if (actual > required)
				throw new TomCatException("World.Query changed count while being read.");
			Entity[] result = new Entity[actual];
			for (int index = 0; index < result.Length; ++index)
				result[index] = FromNative(native[index]);
			return result;
		}
	}

	internal static Entity? GetParent(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.GetParent != null, "Entity.Parent");
		NativeEntityHandleV1 parent = default;
		Check(s_gameplayApi.GetParent(ToNative(entity), &parent), "Entity.Parent");
		return parent.EntityId == 0 ? null : FromNative(parent);
	}

	internal static void SetParent(Entity entity, Entity? parent)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SetParentDeferred != null, "Entity.Parent");
		Check(s_gameplayApi.SetParentDeferred(ToNative(entity),
			parent is null ? default : ToNative(parent)), "Entity.Parent");
	}

	internal static Entity[] GetChildren(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.GetChildren != null, "Entity.Children");
		uint required = 0;
		int probeStatus = s_gameplayApi.GetChildren(ToNative(entity), null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, "Entity.Children");
		if (required == 0)
			return [];
		if (required > 1_000_000)
			throw new TomCatException("Entity.Children returned an invalid count.");
		NativeEntityHandleV1[] native = GC.AllocateUninitializedArray<NativeEntityHandleV1>(
			checked((int)required));
		fixed (NativeEntityHandleV1* values = native)
		{
			uint actual = required;
			Check(s_gameplayApi.GetChildren(ToNative(entity), values, required, &actual),
				"Entity.Children");
			if (actual > required)
				throw new TomCatException("Entity.Children changed count while being read.");
			Entity[] result = new Entity[actual];
			for (int index = 0; index < result.Length; ++index)
				result[index] = FromNative(native[index]);
			return result;
		}
	}

	internal static bool GetActiveSelf(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.GetActiveSelf != null, "Entity.ActiveSelf");
		return ReadBoolean(s_gameplayApi.GetActiveSelf(ToNative(entity)),
			"Entity.ActiveSelf");
	}

	internal static void SetActiveSelf(Entity entity, bool active)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SetActiveSelf != null, "Entity.ActiveSelf");
		Check(s_gameplayApi.SetActiveSelf(ToNative(entity), active ? 1 : 0),
			"Entity.ActiveSelf");
	}

	internal static bool GetActiveInHierarchy(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.GetActiveInHierarchy != null,
			"Entity.ActiveInHierarchy");
		return ReadBoolean(s_gameplayApi.GetActiveInHierarchy(ToNative(entity)),
			"Entity.ActiveInHierarchy");
	}

	// Host-side lifecycle filtering occurs between user callbacks, so it is on
	// the main thread but intentionally outside ScriptExecutionContext.Enter.
	internal static bool IsActiveForScriptHost(Entity entity)
	{
		if (!Volatile.Read(ref s_gameplayBound)
			|| s_gameplayApi.GetActiveInHierarchy == null)
			return true;
		EnsureMainThread(requireLifecycleContext: false);
		return ReadBoolean(s_gameplayApi.GetActiveInHierarchy(ToNative(entity)),
			"ScriptHost.ActiveInHierarchy");
	}

    internal static bool HasComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.HasComponent != null, "Entity.HasComponent");
        return ReadBoolean(s_api.HasComponent(ToNative(entity), (int)type), "Entity.HasComponent");
    }

    internal static void AddComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.AddComponentDeferred != null, "Entity.AddComponent");
        Check(s_api.AddComponentDeferred(ToNative(entity), (int)type), "Entity.AddComponent");
    }

    internal static void RemoveComponent(Entity entity, NativeComponentTypeV1 type)
    {
        EnsureMainThread();
        Require(s_api.RemoveComponentDeferred != null, "Entity.RemoveComponent");
        Check(s_api.RemoveComponentDeferred(ToNative(entity), (int)type), "Entity.RemoveComponent");
    }

	internal static bool HasRegisteredComponent(Entity entity, ulong typeId)
	{
		EnsureMainThread();
		RequireComponent(s_componentApi.Has != null, "Entity.HasComponent");
		return ReadBoolean(s_componentApi.Has(ToNative(entity), typeId),
			"Entity.HasComponent");
	}

	internal static void AddRegisteredComponent(Entity entity, ulong typeId)
	{
		EnsureMainThread();
		RequireComponent(s_componentApi.Add != null, "Entity.AddComponent");
		Check(s_componentApi.Add(ToNative(entity), typeId), "Entity.AddComponent");
	}

	internal static void RemoveRegisteredComponent(Entity entity, ulong typeId)
	{
		EnsureMainThread();
		RequireComponent(s_componentApi.Remove != null, "Entity.RemoveComponent");
		Check(s_componentApi.Remove(ToNative(entity), typeId), "Entity.RemoveComponent");
	}

	internal static bool IsComponentSchemaAvailable =>
		Volatile.Read(ref s_componentSchemaBound);

	internal static IReadOnlyList<ComponentSchemaInfo> GetComponentSchemas()
	{
		const uint MaximumComponents = 65_536;
		const uint MaximumPropertiesPerComponent = 65_536;
		const uint MaximumTotalProperties = 1_048_576;

		EnsureMainThread(requireLifecycleContext: false);
		RequireComponentSchema(s_componentSchemaApi.GetComponentCount != null,
			"ComponentSchema.GetComponents");
		uint componentCount = 0;
		Check(s_componentSchemaApi.GetComponentCount(&componentCount),
			"ComponentSchema.GetComponents");
		if (componentCount > MaximumComponents)
			throw new TomCatException(
				"ComponentSchema.GetComponents returned too many components.");

		List<ComponentSchemaInfo> components = new((int)componentCount);
		HashSet<ulong> componentIds = [];
		uint totalProperties = 0;
		for (uint componentIndex = 0; componentIndex < componentCount;
			++componentIndex)
		{
			NativeComponentSchemaInfoV1 nativeComponent = default;
			Check(s_componentSchemaApi.GetComponent(componentIndex, &nativeComponent),
				"ComponentSchema.GetComponent");
			if (nativeComponent.TypeId == 0 || nativeComponent.SchemaVersion == 0
				|| !componentIds.Add(nativeComponent.TypeId))
				throw new TomCatException(
					"ComponentSchema.GetComponent returned invalid component identity.");

			uint propertyCount = 0;
			Check(s_componentSchemaApi.GetPropertyCount(nativeComponent.TypeId,
				&propertyCount), "ComponentSchema.GetProperties");
			if (propertyCount != nativeComponent.PropertyCount
				|| propertyCount > MaximumPropertiesPerComponent
				|| totalProperties > MaximumTotalProperties - propertyCount)
				throw new TomCatException(
					"ComponentSchema.GetProperties returned an invalid property count.");
			totalProperties += propertyCount;

			List<ComponentPropertySchemaInfo> properties = new((int)propertyCount);
			HashSet<ulong> propertyIds = [];
			for (uint propertyIndex = 0; propertyIndex < propertyCount;
				++propertyIndex)
			{
				NativeComponentPropertySchemaInfoV1 nativeProperty = default;
				Check(s_componentSchemaApi.GetProperty(nativeComponent.TypeId,
					propertyIndex, &nativeProperty),
					"ComponentSchema.GetProperty");
				if (nativeProperty.ComponentTypeId != nativeComponent.TypeId
					|| nativeProperty.PropertyId == 0
					|| !propertyIds.Add(nativeProperty.PropertyId)
					|| !Enum.IsDefined(nativeProperty.Kind))
					throw new TomCatException(
						"ComponentSchema.GetProperty returned invalid metadata.");
				properties.Add(new ComponentPropertySchemaInfo(
					nativeProperty.PropertyId,
					(ComponentPropertyKind)nativeProperty.Kind,
					(ComponentPropertySchemaFlags)nativeProperty.Flags,
					CopyUtf8(nativeProperty.StableName,
						"ComponentSchema.Property.StableName"),
					CopyUtf8(nativeProperty.DisplayName,
						"ComponentSchema.Property.DisplayName")));
			}

			components.Add(new ComponentSchemaInfo(nativeComponent.TypeId,
				nativeComponent.ProviderId, nativeComponent.SchemaVersion,
				(ComponentSchemaFlags)nativeComponent.Flags,
				CopyUtf8(nativeComponent.StableName,
					"ComponentSchema.Component.StableName"),
				CopyUtf8(nativeComponent.DisplayName,
					"ComponentSchema.Component.DisplayName"),
				properties.AsReadOnly()));
		}
		return components.AsReadOnly();
	}

	internal static int GetRegisteredInt32(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Int32
			|| value.Integer < int.MinValue || value.Integer > int.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (int)value.Integer;
	}

	internal static void SetRegisteredInt32(Entity entity, ulong typeId,
		ulong propertyId, int value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Int32,
			Integer = value
		}, operation);

	internal static long GetRegisteredInt64(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Int64)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return value.Integer;
	}

	internal static void SetRegisteredInt64(Entity entity, ulong typeId,
		ulong propertyId, long value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Int64,
			Integer = value
		}, operation);

	internal static uint GetRegisteredUInt32(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.UInt32
			|| value.Integer < uint.MinValue || value.Integer > uint.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (uint)value.Integer;
	}

	internal static void SetRegisteredUInt32(Entity entity, ulong typeId,
		ulong propertyId, uint value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.UInt32,
			Integer = value
		}, operation);

	internal static bool GetRegisteredBool(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Bool
			|| (value.Integer != 0 && value.Integer != 1))
			throw new TomCatException($"{operation} returned an incompatible value.");
		return value.Integer != 0;
	}

	internal static void SetRegisteredBool(Entity entity, ulong typeId,
		ulong propertyId, bool value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Bool,
			Integer = value ? 1 : 0
		}, operation);

	internal static ulong GetRegisteredUInt64(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.UInt64)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return unchecked((ulong)value.Integer);
	}

	internal static void SetRegisteredUInt64(Entity entity, ulong typeId,
		ulong propertyId, ulong value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.UInt64,
			Integer = unchecked((long)value)
		}, operation);

	internal static float GetRegisteredFloat(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Float || !double.IsFinite(value.Number)
			|| value.Number < -float.MaxValue || value.Number > float.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (float)value.Number;
	}

	internal static void SetRegisteredFloat(Entity entity, ulong typeId,
		ulong propertyId, float value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Float,
			Number = value
		}, operation);

	internal static double GetRegisteredDouble(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Double
			|| !double.IsFinite(value.Number))
			throw new TomCatException($"{operation} returned an incompatible value.");
		return value.Number;
	}

	internal static void SetRegisteredDouble(Entity entity, ulong typeId,
		ulong propertyId, double value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Double,
			Number = value
		}, operation);

	internal static Vector2 GetRegisteredVector2(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Vector2)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return new(value.X, value.Y);
	}

	internal static void SetRegisteredVector2(Entity entity, ulong typeId,
		ulong propertyId, Vector2 value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Vector2,
			X = value.X,
			Y = value.Y
		}, operation);

	internal static Vector3 GetRegisteredVector3(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Vector3)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return new(value.X, value.Y, value.Z);
	}

	internal static void SetRegisteredVector3(Entity entity, ulong typeId,
		ulong propertyId, Vector3 value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Vector3,
			X = value.X,
			Y = value.Y,
			Z = value.Z
		}, operation);

	internal static Vector4 GetRegisteredVector4(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		NativePropertyValueV1 value = GetRegisteredProperty(entity, typeId,
			propertyId, operation);
		if (value.Kind != NativePropertyKindV1.Vector4)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return new(value.X, value.Y, value.Z, value.W);
	}

	internal static void SetRegisteredVector4(Entity entity, ulong typeId,
		ulong propertyId, Vector4 value, string operation) => SetRegisteredProperty(entity,
		typeId, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Vector4,
			X = value.X,
			Y = value.Y,
			Z = value.Z,
			W = value.W
		}, operation);

	internal static Color GetRegisteredColor(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		Vector4 value = GetRegisteredVector4(entity, typeId, propertyId, operation);
		return new(value.X, value.Y, value.Z, value.W);
	}

	internal static void SetRegisteredColor(Entity entity, ulong typeId,
		ulong propertyId, Color value, string operation) => SetRegisteredVector4(entity,
		typeId, propertyId, new(value.R, value.G, value.B, value.A), operation);

	internal static bool IsComponentStringAvailable =>
		Volatile.Read(ref s_componentStringBound);

	internal static string GetRegisteredString(Entity entity, ulong typeId,
		ulong propertyId, string operation)
	{
		const uint MaximumBytes = RegisteredComponentProperties.MaximumStringUtf8Bytes;
		EnsureMainThread();
		RequireComponentString(s_componentStringApi.GetProperty != null, operation);
		uint required = 0;
		int status = s_componentStringApi.GetProperty(ToNative(entity), typeId,
			propertyId, null, 0, &required);
		if (status != 0 && status != NativeBufferTooSmall)
			Check(status, operation);
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			if (required == 0)
				return string.Empty;
			if (required > MaximumBytes)
				throw new TomCatException(
					$"{operation} returned an invalid UTF-8 length.");
			byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
			fixed (byte* buffer = bytes)
			{
				uint actual = required;
				status = s_componentStringApi.GetProperty(ToNative(entity), typeId,
					propertyId, buffer, required, &actual);
				if (status == NativeBufferTooSmall && actual > required)
				{
					required = actual;
					continue;
				}
				Check(status, operation);
				if (actual > required)
					throw new TomCatException(
						$"{operation} returned an invalid UTF-8 length.");
				try
				{
					return s_strictUtf8.GetString(bytes, 0, (int)actual);
				}
				catch (DecoderFallbackException error)
				{
					throw new TomCatException(
						$"{operation} returned malformed UTF-8: {error.Message}");
				}
			}
		}
		throw new TomCatException($"{operation} changed length while being read.");
	}

	internal static void SetRegisteredString(Entity entity, ulong typeId,
		ulong propertyId, string value, string operation)
	{
		const int MaximumBytes = RegisteredComponentProperties.MaximumStringUtf8Bytes;
		EnsureMainThread();
		if (value is null)
		{
			AbortDeferredCommandBatch(entity, $"{operation} value cannot be null");
			throw new ArgumentNullException(nameof(value));
		}
		RequireComponentString(s_componentStringApi.SetProperty != null, operation);
		byte[] bytes;
		try
		{
			bytes = s_strictUtf8.GetBytes(value);
		}
		catch (EncoderFallbackException error)
		{
			AbortDeferredCommandBatch(entity,
				$"{operation} value is not valid Unicode");
			throw new ArgumentException("Value is not valid Unicode.", nameof(value),
				error);
		}
		if (bytes.Length > MaximumBytes)
		{
			AbortDeferredCommandBatch(entity,
				$"{operation} value exceeds the UTF-8 size limit");
			throw new ArgumentOutOfRangeException(nameof(value),
				$"Registered component strings may contain at most {MaximumBytes} UTF-8 bytes.");
		}
		fixed (byte* pointer = bytes)
			Check(s_componentStringApi.SetProperty(ToNative(entity), typeId,
				propertyId, new NativeUtf8View(pointer, (ulong)bytes.Length)), operation);
	}

	internal static string GetRuntimeUIText(Entity entity, ulong typeId,
		string operation)
	{
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.GetText != null, operation);
		uint required = 0;
		int probe = s_runtimeUIApi.GetText(ToNative(entity), typeId, null, 0,
			&required);
		if (probe != 0 && probe != NativeBufferTooSmall)
			Check(probe, operation);
		if (required == 0)
			return string.Empty;
		if (required > 65536)
			throw new TomCatException($"{operation} returned an invalid UTF-8 length.");
		byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
		fixed (byte* buffer = bytes)
		{
			uint actual = required;
			Check(s_runtimeUIApi.GetText(ToNative(entity), typeId, buffer,
				required, &actual), operation);
			if (actual > required)
				throw new TomCatException($"{operation} changed length while being read.");
			return s_strictUtf8.GetString(bytes, 0, (int)actual);
		}
	}

	internal static void SetRuntimeUIText(Entity entity, ulong typeId,
		string value, string operation)
	{
		ArgumentNullException.ThrowIfNull(value);
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.SetText != null, operation);
		byte[] bytes = s_strictUtf8.GetBytes(value);
		if (bytes.Length > 65536)
			throw new ArgumentOutOfRangeException(nameof(value),
				"Runtime UI text may contain at most 65536 UTF-8 bytes.");
		fixed (byte* pointer = bytes)
			Check(s_runtimeUIApi.SetText(ToNative(entity), typeId,
				new NativeUtf8View(pointer, (ulong)bytes.Length)), operation);
	}

	internal static bool RuntimeUIButtonWasClicked(Entity entity)
	{
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.WasButtonClicked != null,
			"UIButton.WasClickedThisFrame");
		return ReadBoolean(s_runtimeUIApi.WasButtonClicked(ToNative(entity)),
			"UIButton.WasClickedThisFrame");
	}

	internal static ulong GetRuntimeUIButtonClickSerial(Entity entity)
	{
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.GetButtonClickSerial != null,
			"UIButton.ClickSerial");
		ulong value = 0;
		Check(s_runtimeUIApi.GetButtonClickSerial(ToNative(entity), &value),
			"UIButton.ClickSerial");
		return value;
	}

	internal static void FocusRuntimeUIButton(Entity entity)
	{
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.FocusButton != null, "UIButton.Focus");
		Check(s_runtimeUIApi.FocusButton(ToNative(entity)), "UIButton.Focus");
	}

	internal static Vector4 GetRuntimeUIRect(Entity entity)
	{
		EnsureMainThread();
		RequireRuntimeUI(s_runtimeUIApi.GetRect != null, "RectTransform.RuntimeRect");
		NativeVector4 value = default;
		Check(s_runtimeUIApi.GetRect(ToNative(entity), &value),
			"RectTransform.RuntimeRect");
		return new(value.X, value.Y, value.Z, value.W);
	}

	internal static bool IsRuntimeUIInputCaptured()
	{
		if (!Volatile.Read(ref s_runtimeUIBound)
			|| s_runtimeUIApi.IsGameplayInputCaptured == null)
			return false;
		EnsureMainThread();
		return ReadBoolean(s_runtimeUIApi.IsGameplayInputCaptured(),
			"UIEventSystem.IsGameplayInputCaptured");
	}

	private static NativePropertyValueV1 GetRegisteredProperty(Entity entity,
		ulong typeId, ulong propertyId, string operation)
	{
		EnsureMainThread();
		RequireComponent(s_componentApi.GetProperty != null, operation);
		NativePropertyValueV1 value = default;
		Check(s_componentApi.GetProperty(ToNative(entity), typeId, propertyId,
			&value), operation);
		return value;
	}

	private static void SetRegisteredProperty(Entity entity, ulong typeId,
		ulong propertyId, NativePropertyValueV1 value, string operation)
	{
		EnsureMainThread();
		RequireComponent(s_componentApi.SetProperty != null, operation);
		Check(s_componentApi.SetProperty(ToNative(entity), typeId, propertyId,
			value), operation);
	}

	internal static bool GetGameplayBool(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.Bool
			|| value.Integer is not (0 or 1))
			throw new TomCatException($"{operation} returned an incompatible value.");
		return value.Integer != 0;
	}

	internal static bool SpriteAnimatorPlay(Entity entity, string clip, bool restart)
	{
		ArgumentException.ThrowIfNullOrEmpty(clip);
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorPlay != null,
			"SpriteAnimator.Play");
		byte[] bytes = s_strictUtf8.GetBytes(clip);
		fixed (byte* pointer = bytes)
		{
			return ReadBoolean(s_gameplayApi.SpriteAnimatorPlay(ToNative(entity),
				new NativeUtf8View(pointer, (ulong)bytes.Length), restart ? 1 : 0),
				"SpriteAnimator.Play");
		}
	}

	internal static void SpriteAnimatorStop(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorStop != null,
			"SpriteAnimator.Stop");
		Check(s_gameplayApi.SpriteAnimatorStop(ToNative(entity)),
			"SpriteAnimator.Stop");
	}

	internal static void SpriteAnimatorSetBool(Entity entity, string parameter, bool value)
	{
		ArgumentException.ThrowIfNullOrEmpty(parameter); EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorSetBool != null,
			"SpriteAnimator.SetBool");
		byte[] bytes = s_strictUtf8.GetBytes(parameter);
		fixed (byte* pointer = bytes)
			Check(s_gameplayApi.SpriteAnimatorSetBool(ToNative(entity),
				new NativeUtf8View(pointer, (ulong)bytes.Length), value ? 1 : 0),
				"SpriteAnimator.SetBool");
	}

	internal static void SpriteAnimatorSetInt(Entity entity, string parameter, int value)
	{
		ArgumentException.ThrowIfNullOrEmpty(parameter); EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorSetInt != null,
			"SpriteAnimator.SetInt");
		byte[] bytes = s_strictUtf8.GetBytes(parameter);
		fixed (byte* pointer = bytes)
			Check(s_gameplayApi.SpriteAnimatorSetInt(ToNative(entity),
				new NativeUtf8View(pointer, (ulong)bytes.Length), value),
				"SpriteAnimator.SetInt");
	}

	internal static void SpriteAnimatorSetFloat(Entity entity, string parameter, float value)
	{
		ArgumentException.ThrowIfNullOrEmpty(parameter);
		if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorSetFloat != null,
			"SpriteAnimator.SetFloat");
		byte[] bytes = s_strictUtf8.GetBytes(parameter);
		fixed (byte* pointer = bytes)
			Check(s_gameplayApi.SpriteAnimatorSetFloat(ToNative(entity),
				new NativeUtf8View(pointer, (ulong)bytes.Length), value),
				"SpriteAnimator.SetFloat");
	}

	internal static void SpriteAnimatorSetTrigger(Entity entity, string parameter,
		bool reset)
	{
		ArgumentException.ThrowIfNullOrEmpty(parameter); EnsureMainThread();
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeUtf8View, int> callback =
			reset ? s_gameplayApi.SpriteAnimatorResetTrigger
			: s_gameplayApi.SpriteAnimatorSetTrigger;
		RequireGameplay(callback != null, reset
			? "SpriteAnimator.ResetTrigger" : "SpriteAnimator.SetTrigger");
		byte[] bytes = s_strictUtf8.GetBytes(parameter);
		fixed (byte* pointer = bytes)
			Check(callback(ToNative(entity),
				new NativeUtf8View(pointer, (ulong)bytes.Length)), reset
				? "SpriteAnimator.ResetTrigger" : "SpriteAnimator.SetTrigger");
	}

	internal static string SpriteAnimatorGetCurrentState(Entity entity)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SpriteAnimatorGetCurrentState != null,
			"SpriteAnimator.CurrentState");
		uint required = 0;
		int probe = s_gameplayApi.SpriteAnimatorGetCurrentState(
			ToNative(entity), null, 0, &required);
		if (probe != 0 && probe != NativeBufferTooSmall)
			Check(probe, "SpriteAnimator.CurrentState");
		if (required == 0) return string.Empty;
		if (required > 1024 * 1024)
			throw new TomCatException("SpriteAnimator.CurrentState returned an invalid length.");
		byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
		fixed (byte* buffer = bytes)
		{
			uint actual = required;
			Check(s_gameplayApi.SpriteAnimatorGetCurrentState(ToNative(entity), buffer,
				required, &actual), "SpriteAnimator.CurrentState");
			if (actual > required)
				throw new TomCatException("SpriteAnimator.CurrentState changed length while being read.");
			return s_strictUtf8.GetString(bytes, 0, (int)actual);
		}
	}

	internal static int GetGameplayInt32(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.Int32
			|| value.Integer < int.MinValue || value.Integer > int.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (int)value.Integer;
	}

	internal static uint GetGameplayUInt32(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.UInt32
			|| value.Integer < 0 || (ulong)value.Integer > uint.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (uint)value.Integer;
	}

	internal static ulong GetGameplayUInt64(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.UInt64)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return unchecked((ulong)value.Integer);
	}

	internal static float GetGameplayFloat(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.Float || !double.IsFinite(value.Number)
			|| value.Number < -float.MaxValue || value.Number > float.MaxValue)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return (float)value.Number;
	}

	internal static Vector2 GetGameplayVector2(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.Vector2)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return new Vector2(value.X, value.Y);
	}

	internal static Color GetGameplayColor(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, string operation)
	{
		NativePropertyValueV1 value = GetGameplayProperty(entity, type, propertyId,
			operation);
		if (value.Kind != NativePropertyKindV1.Vector4)
			throw new TomCatException($"{operation} returned an incompatible value.");
		return new Color(value.X, value.Y, value.Z, value.W);
	}

	internal static void SetGameplayBool(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, bool value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Bool,
			Integer = value ? 1 : 0
		}, operation);

	internal static void SetGameplayInt32(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, int value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Int32,
			Integer = value
		}, operation);

	internal static void SetGameplayUInt32(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, uint value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.UInt32,
			Integer = value
		}, operation);

	internal static void SetGameplayUInt64(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, ulong value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.UInt64,
			Integer = unchecked((long)value)
		}, operation);

	internal static void SetGameplayFloat(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, float value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Float,
			Number = value
		}, operation);

	internal static void SetGameplayVector2(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, Vector2 value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Vector2,
			X = value.X,
			Y = value.Y
		}, operation);

	internal static void SetGameplayColor(Entity entity, NativeComponentTypeV1 type,
		uint propertyId, Color value, string operation) => SetGameplayProperty(entity,
		type, propertyId, new NativePropertyValueV1
		{
			Kind = NativePropertyKindV1.Vector4,
			X = value.R,
			Y = value.G,
			Z = value.B,
			W = value.A
		}, operation);

	private static NativePropertyValueV1 GetGameplayProperty(Entity entity,
		NativeComponentTypeV1 type, uint propertyId, string operation)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.GetComponentProperty != null, operation);
		NativePropertyValueV1 value = default;
		Check(s_gameplayApi.GetComponentProperty(ToNative(entity), (int)type,
			propertyId, &value), operation);
		return value;
	}

	private static void SetGameplayProperty(Entity entity,
		NativeComponentTypeV1 type, uint propertyId, NativePropertyValueV1 value,
		string operation)
	{
		EnsureMainThread();
		RequireGameplay(s_gameplayApi.SetComponentProperty != null, operation);
		Check(s_gameplayApi.SetComponentProperty(ToNative(entity), (int)type,
			propertyId, value), operation);
	}

    internal static Vector3 GetTransformVector(Entity entity,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        NativeVector3 value = default;
        Check(callback(ToNative(entity), &value), operation);
        return new(value.X, value.Y, value.Z);
    }

    internal static void SetTransformVector(Entity entity, Vector3 value,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        Check(callback(ToNative(entity), new NativeVector3 { X = value.X, Y = value.Y, Z = value.Z }), operation);
    }

    internal static Matrix4 GetWorldMatrix(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.TransformGetWorldMatrix != null, "Transform.WorldMatrix");
        NativeMatrix4 value = default;
        Check(s_api.TransformGetWorldMatrix(ToNative(entity), &value), "Transform.WorldMatrix");
        return new Matrix4
        {
            M11 = value.M11, M12 = value.M12, M13 = value.M13, M14 = value.M14,
            M21 = value.M21, M22 = value.M22, M23 = value.M23, M24 = value.M24,
            M31 = value.M31, M32 = value.M32, M33 = value.M33, M34 = value.M34,
            M41 = value.M41, M42 = value.M42, M43 = value.M43, M44 = value.M44
        };
    }

    internal static Vector2 GetRigidbodyVelocity(Entity entity)
    {
        EnsureMainThread();
        Require(s_api.RigidbodyGetLinearVelocity != null, "Rigidbody2D.LinearVelocity");
        NativeVector2 value = default;
        Check(s_api.RigidbodyGetLinearVelocity(ToNative(entity), &value), "Rigidbody2D.LinearVelocity");
        return new(value.X, value.Y);
    }

    internal static void SetRigidbodyVector(Entity entity, Vector2 value,
        delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        Check(callback(ToNative(entity), new NativeVector2 { X = value.X, Y = value.Y }), operation);
    }

    internal static bool InputBoolean(KeyCode key,
        delegate* unmanaged[Cdecl]<uint, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        return ReadBoolean(callback((uint)key), operation);
    }

    internal static Vector2 InputVector(delegate* unmanaged[Cdecl]<NativeVector2*, int> callback, string operation)
    {
        EnsureMainThread();
        Require(callback != null, operation);
        NativeVector2 value = default;
        Check(callback(&value), operation);
        return new(value.X, value.Y);
    }

    internal static KeyModifiers GetModifiers()
    {
        EnsureMainThread();
        Require(s_api.InputGetModifiers != null, "Input.Modifiers");
        uint value = 0;
        Check(s_api.InputGetModifiers(&value), "Input.Modifiers");
        return (KeyModifiers)value;
    }

	internal static bool InputMouseBoolean(uint button,
		delegate* unmanaged[Cdecl]<uint, int> callback, string operation)
	{
		EnsureMainThread();
		RequireInput(callback != null, operation);
		return ReadBoolean(callback(button), operation);
	}

	internal static Vector2 GetScrollDelta()
	{
		EnsureMainThread();
		RequireInput(s_inputApi.GetScrollDelta != null, "Input.ScrollDelta");
		NativeVector2 value = default;
		Check(s_inputApi.GetScrollDelta(&value), "Input.ScrollDelta");
		return new(value.X, value.Y);
	}

	internal static bool GetWindowFocused()
	{
		EnsureMainThread();
		RequireInput(s_inputApi.IsWindowFocused != null, "Input.IsWindowFocused");
		return ReadBoolean(s_inputApi.IsWindowFocused(), "Input.IsWindowFocused");
	}

	internal static InputEventBatch GetInputEventBatch()
	{
		EnsureMainThread();
		RequireInputEvents(s_inputEventsApi.GetBatchInfo != null
			&& s_inputEventsApi.CopyEvents != null, "Input.EventBatch");
		NativeInputEventBatchInfoV1 info = default;
		Check(s_inputEventsApi.GetBatchInfo(&info), "Input.EventBatch.GetBatchInfo");
		if (info.EventCount > 16_384)
			throw new TomCatException("Input event batch exceeds the native safety limit.");

		NativeInputEventV1[] nativeEvents = new NativeInputEventV1[info.EventCount];
		uint required = 0;
		fixed (NativeInputEventV1* pointer = nativeEvents)
		{
			Check(s_inputEventsApi.CopyEvents(pointer, (uint)nativeEvents.Length,
				&required), "Input.EventBatch.CopyEvents");
		}
		if (required != info.EventCount)
			throw new TomCatException("Input event batch changed while it was being read.");

		InputEvent[] events = new InputEvent[nativeEvents.Length];
		ulong previousSequence = 0;
		for (int index = 0; index < nativeEvents.Length; ++index)
		{
			NativeInputEventV1 value = nativeEvents[index];
			if (!double.IsFinite(value.TimestampSeconds)
				|| value.Sequence == 0
				|| (index != 0 && value.Sequence <= previousSequence)
				|| value.FrameNumber < info.FirstFrameNumber
				|| value.FrameNumber > info.LastFrameNumber)
				throw new TomCatException("Native input event ordering metadata is invalid.");
			InputEventDevice device = value.Device switch
			{
				NativeInputDeviceV1.Keyboard => InputEventDevice.Keyboard,
				NativeInputDeviceV1.MouseButton => InputEventDevice.MouseButton,
				NativeInputDeviceV1.GamepadConnection => InputEventDevice.GamepadConnection,
				NativeInputDeviceV1.GamepadButton => InputEventDevice.GamepadButton,
				_ => throw new TomCatException("Native input event device is invalid.")
			};
			InputEventAction action = value.Action switch
			{
				NativeInputActionV1.Pressed => InputEventAction.Pressed,
				NativeInputActionV1.Released => InputEventAction.Released,
				NativeInputActionV1.Repeated => InputEventAction.Repeated,
				_ => throw new TomCatException("Native input event action is invalid.")
			};
			events[index] = new InputEvent(value.Sequence, value.TimestampSeconds,
				value.FrameNumber, device, action, value.Code, value.DeviceIndex);
			previousSequence = value.Sequence;
		}
		if (events.Length != 0 && (events[0].Sequence != info.FirstSequence
			|| events[^1].Sequence != info.LastSequence))
			throw new TomCatException("Native input event batch sequence range is invalid.");
		return new InputEventBatch(info.FirstFrameNumber, info.LastFrameNumber,
			info.FirstSequence, info.LastSequence, info.DroppedEventCount, events);
	}

	internal static string? GetApplicationDirectory(
		delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireApplicationPaths(callback != null, operation);
		uint required = 0;
		int probe = callback(null, 0, &required);
		if (probe == NativeNotFound)
			return null;
		if (probe != 0 && probe != NativeBufferTooSmall)
			Check(probe, operation);
		if (required > 1024 * 1024)
			throw new TomCatException($"{operation} returned an invalid length.");
		if (required == 0)
			return string.Empty;
		byte[] bytes = new byte[required];
		fixed (byte* pointer = bytes)
		{
			uint actual = 0;
			Check(callback(pointer, required, &actual), operation);
			if (actual != required)
				throw new TomCatException($"{operation} changed length while being read.");
		}
		try
		{
			return s_strictUtf8.GetString(bytes);
		}
		catch (DecoderFallbackException error)
		{
			throw new TomCatException($"{operation} returned invalid UTF-8: {error.Message}");
		}
	}

	internal static bool InputGamepadBoolean(uint gamepad,
		delegate* unmanaged[Cdecl]<uint, int> callback, string operation)
	{
		EnsureMainThread();
		RequireInput(callback != null, operation);
		return ReadBoolean(callback(gamepad), operation);
	}

	internal static bool InputGamepadButtonBoolean(uint gamepad, uint button,
		delegate* unmanaged[Cdecl]<uint, uint, int> callback, string operation)
	{
		EnsureMainThread();
		RequireInput(callback != null, operation);
		return ReadBoolean(callback(gamepad, button), operation);
	}

	internal static float GetGamepadAxis(uint gamepad, uint axis)
	{
		EnsureMainThread();
		RequireInput(s_inputApi.GetGamepadAxis != null, "Input.GetGamepadAxis");
		float value = 0.0f;
		Check(s_inputApi.GetGamepadAxis(gamepad, axis, &value),
			"Input.GetGamepadAxis");
		return value;
	}

	internal static bool AudioHasSource(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.HasSource != null, "Entity.HasComponent<AudioSource>");
		return ReadBoolean(s_audioApi.HasSource(ToNative(entity)),
			"Entity.HasComponent<AudioSource>");
	}

	internal static void AudioAddSource(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.AddSource != null, "Entity.AddComponent<AudioSource>");
		Check(s_audioApi.AddSource(ToNative(entity)), "Entity.AddComponent<AudioSource>");
	}

	internal static void AudioRemoveSource(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.RemoveSource != null, "Entity.RemoveComponent<AudioSource>");
		Check(s_audioApi.RemoveSource(ToNative(entity)), "Entity.RemoveComponent<AudioSource>");
	}

	internal static bool AudioHasListener(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.HasListener != null, "Entity.HasComponent<AudioListener>");
		return ReadBoolean(s_audioApi.HasListener(ToNative(entity)),
			"Entity.HasComponent<AudioListener>");
	}

	internal static void AudioAddListener(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.AddListener != null, "Entity.AddComponent<AudioListener>");
		Check(s_audioApi.AddListener(ToNative(entity)), "Entity.AddComponent<AudioListener>");
	}

	internal static void AudioRemoveListener(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.RemoveListener != null,
			"Entity.RemoveComponent<AudioListener>");
		Check(s_audioApi.RemoveListener(ToNative(entity)),
			"Entity.RemoveComponent<AudioListener>");
	}

	internal static ulong AudioGetClip(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.GetClip != null, "AudioSource.Clip");
		ulong value = 0;
		Check(s_audioApi.GetClip(ToNative(entity), &value), "AudioSource.Clip");
		return value;
	}

	internal static void AudioSetClip(Entity entity, ulong value)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.SetClip != null, "AudioSource.Clip");
		Check(s_audioApi.SetClip(ToNative(entity), value), "AudioSource.Clip");
	}

	internal static bool AudioGetSourceBool(Entity entity,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudio(callback != null, operation);
		return ReadBoolean(callback(ToNative(entity)), operation);
	}

	internal static void AudioSetSourceBool(Entity entity, bool value,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudio(callback != null, operation);
		Check(callback(ToNative(entity), value ? 1 : 0), operation);
	}

	internal static float AudioGetSourceFloat(Entity entity,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudio(callback != null, operation);
		float value = 0.0f;
		Check(callback(ToNative(entity), &value), operation);
		return value;
	}

	internal static void AudioSetSourceFloat(Entity entity, float value,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudio(callback != null, operation);
		Check(callback(ToNative(entity), value), operation);
	}

	internal static bool AudioGetSpatialBool(Entity entity,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudioSpatial(callback != null, operation);
		return ReadBoolean(callback(ToNative(entity)), operation);
	}

	internal static void AudioSetSpatialBool(Entity entity, bool value,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudioSpatial(callback != null, operation);
		Check(callback(ToNative(entity), value ? 1 : 0), operation);
	}

	internal static float AudioGetSpatialFloat(Entity entity,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudioSpatial(callback != null, operation);
		float value = 0.0f;
		Check(callback(ToNative(entity), &value), operation);
		return value;
	}

	internal static void AudioSetSpatialFloat(Entity entity, float value,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudioSpatial(callback != null, operation);
		Check(callback(ToNative(entity), value), operation);
	}

	internal static AudioMixerGroup AudioGetMixerGroup(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.GetMixerGroup != null, "AudioSource.MixerGroup");
		int value = 0;
		Check(s_audioApi.GetMixerGroup(ToNative(entity), &value),
			"AudioSource.MixerGroup");
		if (value is < 0 or > 2)
			throw new TomCatException("AudioSource.MixerGroup returned an invalid value.");
		return (AudioMixerGroup)value;
	}

	internal static void AudioSetMixerGroup(Entity entity, AudioMixerGroup value)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.SetMixerGroup != null, "AudioSource.MixerGroup");
		Check(s_audioApi.SetMixerGroup(ToNative(entity), (int)value),
			"AudioSource.MixerGroup");
	}

	internal static void AudioSourceCommand(Entity entity,
		delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> callback,
		string operation)
	{
		EnsureMainThread();
		RequireAudio(callback != null, operation);
		Check(callback(ToNative(entity)), operation);
	}

	internal static AudioPlaybackState AudioGetPlaybackState(Entity entity)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.GetPlaybackState != null, "AudioSource.State");
		int value = 0;
		Check(s_audioApi.GetPlaybackState(ToNative(entity), &value),
			"AudioSource.State");
		if (value is < 0 or > 2)
			throw new TomCatException("AudioSource.State returned an invalid value.");
		return (AudioPlaybackState)value;
	}

	internal static bool AudioHardwareAvailable()
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.IsHardwareAvailable != null,
			"AudioSystem.IsHardwareAvailable");
		return ReadBoolean(s_audioApi.IsHardwareAvailable(),
			"AudioSystem.IsHardwareAvailable");
	}

	internal static string AudioBackendName()
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.GetBackendName != null, "AudioSystem.BackendName");
		uint required = 0;
		int probe = s_audioApi.GetBackendName(null, 0, &required);
		if (probe != 0 && probe != NativeBufferTooSmall)
			Check(probe, "AudioSystem.BackendName");
		if (required == 0) return string.Empty;
		if (required > 1024 * 1024)
			throw new TomCatException("AudioSystem.BackendName returned an invalid length.");
		byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
		fixed (byte* buffer = bytes)
		{
			uint actual = required;
			Check(s_audioApi.GetBackendName(buffer, required, &actual),
				"AudioSystem.BackendName");
			return Encoding.UTF8.GetString(bytes, 0, (int)actual);
		}
	}

	internal static float AudioGetMixerVolume(AudioMixerGroup group)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.GetMixerVolume != null, "AudioSystem.GetMixerVolume");
		float value = 0.0f;
		Check(s_audioApi.GetMixerVolume((int)group, &value),
			"AudioSystem.GetMixerVolume");
		return value;
	}

	internal static void AudioSetMixerVolume(AudioMixerGroup group, float value)
	{
		EnsureMainThread();
		RequireAudio(s_audioApi.SetMixerVolume != null, "AudioSystem.SetMixerVolume");
		Check(s_audioApi.SetMixerVolume((int)group, value),
			"AudioSystem.SetMixerVolume");
	}

	internal static string GetGamepadName(uint gamepad)
	{
		EnsureMainThread();
		RequireInput(s_inputApi.GetGamepadName != null, "Input.GetGamepadName");
		uint required = 0;
		int probe = s_inputApi.GetGamepadName(gamepad, null, 0, &required);
		if (probe != 0 && probe != NativeBufferTooSmall)
			Check(probe, "Input.GetGamepadName");
		if (required == 0)
			return string.Empty;
		if (required > 1024 * 1024)
			throw new TomCatException("Input.GetGamepadName returned an invalid length.");
		byte[] bytes = GC.AllocateUninitializedArray<byte>((int)required);
		fixed (byte* buffer = bytes)
		{
			uint actual = required;
			Check(s_inputApi.GetGamepadName(gamepad, buffer, required, &actual),
				"Input.GetGamepadName");
			if (actual > required)
				throw new TomCatException("Input.GetGamepadName changed length while being read.");
			return Encoding.UTF8.GetString(bytes, 0, (int)actual);
		}
	}

    internal static RaycastHit2D? Raycast(Entity context, Vector2 start, Vector2 end,
        uint layerMask, bool includeTriggers)
    {
        EnsureMainThread();
        Require(s_api.PhysicsRaycast != null, "Physics2D.Raycast");
        NativeRaycastHit2D hit = default;
        int status = s_api.PhysicsRaycast(ToNative(context), ToNative(start), ToNative(end),
            layerMask, includeTriggers ? 1 : 0, &hit);
		if (status == 0)
			return new RaycastHit2D(FromNative(hit.Entity), new(hit.Point.X, hit.Point.Y),
				new(hit.Normal.X, hit.Normal.Y), hit.Fraction, hit.IsTrigger != 0, hit.CollisionLayer);
		if (status == NativeNotFound)
			return null;
        throw new TomCatException($"Physics2D.Raycast failed with status {status}.");
    }

    internal static PhysicsQueryHit2D[] QueryAabb(Entity context, Vector2 minimum, Vector2 maximum,
        uint layerMask, bool includeTriggers)
    {
        EnsureMainThread();
		Require(s_api.PhysicsQueryAabb != null, "Physics2D.QueryAABB");
		uint required = 0;
		int probeStatus = s_api.PhysicsQueryAabb(ToNative(context), ToNative(minimum), ToNative(maximum),
			layerMask, includeTriggers ? 1 : 0, null, 0, &required);
		if (probeStatus != 0 && probeStatus != NativeBufferTooSmall)
			Check(probeStatus, "Physics2D.QueryAABB");
        if (required == 0)
            return [];
        if (required > 1_000_000)
            throw new TomCatException("Physics2D.QueryAABB returned an invalid result count.");

        NativePhysicsQueryHit2D[] native = GC.AllocateUninitializedArray<NativePhysicsQueryHit2D>((int)required);
        fixed (NativePhysicsQueryHit2D* values = native)
        {
            uint actual = required;
            Check(s_api.PhysicsQueryAabb(ToNative(context), ToNative(minimum), ToNative(maximum),
                layerMask, includeTriggers ? 1 : 0, values, required, &actual), "Physics2D.QueryAABB");
            if (actual > required)
                throw new TomCatException("Physics2D.QueryAABB changed count while being read.");
            var result = new PhysicsQueryHit2D[actual];
            for (int index = 0; index < result.Length; ++index)
                result[index] = new(FromNative(native[index].Entity), native[index].IsTrigger != 0,
                    native[index].CollisionLayer);
            return result;
        }
    }

    internal static bool AssetIsValid(ulong handle)
    {
        EnsureMainThread();
        Require(s_api.AssetIsValid != null, "AssetRef.IsValid");
        return ReadBoolean(s_api.AssetIsValid(handle), "AssetRef.IsValid");
    }

    internal static AssetType GetAssetType(ulong handle)
    {
        EnsureMainThread();
        Require(s_api.AssetGetType != null, "AssetRef.Type");
        int type = 0;
        Check(s_api.AssetGetType(handle, &type), "AssetRef.Type");
        return (AssetType)type;
    }

    internal static bool GetBehaviourEnabled(ScriptInstanceHandle instance)
    {
        EnsureMainThread();
        Require(s_api.BehaviourGetEnabled != null, "TomCatBehaviour.Enabled");
        return ReadBoolean(s_api.BehaviourGetEnabled(instance.Value), "TomCatBehaviour.Enabled");
    }

	internal static void SetBehaviourEnabled(ScriptInstanceHandle instance, bool enabled)
    {
        EnsureMainThread();
        Require(s_api.BehaviourSetEnabledDeferred != null, "TomCatBehaviour.Enabled");
		int status = s_api.BehaviourSetEnabledDeferred(instance.Value,
			enabled ? 1 : 0);
		if (status != 0 && ScriptExecutionContext.IsActive)
			AbortDeferredCommandBatch(ScriptExecutionContext.CurrentEntity,
				"TomCatBehaviour.Enabled mutation was rejected");
		Check(status, "TomCatBehaviour.Enabled");
	}

	internal static void RemoveBehaviour(ScriptInstanceHandle instance)
	{
		EnsureMainThread();
		Require(s_api.BehaviourRemoveDeferred != null, "TomCatBehaviour.RemoveFromEntity");
		int status = s_api.BehaviourRemoveDeferred(instance.Value);
		if (status != 0 && ScriptExecutionContext.IsActive)
			AbortDeferredCommandBatch(ScriptExecutionContext.CurrentEntity,
				"TomCatBehaviour.RemoveFromEntity mutation was rejected");
		Check(status, "TomCatBehaviour.RemoveFromEntity");
	}

	internal static ulong GetActiveSceneHandle()
	{
		EnsureMainThread();
		Require(s_api.SceneGetActiveHandle != null, "SceneManager.ActiveScene");
		ulong handle = 0;
		Check(s_api.SceneGetActiveHandle(&handle), "SceneManager.ActiveScene");
		return handle;
	}

	internal static int GetActiveSceneBuildIndex()
	{
		EnsureMainThread();
		Require(s_api.SceneGetActiveBuildIndex != null, "SceneManager.ActiveBuildIndex");
		int buildIndex = -1;
		Check(s_api.SceneGetActiveBuildIndex(&buildIndex),
			"SceneManager.ActiveBuildIndex");
		return buildIndex;
	}

	internal static bool RequestLoadScene(ulong sceneHandle)
	{
		EnsureMainThread();
		Require(s_api.SceneRequestLoadHandle != null, "SceneManager.LoadScene(SceneAsset)");
		return ReadBoolean(s_api.SceneRequestLoadHandle(sceneHandle),
			"SceneManager.LoadScene(SceneAsset)");
	}

	internal static bool RequestLoadScene(int buildIndex)
	{
		EnsureMainThread();
		Require(s_api.SceneRequestLoadIndex != null, "SceneManager.LoadScene(int)");
		return ReadBoolean(s_api.SceneRequestLoadIndex(buildIndex),
			"SceneManager.LoadScene(int)");
	}

	internal static bool RequestReloadScene()
	{
		EnsureMainThread();
		Require(s_api.SceneRequestReload != null, "SceneManager.ReloadActiveScene");
		return ReadBoolean(s_api.SceneRequestReload(),
			"SceneManager.ReloadActiveScene");
	}

	internal static bool InstantiatePrefab(Entity context, PrefabAsset prefab,
		Vector3 worldPosition, Entity? parent)
	{
		EnsureMainThread();
		Require(s_api.PrefabInstantiateDeferred != null,
			"TomCatBehaviour.Instantiate");
		if (prefab.Handle == 0)
		{
			AbortDeferredCommandBatch(context,
				"Prefab handle is invalid");
			return false;
		}
		NativeEntityHandleV1 parentHandle = parent is null
			? default : ToNative(parent);
		return ReadBoolean(s_api.PrefabInstantiateDeferred(ToNative(context),
			prefab.Handle, ToNative(worldPosition), parentHandle),
			"TomCatBehaviour.Instantiate");
	}

    internal static void WriteLog(int level, string message)
    {
        WithUtf8(message, "Log", view => s_api.Log(level, view), s_api.Log != null);
    }

	internal static bool TryBeginDeferredCallbackTransaction(Entity context,
		out ulong token)
	{
		token = 0;
		// Each top-level managed callback owns one transaction on the main thread.
		// Clear any completed/fail-stopped callback state before opening its successor.
		s_activeDeferredCallbackToken = 0;
		s_deferredAbortProtocolFailed = false;
		if (!SupportsDeferredCallbackTransactions
			|| s_deferredCallbackTransactionsApi.BeginCallback == null)
			return false;
		ulong nativeToken = 0;
		if (s_deferredCallbackTransactionsApi.BeginCallback(
			ToNative(context), &nativeToken) != 0 || nativeToken == 0)
			return false;
		token = nativeToken;
		s_activeDeferredCallbackToken = nativeToken;
		return true;
	}

	internal static bool CompleteDeferredCallbackTransaction(ulong token)
	{
		if (token == 0 || token != s_activeDeferredCallbackToken
			|| !SupportsDeferredCallbackTransactions
			|| s_deferredCallbackTransactionsApi.CompleteCallback == null)
			return false;

		// AbortBatch is the only operation that can guarantee rollback of commands
		// already staged by a failing callback. If that signal failed, never call
		// CompleteCallback: native must fail-stop the Scene and discard the open batch.
		if (s_deferredAbortProtocolFailed)
		{
			s_activeDeferredCallbackToken = 0;
			s_deferredAbortProtocolFailed = false;
			return false;
		}

		try
		{
			return s_deferredCallbackTransactionsApi.CompleteCallback(token) == 0;
		}
		finally
		{
			// CompleteCallback may synchronously re-enter managed code and finish
			// another callback. Do not clear tracking that belongs to that callback.
			if (s_activeDeferredCallbackToken == token)
			{
				s_activeDeferredCallbackToken = 0;
				s_deferredAbortProtocolFailed = false;
			}
		}
	}

	internal static void AbortDeferredCommandBatch(Entity context, string reason)
	{
		// Legacy hosts without this optional capability have no callback transaction
		// to poison. Once it is bound, a rejected or throwing abort is a protocol
		// failure: allowing CompleteCallback to run could commit a failed callback.
		if (!Volatile.Read(ref s_deferredCommandsBound)
			|| s_deferredCommandsApi.AbortBatch == null)
			return;
		try
		{
			byte[] bytes = Encoding.UTF8.GetBytes(reason ?? string.Empty);
			fixed (byte* pointer = bytes)
			{
				int status = s_deferredCommandsApi.AbortBatch(ToNative(context),
					new NativeUtf8View(pointer, (ulong)bytes.Length));
				if (status != 0)
				{
					s_deferredAbortProtocolFailed = true;
					throw new DeferredCallbackProtocolException(
						$"Native rejected deferred callback abort with status {status}.");
				}
			}
		}
		catch (DeferredCallbackProtocolException)
		{
			throw;
		}
		catch (Exception exception)
		{
			s_deferredAbortProtocolFailed = true;
			throw new DeferredCallbackProtocolException(
				"Native deferred callback abort threw an exception.", exception);
		}
	}

    internal static void ReportManagedException(string message, Exception exception)
    {
        var frames = new System.Diagnostics.StackTrace(exception, true).GetFrames();
        var frame = frames?.FirstOrDefault(frame => frame.GetFileLineNumber() > 0
            && !string.IsNullOrEmpty(frame.GetFileName()));
        ReportManagedException(message, frame?.GetFileName(),
            frame?.GetFileLineNumber() ?? 0, frame?.GetFileColumnNumber() ?? 0);
    }

    internal static void ReportManagedException(string message, string? file = null, int line = 0, int column = 0)
    {
		// Host failures can originate in the background metadata compiler. This
		// diagnostic-only callback is thread-safe on the native side and must never
		// throw while an UnmanagedCallersOnly export is already handling an error.
		try
		{
			if (!Volatile.Read(ref s_bound) || s_api.EmitDiagnostic == null)
			{
				Console.Error.WriteLine(message);
				return;
			}

			byte[] messageBytes = Encoding.UTF8.GetBytes(message);
			byte[] fileBytes = Encoding.UTF8.GetBytes(file ?? string.Empty);
			fixed (byte* messagePointer = messageBytes)
			fixed (byte* filePointer = fileBytes)
			{
				NativeDiagnosticV1 diagnostic = new()
				{
					Severity = 2,
					Message = new NativeUtf8View(messagePointer,
						(ulong)messageBytes.Length),
					File = new NativeUtf8View(filePointer,
						(ulong)fileBytes.Length),
					Line = line,
					Column = column
				};
				_ = s_api.EmitDiagnostic(&diagnostic);
			}
		}
		catch
		{
			try { Console.Error.WriteLine(message); }
			catch { }
		}
    }

    private static NativeVector2 ToNative(Vector2 value) => new() { X = value.X, Y = value.Y };
	private static NativeVector3 ToNative(Vector3 value) =>
		new() { X = value.X, Y = value.Y, Z = value.Z };

	private static string CopyUtf8(NativeUtf8View value, string operation)
	{
		if (value.Length == 0)
			return string.Empty;
		if (value.Data is null || value.Length > int.MaxValue)
			throw new TomCatException($"{operation} returned an invalid UTF-8 view.");
		try
		{
			return s_strictUtf8.GetString(
				new ReadOnlySpan<byte>(value.Data, (int)value.Length));
		}
		catch (DecoderFallbackException error)
		{
			throw new TomCatException(
				$"{operation} returned malformed UTF-8: {error.Message}");
		}
	}

	private static void WithEntityUtf8(Entity entity, string value,
		string operation, Func<NativeUtf8View, int> callback, bool available)
	{
		EnsureMainThread();
		if (value is null)
		{
			AbortDeferredCommandBatch(entity, $"{operation} value cannot be null");
			throw new ArgumentNullException(nameof(value));
		}
		Require(available, operation);
		byte[] bytes;
		try
		{
			bytes = s_strictUtf8.GetBytes(value);
		}
		catch (EncoderFallbackException error)
		{
			AbortDeferredCommandBatch(entity,
				$"{operation} value is not valid Unicode");
			throw new ArgumentException("Value is not valid Unicode.", nameof(value),
				error);
		}
		fixed (byte* pointer = bytes)
			Check(callback(new NativeUtf8View(pointer, (ulong)bytes.Length)),
				operation);
	}

    private static void WithUtf8(string value, string operation,
        Func<NativeUtf8View, int> callback, bool available)
    {
        ArgumentNullException.ThrowIfNull(value);
        EnsureMainThread();
        Require(available, operation);
        byte[] bytes = Encoding.UTF8.GetBytes(value);
        fixed (byte* pointer = bytes)
            Check(callback(new NativeUtf8View(pointer, (ulong)bytes.Length)), operation);
    }

    private static bool ReadBoolean(int result, string operation)
    {
        if (result >= 0)
            return result != 0;
        throw new TomCatException($"{operation} failed with status {result}.");
    }

    private static void Check(int status, string operation)
    {
        if (status != 0)
            throw new TomCatException($"{operation} failed with status {status}.");
    }

    private static void Require(bool condition, string operation)
    {
        if (!condition)
            throw new TomCatException($"{operation} is unavailable in NativeApiV1.");
    }

	private static void RequireInput(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_inputBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.InputApiV1 capability.");
	}

	private static void RequireInputEvents(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_inputEventsBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.InputEventsApiV1 capability.");
	}

	private static void RequireApplicationPaths(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_applicationPathsBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.ApplicationPathsApiV1 capability.");
	}

	private static void RequireComponent(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_componentBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.ComponentApiV1 capability.");
	}

	private static void RequireComponentString(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_componentStringBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.ComponentStringApiV1 capability.");
	}

	private static void RequireComponentSchema(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_componentSchemaBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.ComponentSchemaApiV1 capability.");
	}

	private static void RequireAudio(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_audioBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.AudioApiV1 capability.");
	}

	private static void RequireAudioSpatial(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_audioSpatialBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.AudioSpatialApiV1 capability.");
	}

	private static void RequireRuntimeUI(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_runtimeUIBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.RuntimeUIApiV1 capability.");
	}

	private static void RequireGameplay(bool condition, string operation)
	{
		if (!Volatile.Read(ref s_gameplayBound) || !condition)
			throw new TomCatException(
				$"{operation} requires the optional TomCat.GameplayApiV1 capability.");
	}

    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetPosition => s_api.TransformGetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetPosition => s_api.TransformSetPosition;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetRotationEuler => s_api.TransformGetRotationEuler;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetRotationEuler => s_api.TransformSetRotationEuler;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetScale => s_api.TransformGetScale;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetScale => s_api.TransformSetScale;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetLocalPosition
	{
		get { RequireGameplay(s_gameplayApi.TransformGetLocalPosition != null, "Transform.LocalPosition"); return s_gameplayApi.TransformGetLocalPosition; }
	}
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetLocalPosition
	{
		get { RequireGameplay(s_gameplayApi.TransformSetLocalPosition != null, "Transform.LocalPosition"); return s_gameplayApi.TransformSetLocalPosition; }
	}
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetLocalRotationEuler
	{
		get { RequireGameplay(s_gameplayApi.TransformGetLocalRotationEuler != null, "Transform.LocalRotationEuler"); return s_gameplayApi.TransformGetLocalRotationEuler; }
	}
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetLocalRotationEuler
	{
		get { RequireGameplay(s_gameplayApi.TransformSetLocalRotationEuler != null, "Transform.LocalRotationEuler"); return s_gameplayApi.TransformSetLocalRotationEuler; }
	}
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3*, int> TransformGetLocalScale
	{
		get { RequireGameplay(s_gameplayApi.TransformGetLocalScale != null, "Transform.LocalScale"); return s_gameplayApi.TransformGetLocalScale; }
	}
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector3, int> TransformSetLocalScale
	{
		get { RequireGameplay(s_gameplayApi.TransformSetLocalScale != null, "Transform.LocalScale"); return s_gameplayApi.TransformSetLocalScale; }
	}
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodySetLinearVelocity => s_api.RigidbodySetLinearVelocity;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyForce => s_api.RigidbodyApplyForce;
    internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, NativeVector2, int> RigidbodyApplyLinearImpulse => s_api.RigidbodyApplyLinearImpulse;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputIsKeyHeld => s_api.InputIsKeyHeld;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputWasKeyPressed => s_api.InputWasKeyPressed;
    internal static delegate* unmanaged[Cdecl]<uint, int> InputWasKeyReleased => s_api.InputWasKeyReleased;
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMousePosition => s_api.InputGetMousePosition;
    internal static delegate* unmanaged[Cdecl]<NativeVector2*, int> InputGetMouseDelta => s_api.InputGetMouseDelta;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputIsMouseButtonHeld => s_inputApi.IsMouseButtonHeld;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputWasMouseButtonPressed => s_inputApi.WasMouseButtonPressed;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputWasMouseButtonReleased => s_inputApi.WasMouseButtonReleased;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputIsGamepadConnected => s_inputApi.IsGamepadConnected;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputWasGamepadConnected => s_inputApi.WasGamepadConnected;
	internal static delegate* unmanaged[Cdecl]<uint, int> InputWasGamepadDisconnected => s_inputApi.WasGamepadDisconnected;
	internal static delegate* unmanaged[Cdecl]<uint, uint, int> InputIsGamepadButtonHeld => s_inputApi.IsGamepadButtonHeld;
	internal static delegate* unmanaged[Cdecl]<uint, uint, int> InputWasGamepadButtonPressed => s_inputApi.WasGamepadButtonPressed;
	internal static delegate* unmanaged[Cdecl]<uint, uint, int> InputWasGamepadButtonReleased => s_inputApi.WasGamepadButtonReleased;
	internal static delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> ApplicationGetSaveDirectory => s_applicationPathsApi.GetSaveDirectory;
	internal static delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> ApplicationGetLogDirectory => s_applicationPathsApi.GetLogDirectory;
	internal static delegate* unmanaged[Cdecl]<byte*, uint, uint*, int> ApplicationGetCrashDirectory => s_applicationPathsApi.GetCrashDirectory;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetEnabled => s_audioApi.GetEnabled;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetEnabled => s_audioApi.SetEnabled;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetPlayOnStart => s_audioApi.GetPlayOnStart;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetPlayOnStart => s_audioApi.SetPlayOnStart;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetLoop => s_audioApi.GetLoop;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetLoop => s_audioApi.SetLoop;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> AudioGetVolume => s_audioApi.GetVolume;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> AudioSetVolume => s_audioApi.SetVolume;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> AudioGetPitch => s_audioApi.GetPitch;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> AudioSetPitch => s_audioApi.SetPitch;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioPlay => s_audioApi.Play;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioPause => s_audioApi.Pause;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioStop => s_audioApi.Stop;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetListenerEnabled => s_audioApi.GetListenerEnabled;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetListenerEnabled => s_audioApi.SetListenerEnabled;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetListenerPrimary => s_audioApi.GetListenerPrimary;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetListenerPrimary => s_audioApi.SetListenerPrimary;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int> AudioGetStreaming => s_audioSpatialApi.GetStreaming;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, int, int> AudioSetStreaming => s_audioSpatialApi.SetStreaming;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> AudioGetSpatialBlend => s_audioSpatialApi.GetSpatialBlend;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> AudioSetSpatialBlend => s_audioSpatialApi.SetSpatialBlend;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> AudioGetMinDistance => s_audioSpatialApi.GetMinDistance;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> AudioSetMinDistance => s_audioSpatialApi.SetMinDistance;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float*, int> AudioGetMaxDistance => s_audioSpatialApi.GetMaxDistance;
	internal static delegate* unmanaged[Cdecl]<NativeEntityHandleV1, float, int> AudioSetMaxDistance => s_audioSpatialApi.SetMaxDistance;
}
