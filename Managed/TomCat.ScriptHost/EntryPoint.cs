using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using TomCat.Interop;

namespace TomCat.ScriptHost;

public static unsafe class EntryPoint
{
    // Native host lookup contract:
    //   assembly: TomCat.ScriptHost.dll
    //   type:     TomCat.ScriptHost.EntryPoint, TomCat.ScriptHost
    //   method:   GetManagedApi
    //   delegate: UNMANAGEDCALLERSONLY_METHOD
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int GetManagedApi(NativeApiV1* nativeApi, ManagedApiV1* managedApi)
    {
		try
		{
			if (nativeApi is null || managedApi is null)
				return HostStatus.InvalidArgument;
			if (nativeApi->Version != ManagedAbi.NativeApiVersion
				|| nativeApi->Size < (uint)sizeof(NativeApiV1)
				|| managedApi->Version != ManagedAbi.ManagedApiVersion
				|| managedApi->Size < (uint)sizeof(ManagedApiV1))
				return HostStatus.VersionMismatch;
			int bindStatus = NativeBridge.Bind(nativeApi);
			if (bindStatus != HostStatus.Success)
				return bindStatus;

            *managedApi = new ManagedApiV1
            {
                Version = ManagedAbi.ManagedApiVersion,
                Size = (uint)sizeof(ManagedApiV1),
                CreateDomain = &Exports.CreateDomain,
                LoadProjectAssembly = &Exports.LoadProjectAssembly,
                ReadScriptMetadata = &Exports.ReadScriptMetadata,
                CreateSceneRuntime = &Exports.CreateSceneRuntime,
                InstantiateAll = &Exports.InstantiateAll,
                ApplySerializedFields = &Exports.ApplySerializedFields,
                InvokeCreateAll = &Exports.InvokeCreateAll,
                SetEnabled = &Exports.SetEnabled,
                UpdateAll = &Exports.UpdateAll,
                FixedUpdateAll = &Exports.FixedUpdateAll,
                DispatchPhysicsEvents = &Exports.DispatchPhysicsEvents,
                DestroyAll = &Exports.DestroyAll,
				BeginUnloadDomain = &Exports.BeginUnloadDomain,
				PollUnload = &Exports.PollUnload,
				DestroyAttachments = &Exports.DestroyAttachments,
				InstantiateAttachments = &Exports.InstantiateAttachments
            };
            return HostStatus.Success;
        }
        catch (Exception exception)
        {
            HostErrors.Report("GetManagedApi", exception);
            return HostStatus.ManagedException;
        }
    }
}

internal static class HostStatus
{
	internal const int Success = 0;
	internal const int InvalidArgument = -1;
	internal const int InvalidState = -2;
	internal const int NotFound = -3;
	internal const int VersionMismatch = -4;
	internal const int ManagedException = -5;
	internal const int Unavailable = -8;
}

internal static class HostErrors
{
    internal static int Guard(string operation, Action action)
    {
        try
        {
            action();
            return HostStatus.Success;
        }
        catch (KeyNotFoundException exception)
        {
            Report(operation, exception);
            return HostStatus.NotFound;
        }
		catch (InvalidOperationException exception)
		{
			Report(operation, exception);
			return HostStatus.InvalidState;
		}
		catch (Exception exception) when (exception is ArgumentException
			or InvalidDataException or JsonException)
		{
			Report(operation, exception);
			return HostStatus.InvalidArgument;
		}
        catch (Exception exception)
        {
            Report(operation, exception);
            return HostStatus.ManagedException;
        }
    }

    internal static void Report(string operation, Exception exception) =>
        NativeBridge.ReportManagedException(
            $"TomCat.ScriptHost {operation} failed: {exception.GetType().FullName}: {exception.Message}\n{exception.StackTrace}");
}

internal static unsafe class Exports
{
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int CreateDomain(int domainKind, ulong* domainId)
    {
        if (domainId is null || (domainKind != 0 && domainKind != 1))
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(CreateDomain), () =>
            *domainId = HostRegistry.CreateDomain((ScriptDomainKind)domainKind));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int LoadProjectAssembly(ulong domainId, NativeByteView assembly, NativeByteView pdb)
    {
        if (!TryCopy(assembly, allowEmpty: false, out byte[]? assemblyBytes) ||
            !TryCopy(pdb, allowEmpty: true, out byte[]? pdbBytes))
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(LoadProjectAssembly), () =>
            HostRegistry.GetDomain(domainId).LoadProjectAssembly(assemblyBytes!, pdbBytes!));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ReadScriptMetadata(ulong domainId,
        delegate* unmanaged[Cdecl]<NativeByteView, ulong, int> receiver, ulong receiverToken)
    {
        if (receiver == null)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(ReadScriptMetadata), () =>
        {
            byte[] bytes = Encoding.UTF8.GetBytes(HostRegistry.GetDomain(domainId).ManifestJson);
            fixed (byte* pointer = bytes)
            {
                int status = receiver(new NativeByteView(pointer, (ulong)bytes.Length), receiverToken);
                if (status != 0)
                    throw new InvalidOperationException($"Native metadata receiver returned {status}.");
            }
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int CreateSceneRuntime(ulong domainId, ulong sceneSessionId,
        ulong runtimeGeneration, ulong* sceneRuntimeId)
    {
        if (sceneRuntimeId is null)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(CreateSceneRuntime), () =>
            *sceneRuntimeId = HostRegistry.CreateScene(domainId, sceneSessionId, runtimeGeneration));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int InstantiateAll(ulong sceneRuntimeId, NativeScriptAttachmentV1* items,
        uint count)
    {
        if (count != 0 && items is null || count > 1_000_000)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(InstantiateAll), () =>
        {
            var attachments = new ScriptAttachment[count];
            for (int index = 0; index < attachments.Length; ++index)
            {
                NativeScriptAttachmentV1 item = items[index];
                if (item.Reserved != 0)
                    throw new InvalidDataException("NativeScriptAttachmentV1.Reserved must be zero.");
                attachments[index] = new ScriptAttachment(NativeBridge.FromNative(item.Entity),
                    item.AttachmentId, item.ScriptAsset, item.Enabled != 0);
            }
            HostRegistry.GetScene(sceneRuntimeId).InstantiateAll(attachments);
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int ApplySerializedFields(ulong sceneRuntimeId, NativeByteView fieldsJson)
    {
        if (!TryCopy(fieldsJson, allowEmpty: false, out byte[]? bytes))
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(ApplySerializedFields), () =>
            HostRegistry.GetScene(sceneRuntimeId).ApplySerializedFields(Encoding.UTF8.GetString(bytes!)));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int InvokeCreateAll(ulong sceneRuntimeId) =>
        HostErrors.Guard(nameof(InvokeCreateAll), () => HostRegistry.GetScene(sceneRuntimeId).InvokeCreateAll());

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int SetEnabled(ulong sceneRuntimeId, ulong attachmentId, int enabled)
    {
        if (enabled != 0 && enabled != 1)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(SetEnabled), () =>
            HostRegistry.GetScene(sceneRuntimeId).SetEnabled(attachmentId, enabled != 0));
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int UpdateAll(ulong sceneRuntimeId, float deltaTime) =>
        HostErrors.Guard(nameof(UpdateAll), () => HostRegistry.GetScene(sceneRuntimeId).UpdateAll(deltaTime));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int FixedUpdateAll(ulong sceneRuntimeId, float fixedDeltaTime) =>
        HostErrors.Guard(nameof(FixedUpdateAll), () => HostRegistry.GetScene(sceneRuntimeId).FixedUpdateAll(fixedDeltaTime));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int DispatchPhysicsEvents(ulong sceneRuntimeId, NativePhysicsEventV1* events,
        uint count)
    {
        if (count != 0 && events is null || count > 1_000_000)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(DispatchPhysicsEvents), () =>
        {
            var managedEvents = new ScriptPhysicsEvent[count];
            for (int index = 0; index < managedEvents.Length; ++index)
            {
                NativePhysicsEventV1 value = events[index];
                if (value.Reserved != 0 || value.Kind > NativePhysicsEventKindV1.TriggerExit)
                    throw new InvalidDataException("NativePhysicsEventV1 contains an invalid value.");
                managedEvents[index] = new ScriptPhysicsEvent(value.Kind,
                    NativeBridge.FromNative(value.EntityA), NativeBridge.FromNative(value.EntityB));
            }
            HostRegistry.GetScene(sceneRuntimeId).DispatchPhysicsEvents(managedEvents);
        });
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	internal static int DestroyAll(ulong sceneRuntimeId) =>
		HostErrors.Guard(nameof(DestroyAll), () => HostRegistry.DestroyScene(sceneRuntimeId));

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	internal static int DestroyAttachments(ulong sceneRuntimeId, ulong* attachmentIds, uint count)
	{
		if (count != 0 && attachmentIds is null || count > 1_000_000)
			return HostStatus.InvalidArgument;
		return HostErrors.Guard(nameof(DestroyAttachments), () =>
		{
			var ids = new ulong[count];
			for (int index = 0; index < ids.Length; ++index)
				ids[index] = attachmentIds[index];
			HostRegistry.GetScene(sceneRuntimeId).DestroyAttachments(ids);
		});
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	internal static int InstantiateAttachments(ulong sceneRuntimeId,
		NativeScriptAttachmentV1* items, uint count, NativeByteView fieldsJson)
	{
		if (count != 0 && items is null || count > 1_000_000
			|| !TryCopy(fieldsJson, allowEmpty: false, out byte[]? bytes))
			return HostStatus.InvalidArgument;
		return HostErrors.Guard(nameof(InstantiateAttachments), () =>
		{
			var attachments = new ScriptAttachment[count];
			for (int index = 0; index < attachments.Length; ++index)
			{
				NativeScriptAttachmentV1 item = items[index];
				if (item.Reserved != 0)
					throw new InvalidDataException(
						"NativeScriptAttachmentV1.Reserved must be zero.");
				attachments[index] = new ScriptAttachment(
					NativeBridge.FromNative(item.Entity), item.AttachmentId,
					item.ScriptAsset, item.Enabled != 0);
			}
			HostRegistry.GetScene(sceneRuntimeId).InstantiateAttachments(
				attachments, Encoding.UTF8.GetString(bytes!));
		});
	}

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int BeginUnloadDomain(ulong domainId) =>
        HostErrors.Guard(nameof(BeginUnloadDomain), () => HostRegistry.BeginUnloadDomain(domainId));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    internal static int PollUnload(ulong domainId, int* unloaded)
    {
        if (unloaded is null)
            return HostStatus.InvalidArgument;
        return HostErrors.Guard(nameof(PollUnload), () =>
            *unloaded = HostRegistry.PollUnload(domainId) ? 1 : 0);
    }

    private static bool TryCopy(NativeByteView view, bool allowEmpty, out byte[]? bytes)
    {
        bytes = null;
        if (view.Length > int.MaxValue || view.Length != 0 && view.Data is null ||
            !allowEmpty && view.Length == 0)
            return false;
        bytes = view.Length == 0 ? [] : new ReadOnlySpan<byte>(view.Data, (int)view.Length).ToArray();
        return true;
    }
}

internal static class HostRegistry
{
    private static readonly object Gate = new();
    private static readonly Dictionary<ulong, ScriptDomain> Domains = [];
    private static readonly Dictionary<ulong, (ulong DomainId, ScriptSceneRuntime Scene)> Scenes = [];
    private static ulong s_nextDomainId = 1;

    internal static ulong CreateDomain(ScriptDomainKind kind)
    {
        lock (Gate)
        {
            ulong id = s_nextDomainId++;
            Domains.Add(id, new ScriptDomain(kind));
            return id;
        }
    }

    internal static ScriptDomain GetDomain(ulong id)
    {
        lock (Gate)
            return Domains.TryGetValue(id, out ScriptDomain? domain)
                ? domain
                : throw new KeyNotFoundException($"Script domain {id} does not exist.");
    }

    internal static ulong CreateScene(ulong domainId, ulong sceneSessionId, ulong runtimeGeneration)
    {
        lock (Gate)
        {
            ScriptSceneRuntime scene = GetDomain(domainId).CreateSceneRuntime(sceneSessionId,
                runtimeGeneration);
            if (!Scenes.TryAdd(scene.Id, (domainId, scene)))
                throw new InvalidOperationException($"Duplicate scene runtime ID {scene.Id}.");
            return scene.Id;
        }
    }

    internal static ScriptSceneRuntime GetScene(ulong id)
    {
        lock (Gate)
            return Scenes.TryGetValue(id, out var entry)
                ? entry.Scene
                : throw new KeyNotFoundException($"Script scene runtime {id} does not exist.");
    }

    internal static void DestroyScene(ulong id)
    {
        ScriptSceneRuntime scene;
        lock (Gate)
        {
            if (!Scenes.Remove(id, out var entry))
                throw new KeyNotFoundException($"Script scene runtime {id} does not exist.");
            scene = entry.Scene;
        }
        scene.DestroyAll();
    }

    internal static void BeginUnloadDomain(ulong id)
    {
        ScriptDomain domain;
        lock (Gate)
        {
            domain = GetDomain(id);
            foreach (ulong sceneId in Scenes.Where(entry => entry.Value.DomainId == id)
                         .Select(entry => entry.Key).ToArray())
                Scenes.Remove(sceneId);
        }
        domain.BeginUnload();
    }

    internal static bool PollUnload(ulong id)
    {
        ScriptDomain domain = GetDomain(id);
        bool unloaded = domain.PollUnload();
        if (unloaded)
        {
            lock (Gate)
                Domains.Remove(id);
        }
        return unloaded;
    }
}
