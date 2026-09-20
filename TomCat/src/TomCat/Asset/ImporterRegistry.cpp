#include "tcpch.h"
#include "ImporterRegistry.h"
#include "AudioArtifact.h"
#include "MaterialArtifact.h"
#include "MeshArtifact.h"
#include "ShaderArtifact.h"
#include "SpriteAsset.h"
#include "TextureArtifact.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace TomCat {

	namespace {

		constexpr uint32_t SfntTag(char a, char b, char c, char d)
		{
			return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
				| (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
				| (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
				| static_cast<uint32_t>(static_cast<uint8_t>(d));
		}

		bool ReadBigEndian16(std::span<const uint8_t> bytes, uint64_t offset,
			uint16_t& value)
		{
			if (offset > bytes.size() || bytes.size() - static_cast<size_t>(offset) < 2)
				return false;
			const size_t index = static_cast<size_t>(offset);
			value = static_cast<uint16_t>(
				(static_cast<uint16_t>(bytes[index]) << 8) | bytes[index + 1]);
			return true;
		}

		bool ReadBigEndian32(std::span<const uint8_t> bytes, uint64_t offset,
			uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - static_cast<size_t>(offset) < 4)
				return false;
			const size_t index = static_cast<size_t>(offset);
			value = (static_cast<uint32_t>(bytes[index]) << 24)
				| (static_cast<uint32_t>(bytes[index + 1]) << 16)
				| (static_cast<uint32_t>(bytes[index + 2]) << 8)
				| static_cast<uint32_t>(bytes[index + 3]);
			return true;
		}

		bool HasByteRange(std::span<const uint8_t> bytes, uint64_t offset,
			uint64_t length)
		{
			return offset <= bytes.size() && length <= bytes.size() - offset;
		}

		struct SfntTableRange
		{
			uint32_t Tag = 0;
			uint32_t Offset = 0;
			uint32_t Length = 0;
		};

		const SfntTableRange* FindSfntTable(
			const std::vector<SfntTableRange>& tables, uint32_t tag)
		{
			const auto found = std::find_if(tables.begin(), tables.end(),
				[tag](const SfntTableRange& table) { return table.Tag == tag; });
			return found == tables.end() ? nullptr : &*found;
		}

		bool FailFontValidation(std::string& error, std::string message)
		{
			error = "invalid SFNT structure: " + std::move(message);
			return false;
		}

		bool ValidateCmap(std::span<const uint8_t> bytes,
			const SfntTableRange& table, uint16_t glyphCount, std::string& error)
		{
			uint16_t version = 0;
			uint16_t recordCount = 0;
			if (table.Length < 4
				|| !ReadBigEndian16(bytes, table.Offset, version)
				|| !ReadBigEndian16(bytes, static_cast<uint64_t>(table.Offset) + 2,
					recordCount)
				|| version != 0 || recordCount == 0 || recordCount > 256)
				return FailFontValidation(error, "cmap header is invalid");
			const uint64_t recordsLength = 4ULL + 8ULL * recordCount;
			if (recordsLength > table.Length)
				return FailFontValidation(error, "cmap encoding records are truncated");

			uint32_t selectedOffset = 0;
			bool hasSelectedUnicodeRecord = false;
			for (uint16_t index = 0; index < recordCount; ++index)
			{
				const uint64_t record = static_cast<uint64_t>(table.Offset) + 4
					+ 8ULL * index;
				uint16_t platform = 0;
				uint16_t encoding = 0;
				uint32_t subtableOffset = 0;
				if (!ReadBigEndian16(bytes, record, platform)
					|| !ReadBigEndian16(bytes, record + 2, encoding)
					|| !ReadBigEndian32(bytes, record + 4, subtableOffset)
					|| subtableOffset < recordsLength
					|| static_cast<uint64_t>(subtableOffset) + 2 > table.Length)
					return FailFontValidation(error,
						"cmap encoding subtable offset is out of range");
				if (platform == 0 || (platform == 3
					&& (encoding == 1 || encoding == 10)))
				{
					// stb_truetype retains the last supported Unicode record.
					selectedOffset = subtableOffset;
					hasSelectedUnicodeRecord = true;
				}
			}
			if (!hasSelectedUnicodeRecord)
				return FailFontValidation(error, "cmap has no supported Unicode record");

			const uint64_t subtable = static_cast<uint64_t>(table.Offset)
				+ selectedOffset;
			uint16_t format = 0;
			if (!ReadBigEndian16(bytes, subtable, format))
				return FailFontValidation(error, "selected cmap subtable is truncated");
			uint32_t declaredLength = 0;
			if (format == 0 || format == 4 || format == 6)
			{
				uint16_t shortLength = 0;
				if (!ReadBigEndian16(bytes, subtable + 2, shortLength))
					return FailFontValidation(error,
						"selected cmap subtable has no length");
				declaredLength = shortLength;
			}
			else if (format == 12 || format == 13)
			{
				if (!ReadBigEndian32(bytes, subtable + 4, declaredLength))
					return FailFontValidation(error,
						"selected cmap subtable has no length");
			}
			else
				return FailFontValidation(error,
					"selected cmap format is unsupported by stb_truetype");
			if (declaredLength == 0
				|| static_cast<uint64_t>(selectedOffset) + declaredLength > table.Length)
				return FailFontValidation(error,
					"selected cmap subtable exceeds its table range");

			if (format == 0)
			{
				if (declaredLength < 262)
					return FailFontValidation(error, "cmap format 0 is truncated");
				return true;
			}
			if (format == 6)
			{
				uint16_t entryCount = 0;
				if (declaredLength < 10
					|| !ReadBigEndian16(bytes, subtable + 8, entryCount)
					|| 10ULL + 2ULL * entryCount > declaredLength)
					return FailFontValidation(error, "cmap format 6 is truncated");
				return true;
			}
			if (format == 4)
			{
				uint16_t segmentBytes = 0;
				uint16_t searchRange = 0;
				uint16_t entrySelector = 0;
				uint16_t rangeShift = 0;
				if (declaredLength < 24
					|| !ReadBigEndian16(bytes, subtable + 6, segmentBytes)
					|| !ReadBigEndian16(bytes, subtable + 8, searchRange)
					|| !ReadBigEndian16(bytes, subtable + 10, entrySelector)
					|| !ReadBigEndian16(bytes, subtable + 12, rangeShift)
					|| segmentBytes == 0 || (segmentBytes & 1) != 0)
					return FailFontValidation(error, "cmap format 4 header is invalid");
				const uint32_t segmentCount = segmentBytes / 2;
				if (segmentCount > 8192
					|| 16ULL + 8ULL * segmentCount > declaredLength)
					return FailFontValidation(error,
						"cmap format 4 segment arrays are out of range");
				uint32_t powerOfTwo = 1;
				uint16_t expectedSelector = 0;
				while (powerOfTwo * 2 <= segmentCount)
				{
					powerOfTwo *= 2;
					++expectedSelector;
				}
				if (searchRange != powerOfTwo * 2
					|| entrySelector != expectedSelector
					|| rangeShift != segmentCount * 2 - searchRange)
					return FailFontValidation(error,
						"cmap format 4 search parameters are inconsistent");

				uint16_t previousEnd = 0;
				for (uint32_t index = 0; index < segmentCount; ++index)
				{
					uint16_t start = 0;
					uint16_t end = 0;
					uint16_t rangeOffset = 0;
					const uint64_t endPosition = subtable + 14ULL + 2ULL * index;
					const uint64_t startPosition = subtable + 16ULL
						+ 2ULL * segmentCount + 2ULL * index;
					const uint64_t rangeOffsetPosition = subtable + 16ULL
						+ 6ULL * segmentCount + 2ULL * index;
					if (!ReadBigEndian16(bytes, startPosition, start)
						|| !ReadBigEndian16(bytes, endPosition, end)
						|| !ReadBigEndian16(bytes, rangeOffsetPosition, rangeOffset)
						|| start > end || (index != 0 && end < previousEnd))
						return FailFontValidation(error,
							"cmap format 4 segments are malformed");
					previousEnd = end;
					if (rangeOffset != 0)
					{
						const uint64_t relativeWord = rangeOffsetPosition - subtable;
						const uint64_t lastGlyph = relativeWord + rangeOffset
							+ 2ULL * (end - start);
						if (lastGlyph + 2 > declaredLength)
							return FailFontValidation(error,
								"cmap format 4 glyph array is out of range");
					}
				}
				if (previousEnd != 0xffffu)
					return FailFontValidation(error,
						"cmap format 4 is missing its terminal segment");
				return true;
			}

			uint32_t groupCount = 0;
			if (declaredLength < 16
				|| !ReadBigEndian32(bytes, subtable + 12, groupCount)
				|| groupCount == 0 || groupCount > 131072
				|| 16ULL + 12ULL * groupCount > declaredLength)
				return FailFontValidation(error,
					"cmap format 12/13 groups are out of range");
			uint32_t previousEnd = 0;
			for (uint32_t index = 0; index < groupCount; ++index)
			{
				const uint64_t group = subtable + 16ULL + 12ULL * index;
				uint32_t start = 0;
				uint32_t end = 0;
				uint32_t startGlyph = 0;
				if (!ReadBigEndian32(bytes, group, start)
					|| !ReadBigEndian32(bytes, group + 4, end)
					|| !ReadBigEndian32(bytes, group + 8, startGlyph)
					|| start > end || end > 0x10ffffu
					|| (index != 0 && start <= previousEnd))
					return FailFontValidation(error,
						"cmap format 12/13 groups are malformed");
				const uint64_t finalGlyph = format == 12
					? static_cast<uint64_t>(startGlyph) + end - start : startGlyph;
				if (finalGlyph >= glyphCount)
					return FailFontValidation(error,
						"cmap references a glyph outside maxp");
				previousEnd = end;
			}
			return true;
		}

		bool ValidateSfntFace(std::span<const uint8_t> bytes, uint32_t faceOffset,
			std::string& error)
		{
			uint32_t scaler = 0;
			uint16_t tableCount = 0;
			if (!HasByteRange(bytes, faceOffset, 12)
				|| !ReadBigEndian32(bytes, faceOffset, scaler)
				|| !ReadBigEndian16(bytes, static_cast<uint64_t>(faceOffset) + 4,
					tableCount))
				return FailFontValidation(error, "face offset/header is truncated");
			if (scaler != 0x00010000u && scaler != SfntTag('O', 'T', 'T', 'O')
				&& scaler != SfntTag('t', 'r', 'u', 'e'))
				return FailFontValidation(error, "face scaler type is unsupported");
			if (tableCount == 0 || tableCount > 256
				|| !HasByteRange(bytes, static_cast<uint64_t>(faceOffset) + 12,
					16ULL * tableCount))
				return FailFontValidation(error, "face table directory is invalid");

			std::vector<SfntTableRange> tables;
			tables.reserve(tableCount);
			for (uint16_t index = 0; index < tableCount; ++index)
			{
				const uint64_t record = static_cast<uint64_t>(faceOffset) + 12
					+ 16ULL * index;
				SfntTableRange table;
				if (!ReadBigEndian32(bytes, record, table.Tag)
					|| !ReadBigEndian32(bytes, record + 8, table.Offset)
					|| !ReadBigEndian32(bytes, record + 12, table.Length)
					|| !HasByteRange(bytes, table.Offset, table.Length))
					return FailFontValidation(error,
						"table offset/length is outside the font");
				if (FindSfntTable(tables, table.Tag))
					return FailFontValidation(error, "duplicate table tag");
				tables.push_back(table);
			}

			const SfntTableRange* cmap = FindSfntTable(tables,
				SfntTag('c', 'm', 'a', 'p'));
			const SfntTableRange* head = FindSfntTable(tables,
				SfntTag('h', 'e', 'a', 'd'));
			const SfntTableRange* hhea = FindSfntTable(tables,
				SfntTag('h', 'h', 'e', 'a'));
			const SfntTableRange* hmtx = FindSfntTable(tables,
				SfntTag('h', 'm', 't', 'x'));
			const SfntTableRange* maxp = FindSfntTable(tables,
				SfntTag('m', 'a', 'x', 'p'));
			if (!cmap || cmap->Length < 4 || !head || head->Length < 54
				|| !hhea || hhea->Length < 36 || !hmtx || hmtx->Length < 4
				|| !maxp || maxp->Length < 6)
				return FailFontValidation(error,
					"required cmap/head/hhea/hmtx/maxp table is missing or truncated");

			uint16_t glyphCount = 0;
			uint16_t metricCount = 0;
			if (!ReadBigEndian16(bytes, static_cast<uint64_t>(maxp->Offset) + 4,
					glyphCount)
				|| !ReadBigEndian16(bytes, static_cast<uint64_t>(hhea->Offset) + 34,
					metricCount)
				|| glyphCount == 0 || metricCount == 0 || metricCount > glyphCount
				|| 4ULL * metricCount + 2ULL * (glyphCount - metricCount)
					> hmtx->Length)
				return FailFontValidation(error, "hmtx/maxp metrics are inconsistent");
			if (!ValidateCmap(bytes, *cmap, glyphCount, error))
				return false;

			const SfntTableRange* glyf = FindSfntTable(tables,
				SfntTag('g', 'l', 'y', 'f'));
			const SfntTableRange* loca = FindSfntTable(tables,
				SfntTag('l', 'o', 'c', 'a'));
			const SfntTableRange* cff = FindSfntTable(tables,
				SfntTag('C', 'F', 'F', ' '));
			if (glyf && glyf->Length != 0)
			{
				uint16_t locaFormat = 0;
				if (!loca
					|| !ReadBigEndian16(bytes,
						static_cast<uint64_t>(head->Offset) + 50, locaFormat)
					|| locaFormat > 1)
					return FailFontValidation(error,
						"TrueType outline loca format is invalid");
				const uint64_t locaEntrySize = locaFormat == 0 ? 2 : 4;
				if ((static_cast<uint64_t>(glyphCount) + 1) * locaEntrySize
					> loca->Length)
					return FailFontValidation(error, "loca table is truncated");
				uint32_t previous = 0;
				for (uint32_t index = 0; index <= glyphCount; ++index)
				{
					uint32_t current = 0;
					const uint64_t entry = static_cast<uint64_t>(loca->Offset)
						+ static_cast<uint64_t>(index) * locaEntrySize;
					if (locaFormat == 0)
					{
						uint16_t shortOffset = 0;
						if (!ReadBigEndian16(bytes, entry, shortOffset))
							return FailFontValidation(error, "loca entry is truncated");
						current = static_cast<uint32_t>(shortOffset) * 2;
					}
					else if (!ReadBigEndian32(bytes, entry, current))
						return FailFontValidation(error, "loca entry is truncated");
					if (current < previous || current > glyf->Length
						|| (current != previous && current - previous < 10))
						return FailFontValidation(error,
							"loca entry points outside a complete glyf header");
					previous = current;
				}
			}
			else
			{
				if (!cff || cff->Length < 4)
					return FailFontValidation(error,
						"font has neither TrueType glyf/loca nor a CFF table");
				const uint8_t headerSize = bytes[static_cast<size_t>(cff->Offset) + 2];
				const uint8_t offsetSize = bytes[static_cast<size_t>(cff->Offset) + 3];
				if (headerSize < 4 || headerSize > cff->Length
					|| offsetSize < 1 || offsetSize > 4)
					return FailFontValidation(error, "CFF header is invalid");
			}
			return true;
		}

		bool ValidateSfntSource(std::span<const uint8_t> bytes,
			std::string& error)
		{
			if (bytes.size() < 12
				|| bytes.size() > static_cast<size_t>(
					(std::numeric_limits<int>::max)()))
				return FailFontValidation(error, "font size is unsupported");
			uint32_t signature = 0;
			if (!ReadBigEndian32(bytes, 0, signature))
				return FailFontValidation(error, "font signature is truncated");
			if (signature != SfntTag('t', 't', 'c', 'f'))
				return ValidateSfntFace(bytes, 0, error);

			uint32_t version = 0;
			uint32_t faceCount = 0;
			if (!ReadBigEndian32(bytes, 4, version)
				|| !ReadBigEndian32(bytes, 8, faceCount)
				|| (version != 0x00010000u && version != 0x00020000u)
				|| faceCount == 0 || faceCount > 256)
				return FailFontValidation(error, "TTC header is invalid");
			const uint64_t headerLength = 12ULL + 4ULL * faceCount
				+ (version == 0x00020000u ? 12ULL : 0ULL);
			if (!HasByteRange(bytes, 0, headerLength))
				return FailFontValidation(error, "TTC face-offset array is truncated");
			if (version == 0x00020000u)
			{
				uint32_t dsigLength = 0;
				uint32_t dsigOffset = 0;
				const uint64_t dsig = 12ULL + 4ULL * faceCount;
				if (!ReadBigEndian32(bytes, dsig + 4, dsigLength)
					|| !ReadBigEndian32(bytes, dsig + 8, dsigOffset)
					|| (dsigLength != 0
						&& !HasByteRange(bytes, dsigOffset, dsigLength)))
					return FailFontValidation(error, "TTC DSIG range is invalid");
			}
			for (uint32_t index = 0; index < faceCount; ++index)
			{
				uint32_t faceOffset = 0;
				if (!ReadBigEndian32(bytes, 12ULL + 4ULL * index, faceOffset)
					|| faceOffset < headerLength
					|| !ValidateSfntFace(bytes, faceOffset, error))
					return error.empty()
						? FailFontValidation(error, "TTC face offset is invalid")
						: false;
			}
			return true;
		}

		class SpriteTextureImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.sprite-atlas";
			}
			uint32_t GetVersion() const noexcept override { return 3; }
			AssetType GetAssetType() const noexcept override { return AssetType::Texture2D; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "texture/tctx-v1";
				if (request.Type != AssetType::Texture2D)
				{
					result.Error = "Sprite importer was invoked for a non-texture asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				if (request.SourceBytes.empty() || request.SourceBytes.size() >
					static_cast<size_t>((std::numeric_limits<int>::max)()))
				{
					result.Error = "source image is empty or too large";
					return result;
				}
				uint32_t width = 0;
				uint32_t height = 0;
				if (!BuildTextureArtifact(request.SourceBytes, request.Settings,
					request.Platform, result.ArtifactBytes, result.Error, &width, &height))
					return result;

				std::vector<AssetSubAsset> sprites;
				if (!ParseSpriteAtlasSettings(request.Settings, sprites, result.Error))
					return result;
				for (const AssetSubAsset& sprite : sprites)
				{
					const uint64_t right = static_cast<uint64_t>(sprite.Sprite.X)
						+ sprite.Sprite.Width;
					const uint64_t bottom = static_cast<uint64_t>(sprite.Sprite.Y)
						+ sprite.Sprite.Height;
					if (right > static_cast<uint64_t>(width)
						|| bottom > static_cast<uint64_t>(height))
					{
						result.Error = "Sprite Rect exceeds source image dimensions";
						return result;
					}
					result.SubAssets.push_back({ sprite.PersistentID, sprite.Name,
						sprite.Type, sprite.Sprite });
				}
				return result;
			}
		};

		class PassthroughImporter final : public IAssetImporter
		{
		public:
			PassthroughImporter(AssetType type, std::string id, std::string format)
				: m_Type(type), m_ID(std::move(id)), m_Format(std::move(format))
			{
			}

			std::string_view GetID() const noexcept override { return m_ID; }
			uint32_t GetVersion() const noexcept override { return 1; }
			AssetType GetAssetType() const noexcept override { return m_Type; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = m_Format;
				if (request.Type != m_Type)
				{
					result.Error = "importer was invoked for a different asset type";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				if (request.SourceBytes.empty())
				{
					result.Error = "source asset is empty";
					return result;
				}
				result.ArtifactBytes.assign(request.SourceBytes.begin(), request.SourceBytes.end());
				return result;
			}

		private:
			AssetType m_Type;
			std::string m_ID;
			std::string m_Format;
		};

		class CompiledShaderImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.shader.spirv";
			}
			uint32_t GetVersion() const noexcept override { return 3; }
			AssetType GetAssetType() const noexcept override { return AssetType::Shader; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "shader/spirv-reflection-v1";
				if (request.Type != AssetType::Shader)
				{
					result.Error = "Shader importer was invoked for a non-shader asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				if (!BuildShaderArtifact(request.SourceBytes, request.SourcePath,
					request.Settings, request.Backend, result.ArtifactBytes, result.Error))
					return result;
				if (request.IsCancellationRequested())
				{
					result.ArtifactBytes.clear();
					result.Error = "cancelled";
				}
				return result;
			}
		};

		class CanonicalMaterialImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.material.canonical";
			}
			uint32_t GetVersion() const noexcept override { return 2; }
			AssetType GetAssetType() const noexcept override { return AssetType::Material; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "material/canonical-v1";
				if (request.Type != AssetType::Material)
				{
					result.Error = "Material importer was invoked for a non-material asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				(void)BuildMaterialArtifact(request.SourceBytes,
					result.ArtifactBytes, result.Error);
				return result;
			}
		};

		class ObjMeshImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.mesh.obj";
			}
			uint32_t GetVersion() const noexcept override { return 2; }
			AssetType GetAssetType() const noexcept override { return AssetType::Mesh; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "mesh/packed-p3n3uv2-u32-v1";
				if (request.Type != AssetType::Mesh)
				{
					result.Error = "Mesh importer was invoked for a non-mesh asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				if (!request.Settings.empty())
				{
					result.Error = "the built-in OBJ importer has no import settings";
					return result;
				}
				if (!BuildMeshArtifact(request.SourceBytes, request.SourcePath,
					result.ArtifactBytes, result.Error))
					return result;
				if (request.IsCancellationRequested())
				{
					result.ArtifactBytes.clear();
					result.Error = "cancelled";
				}
				return result;
			}
		};

		class CanonicalAudioImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.audio.pcm16";
			}
			uint32_t GetVersion() const noexcept override { return 2; }
			AssetType GetAssetType() const noexcept override { return AssetType::Audio; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "audio/wav-pcm16-stream-v1";
				if (request.Type != AssetType::Audio)
				{
					result.Error = "Audio importer was invoked for a non-audio asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				(void)BuildAudioArtifact(request.SourceBytes, request.Settings,
					result.ArtifactBytes, result.Error);
				return result;
			}
		};

		class SfntFontImporter final : public IAssetImporter
		{
		public:
			std::string_view GetID() const noexcept override
			{
				return "tomcat.font.sfnt";
			}
			uint32_t GetVersion() const noexcept override { return 2; }
			AssetType GetAssetType() const noexcept override { return AssetType::Font; }

			AssetImportResult Import(const AssetImportRequest& request) const override
			{
				AssetImportResult result;
				result.Format = "font/sfnt-v1";
				if (request.Type != AssetType::Font)
				{
					result.Error = "SFNT importer was invoked for a non-font asset";
					return result;
				}
				if (request.IsCancellationRequested())
				{
					result.Error = "cancelled";
					return result;
				}
				const auto& bytes = request.SourceBytes;
				if (bytes.size() < 4)
				{
					result.Error = "font source is too small for an SFNT signature";
					return result;
				}
				const bool trueType = bytes[0] == 0x00 && bytes[1] == 0x01
					&& bytes[2] == 0x00 && bytes[3] == 0x00;
				const bool openType = bytes[0] == 'O' && bytes[1] == 'T'
					&& bytes[2] == 'T' && bytes[3] == 'O';
				const bool collection = bytes[0] == 't' && bytes[1] == 't'
					&& bytes[2] == 'c' && bytes[3] == 'f';
				const bool legacyTrueType = bytes[0] == 't' && bytes[1] == 'r'
					&& bytes[2] == 'u' && bytes[3] == 'e';
				if (!trueType && !openType && !collection && !legacyTrueType)
				{
					result.Error = "font source is not a supported TTF/OTF/TTC SFNT";
					return result;
				}
				if (!ValidateSfntSource(bytes, result.Error))
					return result;
				result.ArtifactBytes.assign(bytes.begin(), bytes.end());
				return result;
			}
		};

	}

	bool ImporterRegistry::Register(std::shared_ptr<const IAssetImporter> importer,
		bool replaceExisting)
	{
		if (!importer || importer->GetAssetType() == AssetType::None ||
			importer->GetID().empty() || importer->GetVersion() == 0)
			return false;
		const std::string id(importer->GetID());
		std::unique_lock lock(m_Mutex);
		const auto typeMatch = m_ByType.find(importer->GetAssetType());
		const auto idMatch = m_ByID.find(id);
		if (!replaceExisting && (typeMatch != m_ByType.end() || idMatch != m_ByID.end()))
			return false;
		if (idMatch != m_ByID.end() && idMatch->second != importer->GetAssetType())
			return false;
		if (typeMatch != m_ByType.end())
			m_ByID.erase(std::string(typeMatch->second->GetID()));
		m_ByType.insert_or_assign(importer->GetAssetType(), importer);
		m_ByID.insert_or_assign(id, importer->GetAssetType());
		return true;
	}

	bool ImporterRegistry::Unregister(AssetType type)
	{
		std::unique_lock lock(m_Mutex);
		const auto found = m_ByType.find(type);
		if (found == m_ByType.end())
			return false;
		m_ByID.erase(std::string(found->second->GetID()));
		m_ByType.erase(found);
		return true;
	}

	std::shared_ptr<const IAssetImporter> ImporterRegistry::Find(AssetType type) const
	{
		std::shared_lock lock(m_Mutex);
		const auto found = m_ByType.find(type);
		return found == m_ByType.end() ? nullptr : found->second;
	}

	std::vector<AssetType> ImporterRegistry::GetRegisteredTypes() const
	{
		std::shared_lock lock(m_Mutex);
		std::vector<AssetType> result;
		result.reserve(m_ByType.size());
		for (const auto& [type, importer] : m_ByType)
			result.push_back(type);
		std::sort(result.begin(), result.end(), [](AssetType left, AssetType right)
		{
			return static_cast<uint16_t>(left) < static_cast<uint16_t>(right);
		});
		return result;
	}

	void ImporterRegistry::Clear()
	{
		std::unique_lock lock(m_Mutex);
		m_ByType.clear();
		m_ByID.clear();
	}

	void ImporterRegistry::RegisterBuiltInImporters()
	{
		struct Descriptor { AssetType Type; const char* ID; const char* Format; };
		static constexpr std::array<Descriptor, 7> descriptors = {{
			{ AssetType::Other, "tomcat.other.passthrough", "source-other/v1" },
			{ AssetType::Scene, "tomcat.scene.passthrough", "scene-archive/v1" },
			{ AssetType::Prefab, "tomcat.prefab.passthrough", "prefab-archive/v1" },
			{ AssetType::CSharpScript, "tomcat.csharp.passthrough", "csharp-source/v1" },
			{ AssetType::AnimationClip, "tomcat.animation-clip.passthrough", "animation-clip/v1" },
			{ AssetType::AnimatorController, "tomcat.animator-controller.passthrough", "animator-controller/v1" },
			{ AssetType::TilePalette, "tomcat.tile-palette.passthrough", "tile-palette/v1" }
		}};
		(void)Register(std::make_shared<SpriteTextureImporter>(), false);
		(void)Register(std::make_shared<CanonicalAudioImporter>(), false);
		(void)Register(std::make_shared<SfntFontImporter>(), false);
		(void)Register(std::make_shared<CompiledShaderImporter>(), false);
		(void)Register(std::make_shared<CanonicalMaterialImporter>(), false);
		(void)Register(std::make_shared<ObjMeshImporter>(), false);
		for (const Descriptor& descriptor : descriptors)
		{
			(void)Register(std::make_shared<PassthroughImporter>(descriptor.Type,
				descriptor.ID, descriptor.Format), false);
		}
	}

}
