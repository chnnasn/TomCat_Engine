#pragma once

#include "Core.h"

namespace TomCat {
	class TomCat_API Application
	{ 
	public :
		Application();


		virtual ~Application();


		void Run();
	};


	//客户端定义
	Application* CreateApplication();

}

