#pragma once

#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/MouseCodes.h"

#include <memory>

// Platform detection using predefined macros
#ifdef _WIN32
	/* Windows x64/x86 */
	#ifdef _WIN64
		/* Windows x64  */
		#define TC_PLATFORM_WINDOWS
	#else
		/* Windows x86 */
		#error "x86 Builds are not supported!"
	#endif
#elif defined(__APPLE__) || defined(__MACH__)
	#include <TargetConditionals.h>
	/* TARGET_OS_MAC exists on all the platforms
	* so we must check all of them (in this order)
	* to ensure that we're running on MAC
	* and not some other Apple platform */
	#if TARGET_IPHONE_SIMULATOR == 1
		#error "IOS simulator is not supported!"
	#elif TARGET_OS_IPHONE == 1
		#define TC_PLATFORM_IOS
		#error "IOS is not supported!"
	#elif TARGET_OS_MAC == 1
		#define TC_PLATFORM_MACOS
		#error "MacOS is not supported!"
	#else
		#error "Unknown Apple platform!"
	#endif
/* We also have to check __ANDROID__ before __linux__
 * since android is based on the linux kernel
 * it has __linux__ defined */
#elif defined(__ANDROID__)
	#define TC_PLATFORM_ANDROID
	#error "Android is not supported!"
#elif defined(__linux__)
	#define TC_PLATFORM_LINUX
	#error "Linux is not supported!"

#else
		/* Unknown compiler/platform */
	#error "Unknown platform!"
#endif // End of platform detection

// DLL support
#ifdef TC_PLATFORM_WINDOWS
	#if TC_DYNAMIC_LINK
		#ifdef TC_BUILD_DLL
			#define TomCat_API __declspec(dllexport)
		#else
			#define TomCat_API __declspec(dllimport)
		#endif
	#else
		#define TomCat_API
	#endif
#else
	#error TomCat only supports Windows!
#endif // End of DLL support

#ifdef TC_Core_Assert
		#define TC_Assert(x, ...) { if(!(x)) { TC_Error("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
		#define TC_Core_Assert(x, ...) { if(!(x)) { TC_Core_Error("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
		#define TC_Assert(x, ...)
		#define TC_Core_Assert(x, ...)
#endif


#define BIT(x) (1<<x)

#define TC_Bind_Event_Fn(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }


namespace TomCat {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}
}

