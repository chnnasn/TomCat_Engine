#pragma once

#include <TomCat.h>

#include <filesystem>

namespace TomCat {

	// Minimal shipping-player layer. Its only filesystem input is a cooked package;
	// scenes and their dependencies are resolved by AssetHandle from that package.
	class CookedPlayerLayer final : public Layer
	{
	public:
		explicit CookedPlayerLayer(std::filesystem::path packagePath);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& event) override;

	private:
		bool OnWindowResize(WindowResizeEvent& event);
		void Fail(const std::string& message);

	private:
		std::filesystem::path m_PackagePath;
		Ref<Scene> m_RuntimeScene;
		bool m_RuntimeStarted = false;
	};

}
