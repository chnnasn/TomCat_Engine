#include "tcpch.h"
#include "ManagedScriptRuntime.h"

#include "ManagedRuntimeFactory.h"

#include "TomCat/Core/Log.h"
#include "TomCat/Utils/PathUtils.h"

#include <atomic>
#include <fstream>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace TomCat::Scripting {

	namespace {

		struct MetadataReceiveContext
		{
			std::string Payload;
			bool Received = false;
		};

		std::atomic<uint64_t> s_NextMetadataReceiverToken{ 1 };
		std::mutex s_MetadataReceiverMutex;
		std::unordered_map<uint64_t, MetadataReceiveContext> s_MetadataReceivers;

		class ScopedMetadataReceiver final
		{
		public:
			ScopedMetadataReceiver()
			{
				for (;;)
				{
					const uint64_t candidate = s_NextMetadataReceiverToken.fetch_add(
						1, std::memory_order_relaxed);
					if (candidate == 0)
						continue;
					std::lock_guard<std::mutex> lock(s_MetadataReceiverMutex);
					if (s_MetadataReceivers.try_emplace(candidate).second)
					{
						m_Token = candidate;
						break;
					}
				}
			}

			~ScopedMetadataReceiver()
			{
				std::lock_guard<std::mutex> lock(s_MetadataReceiverMutex);
				s_MetadataReceivers.erase(m_Token);
			}

			ScopedMetadataReceiver(const ScopedMetadataReceiver&) = delete;
			ScopedMetadataReceiver& operator=(const ScopedMetadataReceiver&) = delete;

			uint64_t GetToken() const { return m_Token; }

			bool Take(std::string& output)
			{
				std::lock_guard<std::mutex> lock(s_MetadataReceiverMutex);
				const auto iterator = s_MetadataReceivers.find(m_Token);
				if (iterator == s_MetadataReceivers.end() || !iterator->second.Received)
					return false;
				output = std::move(iterator->second.Payload);
				return true;
			}

		private:
			uint64_t m_Token = 0;
		};

		int32_t ReceiveMetadata(NativeByteView value, uint64_t receiverToken) noexcept
		{
			if (receiverToken == 0 || (!value.Data && value.Length != 0)
				|| value.Length > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
				return static_cast<int32_t>(ScriptStatus::InvalidArgument);
			try
			{
				std::string payload;
				if (value.Length != 0)
					payload.assign(reinterpret_cast<const char*>(value.Data),
						static_cast<size_t>(value.Length));
				std::lock_guard<std::mutex> lock(s_MetadataReceiverMutex);
				const auto iterator = s_MetadataReceivers.find(receiverToken);
				if (iterator == s_MetadataReceivers.end())
					return static_cast<int32_t>(ScriptStatus::InvalidArgument);
				iterator->second.Payload = std::move(payload);
				iterator->second.Received = true;
				return static_cast<int32_t>(ScriptStatus::Success);
			}
			catch (...)
			{
				return static_cast<int32_t>(ScriptStatus::InvalidState);
			}
		}

		NativeByteView AsView(const std::vector<uint8_t>& bytes)
		{
			return NativeByteView{ bytes.empty() ? nullptr : bytes.data(),
				static_cast<uint64_t>(bytes.size()) };
		}

	}

	ManagedScriptRuntime::~ManagedScriptRuntime()
	{
		DestroyAll();
		if (m_DomainId != 0)
		{
			bool unloaded = false;
			for (uint32_t attempt = 0; attempt < 8 && !unloaded; ++attempt)
				unloaded = PollUnload();
			if (!unloaded)
				OnUnloadFailed("A managed project domain survived runtime destruction");
		}
	}

	bool ManagedScriptRuntime::Initialize(
		const DotNetHost::Configuration& hostConfiguration, const NativeApiV1& nativeApi)
	{
		if (IsManagedScriptReloadBlocked())
		{
			m_LastError = "Managed script reload is blocked until restart: "
				+ GetManagedScriptReloadBlockReason();
			return false;
		}
		if (nativeApi.Version != NativeApiVersion || nativeApi.Size < sizeof(NativeApiV1))
		{
			m_LastError = "NativeApiV1 version or size mismatch";
			return false;
		}
		if (!m_Host.Initialize(hostConfiguration))
		{
			m_LastError = m_Host.GetLastError();
			return false;
		}

		void* bootstrap = m_Host.GetUnmanagedFunction(
			L"TomCat.ScriptHost.EntryPoint, TomCat.ScriptHost", L"GetManagedApi");
		if (!bootstrap)
		{
			m_LastError = m_Host.GetLastError();
			return false;
		}

		m_NativeApi = nativeApi;
		m_ManagedApi = {};
		m_ManagedApi.Version = ManagedApiVersion;
		m_ManagedApi.Size = sizeof(ManagedApiV1);
		const auto getManagedApi = reinterpret_cast<GetManagedApiFn>(bootstrap);
		const int32_t status = getManagedApi(&m_NativeApi, &m_ManagedApi);
		if (status != 0 || m_ManagedApi.Version != ManagedApiVersion
			|| m_ManagedApi.Size < sizeof(ManagedApiV1) || !m_ManagedApi.CreateDomain
			|| !m_ManagedApi.LoadProjectAssembly || !m_ManagedApi.CreateSceneRuntime
			|| !m_ManagedApi.ReadScriptMetadata
			|| !m_ManagedApi.InstantiateAll || !m_ManagedApi.ApplySerializedFields
			|| !m_ManagedApi.InvokeCreateAll || !m_ManagedApi.SetEnabled
			|| !m_ManagedApi.UpdateAll
			|| !m_ManagedApi.FixedUpdateAll || !m_ManagedApi.DispatchPhysicsEvents
			|| !m_ManagedApi.DestroyAll || !m_ManagedApi.BeginUnloadDomain
			|| !m_ManagedApi.PollUnload || !m_ManagedApi.DestroyAttachments
			|| !m_ManagedApi.InstantiateAttachments)
		{
			m_LastError = "TomCat.ScriptHost returned an incompatible ManagedApiV1 table";
			return false;
		}
		return true;
	}

	bool ManagedScriptRuntime::ReadBytes(const std::filesystem::path& path, bool optional,
		std::vector<uint8_t>& output)
	{
		output.clear();
		if (path.empty())
			return optional;
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input)
		{
			if (!optional)
				m_LastError = "Could not open managed assembly '" + PathToUTF8(path) + "'";
			return optional;
		}
		const std::streamoff end = input.tellg();
		if (end < 0 || static_cast<uint64_t>(end)
			> static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
		{
			m_LastError = "Managed file is too large: " + PathToUTF8(path);
			return false;
		}
		output.resize(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!output.empty() && !input.read(reinterpret_cast<char*>(output.data()), end))
		{
			output.clear();
			m_LastError = "Could not read managed file: " + PathToUTF8(path);
			return false;
		}
		return true;
	}

	bool ManagedScriptRuntime::SetProjectAssembly(
		const std::filesystem::path& assemblyPath, const std::filesystem::path& pdbPath)
	{
		if (m_DomainId != 0 || m_UnloadPending)
		{
			m_LastError = "Cannot replace the project assembly while a Play Domain is active or unloading";
			return false;
		}
		std::vector<uint8_t> assembly;
		std::vector<uint8_t> pdb;
		if (!ReadBytes(assemblyPath, false, assembly) || assembly.empty()
			|| !ReadBytes(pdbPath, true, pdb))
			return false;
		m_ProjectAssembly = std::move(assembly);
		m_ProjectPdb = std::move(pdb);
		m_ProjectAssemblyPath = assemblyPath;
		return true;
	}

	ScriptStatus ManagedScriptRuntime::ConvertStatus(int32_t status, const char* operation)
	{
		if (status == 0)
			return ScriptStatus::Success;
		m_LastError = std::string(operation ? operation : "Managed operation")
			+ " failed with status " + std::to_string(status);
		TC_Core_Error("{0}", m_LastError);
		return static_cast<ScriptStatus>(status);
	}

	ScriptStatus ManagedScriptRuntime::EnsureScene(const char* operation) const
	{
		if (m_SceneRuntimeId != 0)
			return ScriptStatus::Success;
		TC_Core_Error("Cannot {0} without an active managed scene runtime",
			operation ? operation : "run scripts");
		return ScriptStatus::InvalidState;
	}

	ScriptStatus ManagedScriptRuntime::BeginPlayDomain()
	{
		if (IsManagedScriptReloadBlocked())
		{
			m_LastError = "Managed script reload is blocked until restart: "
				+ GetManagedScriptReloadBlockReason();
			return ScriptStatus::InvalidState;
		}
		if (m_UnloadPending)
			return ScriptStatus::InvalidState;
		if (m_DomainId != 0)
			return ScriptStatus::Success;
		if (m_ProjectAssembly.empty())
			return ScriptStatus::NotFound;
		uint64_t domain = 0;
		ScriptStatus status = ConvertStatus(m_ManagedApi.CreateDomain(1, &domain),
			"Create Play Domain");
		if (status != ScriptStatus::Success || domain == 0)
			return status == ScriptStatus::Success ? ScriptStatus::InvalidState : status;
		status = ConvertStatus(m_ManagedApi.LoadProjectAssembly(domain,
			AsView(m_ProjectAssembly), AsView(m_ProjectPdb)), "Load project assembly");
		if (status != ScriptStatus::Success)
		{
			if (!BeginAndPollMetadataUnload(domain))
				OnUnloadFailed("The failed Play Domain did not unload");
			return status;
		}
		m_DomainId = domain;
		return ScriptStatus::Success;
	}

	bool ManagedScriptRuntime::IsReady() const
	{
		return m_Host.IsInitialized() && m_ManagedApi.CreateDomain
			&& !m_ProjectAssembly.empty() && !m_UnloadPending
			&& !IsManagedScriptReloadBlocked();
	}

	ScriptStatus ManagedScriptRuntime::CreateSceneRuntime(uint64_t sceneSessionId,
		uint64_t runtimeGeneration)
	{
		if (m_SceneRuntimeId != 0 || sceneSessionId == 0 || runtimeGeneration == 0)
			return ScriptStatus::InvalidState;
		ScriptStatus status = BeginPlayDomain();
		if (status != ScriptStatus::Success)
			return status;
		uint64_t sceneRuntime = 0;
		status = ConvertStatus(m_ManagedApi.CreateSceneRuntime(m_DomainId,
			sceneSessionId, runtimeGeneration, &sceneRuntime), "Create managed scene runtime");
		if (status == ScriptStatus::Success && sceneRuntime != 0)
			m_SceneRuntimeId = sceneRuntime;
		else if (status == ScriptStatus::Success)
			status = ScriptStatus::InvalidState;
		return status;
	}

	ScriptStatus ManagedScriptRuntime::InstantiateAll(
		std::span<const NativeScriptAttachmentV1> attachments)
	{
		if (EnsureScene("instantiate scripts") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		if (attachments.size() > std::numeric_limits<uint32_t>::max())
			return ScriptStatus::InvalidArgument;
		return ConvertStatus(m_ManagedApi.InstantiateAll(m_SceneRuntimeId,
			attachments.empty() ? nullptr : attachments.data(),
			static_cast<uint32_t>(attachments.size())), "Instantiate scripts");
	}

	ScriptStatus ManagedScriptRuntime::ApplySerializedFields(std::string_view fieldsJson)
	{
		if (EnsureScene("apply serialized script fields") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		const NativeByteView view{ reinterpret_cast<const uint8_t*>(fieldsJson.data()),
			static_cast<uint64_t>(fieldsJson.size()) };
		return ConvertStatus(m_ManagedApi.ApplySerializedFields(m_SceneRuntimeId, view),
			"Apply serialized script fields");
	}

	ScriptStatus ManagedScriptRuntime::InvokeCreateAll()
	{
		if (EnsureScene("invoke OnCreate") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		return ConvertStatus(m_ManagedApi.InvokeCreateAll(m_SceneRuntimeId), "Invoke OnCreate");
	}

	ScriptStatus ManagedScriptRuntime::SetEnabled(uint64_t attachmentId, bool enabled)
	{
		if (EnsureScene("change script enabled state") != ScriptStatus::Success
			|| !m_ManagedApi.SetEnabled)
			return ScriptStatus::InvalidState;
		return ConvertStatus(m_ManagedApi.SetEnabled(m_SceneRuntimeId, attachmentId,
			enabled ? 1 : 0), "Set script enabled state");
	}

	ScriptStatus ManagedScriptRuntime::UpdateAll(float deltaTime)
	{
		if (EnsureScene("invoke OnUpdate") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		return ConvertStatus(m_ManagedApi.UpdateAll(m_SceneRuntimeId, deltaTime), "Invoke OnUpdate");
	}

	ScriptStatus ManagedScriptRuntime::FixedUpdateAll(float fixedDeltaTime)
	{
		if (EnsureScene("invoke OnFixedUpdate") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		return ConvertStatus(m_ManagedApi.FixedUpdateAll(m_SceneRuntimeId, fixedDeltaTime),
			"Invoke OnFixedUpdate");
	}

	ScriptStatus ManagedScriptRuntime::DispatchPhysicsEvents(
		std::span<const NativePhysicsEventV1> events)
	{
		if (EnsureScene("dispatch physics events") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		if (events.size() > std::numeric_limits<uint32_t>::max())
			return ScriptStatus::InvalidArgument;
		return ConvertStatus(m_ManagedApi.DispatchPhysicsEvents(m_SceneRuntimeId,
			events.empty() ? nullptr : events.data(), static_cast<uint32_t>(events.size())),
			"Dispatch physics events");
	}

	ScriptStatus ManagedScriptRuntime::DestroyAll()
	{
		ScriptStatus result = ScriptStatus::Success;
		if (m_SceneRuntimeId != 0 && m_ManagedApi.DestroyAll)
		{
			result = ConvertStatus(m_ManagedApi.DestroyAll(m_SceneRuntimeId),
				"Destroy managed scripts");
			m_SceneRuntimeId = 0;
		}
		if (m_DomainId != 0 && !m_UnloadPending && m_ManagedApi.BeginUnloadDomain)
		{
			const ScriptStatus unload = ConvertStatus(
				m_ManagedApi.BeginUnloadDomain(m_DomainId), "Begin Play Domain unload");
			if (result == ScriptStatus::Success)
				result = unload;
			m_UnloadPending = unload == ScriptStatus::Success;
		}
		return result;
	}

	ScriptStatus ManagedScriptRuntime::DestroyAttachments(
		std::span<const uint64_t> attachmentIds)
	{
		if (EnsureScene("destroy script attachments") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		if (attachmentIds.size() > std::numeric_limits<uint32_t>::max())
			return ScriptStatus::InvalidArgument;
		return ConvertStatus(m_ManagedApi.DestroyAttachments(m_SceneRuntimeId,
			attachmentIds.empty() ? nullptr : attachmentIds.data(),
			static_cast<uint32_t>(attachmentIds.size())), "Destroy script attachments");
	}

	ScriptStatus ManagedScriptRuntime::InstantiateAttachments(
		std::span<const NativeScriptAttachmentV1> attachments,
		std::string_view fieldsJson)
	{
		if (EnsureScene("instantiate script attachments") != ScriptStatus::Success)
			return ScriptStatus::InvalidState;
		if (attachments.size() > std::numeric_limits<uint32_t>::max()
			|| fieldsJson.empty())
			return ScriptStatus::InvalidArgument;
		const NativeByteView fields{ reinterpret_cast<const uint8_t*>(fieldsJson.data()),
			static_cast<uint64_t>(fieldsJson.size()) };
		return ConvertStatus(m_ManagedApi.InstantiateAttachments(m_SceneRuntimeId,
			attachments.empty() ? nullptr : attachments.data(),
			static_cast<uint32_t>(attachments.size()), fields),
			"Instantiate script attachments");
	}

	bool ManagedScriptRuntime::PollUnload()
	{
		if (!m_UnloadPending)
			return m_DomainId == 0;
		int32_t unloaded = 0;
		if (ConvertStatus(m_ManagedApi.PollUnload(m_DomainId, &unloaded),
			"Poll Play Domain unload") != ScriptStatus::Success)
			return false;
		if (unloaded != 0)
		{
			m_DomainId = 0;
			m_UnloadPending = false;
			return true;
		}
		return false;
	}

	void ManagedScriptRuntime::OnUnloadFailed(std::string_view reason)
	{
		BlockManagedScriptReload(reason);
		m_LastError = "Managed script reload is blocked until restart: "
			+ GetManagedScriptReloadBlockReason();
	}

	bool ManagedScriptRuntime::BeginAndPollMetadataUnload(uint64_t domainId)
	{
		if (m_ManagedApi.BeginUnloadDomain(domainId) != 0)
			return false;
		for (uint32_t attempt = 0; attempt < 8; ++attempt)
		{
			int32_t unloaded = 0;
			if (m_ManagedApi.PollUnload(domainId, &unloaded) != 0)
				return false;
			if (unloaded != 0)
				return true;
		}
		return false;
	}

	bool ManagedScriptRuntime::ReadProjectMetadata(std::string& manifestJson)
	{
		manifestJson.clear();
		if (IsManagedScriptReloadBlocked() || !IsReady()
			|| !m_ManagedApi.ReadScriptMetadata || m_DomainId != 0)
			return false;
		uint64_t metadataDomain = 0;
		if (m_ManagedApi.CreateDomain(0, &metadataDomain) != 0 || metadataDomain == 0)
			return false;
		// Metadata validation also runs on the background compiler thread. Keep the
		// temporary receive state entirely native and identify it across the ABI by
		// a fixed-width token; the guarded registry makes concurrent validations safe.
		ScopedMetadataReceiver receiver;
		bool success = m_ManagedApi.LoadProjectAssembly(metadataDomain,
			AsView(m_ProjectAssembly), AsView(m_ProjectPdb)) == 0
			&& m_ManagedApi.ReadScriptMetadata(metadataDomain, &ReceiveMetadata,
				receiver.GetToken()) == 0
			&& receiver.Take(manifestJson);
		if (!BeginAndPollMetadataUnload(metadataDomain))
		{
			OnUnloadFailed("Metadata Domain " + std::to_string(metadataDomain)
				+ " did not unload");
			success = false;
		}
		return success;
	}

}
