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

	struct PlayerAutomationOptions
	{
		std::filesystem::path RenderDiagnosticsPath;
		// Captured after frame Frames renders; the Player then exits.
		std::filesystem::path ScreenshotPath;
		// 0 runs until the game quits.
		uint32_t Frames = 0;
	};

	class PlayerRuntimeLayer final : public Layer
	{
	public:
		explicit PlayerRuntimeLayer(std::filesystem::path packagePath,
			uint32_t viewportWidth = 1280, uint32_t viewportHeight = 720,
			PlayerAutomationOptions automation = {});

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& event) override;

	private:
		bool OnWindowResize(WindowResizeEvent& event);
		void Fail(const std::string& message, int exitCode = 5);
		void WriteRenderDiagnostics();
		bool WriteScreenshot(std::string& error);

	private:
		std::filesystem::path m_PackagePath;
		uint32_t m_ViewportWidth = 1280;
		uint32_t m_ViewportHeight = 720;
		Scope<SceneManager> m_SceneManager;
		std::filesystem::path m_RenderDiagnosticsPath;
		std::filesystem::path m_ScreenshotPath;
		uint32_t m_ExitAfterFrames = 0;
		uint64_t m_Frame = 0;
	};

}
