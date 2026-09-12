#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace TomCat {

	enum class ApplicationProduct
	{
		Unknown,
		Editor,
		Hub,
		Player
	};

	class ApplicationPaths
	{
	public:
		[[nodiscard]] static std::string_view GetProductDirectoryName(
			ApplicationProduct product);
		[[nodiscard]] static ApplicationProduct IdentifyExecutable(
			const std::filesystem::path& executablePath);
		[[nodiscard]] static ApplicationProduct IdentifyCurrentExecutable();

		// Pure path construction kept separate from OS discovery so startup and
		// regression tests can prove that user data never resolves beside an exe.
		[[nodiscard]] static std::optional<std::filesystem::path> ResolveProductDataRoot(
			const std::filesystem::path& localAppData,
			ApplicationProduct product);
		[[nodiscard]] static std::optional<std::filesystem::path> GetLocalAppDataRoot();
		[[nodiscard]] static std::optional<std::filesystem::path> GetProductDataRoot(
			ApplicationProduct product);
		[[nodiscard]] static std::optional<std::filesystem::path> GetLogFile(
			ApplicationProduct product);
	};

}
