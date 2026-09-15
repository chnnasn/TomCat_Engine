#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace TomCat {

	enum class ApplicationProduct
	{
		Unknown,
		Editor,
		Hub,
		Player
	};

	struct GameDataPaths
	{
		std::filesystem::path Root;
		std::filesystem::path Saves;
		std::filesystem::path Logs;
		std::filesystem::path Crashes;

		bool operator==(const GameDataPaths&) const = default;
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

		// Packaged Editors publish immutable child-process/runtime files below this
		// LocalAppData cache. The active root is set only after a complete bundle has
		// been validated and atomically published; development builds leave it unset.
		[[nodiscard]] static std::optional<std::filesystem::path>
			ResolveEditorRuntimeCacheRoot(const std::filesystem::path& localAppData);
		[[nodiscard]] static std::optional<std::filesystem::path>
			GetEditorRuntimeCacheRoot();
		static void SetRuntimeEditorRoot(const std::filesystem::path& root);
		static void ClearRuntimeEditorRoot();
		[[nodiscard]] static std::optional<std::filesystem::path>
			GetRuntimeEditorRoot();

		// A game's data is isolated from the generic TomCatPlayer host and from
		// every other game. Company/product are single path segments; the three
		// configured directories must remain relative to the game root.
		[[nodiscard]] static std::optional<GameDataPaths> ResolveGameDataPaths(
			const std::filesystem::path& localAppData,
			std::string_view companyName,
			std::string_view productName,
			const std::filesystem::path& saveDirectory,
			const std::filesystem::path& logDirectory,
			const std::filesystem::path& crashDirectory);
		[[nodiscard]] static std::optional<GameDataPaths> GetGameDataPaths(
			std::string_view companyName,
			std::string_view productName,
			const std::filesystem::path& saveDirectory,
			const std::filesystem::path& logDirectory,
			const std::filesystem::path& crashDirectory);

		// Player publishes the active paths once its package manifest has been
		// validated. Runtime systems can then save without depending on PlayerApp.
		static void SetRuntimeGameDataPaths(const GameDataPaths& paths);
		static void ClearRuntimeGameDataPaths();
		[[nodiscard]] static std::optional<GameDataPaths> GetRuntimeGameDataPaths();
		[[nodiscard]] static std::optional<std::filesystem::path>
			GetRuntimeSaveDirectory();
		[[nodiscard]] static std::optional<std::filesystem::path>
			GetRuntimeLogDirectory();
		[[nodiscard]] static std::optional<std::filesystem::path>
			GetRuntimeCrashDirectory();
	};

}
