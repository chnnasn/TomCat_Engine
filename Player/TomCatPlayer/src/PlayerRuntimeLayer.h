#pragma once

#include <TomCat.h>
#include <TomCat/Scene/SceneManager.h>

#include <filesystem>
#include <string>

namespace TomCat {

	// Validates the managed payload of the currently mounted package against the
	// runtime files beside TomCatPlayer.exe. Script-free packages succeed without
	// initializing CoreCLR.
	bool ValidateMountedPlayerManagedRuntime(std::string& errorMessage);

	class PlayerRuntimeLayer final : public Layer
	{
	public:
		explicit PlayerRuntimeLayer(std::filesystem::path packagePath);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& event) override;

	private:
		bool OnWindowResize(WindowResizeEvent& event);
		void Fail(const std::string& message, int exitCode = 5);

	private:
		std::filesystem::path m_PackagePath;
		Scope<SceneManager> m_SceneManager;
	};

}
