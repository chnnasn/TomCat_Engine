#pragma once

#include "Base.h"
#include "ApplicationPaths.h"

#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>


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


namespace TomCat::Detail {

	// Compile-time checks on log format strings, on top of fmt's own
	// validation of {} placeholders. fmt accepts arguments the format string
	// never uses, so a printf-style string such as "loaded %s" compiled and
	// logged the literal "%s". Two mistakes are rejected at compile time:
	//   - a printf conversion (%s, %d, %.2f, %zu, ...); write {} instead, and
	//     %% for a literal percent sign before a conversion letter;
	//   - more arguments than the placeholders consume.
	// fmt::runtime(...) strings skip these checks, as with fmt itself.
	consteval bool IsPrintfConversion(std::string_view text, size_t percent)
	{
		size_t index = percent + 1;
		if (index < text.size() && text[index] == '%')
			return false;
		while (index < text.size() && (text[index] == '-' || text[index] == '+'
			|| text[index] == '#' || text[index] == '0'))
			++index;
		while (index < text.size() && text[index] >= '0' && text[index] <= '9')
			++index;
		if (index < text.size() && text[index] == '.')
		{
			++index;
			while (index < text.size() && text[index] >= '0' && text[index] <= '9')
				++index;
		}
		while (index < text.size() && (text[index] == 'h' || text[index] == 'l'
			|| text[index] == 'z' || text[index] == 'j' || text[index] == 't'
			|| text[index] == 'L'))
			++index;
		if (index >= text.size())
			return false;
		constexpr std::string_view conversions = "diouxXeEfFgGaAcsp";
		if (conversions.find(text[index]) == std::string_view::npos)
			return false;
		// A conversion ends a token; "%APPDATA%", "%Logs%" or "%systemroot%"
		// continue with letters and are environment-variable text, not printf.
		const size_t next = index + 1;
		if (next >= text.size())
			return true;
		const char after = text[next];
		return !((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z')
			|| (after >= '0' && after <= '9') || after == '_');
	}

	consteval bool HasPrintfConversion(std::string_view text)
	{
		for (size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] != '%')
				continue;
			if (index + 1 < text.size() && text[index + 1] == '%')
			{
				++index;
				continue;
			}
			if (IsPrintfConversion(text, index))
				return true;
		}
		return false;
	}

	// Number of arguments the placeholders reference: automatic {} count, or
	// the highest positional {N} plus one.
	consteval size_t ReferencedArgumentCount(std::string_view text)
	{
		size_t automatic = 0;
		size_t positional = 0;
		for (size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] != '{')
				continue;
			if (index + 1 < text.size() && text[index + 1] == '{')
			{
				++index;
				continue;
			}
			size_t cursor = index + 1;
			if (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
			{
				size_t value = 0;
				while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
					value = value * 10 + static_cast<size_t>(text[cursor++] - '0');
				positional = value + 1 > positional ? value + 1 : positional;
			}
			else
				++automatic;
		}
		return automatic > positional ? automatic : positional;
	}

	// Thrown only during constant evaluation, where it becomes a compile
	// error naming this function.
	inline void LogFormatUsesPrintfConversion_UseBracesInstead() {}
	inline void LogFormatHasMoreArgumentsThanPlaceholders() {}

	template<typename... Args>
	struct CheckedLogFormat
	{
		fmt::format_string<Args...> Format;

		template<typename S>
			requires std::is_convertible_v<const S&, std::string_view>
		consteval CheckedLogFormat(const S& text)
			: Format(text)
		{
			const std::string_view view(text);
			if (HasPrintfConversion(view))
				LogFormatUsesPrintfConversion_UseBracesInstead();
			if (ReferencedArgumentCount(view) < sizeof...(Args))
				LogFormatHasMoreArgumentsThanPlaceholders();
		}

		CheckedLogFormat(fmt::runtime_format_string<char> text)
			: Format(text)
		{
		}
	};

	template<typename... Args>
	using LogFormat = CheckedLogFormat<std::type_identity_t<Args>...>;

	template<typename... Args>
	inline void Log(const Ref<spdlog::logger>& logger, spdlog::level::level_enum level,
		LogFormat<Args...> format, Args&&... args)
	{
		logger->log(level, format.Format, std::forward<Args>(args)...);
	}

	// A single message is logged as-is, so runtime strings stay allowed.
	template<typename T>
	inline void Log(const Ref<spdlog::logger>& logger, spdlog::level::level_enum level,
		const T& message)
	{
		logger->log(level, message);
	}

}

//服务端日志宏
#define TC_Core_Error(...) ::TomCat::Detail::Log(::TomCat::Log::GetCoreLogger(), spdlog::level::err, __VA_ARGS__)
#define TC_Core_Warn(...) ::TomCat::Detail::Log(::TomCat::Log::GetCoreLogger(), spdlog::level::warn, __VA_ARGS__)
#define TC_Core_Info(...) ::TomCat::Detail::Log(::TomCat::Log::GetCoreLogger(), spdlog::level::info, __VA_ARGS__)
#define TC_Core_Trace(...) ::TomCat::Detail::Log(::TomCat::Log::GetCoreLogger(), spdlog::level::trace, __VA_ARGS__)
#define TC_Core_Fatal(...) ::TomCat::Detail::Log(::TomCat::Log::GetCoreLogger(), spdlog::level::critical, __VA_ARGS__)

//客户端日志宏
#define TC_Error(...) ::TomCat::Detail::Log(::TomCat::Log::GetClientLogger(), spdlog::level::err, __VA_ARGS__)
#define TC_Warn(...) ::TomCat::Detail::Log(::TomCat::Log::GetClientLogger(), spdlog::level::warn, __VA_ARGS__)
#define TC_Info(...) ::TomCat::Detail::Log(::TomCat::Log::GetClientLogger(), spdlog::level::info, __VA_ARGS__)
#define TC_Trace(...) ::TomCat::Detail::Log(::TomCat::Log::GetClientLogger(), spdlog::level::trace, __VA_ARGS__)
#define TC_Fatal(...) ::TomCat::Detail::Log(::TomCat::Log::GetClientLogger(), spdlog::level::critical, __VA_ARGS__)

