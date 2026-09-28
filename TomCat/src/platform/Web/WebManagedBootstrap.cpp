#include "tcpch.h"

#ifdef __EMSCRIPTEN__
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include <emscripten/emscripten.h>

extern "C" EMSCRIPTEN_KEEPALIVE int tc_web_managed_install(
	TomCat::Scripting::GetManagedApiFn getManagedApi)
{
	std::string error;
	if (TomCat::Scripting::InstallWebManagedApi(getManagedApi, &error))
		return 0;
	TC_Core_Error("Could not install the bundled .NET WebAssembly runtime: {0}", error);
	return 1;
}
#endif
