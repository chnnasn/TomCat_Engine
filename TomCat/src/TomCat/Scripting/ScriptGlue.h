#pragma once

#include "ScriptTypes.h"

namespace TomCat::Scripting {

	// Builds the process-lifetime table passed to TomCat.ScriptHost. Every callback
	// catches native exceptions and returns a ScriptStatus-compatible integer.
	NativeApiV1 BuildNativeApiV1();
	NativeApiV2 BuildNativeApiV2();

	// Internal safe-point hook used by ScriptEngine after a reserved entity and
	// its queued component additions have become live.
	int32_t ApplyGameplayComponentPropertyNow(EntityHandleV1 entity,
		NativeComponentType componentType, uint32_t propertyId,
		NativePropertyValueV1 value) noexcept;
	int32_t ApplyRegisteredComponentPropertyNow(EntityHandleV1 entity,
		uint64_t componentTypeId, uint64_t propertyId,
		NativePropertyValueV1 value) noexcept;
	int32_t ApplyRegisteredComponentStringPropertyNow(EntityHandleV1 entity,
		uint64_t componentTypeId, uint64_t propertyId,
		NativeUtf8View value) noexcept;

}
