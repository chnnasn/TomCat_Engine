#include "PlayerRuntimeLayer.h"

#include "PlayerCommandLine.h"

#include <TomCat/Scripting/ManagedRuntimeFactory.h>
#include <TomCat/Scripting/ScriptEngine.h>
#include <TomCat/Utils/FileSystemUtils.h>
#include <TomCat/Utils/PathUtils.h>

#include <system_error>

namespace TomCat {
	namespace {

		class ScopedShadowDirectory
		{
		public:
			~ScopedShadowDirectory()
			{
				if (m_Path.empty())
					return;
				std::error_code error;
				std::filesystem::remove_all(m_Path, error);
			}

			bool Create(std::string& errorMessage)
			{
				std::error_code error;
				const std::filesystem::path root =
					std::filesystem::temp_directory_path(error);
				if (error || root.empty())
				{
					errorMessage = "the operating-system temporary directory is unavailable";
					return false;
				}
				for (uint32_t attempt = 0; attempt < 64; ++attempt)
				{
					const std::filesystem::path candidate = root /
						("TomCatPlayer-" + std::to_string(static_cast<uint64_t>(UUID())));
					error.clear();
					if (std::filesystem::create_directory(candidate, error))
					{
						m_Path = candidate;
						return true;
					}
					if (error && error != std::errc::file_exists)
					{
						errorMessage =
							"could not create a private managed Shadow directory: " +
							error.message();
						return false;
					}
				}
				errorMessage = "could not allocate a unique managed Shadow directory";
				return false;
			}

			const std::filesystem::path& GetPath() const { return m_Path; }

		private:
			std::filesystem::path m_Path;
		};

		bool WritePayload(const std::filesystem::path& path,
			const std::vector<uint8_t>& bytes, std::string& errorMessage)
		{
			if (bytes.empty())
			{
				errorMessage = "managed payload file is empty";
				return false;
			}
			return FileSystem::WriteFileAtomically(path,
				std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
				errorMessage);
		}

		bool CreateMountedManagedRuntime(
			std::shared_ptr<Scripting::IScriptRuntime>& runtime,
			std::string& errorMessage)
		{
			runtime.reset();
			AssetManager& assets = AssetManager::Get();
			if (!assets.IsCookedPackageMounted())
			{
				errorMessage = "no cooked package is mounted";
				return false;
			}
			const ManagedPackagePayload* payload = assets.GetCookedManagedPayload();
			if (!payload)
			{
				errorMessage.clear();
				return true;
			}

			ScopedShadowDirectory shadow;
			if (!shadow.Create(errorMessage))
			{
				errorMessage = "could not stage the packaged C# assembly: " + errorMessage;
				return false;
			}

			const std::filesystem::path assembly =
				shadow.GetPath() / "Assembly-CSharp.dll";
			if (!WritePayload(assembly, payload->Assembly, errorMessage))
			{
				errorMessage = "could not stage the packaged C# assembly: " + errorMessage;
				return false;
			}
			std::filesystem::path pdb;
			if (!payload->Pdb.empty())
			{
				pdb = shadow.GetPath() / "Assembly-CSharp.pdb";
				if (!WritePayload(pdb, payload->Pdb, errorMessage))
				{
					errorMessage = "could not stage the packaged C# symbols: " + errorMessage;
					return false;
				}
			}

			const std::filesystem::path playerRoot = GetPlayerExecutableDirectory();
			if (playerRoot.empty())
			{
				errorMessage = "could not resolve the TomCatPlayer executable directory";
				return false;
			}
			auto candidate = Scripting::CreateManagedScriptRuntime(
				playerRoot / "Managed", assembly, pdb, playerRoot / "dotnet",
				&errorMessage);
			if (!candidate)
			{
				if (errorMessage.empty())
					errorMessage = "managed host initialization failed";
				return false;
			}
			std::string manifest;
			if (!candidate->ReadProjectMetadata(manifest) || manifest.empty())
			{
				errorMessage =
					"packaged C# metadata could not be loaded from Assembly-CSharp.dll";
				return false;
			}
			runtime = std::move(candidate);
			errorMessage.clear();
			return true;
		}

	}

	bool ValidateMountedPlayerManagedRuntime(std::string& errorMessage)
	{
		std::shared_ptr<Scripting::IScriptRuntime> runtime;
		return CreateMountedManagedRuntime(runtime, errorMessage);
	}

	PlayerRuntimeLayer::PlayerRuntimeLayer(std::filesystem::path packagePath)
		: Layer("PlayerRuntimeLayer"), m_PackagePath(std::move(packagePath))
	{
	}

	void PlayerRuntimeLayer::OnAttach()
	{
		AssetManager& assets = AssetManager::Get();
		if (!assets.MountCookedPackage(m_PackagePath))
		{
			Fail("could not mount cooked package '" + PathToUTF8(m_PackagePath) + "'", 4);
			return;
		}

		Scripting::ScriptEngine::Get().SetRuntime({});
		std::shared_ptr<Scripting::IScriptRuntime> runtime;
		std::string runtimeError;
		if (!CreateMountedManagedRuntime(runtime, runtimeError))
		{
			Fail("could not initialize the packaged C# runtime: " + runtimeError);
			return;
		}
		if (runtime)
			Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));

		m_SceneManager = CreateScope<SceneManager>();
		Window& window = Application::Get().GetWindow();
		m_SceneManager->SetViewportSize(window.GetWidth(), window.GetHeight());
		if (!m_SceneManager->ConfigureCookedPackage()
			|| !m_SceneManager->ActivateRuntime()
			|| !m_SceneManager->LoadEntryScene())
		{
			Fail("could not start the entry Scene runtime: "
				+ m_SceneManager->GetLastError());
			return;
		}
		const AssetHandle entryScene = m_SceneManager->GetActiveSceneHandle();
		TC_Core_Info("TomCat Player started '{0}' (entry Scene {1})",
			PathToUTF8(assets.GetCookedPackagePath()),
			static_cast<uint64_t>(entryScene));
	}

	void PlayerRuntimeLayer::OnDetach()
	{
		if (m_SceneManager)
			m_SceneManager->Stop();
		m_SceneManager.reset();
		Scripting::ScriptEngine::Get().SetRuntime({});
		AssetManager::Get().UnmountCookedPackage();
	}

	void PlayerRuntimeLayer::OnUpdate(Timestep timestep)
	{
		Scripting::ScriptEngine::Get().CaptureInputState();
		if (!m_SceneManager)
			return;
		if (const Ref<Scene> scene = m_SceneManager->GetActiveScene())
			scene->OnUpdateRuntime(timestep);
		if (!m_SceneManager->CommitPendingTransition()
			&& !m_SceneManager->GetActiveScene())
			Fail("Scene transition failed without a recoverable active Scene: "
				+ m_SceneManager->GetLastError());
	}

	void PlayerRuntimeLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(
			TC_Bind_Event_Fn(PlayerRuntimeLayer::OnWindowResize));
	}

	bool PlayerRuntimeLayer::OnWindowResize(WindowResizeEvent& event)
	{
		if (m_SceneManager)
			m_SceneManager->SetViewportSize(event.GetWidth(), event.GetHeight());
		return false;
	}

	void PlayerRuntimeLayer::Fail(const std::string& message, int exitCode)
	{
		TC_Core_Error("TomCat Player: {0}", message);
		if (m_SceneManager)
			m_SceneManager->Stop();
		m_SceneManager.reset();
		Scripting::ScriptEngine::Get().SetRuntime({});
		AssetManager::Get().UnmountCookedPackage();
		Application::Get().Close(exitCode);
	}

}
