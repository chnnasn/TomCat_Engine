#pragma once

#include <memory>

#include "Core.h"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

namespace TomCat {
	class TomCat_API Log
	{
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger;}
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClinetLogger;}

	private:
		static std::shared_ptr<spdlog::logger> s_CoreLogger;
		static std::shared_ptr<spdlog::logger> s_ClinetLogger;

	};
}


//服务端日志宏
#define TC_Core_Error(...) ;;TomCat::Log::GetCoreLogger()->error(__VA_ARGS__);
#define TC_Core_Warn(...) ;;TomCat::Log::GetCoreLogger()->warn(__VA_ARGS__);
#define TC_Core_Info(...) ;;TomCat::Log::GetCoreLogger()->info(__VA_ARGS__);
#define TC_Core_Trace(...) ;;TomCat::Log::GetCoreLogger()->trace(__VA_ARGS__);
#define TC_Core_Fatal(...) ;;TomCat::Log::GetCoreLogger()->fatal(__VA_ARGS__);

//客户端日志宏
#define TC_Error(...) ;;TomCat::Log::GetClientLogger()->error(__VA_ARGS__);
#define TC_Warn(...) ;;TomCat::Log::GetClientLogger()->warn(__VA_ARGS__);
#define TC_Info(...) ;;TomCat::Log::GetClientLogger()->info(__VA_ARGS__);
#define TC_Trace(...) ;;TomCat::Log::GetClientLogger()->trace(__VA_ARGS__);
#define TC_Fatal(...) ;;TomCat::Log::GetClientLogger()->fatal(__VA_ARGS__);

