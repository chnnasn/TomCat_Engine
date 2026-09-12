#pragma once

#include "ScriptTypes.h"

namespace TomCat::Scripting {

	// Builds the process-lifetime table passed to TomCat.ScriptHost. Every callback
	// catches native exceptions and returns a ScriptStatus-compatible integer.
	NativeApiV1 BuildNativeApiV1();

}
