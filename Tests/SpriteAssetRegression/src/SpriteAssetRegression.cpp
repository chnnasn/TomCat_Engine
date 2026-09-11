#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"

#include "stb_image.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryAssetProject final
	{
	public:
		TemporaryAssetProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path() /
				("tomcat_sprite_assets_" +
					std::to_string(static_cast<uint64_t>(TomCat::UUID())));
			Assets = Root / "Assets";
			Library = Root / "Library";
			std::filesystem::create_directories(Assets);
		}

		~TemporaryAssetProject()
		{
			// The process-global manager may retain the package's Windows file handle.
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
		std::filesystem::path Assets;
		std::filesystem::path Library;
	};

	std::string RequireSetting(const TomCat::AssetMetadata& metadata,
		const char* key)
	{
		const auto iterator = metadata.ImportSettings.find(key);
		Require(iterator != metadata.ImportSettings.end(),
			"primitive Sprite metadata omitted a required import setting");
		return iterator->second;
	}

	std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "primitive Sprite source could not be opened");
		const std::streamoff end = input.tellg();
		Require(end >= 0 && static_cast<uintmax_t>(end) <=
			static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()),
			"primitive Sprite source size is invalid");
		std::vector<uint8_t> bytes(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!bytes.empty())
			input.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(input) || bytes.empty(),
			"primitive Sprite source could not be read completely");
		return bytes;
	}

	void RequireDecodablePrimitive(const std::vector<uint8_t>& bytes,
		bool expectCircle)
	{
		Require(!bytes.empty() &&
			bytes.size() <= static_cast<size_t>((std::numeric_limits<int>::max)()),
			"primitive Sprite payload has an invalid decoder size");
		int width = 0;
		int height = 0;
		int sourceChannels = 0;
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(),
			static_cast<int>(bytes.size()), &width, &height, &sourceChannels, 4);
		Require(pixels != nullptr, "primitive Sprite payload is not decodable by stb_image");

		const bool dimensionsMatch = width == 64 && height == 64;
		const uint8_t cornerAlpha = pixels[3];
		const size_t center = (static_cast<size_t>(height / 2) * width + width / 2) * 4;
		const uint8_t centerAlpha = pixels[center + 3];
		stbi_image_free(pixels);

		Require(dimensionsMatch, "primitive Sprite dimensions changed");
		Require(centerAlpha == 255, "primitive Sprite center is not opaque");
		if (expectCircle)
			Require(cornerAlpha == 0, "Circle primitive corner is not transparent");
		else
			Require(cornerAlpha == 255, "Square primitive corner is not opaque");
	}

	std::vector<uint8_t> RequirePrimitive(TomCat::AssetRegistry& registry,
		TomCat::AssetHandle handle, const char* primitiveName)
	{
		Require(static_cast<uint64_t>(handle) != 0,
			"primitive Sprite has a zero AssetHandle");
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(handle);
		Require(metadata && metadata->Type == TomCat::AssetType::Texture2D &&
			!metadata->IsMissing, "primitive Sprite is not a live Texture2D asset");
		Require(RequireSetting(*metadata, "Usage") == "Sprite",
			"primitive Sprite Usage setting changed");
		Require(RequireSetting(*metadata, "Primitive") == primitiveName,
			"primitive Sprite kind setting changed");
		Require(RequireSetting(*metadata, "PixelsPerUnit") == "100",
			"primitive Sprite PixelsPerUnit setting changed");

		const std::filesystem::path source = registry.GetFileSystemPath(handle);
		std::error_code error;
		Require(std::filesystem::is_regular_file(source, error) && !error,
			"primitive Sprite source file was not created");
		std::vector<uint8_t> bytes = ReadFileBytes(source);
		RequireDecodablePrimitive(bytes, std::string_view(primitiveName) == "Circle");
		return bytes;
	}

	void TestPrimitiveSpriteAuthoringAndCookedPackage()
	{
		TemporaryAssetProject environment;

		// Builder creates starter assets through a standalone registry. Exercise
		// Square first because that was the reported failing menu action.
		TomCat::AssetRegistry builderRegistry;
		Require(builderRegistry.Initialize(environment.Assets, environment.Library),
			"Builder-style AssetRegistry initialization failed");
		const TomCat::AssetHandle square = TomCat::EnsurePrimitiveSpriteAsset(
			builderRegistry, "Square");
		const TomCat::AssetHandle circle = TomCat::EnsurePrimitiveSpriteAsset(
			builderRegistry, "Circle");
		Require(square != circle, "Square and Circle reused one AssetHandle");
		const std::vector<uint8_t> squareBytes = RequirePrimitive(
			builderRegistry, square, "Square");
		const std::vector<uint8_t> circleBytes = RequirePrimitive(
			builderRegistry, circle, "Circle");
		builderRegistry.Shutdown();

		// Editor opens the same project through AssetManager. Existing primitives
		// must be discovered by metadata and remain stable across refreshes.
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"Editor-style AssetManager initialization failed");
		Require(TomCat::FindPrimitiveSpriteAsset(assets.Registry(), "Square") == square &&
			TomCat::FindPrimitiveSpriteAsset(assets.Registry(), "Circle") == circle,
			"Editor did not recover Builder-created primitive handles");
		Require(TomCat::EnsurePrimitiveSpriteAsset(assets, "Square") == square &&
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle") == circle,
			"repeated primitive creation changed an AssetHandle");
		Require(assets.Refresh(), "primitive Sprite registry refresh failed");
		Require(TomCat::EnsurePrimitiveSpriteAsset(assets, "Square") == square &&
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle") == circle,
			"primitive handles changed after a registry refresh");

		const std::filesystem::path packagePath = environment.Root / "Build" / "Sprites.tcpak";
		Require(assets.CookToPackage(packagePath, TomCat::AssetHandle(0)),
			"primitive Sprites could not be cooked into a Player package");
		assets.Shutdown();

		Require(assets.MountCookedPackage(packagePath),
			"Cooked Player could not mount the primitive Sprite package");
		auto requireCooked = [&](TomCat::AssetHandle handle,
			const std::vector<uint8_t>& expected, bool expectCircle)
		{
			std::vector<uint8_t> cooked;
			TomCat::AssetType type = TomCat::AssetType::None;
			Require(assets.ReadAssetBytes(handle, cooked, &type) &&
				type == TomCat::AssetType::Texture2D,
				"Cooked Player could not resolve a primitive Sprite handle");
			Require(cooked == expected,
				"cooking changed a primitive Sprite payload");
			RequireDecodablePrimitive(cooked, expectCircle);
		};
		requireCooked(square, squareBytes, false);
		requireCooked(circle, circleBytes, true);
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestPrimitiveSpriteAuthoringAndCookedPackage();
		std::cout << "PASS primitive Sprite authoring and Cooked Player package\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL primitive Sprite authoring and Cooked Player package: "
			<< exception.what() << '\n';
		return 1;
	}
}
