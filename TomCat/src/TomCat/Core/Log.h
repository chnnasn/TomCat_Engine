#pragma once

#include "Base.h"
#include "ApplicationPaths.h"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include <cstddef>


namespace TomCat {

	class Log
	{
	public:
		// Returns true when the file sink was created. Console logging remains
		// available when LocalAppData cannot be resolved or written.
		static bool Init(ApplicationProduct product = ApplicationProduct::Unknown,
			const std::optional<std::filesystem::path>& localAppDataOverride = std::nullopt);
		// Rebinds both loggers to an explicit product log. The bounded rotating
		// sink retains MaximumFiles archives in addition to the active file.
		static bool InitFile(const std::filesystem::path& logFile,
			size_t maximumFileSize = 10 * 1024 * 1024,
			size_t maximumFiles = 5);
		static void Flush();
		static void Shutdown();

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

	private:
		static bool InitWithFile(
			const std::optional<std::filesystem::path>& logFile,
			size_t maximumFileSize, size_t maximumFiles,
			std::string fileLoggingError = {});

		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;

	};
}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}


//服务端日志宏
#define TC_Core_Error(...) TomCat::Log::GetCoreLogger()->error(__VA_ARGS__)
#define TC_Core_Warn(...) TomCat::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define TC_Core_Info(...) TomCat::Log::GetCoreLogger()->info(__VA_ARGS__)
#define TC_Core_Trace(...) TomCat::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define TC_Core_Fatal(...) TomCat::Log::GetCoreLogger()->fatal(__VA_ARGS__)

//客户端日志宏
#define TC_Error(...) TomCat::Log::GetClientLogger()->error(__VA_ARGS__)
#define TC_Warn(...) TomCat::Log::GetClientLogger()->warn(__VA_ARGS__)
#define TC_Info(...) TomCat::Log::GetClientLogger()->info(__VA_ARGS__)
#define TC_Trace(...) TomCat::Log::GetClientLogger()->trace(__VA_ARGS__)
#define TC_Fatal(...) TomCat::Log::GetClientLogger()->fatal(__VA_ARGS__)

