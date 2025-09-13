#pragma once

#include "TomCat/KeyCodes.h"
#include "TomCat/MouseButtonCodes.h"

#ifdef TC_PLAYTFORM_WINDOWS
	#ifdef TC_BUILD_DLL
		#define TomCat_API _declspec(dllexport)
	#else
		#define TomCat_API _declspec(dllimport)	
	#endif
#else
	#error TomCat only support windows!
#endif

#ifdef TC_Core_Assert
		#define TC_Assert(x, ...) { if(!(x)) { TC_Error("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
		#define TC_Core_Assert(x, ...) { if(!(x)) { TC_Core_Error("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
		#define TC_Assert(x, ...)
		#define TC_Core_Assert(x, ...)
#endif


#define BIT(x) (1<<x)

#define TC_Bind_Event_Fn(x) std::bind(&x,this,std::placeholders::_1)

