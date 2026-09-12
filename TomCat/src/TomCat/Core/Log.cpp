#include "tcpch.h"
#include "TomCat/Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace TomCat {

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;

	bool Log::Init(ApplicationProduct product,
		const std::optional<std::filesystem::path>& localAppDataOverride)
	{
		if (s_CoreLogger || s_ClientLogger)
			Shutdown();
		std::vector<spdlog::sink_ptr> logSinks;
		auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		consoleSink->set_pattern("%^[%T] %n: %v%$");
		logSinks.emplace_back(consoleSink);

		bool fileLoggingAvailable = false;
		std::string fileLoggingError;
		try
		{
			std::optional<std::filesystem::path> logPath;
			if (localAppDataOverride)
			{
				const auto root = ApplicationPaths::ResolveProductDataRoot(
					*localAppDataOverride, product);
				if (root)
					logPath = *root / "TomCat.log";
			}
			else
				logPath = ApplicationPaths::GetLogFile(product);

			if (logPath)
			{
				std::error_code directoryError;
				std::filesystem::create_directories(logPath->parent_path(), directoryError);
				if (directoryError)
					fileLoggingError = directoryError.message();
				else
				{
					auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
						logPath->string(), true);
					fileSink->set_pattern("[%T] [%l] %n: %v");
					logSinks.emplace_back(std::move(fileSink));
					fileLoggingAvailable = true;
				}
			}
			else if (product != ApplicationProduct::Unknown)
				fileLoggingError = "LocalAppData could not be resolved";
		}
		catch (const std::exception& exception)
		{
			fileLoggingError = exception.what();
		}

		s_CoreLogger = std::make_shared<spdlog::logger>("TomCat", begin(logSinks), end(logSinks));
		spdlog::register_logger(s_CoreLogger);
		s_CoreLogger->set_level(spdlog::level::trace);
		s_CoreLogger->flush_on(spdlog::level::trace);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", begin(logSinks), end(logSinks));
		spdlog::register_logger(s_ClientLogger);
		s_ClientLogger->set_level(spdlog::level::trace);
		s_ClientLogger->flush_on(spdlog::level::trace);

		if (!fileLoggingError.empty())
			s_CoreLogger->warn("File logging is unavailable; continuing with console logging: {0}",
				fileLoggingError);
		return fileLoggingAvailable;
	}

	void Log::Shutdown()
	{
		if (s_CoreLogger)
			s_CoreLogger->flush();
		if (s_ClientLogger)
			s_ClientLogger->flush();
		spdlog::drop("TomCat");
		spdlog::drop("APP");
		s_CoreLogger.reset();
		s_ClientLogger.reset();
	}

}
