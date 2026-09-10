#pragma once
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
					std::filesystem::path iconPath = {});

		virtual ~Application();


		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);


		Window& GetWindow() { return *m_Window; }

		void Close();

		ImGuiLayer* GetImGuiLayer() { return m_ImGuiLayer; };

		static Application& Get() { return *s_Instance; }

	private:
		void Run();
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

	private:
		Scope<Window> m_Window;
		ImGuiLayer* m_ImGuiLayer = nullptr;
		bool m_Running = true;
		bool m_Minimized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;

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

