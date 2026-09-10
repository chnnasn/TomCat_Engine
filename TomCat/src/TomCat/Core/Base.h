#pragma once

#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/MouseCodes.h"

#include <cstdio>
#include <memory>

#ifdef _MSC_VER
	#include <intrin.h>
#endif

// Platform detection using predefined macros
#ifdef _WIN32
	/* Windows x64/x86 */
	#ifdef _WIN64
		/* Windows x64  */
		#ifndef TC_PLATFORM_WINDOWS
			#define TC_PLATFORM_WINDOWS
		#endif
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

#if defined(TC_DEBUG) && !defined(TC_ENABLE_ASSERTS)
	#define TC_ENABLE_ASSERTS
#endif

namespace TomCat::Detail {

	inline void ReportAssertionFailure(const char* expression, const char* file, int line)
	{
		std::fprintf(stderr, "Assertion failed: %s (%s:%d)\n", expression, file, line);
	}

	inline void ReportAssertionFailure(const char* expression, const char* file, int line, const char* message)
	{
		if (message && *message)
			std::fprintf(stderr, "Assertion failed: %s - %s (%s:%d)\n", expression, message, file, line);
		else
			ReportAssertionFailure(expression, file, line);
	}

}

#ifdef TC_ENABLE_ASSERTS
	#define TC_Assert(x, ...) do { if (!(x)) { ::TomCat::Detail::ReportAssertionFailure(#x, __FILE__, __LINE__, "" __VA_ARGS__); __debugbreak(); } } while (false)
	#define TC_Core_Assert(x, ...) TC_Assert(x, __VA_ARGS__)
#else
	#define TC_Assert(x, ...) ((void)0)
	#define TC_Core_Assert(x, ...) ((void)0)
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

