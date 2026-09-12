#include "CookedPlayerLayer.h"

#include <TomCat/Scene/SceneSerializer.h>
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
			ScopedShadowDirectory() = default;
			~ScopedShadowDirectory()
			{
				if (!m_Path.empty())
				{
					std::error_code cleanupError;
					std::filesystem::remove_all(m_Path, cleanupError);
				}
			}

			ScopedShadowDirectory(const ScopedShadowDirectory&) = delete;
			ScopedShadowDirectory& operator=(const ScopedShadowDirectory&) = delete;

			bool Create(std::string& errorMessage)
			{
				std::error_code filesystemError;
				const std::filesystem::path temporaryRoot =
					std::filesystem::temp_directory_path(filesystemError);
				if (filesystemError || temporaryRoot.empty())
				{
					errorMessage = "the operating-system temporary directory is unavailable";
					return false;
				}

				for (uint32_t attempt = 0; attempt < 64; ++attempt)
				{
					filesystemError.clear();
					const std::filesystem::path candidate = temporaryRoot
						/ ("TomCatPlayer-" + std::to_string(static_cast<uint64_t>(UUID())));
					if (std::filesystem::create_directory(candidate, filesystemError))
					{
						m_Path = candidate;
						return true;
					}
					if (filesystemError
						&& filesystemError != std::errc::file_exists)
					{
						errorMessage = "could not create a private managed Shadow directory: "
							+ filesystemError.message();
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

		bool WriteManagedPayloadFile(const std::filesystem::path& path,
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

	}

	CookedPlayerLayer::CookedPlayerLayer(std::filesystem::path packagePath)
		: Layer("CookedPlayerLayer"), m_PackagePath(std::move(packagePath))
	{
	}

	void CookedPlayerLayer::OnAttach()
	{
		AssetManager& assetManager = AssetManager::Get();
		if (m_PackagePath.empty() || !assetManager.MountCookedPackage(m_PackagePath))
		{
			Fail("Could not mount cooked package '" + PathToUTF8(m_PackagePath) + "'");
			return;
		}

		const AssetHandle startScene = assetManager.GetCookedStartSceneHandle();
		if (static_cast<uint64_t>(startScene) == 0)
		{
			Fail("Cooked package has no start-scene AssetHandle");
			return;
		}

		m_RuntimeScene = CreateRef<Scene>();
		SceneSerializer serializer(m_RuntimeScene);
		if (!serializer.Deserialize(startScene))
		{
			Fail("Could not deserialize the cooked start scene");
			return;
		}
		m_RuntimeScene->SetPhysics2DSettings(assetManager.GetPhysics2DSettings());

		Window& window = Application::Get().GetWindow();
		m_RuntimeScene->OnViewportResize(window.GetWidth(), window.GetHeight());

		Scripting::ScriptEngine::Get().SetRuntime({});
		if (const ManagedPackagePayload* payload = assetManager.GetCookedManagedPayload())
		{
			ScopedShadowDirectory shadowDirectory;
			std::string runtimeError;
			if (!shadowDirectory.Create(runtimeError))
			{
				Fail("Could not stage the packaged C# assembly: " + runtimeError);
				return;
			}

			const std::filesystem::path assemblyPath =
				shadowDirectory.GetPath() / "Assembly-CSharp.dll";
			if (!WriteManagedPayloadFile(assemblyPath, payload->Assembly, runtimeError))
			{
				Fail("Could not stage the packaged C# assembly: " + runtimeError);
				return;
			}

			std::filesystem::path pdbPath;
			if (!payload->Pdb.empty())
			{
				pdbPath = shadowDirectory.GetPath() / "Assembly-CSharp.pdb";
				if (!WriteManagedPayloadFile(pdbPath, payload->Pdb, runtimeError))
				{
					Fail("Could not stage the packaged C# symbols: " + runtimeError);
					return;
				}
			}

			const std::filesystem::path playerDirectory =
				assetManager.GetCookedPackagePath().parent_path();
			auto runtime = Scripting::CreateManagedScriptRuntime(
				playerDirectory / "Managed", assemblyPath, pdbPath,
				playerDirectory / "dotnet", &runtimeError);
			if (!runtime)
			{
				if (runtimeError.empty())
					runtimeError = "managed host initialization failed";
				Fail("Could not initialize the packaged C# runtime: " + runtimeError);
				return;
			}

			std::string runtimeManifest;
			if (!runtime->ReadProjectMetadata(runtimeManifest))
			{
				Fail("Packaged C# metadata could not be loaded from Assembly-CSharp.dll");
				return;
			}
			Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		}

		if (!m_RuntimeScene->OnRuntimeStart())
		{
			Fail("Could not start the cooked scene runtime");
			return;
		}
		m_RuntimeStarted = true;
		TC_Core_Info("Playing cooked package '{0}' (start scene {1})",
			PathToUTF8(assetManager.GetCookedPackagePath()),
			static_cast<uint64_t>(startScene));
	}

	void CookedPlayerLayer::OnDetach()
	{
		if (m_RuntimeScene && m_RuntimeStarted)
			m_RuntimeScene->OnRuntimeStop();
		m_RuntimeStarted = false;
		Scripting::ScriptEngine::Get().SetRuntime({});
		m_RuntimeScene.reset();
		AssetManager::Get().UnmountCookedPackage();
	}

	void CookedPlayerLayer::OnUpdate(Timestep timestep)
	{
		Scripting::ScriptEngine::Get().CaptureInputState();
		if (m_RuntimeScene && m_RuntimeStarted)
			m_RuntimeScene->OnUpdateRuntime(timestep);
	}

	void CookedPlayerLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowResizeEvent>(TC_Bind_Event_Fn(CookedPlayerLayer::OnWindowResize));
	}

	bool CookedPlayerLayer::OnWindowResize(WindowResizeEvent& event)
	{
		if (m_RuntimeScene && event.GetWidth() > 0 && event.GetHeight() > 0)
			m_RuntimeScene->OnViewportResize(event.GetWidth(), event.GetHeight());
		return false;
	}

	void CookedPlayerLayer::Fail(const std::string& message)
	{
		TC_Core_Error("Cooked Player: {0}", message);
		if (m_RuntimeScene && m_RuntimeStarted)
			m_RuntimeScene->OnRuntimeStop();
		m_RuntimeScene.reset();
		m_RuntimeStarted = false;
		Scripting::ScriptEngine::Get().SetRuntime({});
		AssetManager::Get().UnmountCookedPackage();
		Application::Get().Close(1);
	}

}
