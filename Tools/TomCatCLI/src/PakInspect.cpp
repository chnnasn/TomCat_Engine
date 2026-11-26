#include "PakInspect.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <stb_image/stb_image.h>
#include <yaml-cpp/yaml.h>

#include <array>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace TomCat::CLI {

	namespace {

		// Minimal PNG writer (RGBA8, zlib "stored" blocks). The dump is a
		// debugging aid, so file size matters less than having no dependency.
		uint32_t Crc32(std::span<const uint8_t> bytes, uint32_t crc = 0)
		{
			static const std::array<uint32_t, 256> table = []
			{
				std::array<uint32_t, 256> values{};
				for (uint32_t n = 0; n < 256; ++n)
				{
					uint32_t c = n;
					for (int k = 0; k < 8; ++k)
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					values[n] = c;
				}
				return values;
			}();
			crc = ~crc;
			for (uint8_t byte : bytes)
				crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
			return ~crc;
		}

		void AppendBE32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (int shift = 24; shift >= 0; shift -= 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendChunk(std::vector<uint8_t>& output, const char type[4],
			std::span<const uint8_t> data)
		{
			AppendBE32(output, static_cast<uint32_t>(data.size()));
			const size_t typeOffset = output.size();
			output.insert(output.end(), type, type + 4);
			output.insert(output.end(), data.begin(), data.end());
			const uint32_t crc = Crc32(std::span<const uint8_t>(
				output.data() + typeOffset, 4 + data.size()));
			AppendBE32(output, crc);
		}

		// rgba rows are top-to-bottom.
		std::vector<uint8_t> EncodePng(uint32_t width, uint32_t height,
			std::span<const uint8_t> rgba)
		{
			std::vector<uint8_t> raw;
			const size_t stride = static_cast<size_t>(width) * 4;
			raw.reserve((stride + 1) * height);
			for (uint32_t row = 0; row < height; ++row)
			{
				raw.push_back(0); // filter: none
				const uint8_t* line = rgba.data() + row * stride;
				raw.insert(raw.end(), line, line + stride);
			}
			std::vector<uint8_t> zlib = { 0x78, 0x01 };
			uint32_t a = 1, b = 0;
			for (uint8_t byte : raw)
			{
				a = (a + byte) % 65521u;
				b = (b + a) % 65521u;
			}
			size_t offset = 0;
			do
			{
				const size_t block = std::min<size_t>(65535, raw.size() - offset);
				const bool last = offset + block == raw.size();
				zlib.push_back(last ? 1 : 0);
				zlib.push_back(static_cast<uint8_t>(block));
				zlib.push_back(static_cast<uint8_t>(block >> 8));
				zlib.push_back(static_cast<uint8_t>(~block));
				zlib.push_back(static_cast<uint8_t>(~block >> 8));
				zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + block);
				offset += block;
			} while (offset < raw.size());
			AppendBE32(zlib, (b << 16) | a);

			std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			std::vector<uint8_t> header;
			AppendBE32(header, width);
			AppendBE32(header, height);
			header.insert(header.end(), { 8, 6, 0, 0, 0 }); // 8-bit RGBA
			AppendChunk(png, "IHDR", header);
			AppendChunk(png, "IDAT", zlib);
			AppendChunk(png, "IEND", {});
			return png;
		}

		struct DecodedImage
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			std::vector<uint8_t> RGBA; // top-to-bottom rows
			std::string Format;
		};

		bool DecodeTexture(std::span<const uint8_t> bytes, DecodedImage& image,
			std::string& error)
		{
			image = {};
			if (IsTextureArtifact(bytes))
			{
				TextureArtifactView artifact;
				if (!ParseTextureArtifact(bytes, artifact, error))
					return false;
				if (artifact.Mips.empty())
				{
					error = "texture artifact has no mips";
					return false;
				}
				const TextureArtifactMip& mip = artifact.Mips.front();
				if (!DecompressTextureMip(mip, artifact.Format, image.RGBA, error))
					return false;
				image.Width = mip.Width;
				image.Height = mip.Height;
				image.Format = "artifact v" + std::to_string(artifact.Version)
					+ " format " + std::to_string(static_cast<int>(artifact.Format))
					+ ", " + std::to_string(artifact.Mips.size()) + " mip(s)";
				// Decompressed mips are top-to-bottom, like the source image
				// (PlayerApp's icon staging relies on the same order).
				return true;
			}
			int width = 0, height = 0, channels = 0;
			stbi_set_flip_vertically_on_load(false);
			stbi_uc* pixels = stbi_load_from_memory(bytes.data(),
				static_cast<int>(bytes.size()), &width, &height, &channels, 4);
			if (!pixels)
			{
				error = stbi_failure_reason() ? stbi_failure_reason() : "decode failed";
				return false;
			}
			image.Width = static_cast<uint32_t>(width);
			image.Height = static_cast<uint32_t>(height);
			image.RGBA.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
			stbi_image_free(pixels);
			image.Format = "encoded image, " + std::to_string(channels) + " channel(s)";
			return true;
		}

		bool DimensionsOnly(std::span<const uint8_t> bytes, uint32_t& width,
			uint32_t& height, std::string& format)
		{
			std::string error;
			if (IsTextureArtifact(bytes))
			{
				TextureArtifactView artifact;
				if (!ParseTextureArtifact(bytes, artifact, error))
					return false;
				width = artifact.Width;
				height = artifact.Height;
				format = "artifact format " + std::to_string(static_cast<int>(artifact.Format))
					+ ", " + std::to_string(artifact.Mips.size()) + " mip(s)";
				return true;
			}
			int w = 0, h = 0, channels = 0;
			if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()),
				&w, &h, &channels))
				return false;
			width = static_cast<uint32_t>(w);
			height = static_cast<uint32_t>(h);
			format = "encoded image";
			return true;
		}

		std::string Hex(AssetHandle handle)
		{
			return std::to_string(static_cast<uint64_t>(handle));
		}

		std::string ScalarOr(const YAML::Node& node, const char* fallback)
		{
			try
			{
				if (node && node.IsScalar())
					return node.as<std::string>();
			}
			catch (const std::exception&) {}
			return fallback;
		}

		// yaml-cpp throws when indexing a missing (invalid) node, so walk
		// nested keys only through maps that actually exist.
		YAML::Node Child(const YAML::Node& node, const char* key)
		{
			if (!node || !node.IsMap())
				return YAML::Node(YAML::NodeType::Undefined);
			const YAML::Node child = node[key];
			return child ? child : YAML::Node(YAML::NodeType::Undefined);
		}

		void PrintSceneEntities(const std::string& yaml,
			const std::unordered_set<uint64_t>& packaged)
		{
			YAML::Node document;
			try
			{
				document = YAML::Load(yaml);
			}
			catch (const std::exception& exception)
			{
				std::cout << "      (YAML parse failed: " << exception.what() << ")\n";
				return;
			}
			try
			{
			const YAML::Node entities = Child(document, "Entities");
			std::cout << "      name='" << ScalarOr(Child(document, "SceneName"), "")
				<< "' schema=" << ScalarOr(Child(document, "SchemaVersion"), "?")
				<< " entities=" << (entities && entities.IsSequence() ? entities.size() : 0)
				<< '\n';
			if (!entities || !entities.IsSequence())
				return;
			for (const YAML::Node& entity : entities)
			{
				std::string name = ScalarOr(Child(Child(entity, "Tag"), "Tag"), "");
				std::cout << "        " << ScalarOr(Child(entity, "Entity"), "?")
					<< " '" << name << "'";
				std::string components;
				if (const YAML::Node records = Child(entity, "Components");
					records && records.IsSequence())
				{
					for (const YAML::Node& record : records)
					{
						std::string stable = ScalarOr(Child(record, "StableName"), "?");
						if (stable.rfind("TomCat.", 0) == 0)
							stable.erase(0, 7);
						components += components.empty() ? stable : "," + stable;
					}
				}
				if (!components.empty())
					std::cout << " [" << components << "]";
				if (const YAML::Node sprite = Child(Child(entity, "SpriteRenderer"), "SpriteHandle"))
				{
					const std::string text = ScalarOr(sprite, "0");
					uint64_t handle = 0;
					try { handle = std::stoull(text); } catch (const std::exception&) {}
					if (handle != 0)
						std::cout << " sprite=" << text
							<< (packaged.contains(handle) ? "" : " (NOT IN PACKAGE)");
				}
				std::cout << '\n';
			}
			}
			catch (const std::exception& exception)
			{
				std::cout << "      (scene listing stopped: " << exception.what() << ")\n";
			}
		}

		bool WriteBytes(const std::filesystem::path& path, std::span<const uint8_t> bytes)
		{
			std::string error;
			if (!FileSystem::WriteFileAtomically(path, std::string_view(
				reinterpret_cast<const char*>(bytes.data()), bytes.size()), error))
			{
				std::cerr << "  could not write " << PathToUTF8(path) << ": " << error << '\n';
				return false;
			}
			return true;
		}

	}

	int InspectPackage(const PakInspectOptions& options)
	{
		// Flush per write so partial output survives if a later entry fails hard.
		std::cout << std::unitbuf;
		AssetManager& assets = AssetManager::Get();
		if (!assets.MountCookedPackage(options.PackagePath))
		{
			std::cerr << "TomCatCLI: package validation failed: "
				<< PathToUTF8(options.PackagePath) << '\n';
			return 4;
		}
		struct Unmount { ~Unmount() { AssetManager::Get().UnmountCookedPackage(); } } unmount;

		for (const auto& directory : { options.DumpTexturesDirectory, options.DumpScenesDirectory })
		{
			std::error_code error;
			if (!directory.empty())
				std::filesystem::create_directories(directory, error);
		}

		const PlayerSettings& player = assets.GetCookedPlayerSettings();
		const std::vector<AssetHandle> handles = assets.GetCookedEntryHandles();
		std::unordered_set<uint64_t> packaged;
		for (AssetHandle handle : handles)
			packaged.insert(static_cast<uint64_t>(handle));

		std::cout << "Package  " << PathToUTF8(options.PackagePath) << '\n'
			<< "Version  tcpak v" << assets.GetCookedPackageVersion() << '\n'
			<< "Product  " << player.ProductName << ' ' << player.Version
			<< " by " << player.CompanyName << ", " << player.Width << 'x'
			<< player.Height << '\n'
			<< "Managed  " << (assets.GetCookedManagedPayload() ? "yes" : "no") << '\n'
			<< "Entry    " << Hex(assets.GetCookedEntrySceneHandle()) << '\n'
			<< "Scenes  ";
		for (AssetHandle scene : assets.GetCookedBuildSceneHandles())
			std::cout << ' ' << Hex(scene);
		std::cout << "\nEntries  " << handles.size() << "\n\n";

		size_t failures = 0;
		for (AssetHandle handle : handles)
		{
			CookedAssetRange range;
			if (!assets.TryGetCookedAssetRange(handle, range))
				continue;
			std::cout << "  " << Hex(handle) << "  " << AssetTypeToString(range.Type)
				<< "  " << range.Size << " bytes\n";
			if (range.Type != AssetType::Texture2D && range.Type != AssetType::Scene
				&& range.Type != AssetType::Prefab)
				continue;

			std::vector<uint8_t> bytes;
			if (!assets.ReadAssetBytes(handle, bytes))
			{
				std::cout << "      (could not read entry bytes)\n";
				++failures;
				continue;
			}
			if (range.Type == AssetType::Scene || range.Type == AssetType::Prefab)
			{
				const std::string yaml(bytes.begin(), bytes.end());
				PrintSceneEntities(yaml, packaged);
				if (!options.DumpScenesDirectory.empty())
					WriteBytes(options.DumpScenesDirectory / (Hex(handle)
						+ (range.Type == AssetType::Scene ? ".tcscene.yaml" : ".tcprefab.yaml")),
						bytes);
				continue;
			}

			ResolvedSpriteAsset sprite;
			// The parser clears its output span on failure, so only adopt it
			// when the entry really is a Sprite envelope.
			std::span<const uint8_t> envelopePayload;
			const bool envelope = ParseCookedSpriteSubAsset(bytes, sprite, envelopePayload);
			const std::span<const uint8_t> imageBytes = envelope
				? envelopePayload : std::span<const uint8_t>(bytes);
			uint32_t width = 0, height = 0;
			std::string format;
			if (DimensionsOnly(imageBytes, width, height, format))
				std::cout << "      " << width << 'x' << height << ", " << format << '\n';
			if (envelope)
			{
				const SpriteSubAssetData& data = sprite.Data;
				std::cout << "      sprite of " << Hex(sprite.TextureHandle)
					<< (sprite.TextureHandle == handle ? " (whole texture, pixel-sized)" : "")
					<< " rect=" << data.X << ',' << data.Y << ' ' << data.Width << 'x'
					<< data.Height << " pivot=" << data.PivotX << ',' << data.PivotY
					<< " ppu=" << data.PixelsPerUnit << " world="
					<< data.Width / data.PixelsPerUnit << 'x'
					<< data.Height / data.PixelsPerUnit << '\n';
			}
			else
				std::cout << "      whole texture, unit quad (size = Transform scale)\n";

			if (options.DumpTexturesDirectory.empty())
				continue;
			DecodedImage image;
			std::string error;
			if (!DecodeTexture(imageBytes, image, error))
			{
				std::cout << "      (decode failed: " << error << ")\n";
				++failures;
				continue;
			}
			uint32_t outWidth = image.Width, outHeight = image.Height;
			std::vector<uint8_t> pixels = std::move(image.RGBA);
			if (envelope && (sprite.Data.X + sprite.Data.Width <= image.Width)
				&& (sprite.Data.Y + sprite.Data.Height <= image.Height))
			{
				// Sprite rects use a top-left origin, matching the decoded rows.
				std::vector<uint8_t> cropped(static_cast<size_t>(sprite.Data.Width)
					* sprite.Data.Height * 4);
				for (uint32_t row = 0; row < sprite.Data.Height; ++row)
					std::copy_n(pixels.begin()
						+ ((static_cast<size_t>(sprite.Data.Y) + row) * image.Width
							+ sprite.Data.X) * 4,
						static_cast<size_t>(sprite.Data.Width) * 4,
						cropped.begin() + static_cast<size_t>(row) * sprite.Data.Width * 4);
				pixels = std::move(cropped);
				outWidth = sprite.Data.Width;
				outHeight = sprite.Data.Height;
			}
			const std::vector<uint8_t> png = EncodePng(outWidth, outHeight, pixels);
			if (!WriteBytes(options.DumpTexturesDirectory / (Hex(handle) + ".png"), png))
				++failures;
		}
		if (failures != 0)
		{
			std::cerr << "TomCatCLI: " << failures << " entr"
				<< (failures == 1 ? "y" : "ies") << " could not be inspected\n";
			return 1;
		}
		return 0;
	}

}
