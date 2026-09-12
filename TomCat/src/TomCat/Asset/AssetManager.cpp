#include "tcpch.h"
#include "AssetManager.h"

#include "TomCat/Project/Project.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scripting/ScriptField.h"
#include "TomCat/Scripting/ScriptTypes.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <new>
#include <span>
#include <sstream>
#include <type_traits>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

namespace TomCat {

	namespace {

		constexpr std::array<char, 8> kPackageMagic = { 'T', 'C', 'P', 'A', 'C', 'K', '0', '1' };
		constexpr uint32_t kPackageVersion = 4;
		// Base manifest (32 bytes) followed by sixteen uint16 collision-mask rows.
		constexpr uint32_t kPackageHeaderSize = 64;
		constexpr uint64_t kPackageEntrySize = 32;
		constexpr size_t kCopyBufferSize = 64 * 1024;
		constexpr uint64_t kManagedPayloadHandle =
			(std::numeric_limits<uint64_t>::max)();
		constexpr uint16_t kManagedPayloadEntryFlag = 1;
		constexpr uint32_t kManagedPayloadEntryTag = 0x31444d54; // "TMD1"
		constexpr std::array<char, 8> kManagedEnvelopeMagic = {
			'T', 'C', 'M', 'A', 'N', '0', '0', '1' };
		constexpr uint32_t kManagedEnvelopeVersion = 1;
		constexpr uint64_t kManagedEnvelopeHeaderSize = 64;
		constexpr uint64_t kMaximumManagedAssemblySize = 512ULL * 1024ULL * 1024ULL;
		constexpr uint64_t kMaximumManagedPdbSize = 512ULL * 1024ULL * 1024ULL;
		constexpr uint64_t kMaximumScriptManifestSize = 16ULL * 1024ULL * 1024ULL;
		constexpr std::string_view kManagedTargetFramework = "net10.0";
		constexpr std::string_view kManagedRuntimeIdentifier = "win-x64";

		uint32_t RotateRight(uint32_t value, uint32_t amount)
		{
			return (value >> amount) | (value << (32U - amount));
		}

		class Sha256 final
		{
		public:
			void Update(std::span<const uint8_t> bytes)
			{
				m_TotalBytes += static_cast<uint64_t>(bytes.size());
				size_t offset = 0;
				if (m_BufferSize != 0)
				{
					const size_t copied = (std::min)(bytes.size(),
						m_Buffer.size() - m_BufferSize);
					std::memcpy(m_Buffer.data() + m_BufferSize, bytes.data(), copied);
					m_BufferSize += copied;
					offset += copied;
					if (m_BufferSize == m_Buffer.size())
					{
						Transform(m_Buffer.data());
						m_BufferSize = 0;
					}
				}
				while (bytes.size() - offset >= m_Buffer.size())
				{
					Transform(bytes.data() + offset);
					offset += m_Buffer.size();
				}
				if (offset < bytes.size())
				{
					m_BufferSize = bytes.size() - offset;
					std::memcpy(m_Buffer.data(), bytes.data() + offset, m_BufferSize);
				}
			}

			std::array<uint8_t, 32> Final()
			{
				const uint64_t bitLength = m_TotalBytes * 8ULL;
				std::array<uint8_t, 64> padding{};
				padding[0] = 0x80;
				const size_t paddingLength = m_BufferSize < 56
					? 56 - m_BufferSize : 120 - m_BufferSize;
				Update(std::span<const uint8_t>(padding.data(), paddingLength));
				std::array<uint8_t, 8> length{};
				for (size_t index = 0; index < length.size(); ++index)
					length[length.size() - 1 - index] = static_cast<uint8_t>(
						(bitLength >> (index * 8)) & 0xffULL);
				Update(length);

				std::array<uint8_t, 32> result{};
				for (size_t word = 0; word < m_State.size(); ++word)
				{
					for (size_t byte = 0; byte < 4; ++byte)
						result[word * 4 + byte] = static_cast<uint8_t>(
							m_State[word] >> ((3 - byte) * 8));
				}
				return result;
			}

		private:
			void Transform(const uint8_t* block)
			{
				static constexpr std::array<uint32_t, 64> constants = {
					0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
					0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
					0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
					0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
					0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
					0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
					0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
					0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
					0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
					0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
					0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
					0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
					0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
					0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
					0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
					0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
				std::array<uint32_t, 64> words{};
				for (size_t index = 0; index < 16; ++index)
				{
					words[index] = (static_cast<uint32_t>(block[index * 4]) << 24)
						| (static_cast<uint32_t>(block[index * 4 + 1]) << 16)
						| (static_cast<uint32_t>(block[index * 4 + 2]) << 8)
						| static_cast<uint32_t>(block[index * 4 + 3]);
				}
				for (size_t index = 16; index < words.size(); ++index)
				{
					const uint32_t left = RotateRight(words[index - 15], 7)
						^ RotateRight(words[index - 15], 18)
						^ (words[index - 15] >> 3);
					const uint32_t right = RotateRight(words[index - 2], 17)
						^ RotateRight(words[index - 2], 19)
						^ (words[index - 2] >> 10);
					words[index] = words[index - 16] + left
						+ words[index - 7] + right;
				}

				uint32_t a = m_State[0];
				uint32_t b = m_State[1];
				uint32_t c = m_State[2];
				uint32_t d = m_State[3];
				uint32_t e = m_State[4];
				uint32_t f = m_State[5];
				uint32_t g = m_State[6];
				uint32_t h = m_State[7];
				for (size_t index = 0; index < words.size(); ++index)
				{
					const uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11)
						^ RotateRight(e, 25);
					const uint32_t choose = (e & f) ^ (~e & g);
					const uint32_t temporary1 = h + sum1 + choose
						+ constants[index] + words[index];
					const uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13)
						^ RotateRight(a, 22);
					const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
					const uint32_t temporary2 = sum0 + majority;
					h = g;
					g = f;
					f = e;
					e = d + temporary1;
					d = c;
					c = b;
					b = a;
					a = temporary1 + temporary2;
				}
				m_State[0] += a;
				m_State[1] += b;
				m_State[2] += c;
				m_State[3] += d;
				m_State[4] += e;
				m_State[5] += f;
				m_State[6] += g;
				m_State[7] += h;
			}

			std::array<uint32_t, 8> m_State = { 0x6a09e667, 0xbb67ae85,
				0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
				0x1f83d9ab, 0x5be0cd19 };
			std::array<uint8_t, 64> m_Buffer{};
			size_t m_BufferSize = 0;
			uint64_t m_TotalBytes = 0;
		};

		std::string ComputeSHA256(std::span<const uint8_t> bytes)
		{
			static constexpr char hex[] = "0123456789abcdef";
			Sha256 hasher;
			hasher.Update(bytes);
			const std::array<uint8_t, 32> digest = hasher.Final();
			std::string result;
			result.reserve(digest.size() * 2);
			for (const uint8_t byte : digest)
			{
				result.push_back(hex[byte >> 4]);
				result.push_back(hex[byte & 0x0f]);
			}
			return result;
		}

		bool HasExactFields(const YAML::Node& node,
			std::initializer_list<std::string_view> required,
			std::initializer_list<std::string_view> optional,
			std::string_view context, std::string& errorMessage)
		{
			if (!node || !node.IsMap())
			{
				errorMessage = std::string(context) + " must be an object";
				return false;
			}
			std::unordered_set<std::string> seen;
			for (const auto& pair : node)
			{
				if (!pair.first.IsScalar())
				{
					errorMessage = std::string(context) + " contains a non-string key";
					return false;
				}
				const std::string key = pair.first.as<std::string>();
				if (!seen.emplace(key).second)
				{
					errorMessage = std::string(context) + " contains duplicate key '"
						+ key + "'";
					return false;
				}
				const auto matches = [&](std::string_view candidate)
				{
					return candidate == key;
				};
				if (std::find_if(required.begin(), required.end(), matches)
					== required.end()
					&& std::find_if(optional.begin(), optional.end(), matches)
						== optional.end())
				{
					errorMessage = std::string(context) + " contains unknown key '"
						+ key + "'";
					return false;
				}
			}
			for (const std::string_view field : required)
			{
				if (!node[std::string(field)])
				{
					errorMessage = std::string(context) + " is missing key '"
						+ std::string(field) + "'";
					return false;
				}
			}
			return true;
		}

		bool IsLowerHex(std::string_view value, size_t length)
		{
			if (value.size() != length)
				return false;
			for (const unsigned char character : value)
			{
				if (!((character >= '0' && character <= '9')
					|| (character >= 'a' && character <= 'f')))
					return false;
			}
			return true;
		}

		bool IsSafeBuildID(std::string_view value)
		{
			if (value.empty() || value.size() > 128)
				return false;
			for (const unsigned char character : value)
			{
				if (!(std::isalnum(character) || character == '-'
					|| character == '_' || character == '.'))
					return false;
			}
			return value != "." && value != "..";
		}

		bool IsNonemptyMetadataString(const YAML::Node& node)
		{
			if (!node || !node.IsScalar())
				return false;
			const std::string value = node.as<std::string>();
			if (value.empty())
				return false;
			return std::none_of(value.begin(), value.end(), [](unsigned char character)
			{
				return character < 0x20;
			});
		}

		bool ExtractEmbeddedScriptManifest(std::span<const uint8_t> assembly,
			std::string& manifestJson, std::string& errorMessage)
		{
			manifestJson.clear();
			constexpr std::string_view marker = "{\"version\":1,\"scripts\":";
			for (size_t offset = 0; offset + marker.size() * 2 <= assembly.size();
				++offset)
			{
				bool matches = true;
				for (size_t index = 0; index < marker.size(); ++index)
				{
					if (assembly[offset + index * 2]
						!= static_cast<uint8_t>(marker[index])
						|| assembly[offset + index * 2 + 1] != 0)
					{
						matches = false;
						break;
					}
				}
				if (!matches)
					continue;

				std::string candidate;
				candidate.reserve(4096);
				int depth = 0;
				bool inString = false;
				bool escaped = false;
				for (size_t cursor = offset; cursor + 1 < assembly.size(); cursor += 2)
				{
					if (assembly[cursor + 1] != 0)
						break;
					const char character = static_cast<char>(assembly[cursor]);
					candidate.push_back(character);
					if (inString)
					{
						if (escaped)
							escaped = false;
						else if (character == '\\')
							escaped = true;
						else if (character == '"')
							inString = false;
						continue;
					}
					if (character == '"')
						inString = true;
					else if (character == '{' || character == '[')
						++depth;
					else if (character == '}' || character == ']')
					{
						--depth;
						if (depth == 0)
						{
							manifestJson = std::move(candidate);
							return true;
						}
						if (depth < 0)
							break;
					}
					if (candidate.size() > kMaximumScriptManifestSize)
						break;
				}
			}
			errorMessage = "Assembly-CSharp.dll does not contain the generated ScriptManifest.Json";
			return false;
		}

		bool ValidateScriptManifest(std::string_view json,
			std::unordered_set<uint64_t>& scriptHandles, std::string& errorMessage)
		{
			scriptHandles.clear();
			if (json.empty() || json.size() > kMaximumScriptManifestSize
				|| json.front() != '{' || json.back() != '}'
				|| json.find('\0') != std::string_view::npos)
			{
				errorMessage = "Script manifest is empty, oversized, or not canonical JSON";
				return false;
			}
			try
			{
				const YAML::Node root = YAML::Load(std::string(json));
				if (!HasExactFields(root, { "version", "scripts" }, {},
					"script manifest", errorMessage)
					|| root["version"].as<uint32_t>()
						!= Scripting::ScriptManifestVersion)
				{
					if (errorMessage.empty())
						errorMessage = "Script manifest version is not 1";
					return false;
				}
				const YAML::Node scripts = root["scripts"];
				if (!scripts.IsSequence())
				{
					errorMessage = "Script manifest scripts must be an array";
					return false;
				}
				for (size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex)
				{
					const YAML::Node script = scripts[scriptIndex];
					const std::string context = "script manifest scripts["
						+ std::to_string(scriptIndex) + "]";
					if (!HasExactFields(script,
						{ "assetHandle", "typeName", "executionOrder",
							"disallowMultiple", "lifecycle", "fields" }, {},
						context, errorMessage))
						return false;
					const uint64_t handle = script["assetHandle"].as<uint64_t>();
					if (handle == 0 || handle == kManagedPayloadHandle
						|| !scriptHandles.emplace(handle).second
						|| !IsNonemptyMetadataString(script["typeName"]))
					{
						errorMessage = context
							+ " has an invalid/duplicate handle or typeName";
						return false;
					}
					(void)script["executionOrder"].as<int32_t>();
					(void)script["disallowMultiple"].as<bool>();
					const uint32_t lifecycle = script["lifecycle"].as<uint32_t>();
					if ((lifecycle & ~0x3ffU) != 0)
					{
						errorMessage = context + " contains unknown lifecycle bits";
						return false;
					}
					const YAML::Node fields = script["fields"];
					if (!fields.IsSequence())
					{
						errorMessage = context + ".fields must be an array";
						return false;
					}
					std::unordered_set<std::string> fieldIDs;
					std::unordered_set<std::string> fieldNames;
					for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
					{
						const YAML::Node field = fields[fieldIndex];
						const std::string fieldContext = context + ".fields["
							+ std::to_string(fieldIndex) + "]";
						if (!HasExactFields(field,
							{ "id", "name", "type", "isPublic", "hidden",
								"formerNames" },
							{ "typeName", "header", "tooltip", "rangeMin",
								"rangeMax" }, fieldContext, errorMessage))
							return false;
						const std::string id = field["id"].as<std::string>();
						const std::string name = field["name"].as<std::string>();
						if (!IsLowerHex(id, 32) || name.empty()
							|| !fieldIDs.emplace(id).second
							|| !fieldNames.emplace(name).second)
						{
							errorMessage = fieldContext
								+ " has an invalid or duplicate field identity";
							return false;
						}
						ScriptFieldType fieldType;
						const std::string typeName = field["type"].as<std::string>();
						if (!TryParseScriptFieldType(typeName, fieldType))
						{
							errorMessage = fieldContext + " has unknown field type '"
								+ typeName + "'";
							return false;
						}
						const bool needsManagedType = fieldType == ScriptFieldType::Enum
							|| fieldType == ScriptFieldType::AssetRef;
						if (static_cast<bool>(field["typeName"]) != needsManagedType
							|| (needsManagedType
								&& !IsNonemptyMetadataString(field["typeName"])))
						{
							errorMessage = fieldContext
								+ " has invalid managed type metadata";
							return false;
						}
						(void)field["isPublic"].as<bool>();
						(void)field["hidden"].as<bool>();
						for (const char* optionalText : { "header", "tooltip" })
						{
							if (field[optionalText]
								&& !field[optionalText].IsScalar())
							{
								errorMessage = fieldContext + "." + optionalText
									+ " must be a string";
								return false;
							}
						}
						const bool hasMinimum = static_cast<bool>(field["rangeMin"]);
						const bool hasMaximum = static_cast<bool>(field["rangeMax"]);
						if (hasMinimum != hasMaximum)
						{
							errorMessage = fieldContext
								+ " must provide both rangeMin and rangeMax";
							return false;
						}
						if (hasMinimum)
						{
							const double minimum = field["rangeMin"].as<double>();
							const double maximum = field["rangeMax"].as<double>();
							if (!std::isfinite(minimum) || !std::isfinite(maximum)
								|| minimum > maximum)
							{
								errorMessage = fieldContext + " has an invalid range";
								return false;
							}
						}
						const YAML::Node formerNames = field["formerNames"];
						if (!formerNames.IsSequence())
						{
							errorMessage = fieldContext + ".formerNames must be an array";
							return false;
						}
						std::unordered_set<std::string> aliases;
						for (const YAML::Node aliasNode : formerNames)
						{
							const std::string alias = aliasNode.as<std::string>();
							if (alias.empty() || alias == name
								|| !aliases.emplace(alias).second)
							{
								errorMessage = fieldContext
									+ " has an invalid former field name";
								return false;
							}
						}
					}
				}
				return true;
			}
			catch (const std::exception& error)
			{
				errorMessage = std::string("Invalid script manifest: ") + error.what();
				return false;
			}
		}

		template<typename UInt>
		bool WriteLittleEndian(std::ostream& output, UInt value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			std::array<unsigned char, sizeof(UInt)> bytes{};
			for (size_t index = 0; index < bytes.size(); ++index)
				bytes[index] = static_cast<unsigned char>((value >> (index * 8)) & static_cast<UInt>(0xff));
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			return output.good();
		}

		bool IsSymmetricCollisionMatrix(const Physics2DSettings& settings)
		{
			for (std::size_t layerA = 0; layerA < Physics2DLayerCount; ++layerA)
			{
				for (std::size_t layerB = layerA; layerB < Physics2DLayerCount; ++layerB)
				{
					if (settings.CanLayersCollide(static_cast<uint8_t>(layerA),
						static_cast<uint8_t>(layerB))
						!= settings.CanLayersCollide(static_cast<uint8_t>(layerB),
							static_cast<uint8_t>(layerA)))
						return false;
				}
			}
			return true;
		}

		template<typename UInt>
		bool ReadLittleEndian(std::istream& input, UInt& value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			std::array<unsigned char, sizeof(UInt)> bytes{};
			if (!input.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size())))
				return false;
			value = 0;
			for (size_t index = 0; index < bytes.size(); ++index)
				value |= static_cast<UInt>(bytes[index]) << (index * 8);
			return true;
		}

		bool CheckedAdd(uint64_t left, uint64_t right, uint64_t& result)
		{
			if (right > (std::numeric_limits<uint64_t>::max)() - left)
				return false;
			result = left + right;
			return true;
		}

		bool CheckedMultiply(uint64_t left, uint64_t right, uint64_t& result)
		{
			if (left != 0 && right > (std::numeric_limits<uint64_t>::max)() / left)
				return false;
			result = left * right;
			return true;
		}

		std::filesystem::path AbsoluteLexical(const std::filesystem::path& path)
		{
			if (path.empty())
				return {};
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}

		std::filesystem::path CanonicalForContainment(const std::filesystem::path& path)
		{
			const std::filesystem::path absolute = AbsoluteLexical(path);
			if (absolute.empty())
				return {};
			std::error_code error;
			const std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, error);
			return (error ? absolute : canonical).lexically_normal();
		}

		bool IsWithinOrEqual(const std::filesystem::path& root,
			const std::filesystem::path& candidate)
		{
			const std::filesystem::path normalizedRoot = CanonicalForContainment(root);
			const std::filesystem::path normalizedCandidate = CanonicalForContainment(candidate);
			if (normalizedRoot.empty() || normalizedCandidate.empty())
				return false;
			const std::filesystem::path relative = normalizedCandidate.lexically_relative(normalizedRoot);
			if (relative.empty() || relative.is_absolute())
				return false;
			for (const auto& part : relative)
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		bool ReadWholeFile(const std::filesystem::path& path, std::vector<uint8_t>& bytes)
		{
			bytes.clear();
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input)
				return false;
			const std::streamoff end = input.tellg();
			if (end < 0 || static_cast<uint64_t>(end) >
				static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
				static_cast<uint64_t>(end) > static_cast<uint64_t>(bytes.max_size()))
				return false;

			try
			{
				bytes.resize(static_cast<size_t>(end));
			}
			catch (const std::exception&)
			{
				return false;
			}

			input.seekg(0, std::ios::beg);
			size_t copied = 0;
			while (copied < bytes.size())
			{
				const size_t chunk = (std::min)(kCopyBufferSize, bytes.size() - copied);
				if (!input.read(reinterpret_cast<char*>(bytes.data() + copied),
					static_cast<std::streamsize>(chunk)))
				{
					bytes.clear();
					return false;
				}
				copied += chunk;
			}
			return true;
		}

		bool CopyFileBytes(const std::filesystem::path& source, uint64_t expectedSize,
			std::ostream& output)
		{
			std::ifstream input(source, std::ios::binary);
			if (!input)
				return false;

			std::array<char, kCopyBufferSize> buffer{};
			uint64_t remaining = expectedSize;
			while (remaining > 0)
			{
				const size_t chunk = static_cast<size_t>((std::min)(remaining,
					static_cast<uint64_t>(buffer.size())));
				if (!input.read(buffer.data(), static_cast<std::streamsize>(chunk)))
					return false;
				output.write(buffer.data(), static_cast<std::streamsize>(chunk));
				if (!output.good())
					return false;
				remaining -= chunk;
			}

			// Detect a source that grew after the index was built. A source that
			// shrank is caught by the short read above.
			return input.peek() == std::char_traits<char>::eof();
		}

		bool ReadStreamRange(std::istream& input, uint64_t offset, uint64_t size,
			std::vector<uint8_t>& bytes)
		{
			bytes.clear();
			if (offset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)())
				|| size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())
				|| size > bytes.max_size())
				return false;
			try
			{
				bytes.resize(static_cast<size_t>(size));
			}
			catch (const std::exception&)
			{
				return false;
			}
			input.clear();
			input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
			if (!input)
				return false;
			size_t copied = 0;
			while (copied < bytes.size())
			{
				const size_t chunk = (std::min)(kCopyBufferSize,
					bytes.size() - copied);
				if (!input.read(reinterpret_cast<char*>(bytes.data() + copied),
					static_cast<std::streamsize>(chunk)))
				{
					bytes.clear();
					return false;
				}
				copied += chunk;
			}
			return true;
		}

		template<typename UInt>
		void AppendLittleEndian(std::vector<uint8_t>& output, UInt value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			for (size_t index = 0; index < sizeof(UInt); ++index)
				output.push_back(static_cast<uint8_t>(
					(value >> (index * 8)) & static_cast<UInt>(0xff)));
		}

		void AppendBytes(std::vector<uint8_t>& output, std::string_view value)
		{
			output.insert(output.end(), value.begin(), value.end());
		}

		void AppendBytes(std::vector<uint8_t>& output,
			std::span<const uint8_t> value)
		{
			output.insert(output.end(), value.begin(), value.end());
		}

		bool ValidateManagedPackagePayload(const ManagedPackagePayload& payload,
			const std::unordered_set<uint64_t>* requiredScriptHandles,
			std::string& errorMessage)
		{
			if (payload.NativeApiVersion != Scripting::NativeApiVersion
				|| payload.ManagedApiVersion != Scripting::ManagedApiVersion
				|| payload.ScriptManifestVersion != Scripting::ScriptManifestVersion
				|| payload.TargetFramework != kManagedTargetFramework
				|| payload.RuntimeIdentifier != kManagedRuntimeIdentifier)
			{
				errorMessage = "Managed payload ABI, manifest, TFM, or RID is incompatible";
				return false;
			}
			if (!IsSafeBuildID(payload.BuildID)
				|| !IsLowerHex(payload.AssemblySHA256, 64)
				|| payload.Assembly.empty()
				|| payload.Assembly.size() > kMaximumManagedAssemblySize
				|| payload.Pdb.size() > kMaximumManagedPdbSize
				|| payload.ScriptManifestJson.empty()
				|| payload.ScriptManifestJson.size() > kMaximumScriptManifestSize)
			{
				errorMessage = "Managed payload sizes, build ID, or SHA-256 are invalid";
				return false;
			}
			if (payload.Assembly.size() < 2 || payload.Assembly[0] != 'M'
				|| payload.Assembly[1] != 'Z')
			{
				errorMessage = "Assembly-CSharp.dll is not a PE image";
				return false;
			}
			if (!payload.Pdb.empty() && (payload.Pdb.size() < 4
				|| payload.Pdb[0] != 'B' || payload.Pdb[1] != 'S'
				|| payload.Pdb[2] != 'J' || payload.Pdb[3] != 'B'))
			{
				errorMessage = "Assembly-CSharp.pdb is not a portable PDB";
				return false;
			}
			const std::string actualHash = ComputeSHA256(payload.Assembly);
			if (actualHash != payload.AssemblySHA256)
			{
				errorMessage = "Assembly-CSharp.dll SHA-256 does not match the envelope";
				return false;
			}

			std::unordered_set<uint64_t> manifestHandles;
			if (!ValidateScriptManifest(payload.ScriptManifestJson, manifestHandles,
				errorMessage))
				return false;
			std::string embeddedManifest;
			if (!ExtractEmbeddedScriptManifest(payload.Assembly, embeddedManifest,
				errorMessage) || embeddedManifest != payload.ScriptManifestJson)
			{
				if (errorMessage.empty())
					errorMessage = "Envelope manifest does not match Assembly-CSharp.dll";
				return false;
			}
			if (requiredScriptHandles)
			{
				for (const uint64_t handle : *requiredScriptHandles)
				{
					if (manifestHandles.find(handle) == manifestHandles.end())
					{
						errorMessage = "Script manifest does not contain attached AssetHandle "
							+ std::to_string(handle);
						return false;
					}
				}
			}
			return true;
		}

		bool BuildManagedEnvelope(const ManagedPackagePayload& payload,
			std::vector<uint8_t>& envelope, std::string& errorMessage)
		{
			envelope.clear();
			if (!ValidateManagedPackagePayload(payload, nullptr, errorMessage)
				|| payload.TargetFramework.size() > (std::numeric_limits<uint32_t>::max)()
				|| payload.RuntimeIdentifier.size() > (std::numeric_limits<uint32_t>::max)()
				|| payload.BuildID.size() > (std::numeric_limits<uint32_t>::max)()
				|| payload.AssemblySHA256.size() > (std::numeric_limits<uint32_t>::max)())
				return false;

			uint64_t total = kManagedEnvelopeHeaderSize;
			for (const uint64_t size : {
				static_cast<uint64_t>(payload.TargetFramework.size()),
				static_cast<uint64_t>(payload.RuntimeIdentifier.size()),
				static_cast<uint64_t>(payload.BuildID.size()),
				static_cast<uint64_t>(payload.AssemblySHA256.size()),
				static_cast<uint64_t>(payload.ScriptManifestJson.size()),
				static_cast<uint64_t>(payload.Assembly.size()),
				static_cast<uint64_t>(payload.Pdb.size()) })
			{
				if (!CheckedAdd(total, size, total))
				{
					errorMessage = "Managed payload envelope size overflow";
					return false;
				}
			}
			if (total > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())
				|| total > envelope.max_size())
			{
				errorMessage = "Managed payload envelope is too large";
				return false;
			}

			try
			{
				envelope.reserve(static_cast<size_t>(total));
				AppendBytes(envelope, std::string_view(kManagedEnvelopeMagic.data(),
					kManagedEnvelopeMagic.size()));
				AppendLittleEndian<uint32_t>(envelope, kManagedEnvelopeVersion);
				AppendLittleEndian<uint32_t>(envelope, payload.NativeApiVersion);
				AppendLittleEndian<uint32_t>(envelope, payload.ManagedApiVersion);
				AppendLittleEndian<uint32_t>(envelope, payload.ScriptManifestVersion);
				AppendLittleEndian<uint32_t>(envelope,
					static_cast<uint32_t>(payload.TargetFramework.size()));
				AppendLittleEndian<uint32_t>(envelope,
					static_cast<uint32_t>(payload.RuntimeIdentifier.size()));
				AppendLittleEndian<uint32_t>(envelope,
					static_cast<uint32_t>(payload.BuildID.size()));
				AppendLittleEndian<uint32_t>(envelope,
					static_cast<uint32_t>(payload.AssemblySHA256.size()));
				AppendLittleEndian<uint64_t>(envelope,
					static_cast<uint64_t>(payload.ScriptManifestJson.size()));
				AppendLittleEndian<uint64_t>(envelope,
					static_cast<uint64_t>(payload.Assembly.size()));
				AppendLittleEndian<uint64_t>(envelope,
					static_cast<uint64_t>(payload.Pdb.size()));
				AppendBytes(envelope, payload.TargetFramework);
				AppendBytes(envelope, payload.RuntimeIdentifier);
				AppendBytes(envelope, payload.BuildID);
				AppendBytes(envelope, payload.AssemblySHA256);
				AppendBytes(envelope, payload.ScriptManifestJson);
				AppendBytes(envelope, payload.Assembly);
				AppendBytes(envelope, payload.Pdb);
			}
			catch (const std::exception& error)
			{
				envelope.clear();
				errorMessage = std::string("Could not build managed payload envelope: ")
					+ error.what();
				return false;
			}
			return envelope.size() == total;
		}

		template<typename UInt>
		bool ReadLittleEndian(std::span<const uint8_t> bytes, size_t& cursor,
			UInt& value)
		{
			static_assert(std::is_unsigned_v<UInt>);
			if (cursor > bytes.size() || sizeof(UInt) > bytes.size() - cursor)
				return false;
			value = 0;
			for (size_t index = 0; index < sizeof(UInt); ++index)
				value |= static_cast<UInt>(bytes[cursor + index]) << (index * 8);
			cursor += sizeof(UInt);
			return true;
		}

		bool ReadEnvelopeString(std::span<const uint8_t> bytes, size_t& cursor,
			uint64_t length, std::string& value)
		{
			if (length > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())
				|| cursor > bytes.size() || length > bytes.size() - cursor)
				return false;
			value.assign(reinterpret_cast<const char*>(bytes.data() + cursor),
				static_cast<size_t>(length));
			cursor += static_cast<size_t>(length);
			return value.find('\0') == std::string::npos;
		}

		bool ReadEnvelopeBytes(std::span<const uint8_t> bytes, size_t& cursor,
			uint64_t length, std::vector<uint8_t>& value)
		{
			if (length > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())
				|| cursor > bytes.size() || length > bytes.size() - cursor)
				return false;
			value.assign(bytes.begin() + cursor,
				bytes.begin() + cursor + static_cast<size_t>(length));
			cursor += static_cast<size_t>(length);
			return true;
		}

		bool ParseManagedEnvelope(std::span<const uint8_t> envelope,
			ManagedPackagePayload& payload, std::string& errorMessage)
		{
			payload = {};
			if (envelope.size() < kManagedEnvelopeHeaderSize
				|| !std::equal(kManagedEnvelopeMagic.begin(), kManagedEnvelopeMagic.end(),
					envelope.begin()))
			{
				errorMessage = "Managed payload envelope magic/header is invalid";
				return false;
			}
			try
			{
				size_t cursor = kManagedEnvelopeMagic.size();
				uint32_t envelopeVersion = 0;
				uint32_t frameworkLength = 0;
				uint32_t runtimeLength = 0;
				uint32_t buildLength = 0;
				uint32_t hashLength = 0;
				uint64_t manifestLength = 0;
				uint64_t assemblyLength = 0;
				uint64_t pdbLength = 0;
				if (!ReadLittleEndian(envelope, cursor, envelopeVersion)
					|| !ReadLittleEndian(envelope, cursor, payload.NativeApiVersion)
					|| !ReadLittleEndian(envelope, cursor, payload.ManagedApiVersion)
					|| !ReadLittleEndian(envelope, cursor, payload.ScriptManifestVersion)
					|| !ReadLittleEndian(envelope, cursor, frameworkLength)
					|| !ReadLittleEndian(envelope, cursor, runtimeLength)
					|| !ReadLittleEndian(envelope, cursor, buildLength)
					|| !ReadLittleEndian(envelope, cursor, hashLength)
					|| !ReadLittleEndian(envelope, cursor, manifestLength)
					|| !ReadLittleEndian(envelope, cursor, assemblyLength)
					|| !ReadLittleEndian(envelope, cursor, pdbLength)
					|| envelopeVersion != kManagedEnvelopeVersion
					|| cursor != kManagedEnvelopeHeaderSize
					|| frameworkLength > 64 || runtimeLength > 64
					|| buildLength > 128 || hashLength != 64
					|| manifestLength > kMaximumScriptManifestSize
					|| assemblyLength == 0
					|| assemblyLength > kMaximumManagedAssemblySize
					|| pdbLength > kMaximumManagedPdbSize
					|| !ReadEnvelopeString(envelope, cursor, frameworkLength,
						payload.TargetFramework)
					|| !ReadEnvelopeString(envelope, cursor, runtimeLength,
						payload.RuntimeIdentifier)
					|| !ReadEnvelopeString(envelope, cursor, buildLength,
						payload.BuildID)
					|| !ReadEnvelopeString(envelope, cursor, hashLength,
						payload.AssemblySHA256)
					|| !ReadEnvelopeString(envelope, cursor, manifestLength,
						payload.ScriptManifestJson)
					|| !ReadEnvelopeBytes(envelope, cursor, assemblyLength,
						payload.Assembly)
					|| !ReadEnvelopeBytes(envelope, cursor, pdbLength, payload.Pdb)
					|| cursor != envelope.size())
				{
					errorMessage = "Managed payload envelope fields or lengths are invalid";
					return false;
				}
			}
			catch (const std::exception& error)
			{
				errorMessage = std::string("Could not parse managed payload envelope: ")
					+ error.what();
				return false;
			}
			return ValidateManagedPackagePayload(payload, nullptr, errorMessage);
		}

		bool IsSafeRelativePayloadPath(const std::filesystem::path& path)
		{
			if (path.empty() || path.is_absolute() || path.has_root_name()
				|| path.has_root_directory())
				return false;
			for (const auto& component : path)
			{
				if (component.empty() || component == "." || component == "..")
					return false;
			}
			return true;
		}

		bool IsAuthoringOnlyCookPath(const std::filesystem::path& path)
		{
			std::string extension = PathToUTF8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char character)
				{
					return static_cast<char>(std::tolower(character));
				});
			if (extension == ".cs" || extension == ".csproj")
				return true;

			for (const auto& component : path)
			{
				std::string name = PathToUTF8(component);
				std::transform(name.begin(), name.end(), name.begin(),
					[](unsigned char character)
					{
						return static_cast<char>(std::tolower(character));
					});
				if (name == "obj" || name == "library")
					return true;
			}
			return false;
		}

		bool ReadUtf8File(const std::filesystem::path& path, uint64_t maximumSize,
			std::string& contents)
		{
			std::vector<uint8_t> bytes;
			if (!ReadWholeFile(path, bytes) || bytes.size() > maximumSize
				|| std::find(bytes.begin(), bytes.end(), uint8_t{ 0 }) != bytes.end())
				return false;
			contents.assign(bytes.begin(), bytes.end());
			return true;
		}

		bool ValidateScriptAssetMap(const std::filesystem::path& path,
			const std::unordered_set<uint64_t>& manifestHandles,
			std::string& errorMessage)
		{
			try
			{
				std::string json;
				if (!ReadUtf8File(path, kMaximumScriptManifestSize, json))
				{
					errorMessage = "Could not read Library/ScriptProject/ScriptAssets.json";
					return false;
				}
				const YAML::Node root = YAML::Load(json);
				if (!HasExactFields(root, { "version", "assets" }, {},
					"ScriptAssets.json", errorMessage)
					|| root["version"].as<uint32_t>() != 1
					|| !root["assets"].IsMap())
				{
					if (errorMessage.empty())
						errorMessage = "ScriptAssets.json version/assets are invalid";
					return false;
				}
				std::unordered_set<std::string> paths;
				std::unordered_set<uint64_t> handles;
				for (const auto& pair : root["assets"])
				{
					const std::string pathText = pair.first.as<std::string>();
					const std::filesystem::path sourcePath = UTF8ToPath(pathText);
					std::string extension = PathToUTF8(sourcePath.extension());
					std::transform(extension.begin(), extension.end(), extension.begin(),
						[](unsigned char value)
						{
							return static_cast<char>(std::tolower(value));
						});
					const uint64_t handle = pair.second.as<uint64_t>();
					if (!IsSafeRelativePayloadPath(sourcePath) || extension != ".cs"
						|| handle == 0 || handle == kManagedPayloadHandle
						|| !paths.emplace(PathToUTF8(sourcePath.lexically_normal())).second
						|| !handles.emplace(handle).second)
					{
						errorMessage = "ScriptAssets.json contains an invalid path or handle";
						return false;
					}
				}
				if (handles != manifestHandles)
				{
					errorMessage = "ScriptAssets.json handles do not match the generated manifest";
					return false;
				}
				return true;
			}
			catch (const std::exception& error)
			{
				errorMessage = std::string("Invalid ScriptAssets.json: ") + error.what();
				return false;
			}
		}

		bool LoadProjectManagedPayload(const Project& project,
			ManagedPackagePayload& payload, std::string& errorMessage)
		{
			try
			{
				const std::filesystem::path library = project.GetLibraryPath();
				const std::filesystem::path assemblies =
					library / "ScriptAssemblies";
				const std::filesystem::path lastGoodPath = assemblies / "last-good.json";
				std::string lastGoodJson;
				if (!ReadUtf8File(lastGoodPath, 1024 * 1024, lastGoodJson))
				{
					errorMessage = "C# scripts require Library/ScriptAssemblies/last-good.json";
					return false;
				}
				const YAML::Node lastGood = YAML::Load(lastGoodJson);
				if (!HasExactFields(lastGood,
					{ "version", "sourceHash", "buildId", "assembly" },
					{ "pdb" }, "last-good.json", errorMessage)
					|| lastGood["version"].as<uint32_t>() != 1)
				{
					if (errorMessage.empty())
						errorMessage = "last-good.json version is not 1";
					return false;
				}
				const std::string sourceHash = lastGood["sourceHash"].as<std::string>();
				const std::string buildID = lastGood["buildId"].as<std::string>();
				if (!IsLowerHex(sourceHash, 16) || !IsSafeBuildID(buildID))
				{
					errorMessage = "last-good.json sourceHash or buildId is invalid";
					return false;
				}

				const std::filesystem::path rawRelativeAssembly = UTF8ToPath(
					lastGood["assembly"].as<std::string>());
				const std::filesystem::path relativeAssembly =
					rawRelativeAssembly.lexically_normal();
				const std::filesystem::path expectedAssembly =
					std::filesystem::path("Build") / buildID / "Assembly-CSharp.dll";
				if (!IsSafeRelativePayloadPath(rawRelativeAssembly)
					|| relativeAssembly != expectedAssembly)
				{
					errorMessage = "last-good.json assembly path is not the selected immutable build";
					return false;
				}
				const std::filesystem::path assemblyPath =
					AbsoluteLexical(assemblies / relativeAssembly);
				if (!IsWithinOrEqual(assemblies, assemblyPath)
					|| !ReadWholeFile(assemblyPath, payload.Assembly)
					|| payload.Assembly.empty())
				{
					errorMessage = "Could not read the selected Assembly-CSharp.dll";
					return false;
				}

				payload.Pdb.clear();
				if (lastGood["pdb"])
				{
					const std::filesystem::path rawRelativePdb = UTF8ToPath(
						lastGood["pdb"].as<std::string>());
					const std::filesystem::path relativePdb =
						rawRelativePdb.lexically_normal();
					const std::filesystem::path expectedPdb =
						std::filesystem::path("Build") / buildID / "Assembly-CSharp.pdb";
					const std::filesystem::path pdbPath =
						AbsoluteLexical(assemblies / relativePdb);
					if (!IsSafeRelativePayloadPath(rawRelativePdb)
						|| relativePdb != expectedPdb
						|| !IsWithinOrEqual(assemblies, pdbPath)
						|| !ReadWholeFile(pdbPath, payload.Pdb))
					{
						errorMessage = "Could not read the selected Assembly-CSharp.pdb";
						return false;
					}
				}

				payload.NativeApiVersion = Scripting::NativeApiVersion;
				payload.ManagedApiVersion = Scripting::ManagedApiVersion;
				payload.ScriptManifestVersion = Scripting::ScriptManifestVersion;
				payload.TargetFramework = kManagedTargetFramework;
				payload.RuntimeIdentifier = kManagedRuntimeIdentifier;
				payload.BuildID = buildID;
				payload.AssemblySHA256 = ComputeSHA256(payload.Assembly);
				if (!ExtractEmbeddedScriptManifest(payload.Assembly,
					payload.ScriptManifestJson, errorMessage))
					return false;

				std::unordered_set<uint64_t> manifestHandles;
				if (!ValidateScriptManifest(payload.ScriptManifestJson,
					manifestHandles, errorMessage)
					|| !ValidateScriptAssetMap(library / "ScriptProject"
						/ "ScriptAssets.json", manifestHandles, errorMessage))
					return false;
				return ValidateManagedPackagePayload(payload, nullptr, errorMessage);
			}
			catch (const std::exception& error)
			{
				errorMessage = std::string("Could not load last-good managed payload: ")
					+ error.what();
				return false;
			}
		}

		bool CollectSceneScriptHandles(const YAML::Node& root,
			bool& hasCSharpScripts, std::unordered_set<uint64_t>& scriptHandles,
			std::string& errorMessage)
		{
			try
			{
				const YAML::Node entities = root["Entities"];
				if (!entities || !entities.IsSequence())
				{
					errorMessage = "Scene has no valid Entities array";
					return false;
				}
				for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
				{
					const YAML::Node component = entities[entityIndex]["CSharpScripts"];
					if (!component)
						continue;
					hasCSharpScripts = true;
					const YAML::Node scripts = component["Scripts"];
					if (!scripts || !scripts.IsSequence())
					{
						errorMessage = "CSharpScripts.Scripts must be an array";
						return false;
					}
					for (const YAML::Node script : scripts)
					{
						const uint64_t handle = script["ScriptHandle"].as<uint64_t>();
						if (handle == 0 || handle == kManagedPayloadHandle)
						{
							errorMessage = "CSharpScripts contains an invalid ScriptHandle";
							return false;
						}
						scriptHandles.emplace(handle);
					}
				}
				return true;
			}
			catch (const std::exception& error)
			{
				errorMessage = std::string("Could not inspect scene scripts: ")
					+ error.what();
				return false;
			}
		}

		bool IsCurrentAsset(const AssetRegistry& registry, const AssetMetadata& metadata,
			AssetType expectedType)
		{
			if (metadata.IsMissing || metadata.Type != expectedType ||
				static_cast<uint64_t>(metadata.Handle) == 0)
				return false;
			const AssetMetadata* current = registry.GetMetadata(metadata.FilePath);
			return current && current->Handle == metadata.Handle && !current->IsMissing &&
				current->Type == expectedType;
		}

		bool ValidateCookedSpriteHandle(const AssetRegistry& registry, uint64_t rawHandle,
			const std::filesystem::path& scenePath, const std::string& propertyPath,
			std::string& errorMessage)
		{
			if (rawHandle == 0)
				return true;
			const AssetMetadata* metadata = registry.GetMetadata(AssetHandle(rawHandle));
			if (!metadata || !IsCurrentAsset(registry, *metadata, AssetType::Texture2D))
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) + "' property " + propertyPath +
					" references missing or non-texture asset " + std::to_string(rawHandle);
				return false;
			}
			return true;
		}

		bool ValidateCookedScriptHandle(const AssetRegistry& registry, uint64_t rawHandle,
			const std::filesystem::path& scenePath, const std::string& propertyPath,
			std::string& errorMessage)
		{
			if (rawHandle == 0)
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) + "' property "
					+ propertyPath + " contains a missing C# script";
				return false;
			}
			const AssetMetadata* metadata = registry.GetMetadata(AssetHandle(rawHandle));
			if (!metadata || !IsCurrentAsset(registry, *metadata,
				AssetType::CSharpScript))
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) + "' property "
					+ propertyPath + " references missing or non-C# script asset "
					+ std::to_string(rawHandle);
				return false;
			}
			return true;
		}

		bool PrepareSceneBytesForCook(const AssetRegistry& registry,
			const std::filesystem::path& scenePath, std::vector<uint8_t>& bytes,
			bool& hasCSharpScripts,
			std::unordered_set<uint64_t>& scriptHandles,
			std::string& errorMessage)
		{
			bytes.clear();
			std::vector<uint8_t> sourceBytes;
			if (!ReadWholeFile(scenePath, sourceBytes))
			{
				errorMessage = "Could not read scene '" + PathToUTF8(scenePath) + "'";
				return false;
			}
			if (!SceneSerializer::ValidateCurrentFormat(sourceBytes, scenePath))
			{
				errorMessage = "Scene '" + PathToUTF8(scenePath) +
					"' does not conform to the complete current scene schema";
				return false;
			}
			try
			{
				std::string serialized(sourceBytes.begin(), sourceBytes.end());
				std::istringstream input(std::move(serialized));
				YAML::Node root = YAML::Load(input);
				// Schema 9 is read-only migration input. Every newly emitted scene,
				// including the normalized copy stored in tcpak v4, uses schema 10.
				root["SchemaVersion"] = SceneSerializer::CurrentSchemaVersion;
				if (!CollectSceneScriptHandles(root, hasCSharpScripts,
					scriptHandles, errorMessage))
					return false;
				const YAML::Node entities = root["Entities"];
				for (size_t index = 0; index < entities.size(); ++index)
				{
					const YAML::Node entity = entities[index];
					const YAML::Node sprite = entity["SpriteRenderer"];
					if (sprite)
					{
						const std::string propertyPath = "Entities["
							+ std::to_string(index)
							+ "].SpriteRenderer.SpriteHandle";
						const uint64_t rawHandle =
							sprite["SpriteHandle"].as<uint64_t>();
						if (!ValidateCookedSpriteHandle(registry, rawHandle,
							scenePath, propertyPath, errorMessage))
							return false;
					}

					const YAML::Node csharpScripts = entity["CSharpScripts"];
					if (!csharpScripts)
						continue;
					const YAML::Node scripts = csharpScripts["Scripts"];
					for (size_t scriptIndex = 0; scriptIndex < scripts.size();
						++scriptIndex)
					{
						const std::string scriptPropertyPath = "Entities["
							+ std::to_string(index)
							+ "].CSharpScripts.Scripts["
							+ std::to_string(scriptIndex) + "].ScriptHandle";
						const uint64_t rawScriptHandle =
							scripts[scriptIndex]["ScriptHandle"].as<uint64_t>();
						if (!ValidateCookedScriptHandle(registry, rawScriptHandle,
							scenePath, scriptPropertyPath, errorMessage))
							return false;
					}
				}

				YAML::Emitter output;
				output << root;
				if (!output.good())
				{
					errorMessage = "Could not emit cooked scene '" + PathToUTF8(scenePath) +
						"': " + output.GetLastError();
					return false;
				}
				const std::string cooked = output.c_str();
				bytes.assign(cooked.begin(), cooked.end());
				return true;
			}
			catch (const std::exception& error)
			{
				errorMessage = "Could not prepare scene '" + PathToUTF8(scenePath) +
					"' for cooking: " + error.what();
				return false;
			}
		}

		void RemoveTemporaryFile(const std::filesystem::path& path)
		{
			if (path.empty())
				return;
			std::error_code ignored;
			std::filesystem::remove(path, ignored);
		}

	}

	AssetManager& AssetManager::Get()
	{
		static AssetManager manager;
		return manager;
	}

	bool AssetManager::Initialize(const std::filesystem::path& assetRoot,
		const std::filesystem::path& libraryRoot)
	{
		Shutdown();
		if (assetRoot.empty() || libraryRoot.empty())
		{
			TC_Core_Error("AssetManager requires non-empty Assets and Library roots");
			return false;
		}

		m_RegistryInitialized = m_Registry.Initialize(assetRoot, libraryRoot);
		if (!m_RegistryInitialized)
		{
			m_Registry.Shutdown();
			TC_Core_Error("Failed to initialize the asset registry for '{0}'", PathToUTF8(assetRoot));
		}
		return m_RegistryInitialized;
	}

	bool AssetManager::SetProject(const Ref<Project>& project)
	{
		if (!project || project->GetProjectPath().empty())
		{
			Shutdown();
			return project == nullptr;
		}
		if (!Initialize(project->GetAssetPath(), project->GetLibraryPath()))
			return false;
		m_AuthoringProject = project;
		m_UsesProjectConfiguration = true;

		const AssetHandle configuredHandle = project->GetConfig().StartSceneHandle;
		if (static_cast<uint64_t>(configuredHandle) != 0)
		{
			const AssetMetadata* metadata = m_Registry.GetMetadata(configuredHandle);
			if (metadata && IsCurrentAsset(m_Registry, *metadata, AssetType::Scene) &&
				metadata->FilePath != project->GetConfig().StartScene)
			{
				// The UUID is authoritative. Repair the authoring path after an
				// external move or a crash between an asset move and project save.
				if (project->SetStartScene(metadata->FilePath) && !project->Save())
					TC_Core_Warn("StartScene was resolved by Handle, but Project.tcproj could not store its repaired path");
			}
			return true;
		}
		return true;
	}

	Physics2DSettings AssetManager::GetPhysics2DSettings() const
	{
		if (IsCookedPackageMounted())
			return m_CookedPhysics2DSettings;
		if (const Ref<Project> project = m_AuthoringProject.lock())
			return project->GetSettings().Physics2D;
		return Physics2DSettings{};
	}

	void AssetManager::Shutdown()
	{
		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.close();
			m_CookedPackageStream.clear();
		}
		m_CookedEntries.clear();
		m_CookedPackagePath.clear();
		m_CookedPackageSize = 0;
		m_CookedStartSceneHandle = AssetHandle(0);
		m_CookedPhysics2DSettings = Physics2DSettings{};
		m_CookedManagedPayload.reset();
		m_ManagedCookPayloadOverride.reset();
		m_AuthoringProject.reset();
		m_UsesProjectConfiguration = false;
		m_Registry.Shutdown();
		m_RegistryInitialized = false;
	}

	bool AssetManager::Refresh()
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		const bool complete = m_Registry.Refresh();
		// Refresh may apply a safe partial scan while reporting damaged sidecars.
		// Any such metadata change must invalidate both successful and missing loads.
		ReleaseAll();
		return complete;
	}

	AssetHandle AssetManager::ImportAsset(const std::filesystem::path& path)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return AssetHandle(0);
		const AssetHandle handle = m_Registry.ImportAsset(path);
		// ImportAsset may perform a full registry refresh (for example when a
		// duplicate UUID appears or is resolved). A handle can therefore acquire a
		// different path even when the requested path has no before/after record.
		// Invalidate all typed instances so cached bytes can never cross identities.
		ReleaseAll();
		return handle;
	}

	bool AssetManager::SetImportSettings(AssetHandle handle,
		const AssetImportSettings& settings)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return false;
		if (!m_Registry.SetImportSettings(handle, settings))
			return false;
		Release(handle);
		return true;
	}

	Ref<Texture2D> AssetManager::GetMissingTexture()
	{
		if (m_MissingTexture && m_MissingTexture->IsLoaded())
			return m_MissingTexture;

		m_MissingTexture = Texture2D::Create(2, 2);
		if (!m_MissingTexture || !m_MissingTexture->IsLoaded())
		{
			TC_Core_Error("Failed to create the missing-asset texture");
			m_MissingTexture.reset();
			return nullptr;
		}

		constexpr std::array<uint8_t, 16> pixels = {
			255, 0, 255, 255,   0, 0, 0, 255,
			0, 0, 0, 255,       255, 0, 255, 255
		};
		m_MissingTexture->SetData(pixels.data(), static_cast<uint32_t>(pixels.size()));
		return m_MissingTexture;
	}

	Ref<Texture2D> AssetManager::CacheMissingTexture(AssetHandle handle, const char* reason)
	{
		if (static_cast<uint64_t>(handle) != 0)
			TC_Core_Warn("Using the missing texture for asset {0}: {1}",
				static_cast<uint64_t>(handle), reason ? reason : "asset is unavailable");
		Ref<Texture2D> missing = GetMissingTexture();
		if (static_cast<uint64_t>(handle) != 0 && missing)
			m_TextureCache[handle] = missing;
		return missing;
	}

	Ref<Texture2D> AssetManager::LoadTexture(AssetHandle handle)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return GetMissingTexture();

		AssetType registeredType = AssetType::None;
		if (IsCookedPackageMounted())
		{
			const auto cooked = m_CookedEntries.find(handle);
			if (cooked == m_CookedEntries.end())
				return CacheMissingTexture(handle, "handle is not present in the cooked package");
			registeredType = cooked->second.Type;
		}
		else
		{
			if (!m_RegistryInitialized)
				return CacheMissingTexture(handle, "asset registry is not initialized");
			const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
			if (!metadata || metadata->IsMissing)
				return CacheMissingTexture(handle, "handle is not present in the asset registry");
			const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
			if (!current || current->Handle != handle || current->IsMissing)
				return CacheMissingTexture(handle, "handle no longer owns its registered path");
			registeredType = metadata->Type;
		}
		if (registeredType != AssetType::Texture2D)
			return CacheMissingTexture(handle, "asset type is not Texture2D");

		const auto cached = m_TextureCache.find(handle);
		if (cached != m_TextureCache.end())
		{
			// A formerly missing handle may become valid after a conflict is fixed or
			// the source is restored. Never let the shared placeholder pin that stale
			// state once the current registry/package identity is valid again.
			if (cached->second && cached->second != m_MissingTexture)
				return cached->second;
			m_TextureCache.erase(cached);
		}
		if (IsCookedPackageMounted())
		{
			const auto cooked = m_CookedEntries.find(handle);
			if (cooked == m_CookedEntries.end() || cooked->second.Size >
				static_cast<uint64_t>((std::numeric_limits<int>::max)()))
				return CacheMissingTexture(handle, "encoded texture exceeds the decoder size limit");
		}
		else
		{
			std::error_code error;
			const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
			const uintmax_t size = source.empty() ? 0 : std::filesystem::file_size(source, error);
			if (error || source.empty() || size >
				static_cast<uintmax_t>((std::numeric_limits<int>::max)()))
				return CacheMissingTexture(handle, "encoded texture exceeds the decoder size limit");
		}

		AssetType type = AssetType::None;
		std::vector<uint8_t> bytes;
		if (!ReadAssetBytes(handle, bytes, &type))
			return CacheMissingTexture(handle, "asset bytes could not be read");
		if (type != registeredType)
			return CacheMissingTexture(handle, "asset type changed while it was being loaded");

		const std::filesystem::path sourcePath = ResolvePath(handle);
		Ref<Texture2D> texture = Texture2D::Create(bytes.data(), bytes.size(), sourcePath);
		if (!texture || !texture->IsLoaded())
			return CacheMissingTexture(handle, "encoded image could not be decoded");

		m_TextureCache.emplace(handle, texture);
		return texture;
	}

	void AssetManager::Release(AssetHandle handle)
	{
		m_TextureCache.erase(handle);
	}

	void AssetManager::ReleaseAll()
	{
		m_TextureCache.clear();
		m_MissingTexture.reset();
	}

	void AssetManager::ReleaseHandles(const std::vector<AssetHandle>& handles)
	{
		for (const AssetHandle handle : handles)
			m_TextureCache.erase(handle);
	}

	bool AssetManager::MoveAsset(const std::filesystem::path& source,
		const std::filesystem::path& destination)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		if (!Refresh())
		{
			TC_Core_Error("Asset move refused because the registry refresh was incomplete");
			return false;
		}

		std::vector<AssetHandle> handles = m_Registry.GetHandlesUnderPath(source);
		if (handles.empty())
		{
			if (const AssetMetadata* metadata = m_Registry.GetMetadata(source))
				handles.push_back(metadata->Handle);
		}
		if (!m_Registry.MoveAsset(source, destination))
			return false;
		ReleaseHandles(handles);
		return true;
	}

	bool AssetManager::MoveAsset(AssetHandle handle, const std::filesystem::path& destination)
	{
		if (static_cast<uint64_t>(handle) == 0 || !m_RegistryInitialized ||
			IsCookedPackageMounted())
			return false;
		if (!Refresh())
			return false;
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		if (!current || current->Handle != handle)
			return false;
		const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
		if (source.empty() || !m_Registry.MoveAsset(source, destination, handle))
			return false;
		Release(handle);
		return true;
	}

	bool AssetManager::DeleteAsset(const std::filesystem::path& path, bool force,
		std::vector<AssetReference>* references)
	{
		return DeleteAssetInternal(path, force, references, AssetHandle(0));
	}

	bool AssetManager::DeleteAssetInternal(const std::filesystem::path& path, bool force,
		std::vector<AssetReference>* references, AssetHandle expectedHandle)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted())
			return false;
		const bool refreshComplete = Refresh();
		if (!refreshComplete && (!force || static_cast<uint64_t>(expectedHandle) != 0))
		{
			TC_Core_Error("Asset delete refused because the registry refresh was incomplete");
			return false;
		}

		std::vector<AssetHandle> handles;
		if (static_cast<uint64_t>(expectedHandle) != 0)
		{
			const AssetMetadata* expected = m_Registry.GetMetadata(path);
			if (!expected || expected->IsMissing || expected->Handle != expectedHandle)
				return false;
			handles.push_back(expectedHandle);
		}
		else
		{
			handles = m_Registry.GetHandlesUnderPath(path);
			if (handles.empty())
			{
				if (const AssetMetadata* metadata = m_Registry.GetMetadata(path))
					handles.push_back(metadata->Handle);
			}
		}
		std::vector<AssetReference> liveReferences;
		for (AssetHandle handle : handles)
		{
			if (const Ref<Project> project = m_AuthoringProject.lock(); project &&
				project->GetConfig().StartSceneHandle == handle)
			{
				AssetReference reference;
				reference.ReferencedAsset = handle;
				reference.FilePath = project->GetProjectPath();
				reference.PropertyPath = "Project.StartSceneHandle";
				liveReferences.emplace_back(std::move(reference));
			}
			if (!m_LiveReferenceProvider)
				continue;
			std::vector<AssetReference> current = m_LiveReferenceProvider(handle);
			liveReferences.insert(liveReferences.end(), current.begin(), current.end());
		}
		if (!force && !liveReferences.empty())
		{
			if (references)
				*references = std::move(liveReferences);
			return false;
		}

		std::vector<AssetReference> foundReferences;
		const bool deleted = m_Registry.DeleteAsset(path, force, foundReferences,
			expectedHandle);
		foundReferences.insert(foundReferences.end(), liveReferences.begin(), liveReferences.end());
		if (references)
			*references = foundReferences;
		if (!deleted)
			return false;

		ReleaseHandles(handles);
		return true;
	}

	bool AssetManager::DeleteAsset(AssetHandle handle, bool force,
		std::vector<AssetReference>* references)
	{
		if (static_cast<uint64_t>(handle) == 0 || !m_RegistryInitialized ||
			IsCookedPackageMounted())
			return false;
		if (!Refresh())
			return false;
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return false;
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		if (!current || current->Handle != handle)
			return false;
		const std::filesystem::path path = m_Registry.GetFileSystemPath(handle);
		return !path.empty() && DeleteAssetInternal(path, force, references, handle);
	}

	std::vector<AssetReference> AssetManager::FindReferences(AssetHandle handle) const
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return {};
		std::vector<AssetReference> references = m_Registry.FindReferences(handle);
		if (const Ref<Project> project = m_AuthoringProject.lock(); project &&
			project->GetConfig().StartSceneHandle == handle)
		{
			AssetReference reference;
			reference.ReferencedAsset = handle;
			reference.FilePath = project->GetProjectPath();
			reference.PropertyPath = "Project.StartSceneHandle";
			references.emplace_back(std::move(reference));
		}
		if (m_LiveReferenceProvider)
		{
			std::vector<AssetReference> live = m_LiveReferenceProvider(handle);
			references.insert(references.end(), live.begin(), live.end());
		}
		return references;
	}

	std::filesystem::path AssetManager::ResolvePath(AssetHandle handle) const
	{
		// A mounted package intentionally has no source-path fallback.
		if (!m_RegistryInitialized || IsCookedPackageMounted() ||
			static_cast<uint64_t>(handle) == 0)
			return {};
		const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
		if (!metadata || metadata->IsMissing)
			return {};
		const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
		return current && current->Handle == handle
			? m_Registry.GetFileSystemPath(handle) : std::filesystem::path{};
	}

	bool AssetManager::ReadAssetBytes(AssetHandle handle, std::vector<uint8_t>& bytes,
		AssetType* type) const
	{
		bytes.clear();
		if (type)
			*type = AssetType::None;
		if (static_cast<uint64_t>(handle) == 0)
			return false;

		if (!IsCookedPackageMounted())
		{
			if (!m_RegistryInitialized)
				return false;
			const AssetMetadata* metadata = m_Registry.GetMetadata(handle);
			if (!metadata || metadata->IsMissing)
				return false;
			const AssetMetadata* current = m_Registry.GetMetadata(metadata->FilePath);
			if (!current || current->Handle != handle)
				return false;
			const std::filesystem::path path = m_Registry.GetFileSystemPath(handle);
			if (path.empty() || !ReadWholeFile(path, bytes))
				return false;
			if (type)
				*type = metadata->Type;
			return true;
		}

		const auto iterator = m_CookedEntries.find(handle);
		if (iterator == m_CookedEntries.end())
			return false;
		const CookedEntry& entry = iterator->second;
		if (entry.Offset > m_CookedPackageSize || entry.Size > m_CookedPackageSize - entry.Offset ||
			entry.Size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
			entry.Size > static_cast<uint64_t>(bytes.max_size()) ||
			entry.Offset > static_cast<uint64_t>((std::numeric_limits<std::streamoff>::max)()))
			return false;

		try
		{
			bytes.resize(static_cast<size_t>(entry.Size));
		}
		catch (const std::exception&)
		{
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.clear();
			m_CookedPackageStream.seekg(static_cast<std::streamoff>(entry.Offset), std::ios::beg);
			if (!m_CookedPackageStream)
			{
				bytes.clear();
				return false;
			}

			size_t copied = 0;
			while (copied < bytes.size())
			{
				const size_t chunk = (std::min)(kCopyBufferSize, bytes.size() - copied);
				if (!m_CookedPackageStream.read(
					reinterpret_cast<char*>(bytes.data() + copied),
					static_cast<std::streamsize>(chunk)))
				{
					bytes.clear();
					return false;
				}
				copied += chunk;
			}
		}
		if (type)
			*type = entry.Type;
		return true;
	}

	std::vector<uint8_t> AssetManager::ReadAssetBytes(AssetHandle handle) const
	{
		std::vector<uint8_t> bytes;
		ReadAssetBytes(handle, bytes, nullptr);
		return bytes;
	}

	bool AssetManager::SetManagedCookPayload(std::vector<uint8_t> assembly,
		std::string scriptManifestJson, std::string buildID,
		std::vector<uint8_t> pdb)
	{
		ManagedPackagePayload payload;
		payload.NativeApiVersion = Scripting::NativeApiVersion;
		payload.ManagedApiVersion = Scripting::ManagedApiVersion;
		payload.ScriptManifestVersion = Scripting::ScriptManifestVersion;
		payload.TargetFramework = kManagedTargetFramework;
		payload.RuntimeIdentifier = kManagedRuntimeIdentifier;
		payload.BuildID = std::move(buildID);
		payload.ScriptManifestJson = std::move(scriptManifestJson);
		payload.Assembly = std::move(assembly);
		payload.Pdb = std::move(pdb);
		payload.AssemblySHA256 = ComputeSHA256(payload.Assembly);
		std::string errorMessage;
		if (!ValidateManagedPackagePayload(payload, nullptr, errorMessage))
		{
			TC_Core_Error("Invalid explicit managed Cook payload: {0}", errorMessage);
			return false;
		}
		m_ManagedCookPayloadOverride = std::move(payload);
		return true;
	}

	bool AssetManager::CookToPackage(const std::filesystem::path& packagePath)
	{
		if (m_UsesProjectConfiguration)
		{
			const Ref<Project> project = m_AuthoringProject.lock();
			if (!project)
			{
				TC_Core_Error("Cannot cook because the active Project is no longer available");
				return false;
			}
			const AssetHandle startScene = project->GetConfig().StartSceneHandle;
			const AssetMetadata* metadata = m_Registry.GetMetadata(startScene);
			if (static_cast<uint64_t>(startScene) == 0 || !metadata ||
				!IsCurrentAsset(m_Registry, *metadata, AssetType::Scene))
			{
				TC_Core_Error("Project StartSceneHandle must identify a live Scene asset: {0}",
					static_cast<uint64_t>(startScene));
				return false;
			}
			return CookToPackage(packagePath, startScene);
		}
		return CookToPackage(packagePath, AssetHandle(0));
	}

	bool AssetManager::CookToPackage(const std::filesystem::path& packagePath,
		AssetHandle startSceneHandle)
	{
		if (!m_RegistryInitialized || IsCookedPackageMounted() || packagePath.empty())
			return false;
		if (m_UsesProjectConfiguration && static_cast<uint64_t>(startSceneHandle) == 0)
		{
			TC_Core_Error("A project cook requires a nonzero StartSceneHandle");
			return false;
		}
		if (IsWithinOrEqual(m_Registry.GetAssetDirectory(), packagePath))
		{
			TC_Core_Error("Cooked package must be written outside Assets: {0}", PathToUTF8(packagePath));
			return false;
		}
		if (!Refresh())
			return false;
		Physics2DSettings packagePhysicsSettings;
		if (m_UsesProjectConfiguration)
		{
			const Ref<Project> project = m_AuthoringProject.lock();
			if (!project)
			{
				TC_Core_Error("Cannot cook because the active Project is no longer available");
				return false;
			}
			packagePhysicsSettings = project->GetSettings().Physics2D;
		}
		if (!IsSymmetricCollisionMatrix(packagePhysicsSettings))
		{
			TC_Core_Error("Cannot cook an asymmetric Physics2D collision matrix");
			return false;
		}
		if (static_cast<uint64_t>(startSceneHandle) != 0)
		{
			const AssetMetadata* startScene = m_Registry.GetMetadata(startSceneHandle);
			if (!startScene || !IsCurrentAsset(m_Registry, *startScene, AssetType::Scene))
			{
				TC_Core_Error("Cook start scene {0} is missing or is not a Scene asset",
					static_cast<uint64_t>(startSceneHandle));
				return false;
			}
		}

		struct SourceEntry
		{
			uint64_t RawHandle = 0;
			uint16_t RawType = 0;
			uint16_t Flags = 0;
			uint32_t Reserved = 0;
			std::filesystem::path Path;
			std::vector<uint8_t> CookedBytes;
			bool HasCookedBytes = false;
			uint64_t Offset = 0;
			uint64_t Size = 0;
		};

		std::vector<SourceEntry> entries;
		entries.reserve(m_Registry.GetAssets().size() + 1);
		bool hasCSharpScripts = false;
		std::unordered_set<uint64_t> referencedScriptHandles;
		for (const auto& [handle, metadata] : m_Registry.GetAssets())
		{
			if (static_cast<uint64_t>(handle) == 0 || metadata.Type == AssetType::None)
				continue;
			if (static_cast<uint64_t>(handle) == kManagedPayloadHandle)
			{
				TC_Core_Error("AssetHandle UINT64_MAX is reserved by tcpak v4");
				return false;
			}
			// C# source is an authoring input. tcpak v4 carries the compiled project
			// assembly/manifest payload, never Assets/**/*.cs bytes.
			if (metadata.Type == AssetType::CSharpScript
				|| IsAuthoringOnlyCookPath(metadata.FilePath))
				continue;
			const AssetMetadata* current = m_Registry.GetMetadata(metadata.FilePath);
			// Missing cache tombstones and shadowed historical records are not source
			// assets. References to them are rejected while preparing scenes below.
			if (metadata.IsMissing || !current || current->Handle != handle || current->IsMissing)
				continue;
			const std::filesystem::path source = m_Registry.GetFileSystemPath(handle);
			std::error_code error;
			if (source.empty() || !std::filesystem::is_regular_file(source, error) || error)
			{
				TC_Core_Error("Cannot cook missing asset {0} ('{1}')",
					static_cast<uint64_t>(handle), PathToUTF8(metadata.FilePath));
				return false;
			}
			SourceEntry entry;
			entry.RawHandle = static_cast<uint64_t>(handle);
			entry.RawType = static_cast<uint16_t>(metadata.Type);
			entry.Path = source;
			if (metadata.Type == AssetType::Scene)
			{
				std::string sceneError;
				if (!PrepareSceneBytesForCook(m_Registry, source, entry.CookedBytes,
					hasCSharpScripts, referencedScriptHandles, sceneError))
				{
					TC_Core_Error("Cannot cook scene: {0}", sceneError);
					return false;
				}
				entry.HasCookedBytes = true;
				entry.Size = static_cast<uint64_t>(entry.CookedBytes.size());
			}
			else
			{
				const uintmax_t fileSize = std::filesystem::file_size(source, error);
				if (error || fileSize > (std::numeric_limits<uint64_t>::max)())
				{
					TC_Core_Error("Cannot inspect asset while cooking: {0}", PathToUTF8(source));
					return false;
				}
				entry.Size = static_cast<uint64_t>(fileSize);
			}
			entries.push_back(std::move(entry));
		}

		std::optional<ManagedPackagePayload> managedPayload;
		if (m_ManagedCookPayloadOverride)
			managedPayload = *m_ManagedCookPayloadOverride;
		else if (hasCSharpScripts)
		{
			const Ref<Project> project = m_AuthoringProject.lock();
			std::string payloadError;
			ManagedPackagePayload discovered;
			if (!project || !LoadProjectManagedPayload(*project, discovered, payloadError))
			{
				TC_Core_Error("Cannot cook CSharpScripts without a current managed payload: {0}",
					payloadError.empty() ? "no authoring Project is available" : payloadError);
				return false;
			}
			managedPayload = std::move(discovered);
		}

		if (managedPayload)
		{
			std::string payloadError;
			if (!ValidateManagedPackagePayload(*managedPayload,
				&referencedScriptHandles, payloadError))
			{
				TC_Core_Error("Cannot cook invalid managed payload: {0}", payloadError);
				return false;
			}
			SourceEntry managedEntry;
			managedEntry.RawHandle = kManagedPayloadHandle;
			managedEntry.RawType = static_cast<uint16_t>(AssetType::None);
			managedEntry.Flags = kManagedPayloadEntryFlag;
			managedEntry.Reserved = kManagedPayloadEntryTag;
			std::string envelopeError;
			if (!BuildManagedEnvelope(*managedPayload, managedEntry.CookedBytes,
				envelopeError))
			{
				TC_Core_Error("Cannot build managed tcpak payload: {0}", envelopeError);
				return false;
			}
			managedEntry.HasCookedBytes = true;
			managedEntry.Size = static_cast<uint64_t>(managedEntry.CookedBytes.size());
			entries.push_back(std::move(managedEntry));
		}
		else if (hasCSharpScripts)
		{
			TC_Core_Error("Cannot cook CSharpScripts without Assembly-CSharp.dll");
			return false;
		}

		std::sort(entries.begin(), entries.end(), [](const SourceEntry& left, const SourceEntry& right) {
			return left.RawHandle < right.RawHandle;
		});

		uint64_t indexSize = 0;
		uint64_t dataOffset = 0;
		if (!CheckedMultiply(static_cast<uint64_t>(entries.size()), kPackageEntrySize, indexSize) ||
			!CheckedAdd(kPackageHeaderSize, indexSize, dataOffset))
		{
			TC_Core_Error("Cooked package index is too large");
			return false;
		}
		for (SourceEntry& entry : entries)
		{
			entry.Offset = dataOffset;
			if (!CheckedAdd(dataOffset, entry.Size, dataOffset))
			{
				TC_Core_Error("Cooked package exceeds the supported 64-bit size");
				return false;
			}
		}

		std::error_code directoryError;
		if (!packagePath.parent_path().empty())
			std::filesystem::create_directories(packagePath.parent_path(), directoryError);
		if (directoryError)
		{
			TC_Core_Error("Cannot create cooked package directory '{0}': {1}",
				PathToUTF8(packagePath.parent_path()), directoryError.message());
			return false;
		}

		const std::filesystem::path temporary = FileSystem::MakeTemporarySiblingPath(packagePath);
		if (temporary.empty())
		{
			TC_Core_Error("Could not allocate a temporary cooked package path");
			return false;
		}

		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			TC_Core_Error("Could not open temporary cooked package '{0}'", PathToUTF8(temporary));
			return false;
		}

		output.write(kPackageMagic.data(), static_cast<std::streamsize>(kPackageMagic.size()));
		bool succeeded = output.good() &&
			WriteLittleEndian<uint32_t>(output, kPackageVersion) &&
			WriteLittleEndian<uint32_t>(output, kPackageHeaderSize) &&
			WriteLittleEndian<uint64_t>(output, static_cast<uint64_t>(entries.size())) &&
			WriteLittleEndian<uint64_t>(output, static_cast<uint64_t>(startSceneHandle));
		for (uint16_t mask : packagePhysicsSettings.CollisionMasks)
			succeeded = succeeded && WriteLittleEndian<uint16_t>(output, mask);
		for (const SourceEntry& entry : entries)
		{
			if (!succeeded)
				break;
			succeeded = WriteLittleEndian<uint64_t>(output, entry.RawHandle) &&
				WriteLittleEndian<uint16_t>(output, entry.RawType) &&
				WriteLittleEndian<uint16_t>(output, entry.Flags) &&
				WriteLittleEndian<uint32_t>(output, entry.Reserved) &&
				WriteLittleEndian<uint64_t>(output, entry.Offset) &&
				WriteLittleEndian<uint64_t>(output, entry.Size);
		}
		for (const SourceEntry& entry : entries)
		{
			if (!succeeded)
				break;
			if (entry.HasCookedBytes)
			{
				if (!entry.CookedBytes.empty())
					output.write(reinterpret_cast<const char*>(entry.CookedBytes.data()),
						static_cast<std::streamsize>(entry.CookedBytes.size()));
				succeeded = output.good();
			}
			else
				succeeded = CopyFileBytes(entry.Path, entry.Size, output);
		}

		output.flush();
		succeeded = succeeded && output.good();
		output.close();
		succeeded = succeeded && !output.fail();
		if (!succeeded)
		{
			RemoveTemporaryFile(temporary);
			TC_Core_Error("Failed while writing cooked package '{0}'", PathToUTF8(packagePath));
			return false;
		}

		std::string installError;
		if (!FileSystem::InstallTemporaryFileAtomically(temporary, packagePath, installError))
		{
			TC_Core_Error("Could not install cooked package '{0}': {1}",
				PathToUTF8(packagePath), installError);
			return false;
		}
		return true;
	}

	bool AssetManager::MountCookedPackage(const std::filesystem::path& packagePath)
	{
		if (packagePath.empty())
			return false;
		std::ifstream input(packagePath, std::ios::binary | std::ios::ate);
		if (!input)
			return false;
		const std::streamoff packageEnd = input.tellg();
		if (packageEnd < static_cast<std::streamoff>(kPackageHeaderSize))
			return false;
		const uint64_t packageSize = static_cast<uint64_t>(packageEnd);
		input.seekg(0, std::ios::beg);

		std::array<char, kPackageMagic.size()> magic{};
		uint32_t version = 0;
		uint32_t headerSize = 0;
		uint64_t entryCount = 0;
		uint64_t rawStartSceneHandle = 0;
		if (!input.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
			magic != kPackageMagic ||
			!ReadLittleEndian<uint32_t>(input, version) ||
			version != kPackageVersion ||
			!ReadLittleEndian<uint32_t>(input, headerSize) ||
			!ReadLittleEndian<uint64_t>(input, entryCount) ||
			!ReadLittleEndian<uint64_t>(input, rawStartSceneHandle))
			return false;
		Physics2DSettings mountedPhysicsSettings;
		for (uint16_t& mask : mountedPhysicsSettings.CollisionMasks)
		{
			if (!ReadLittleEndian<uint16_t>(input, mask))
				return false;
		}
		if (headerSize != kPackageHeaderSize || headerSize > packageSize)
			return false;
		if (!IsSymmetricCollisionMatrix(mountedPhysicsSettings))
			return false;

		uint64_t indexSize = 0;
		uint64_t dataStart = 0;
		if (!CheckedMultiply(entryCount, kPackageEntrySize, indexSize) ||
			!CheckedAdd(headerSize, indexSize, dataStart) || dataStart > packageSize ||
			entryCount > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
			return false;

		input.seekg(static_cast<std::streamoff>(headerSize), std::ios::beg);
		if (!input)
			return false;

		std::unordered_map<AssetHandle, CookedEntry> entries;
		std::optional<CookedEntry> managedEnvelopeEntry;
		std::vector<std::pair<uint64_t, uint64_t>> occupiedRanges;
		try
		{
			entries.reserve(static_cast<size_t>(entryCount));
			occupiedRanges.reserve(static_cast<size_t>(entryCount));
		}
		catch (const std::exception&)
		{
			return false;
		}

		for (uint64_t index = 0; index < entryCount; ++index)
		{
			uint64_t rawHandle = 0;
			uint16_t rawType = 0;
			uint16_t flags = 0;
			uint32_t reserved = 0;
			uint64_t offset = 0;
			uint64_t size = 0;
			if (!ReadLittleEndian<uint64_t>(input, rawHandle) ||
				!ReadLittleEndian<uint16_t>(input, rawType) ||
				!ReadLittleEndian<uint16_t>(input, flags) ||
				!ReadLittleEndian<uint32_t>(input, reserved) ||
				!ReadLittleEndian<uint64_t>(input, offset) ||
				!ReadLittleEndian<uint64_t>(input, size))
				return false;

			if (offset < dataStart || offset > packageSize
				|| size > packageSize - offset)
				return false;

			if (rawHandle == kManagedPayloadHandle)
			{
				if (managedEnvelopeEntry
					|| rawType != static_cast<uint16_t>(AssetType::None)
					|| flags != kManagedPayloadEntryFlag
					|| reserved != kManagedPayloadEntryTag || size == 0)
					return false;
				managedEnvelopeEntry = CookedEntry{ AssetType::None, offset, size };
			}
			else
			{
				if (rawHandle == 0
					|| rawType == static_cast<uint16_t>(AssetType::None)
					|| rawType > static_cast<uint16_t>(AssetType::Other)
					|| flags != 0 || reserved != 0)
					return false;
				const AssetHandle handle(rawHandle);
				if (!entries.emplace(handle,
					CookedEntry{ static_cast<AssetType>(rawType), offset, size }).second)
					return false;
			}
			if (size != 0)
				occupiedRanges.emplace_back(offset, offset + size);
		}

		std::sort(occupiedRanges.begin(), occupiedRanges.end());
		for (size_t index = 1; index < occupiedRanges.size(); ++index)
		{
			if (occupiedRanges[index].first < occupiedRanges[index - 1].second)
				return false;
		}
		if (rawStartSceneHandle != 0)
		{
			const auto startScene = entries.find(AssetHandle(rawStartSceneHandle));
			if (startScene == entries.end() || startScene->second.Type != AssetType::Scene)
				return false;
		}

		std::optional<ManagedPackagePayload> mountedManagedPayload;
		std::string validationError;
		if (managedEnvelopeEntry)
		{
			std::vector<uint8_t> envelope;
			if (!ReadStreamRange(input, managedEnvelopeEntry->Offset,
				managedEnvelopeEntry->Size, envelope))
				return false;
			ManagedPackagePayload parsed;
			if (!ParseManagedEnvelope(envelope, parsed, validationError))
			{
				TC_Core_Error("Rejected managed tcpak payload: {0}", validationError);
				return false;
			}
			mountedManagedPayload = std::move(parsed);
		}

		bool hasCSharpScripts = false;
		std::unordered_set<uint64_t> referencedScriptHandles;
		for (const auto& [handle, entry] : entries)
		{
			(void)handle;
			if (entry.Type != AssetType::Scene)
				continue;
			std::vector<uint8_t> sceneBytes;
			if (!ReadStreamRange(input, entry.Offset, entry.Size, sceneBytes)
				|| !SceneSerializer::ValidateCurrentFormat(sceneBytes, packagePath))
				return false;
			try
			{
				const std::string serialized(sceneBytes.begin(), sceneBytes.end());
				const YAML::Node root = YAML::Load(serialized);
				if (root["SchemaVersion"].as<uint32_t>()
					!= SceneSerializer::CurrentSchemaVersion
					|| !CollectSceneScriptHandles(root, hasCSharpScripts,
						referencedScriptHandles, validationError))
					return false;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}
		if (hasCSharpScripts && !mountedManagedPayload)
		{
			TC_Core_Error("Rejected tcpak v4: a scene has CSharpScripts but no managed payload");
			return false;
		}
		if (mountedManagedPayload
			&& !ValidateManagedPackagePayload(*mountedManagedPayload,
				&referencedScriptHandles, validationError))
		{
			TC_Core_Error("Rejected managed tcpak references: {0}", validationError);
			return false;
		}

		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream = std::move(input);
			m_CookedPackageStream.clear();
		}
		m_CookedEntries = std::move(entries);
		m_CookedPackagePath = AbsoluteLexical(packagePath);
		m_CookedPackageSize = packageSize;
		m_CookedStartSceneHandle = AssetHandle(rawStartSceneHandle);
		m_CookedPhysics2DSettings = mountedPhysicsSettings;
		m_CookedManagedPayload = std::move(mountedManagedPayload);
		return true;
	}

	void AssetManager::UnmountCookedPackage()
	{
		if (!IsCookedPackageMounted())
			return;
		ReleaseAll();
		{
			std::lock_guard<std::mutex> lock(m_CookedPackageMutex);
			m_CookedPackageStream.close();
			m_CookedPackageStream.clear();
		}
		m_CookedEntries.clear();
		m_CookedPackagePath.clear();
		m_CookedPackageSize = 0;
		m_CookedStartSceneHandle = AssetHandle(0);
		m_CookedPhysics2DSettings = Physics2DSettings{};
		m_CookedManagedPayload.reset();
	}

}
