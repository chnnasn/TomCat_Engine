#pragma once

#include "ApplicationPaths.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace TomCat {

	class CrashReporter
	{
	public:
		// Installs the process-level terminate/native exception handlers. Before
		// a game package is mounted, reports use the generic product data root.
		static bool Install(ApplicationProduct product);

		// Rebinds reports to the active game's configured Crash directory and
		// records product identity in every report.
		static bool Configure(const std::filesystem::path& crashDirectory,
			std::string companyName, std::string productName,
			std::string version);

		// Writes a best-effort text report for an exception caught by the outer
		// application boundary. Native unhandled exceptions additionally emit a
		// minidump on Windows.
		[[nodiscard]] static std::filesystem::path WriteReport(
			std::string_view reason) noexcept;
		static void Uninstall() noexcept;
	};

}
