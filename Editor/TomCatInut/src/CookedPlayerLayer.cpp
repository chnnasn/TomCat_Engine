#include "CookedPlayerLayer.h"

#include <TomCat/Scene/SceneSerializer.h>
#include <TomCat/Utils/PathUtils.h>

namespace TomCat {

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

		Window& window = Application::Get().GetWindow();
		m_RuntimeScene->OnViewportResize(window.GetWidth(), window.GetHeight());
		m_RuntimeScene->OnRuntimeStart();
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
		m_RuntimeScene.reset();
		AssetManager::Get().UnmountCookedPackage();
	}

	void CookedPlayerLayer::OnUpdate(Timestep timestep)
	{
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
		m_RuntimeScene.reset();
		m_RuntimeStarted = false;
		AssetManager::Get().UnmountCookedPackage();
		Application::Get().Close(1);
	}

}
