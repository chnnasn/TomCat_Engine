#include "tcpch.h"
#include "Log.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace TomCat {

	std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
	std::shared_ptr<spdlog::logger> Log::s_ClinetLogger;
	void Log::Init() {

		spdlog::set_pattern("%^[%T] %n: %v%$");//颜色 时间戳 日志名 问题

		s_CoreLogger = spdlog::stdout_color_mt("TOMCAT");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClinetLogger = spdlog::stdout_color_mt("APP");
		s_ClinetLogger->set_level(spdlog::level::trace);
	}

}