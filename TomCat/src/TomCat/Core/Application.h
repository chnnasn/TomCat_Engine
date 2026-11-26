#pragma once

#include <map>
#include <string>
#include "Base.h"

#include "Window.h"
#include "TomCat/Core/LayerStack.h"
#include "TomCat/Events/Event.h"
#include "TomCat/Events/ApplicationEvent.h"

#include "TomCat/ImGui/ImGuiLayer.h"

#include "TomCat/Core/TimeStep.h"

#ifdef TC_PLATFORM_WINDOWS
int wmain(int argc, wchar_t** argv);
#else
int main(int argc, char** argv);
#endif

namespace TomCat {

	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			TC_Core_Assert(index < Count);
			return Args[index];
		}
	};

	class Application
	{ 
	public :
		Application(const std::string& name = "TomCat App",
					std::filesystem::path iconPath = {}, bool enableImGui = true,
					bool createWindow = true);
		Application(WindowProps windowProps, bool enableImGui,
			bool createWindow = true);

		virtual ~Application();


		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);


		Window& GetWindow()
		{
			TC_Core_Assert(m_Window);
			return *m_Window;
		}
		bool HasWindow() const { return m_Window != nullptr; }

		void Close(int exitCode = 0);

		// Key/value pairs from the Player's --test-arg options. Scripts read them
		// through Application.GetTestArgument to drive engine-level test hooks
		// (auto-load a level, skip menus) without ad-hoc environment variables.
		void SetTestArguments(std::map<std::string, std::string> arguments)
		{
			m_TestArguments = std::move(arguments);
		}
		const std::map<std::string, std::string>& GetTestArguments() const
		{
			return m_TestArguments;
		}

		// When set (> 0), every frame advances by exactly this many seconds
		// instead of wall-clock time, so frame N is reproducible for screenshots
		// and E2E checks. Zero restores wall-clock timing.
		void SetFixedFrameDelta(float seconds) { m_FixedFrameDelta = seconds > 0.0f ? seconds : 0.0f; }
		float GetFixedFrameDelta() const { return m_FixedFrameDelta; }
		int GetExitCode() const { return m_ExitCode; }

		// Host-driven frame entry for browser requestAnimationFrame and embedded players.
		bool Tick(float deltaSeconds);

		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; };

		static Application& Get() { return *s_Instance; }
		static Application* TryGet() { return s_Instance; }

	protected:
		virtual void OnFirstFramePresented() {}
	private:
		void Run();
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

	private:
		Scope<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer = nullptr;
		bool m_Running = true;
		bool m_Minimized = false;
		bool m_FirstFramePresented = false;
		bool m_RendererInitialized = false;
		int m_ExitCode = 0;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;
		std::map<std::string, std::string> m_TestArguments;
		float m_FixedFrameDelta = 0.0f;

	private:
		static Application* s_Instance;
#ifdef TC_PLATFORM_WINDOWS
		friend int ::wmain(int argc, wchar_t** argv);
#else
		friend int ::main(int argc, char** argv);
#endif
	};


	//客户端定义
	Application* CreateApplication(ApplicationCommandLineArgs args);

}

