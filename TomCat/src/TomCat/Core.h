#pragma once
#ifdef TC_PLAYTFORM_WINDOWS
	#ifdef TC_BUILD_DLL
		#define TomCat_API _declspec(dllexport)
	#else
		#define TomCat_API _declspec(dllimport)	
	#endif
#else
	#error TomCat only support windows!
#endif

