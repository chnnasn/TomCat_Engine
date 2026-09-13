#include "ScriptMetadataCache.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace TomCat {

	namespace {
		constexpr uint64_t kFNVOffset = 14695981039346656037ull;
		constexpr uint64_t kFNVPrime = 1099511628211ull;

		void HashBytes(uint64_t& hash, const void* data, size_t size)
		{
			const auto* bytes = static_cast<const uint8_t*>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= kFNVPrime;
			}
		}

		void HashText(uint64_t& hash, std::string_view text)
		{
			HashBytes(hash, text.data(), text.size());
			const uint8_t separator = 0xff;
			HashBytes(hash, &separator, sizeof(separator));
		}

		void AppendHex64(std::string& output, uint64_t value)
		{
			static constexpr char digits[] = "0123456789abcdef";
			for (int shift = 60; shift >= 0; shift -= 4)
				output.push_back(digits[(value >> shift) & 0x0f]);
		}

		std::string MakeUniqueOrphanFieldID(const CSharpScriptEntry& entry,
			size_t fieldIndex)
		{
			const ScriptField& field = entry.Fields[fieldIndex];
			for (uint64_t salt = 1;; ++salt)
			{
				uint64_t high = kFNVOffset;
				HashText(high, "TomCat.OrphanField.v1");
				HashText(high, field.FieldID);
				HashText(high, field.Name);
				const uint8_t type = static_cast<uint8_t>(field.Type);
				HashBytes(high, &type, sizeof(type));
				HashBytes(high, &salt, sizeof(salt));

				uint64_t low = kFNVOffset ^ 0x9e3779b97f4a7c15ull;
				HashBytes(low, &salt, sizeof(salt));
				HashText(low, field.Name);
				HashText(low, field.FieldID);
				HashBytes(low, &type, sizeof(type));

				std::string candidate;
				candidate.reserve(32);
				AppendHex64(candidate, high);
				AppendHex64(candidate, low);
				bool duplicate = false;
				for (size_t index = 0; index < entry.Fields.size(); ++index)
				{
					if (index != fieldIndex && entry.Fields[index].FieldID == candidate)
					{
						duplicate = true;
						break;
					}
				}
				if (!duplicate)
					return candidate;
			}
		}

		enum class JsonType
		{
			Null,
			Boolean,
			Number,
			String,
			Array,
			Object
		};

		struct JsonValue
		{
			JsonType Type = JsonType::Null;
			std::string Text;
			std::vector<JsonValue> Array;
			std::unordered_map<std::string, JsonValue> Object;

			const JsonValue* Find(std::string_view key) const
			{
				const auto found = Object.find(std::string(key));
				return found == Object.end() ? nullptr : &found->second;
			}
		};

		class JsonParser
		{
		public:
			explicit JsonParser(std::string_view input)
				: m_Input(input)
			{
			}

			JsonValue Parse()
			{
				SkipWhitespace();
				JsonValue result = ParseValue(0);
				SkipWhitespace();
				if (m_Position != m_Input.size())
					Fail("unexpected trailing data");
				return result;
			}

		private:
			[[noreturn]] void Fail(const char* message) const
			{
				throw std::runtime_error(std::string(message) + " at byte " +
					std::to_string(m_Position));
			}

			void SkipWhitespace()
			{
				while (m_Position < m_Input.size())
				{
					const char character = m_Input[m_Position];
					if (character != ' ' && character != '\t' && character != '\r' &&
						character != '\n')
						break;
					++m_Position;
				}
			}

			bool Consume(char expected)
			{
				if (m_Position >= m_Input.size() || m_Input[m_Position] != expected)
					return false;
				++m_Position;
				return true;
			}

			void ConsumeLiteral(std::string_view literal)
			{
				if (m_Input.substr(m_Position, literal.size()) != literal)
					Fail("invalid literal");
				m_Position += literal.size();
			}

			JsonValue ParseValue(size_t depth)
			{
				if (depth > 64)
					Fail("JSON nesting is too deep");
				if (m_Position >= m_Input.size())
					Fail("expected a JSON value");

				switch (m_Input[m_Position])
				{
					case 'n': ConsumeLiteral("null"); return {};
					case 't': ConsumeLiteral("true"); return { JsonType::Boolean, "true" };
					case 'f': ConsumeLiteral("false"); return { JsonType::Boolean, "false" };
					case '"': return { JsonType::String, ParseString() };
					case '[': return ParseArray(depth);
					case '{': return ParseObject(depth);
					default:
						if (m_Input[m_Position] == '-' ||
							(m_Input[m_Position] >= '0' && m_Input[m_Position] <= '9'))
							return { JsonType::Number, ParseNumber() };
						Fail("invalid JSON value");
				}
			}

			JsonValue ParseArray(size_t depth)
			{
				JsonValue value;
				value.Type = JsonType::Array;
				++m_Position;
				SkipWhitespace();
				if (Consume(']'))
					return value;
				while (true)
				{
					SkipWhitespace();
					value.Array.emplace_back(ParseValue(depth + 1));
					SkipWhitespace();
					if (Consume(']'))
						return value;
					if (!Consume(','))
						Fail("expected ',' or ']' in array");
				}
			}

			JsonValue ParseObject(size_t depth)
			{
				JsonValue value;
				value.Type = JsonType::Object;
				++m_Position;
				SkipWhitespace();
				if (Consume('}'))
					return value;
				while (true)
				{
					SkipWhitespace();
					if (m_Position >= m_Input.size() || m_Input[m_Position] != '"')
						Fail("expected a string key");
					std::string key = ParseString();
					SkipWhitespace();
					if (!Consume(':'))
						Fail("expected ':' after object key");
					SkipWhitespace();
					if (!value.Object.emplace(std::move(key), ParseValue(depth + 1)).second)
						Fail("duplicate object key");
					SkipWhitespace();
					if (Consume('}'))
						return value;
					if (!Consume(','))
						Fail("expected ',' or '}' in object");
				}
			}

			static int HexDigit(char character)
			{
				if (character >= '0' && character <= '9') return character - '0';
				if (character >= 'a' && character <= 'f') return character - 'a' + 10;
				if (character >= 'A' && character <= 'F') return character - 'A' + 10;
				return -1;
			}

			uint32_t ParseHexCodeUnit()
			{
				if (m_Position + 4 > m_Input.size())
					Fail("incomplete Unicode escape");
				uint32_t result = 0;
				for (uint32_t index = 0; index < 4; ++index)
				{
					const int digit = HexDigit(m_Input[m_Position++]);
					if (digit < 0)
						Fail("invalid Unicode escape");
					result = (result << 4) | static_cast<uint32_t>(digit);
				}
				return result;
			}

			static void AppendUTF8(std::string& output, uint32_t codePoint)
			{
				if (codePoint <= 0x7f)
					output.push_back(static_cast<char>(codePoint));
				else if (codePoint <= 0x7ff)
				{
					output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
				}
				else if (codePoint <= 0xffff)
				{
					output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
				}
				else
				{
					output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
					output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
					output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
				}
			}

			std::string ParseString()
			{
				if (!Consume('"'))
					Fail("expected string");
				std::string result;
				while (m_Position < m_Input.size())
				{
					const unsigned char character =
						static_cast<unsigned char>(m_Input[m_Position++]);
					if (character == '"')
						return result;
					if (character < 0x20)
						Fail("unescaped control character");
					if (character != '\\')
					{
						result.push_back(static_cast<char>(character));
						continue;
					}
					if (m_Position >= m_Input.size())
						Fail("incomplete string escape");
					switch (m_Input[m_Position++])
					{
						case '"': result.push_back('"'); break;
						case '\\': result.push_back('\\'); break;
						case '/': result.push_back('/'); break;
						case 'b': result.push_back('\b'); break;
						case 'f': result.push_back('\f'); break;
						case 'n': result.push_back('\n'); break;
						case 'r': result.push_back('\r'); break;
						case 't': result.push_back('\t'); break;
						case 'u':
						{
							uint32_t codePoint = ParseHexCodeUnit();
							if (codePoint >= 0xd800 && codePoint <= 0xdbff)
							{
								if (m_Position + 2 > m_Input.size() ||
									m_Input[m_Position] != '\\' ||
									m_Input[m_Position + 1] != 'u')
									Fail("missing low surrogate");
								m_Position += 2;
								const uint32_t low = ParseHexCodeUnit();
								if (low < 0xdc00 || low > 0xdfff)
									Fail("invalid low surrogate");
								codePoint = 0x10000 + ((codePoint - 0xd800) << 10) +
									(low - 0xdc00);
							}
							else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
								Fail("unexpected low surrogate");
							AppendUTF8(result, codePoint);
							break;
						}
						default: Fail("invalid string escape");
					}
				}
				Fail("unterminated string");
			}

			std::string ParseNumber()
			{
				const size_t start = m_Position;
				Consume('-');
				if (m_Position >= m_Input.size())
					Fail("incomplete number");
				if (m_Input[m_Position] == '0')
				{
					++m_Position;
					if (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						Fail("leading zero in number");
				}
				else
				{
					if (m_Input[m_Position] < '1' || m_Input[m_Position] > '9')
						Fail("invalid number");
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
				}
				if (m_Position < m_Input.size() && m_Input[m_Position] == '.')
				{
					++m_Position;
					const size_t fractionStart = m_Position;
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
					if (fractionStart == m_Position)
						Fail("fraction requires a digit");
				}
				if (m_Position < m_Input.size() &&
					(m_Input[m_Position] == 'e' || m_Input[m_Position] == 'E'))
				{
					++m_Position;
					if (m_Position < m_Input.size() &&
						(m_Input[m_Position] == '+' || m_Input[m_Position] == '-'))
						++m_Position;
					const size_t exponentStart = m_Position;
					while (m_Position < m_Input.size() && m_Input[m_Position] >= '0' &&
						m_Input[m_Position] <= '9')
						++m_Position;
					if (exponentStart == m_Position)
						Fail("exponent requires a digit");
				}
				return std::string(m_Input.substr(start, m_Position - start));
			}

		private:
			std::string_view m_Input;
			size_t m_Position = 0;
		};

		const JsonValue& Required(const JsonValue& object, std::string_view name,
			JsonType type)
		{
			if (object.Type != JsonType::Object)
				throw std::runtime_error("expected object");
			const JsonValue* value = object.Find(name);
			if (!value)
				throw std::runtime_error("missing property '" + std::string(name) + "'");
			if (value->Type != type)
				throw std::runtime_error("property '" + std::string(name) + "' has wrong type");
			return *value;
		}

		template<typename T>
		T ParseInteger(const JsonValue& value, const char* description)
		{
			if (value.Type != JsonType::Number)
				throw std::runtime_error(std::string(description) + " must be a number");
			T result{};
			const char* begin = value.Text.data();
			const char* end = begin + value.Text.size();
			const auto parsed = std::from_chars(begin, end, result);
			if (parsed.ec != std::errc{} || parsed.ptr != end)
				throw std::runtime_error(std::string(description) + " is out of range");
			return result;
		}

		double ParseReal(const JsonValue& value, const char* description)
		{
			if (value.Type != JsonType::Number)
				throw std::runtime_error(std::string(description) + " must be a number");
			double result{};
			const char* begin = value.Text.data();
			const char* end = begin + value.Text.size();
			const auto parsed = std::from_chars(begin, end, result,
				std::chars_format::general);
			if (parsed.ec != std::errc{} || parsed.ptr != end)
				throw std::runtime_error(std::string(description) + " is invalid");
			return result;
		}

		float ParseFloat(const JsonValue& value, const char* description)
		{
			const double parsed = ParseReal(value, description);
			if (!std::isfinite(parsed) ||
				parsed < -static_cast<double>(std::numeric_limits<float>::max()) ||
				parsed > static_cast<double>(std::numeric_limits<float>::max()))
				throw std::runtime_error(std::string(description) + " is out of float range");
			return static_cast<float>(parsed);
		}

		ScriptFieldValue ParseDefaultValue(const JsonValue& value, ScriptFieldType type)
		{
			auto vectorElements = [&value](size_t expected)
			{
				if (value.Type != JsonType::Array || value.Array.size() != expected)
					throw std::runtime_error("vector defaultValue has the wrong length");
				std::vector<float> elements;
				elements.reserve(expected);
				for (const JsonValue& element : value.Array)
					elements.push_back(ParseFloat(element, "vector defaultValue element"));
				return elements;
			};

			switch (type)
			{
				case ScriptFieldType::Bool:
					if (value.Type != JsonType::Boolean)
						throw std::runtime_error("Bool defaultValue must be boolean");
					return value.Text == "true";
				case ScriptFieldType::Int32:
					return ParseInteger<int32_t>(value, "Int32 defaultValue");
				case ScriptFieldType::Int64:
					return ParseInteger<int64_t>(value, "Int64 defaultValue");
				case ScriptFieldType::Float:
					return ParseFloat(value, "Float defaultValue");
				case ScriptFieldType::Double:
					return ParseReal(value, "Double defaultValue");
				case ScriptFieldType::String:
					if (value.Type != JsonType::String)
						throw std::runtime_error("String defaultValue must be a string");
					return value.Text;
				case ScriptFieldType::Vector2:
				{
					const auto elements = vectorElements(2);
					return glm::vec2{ elements[0], elements[1] };
				}
				case ScriptFieldType::Vector3:
				{
					const auto elements = vectorElements(3);
					return glm::vec3{ elements[0], elements[1], elements[2] };
				}
				case ScriptFieldType::Vector4:
				case ScriptFieldType::Color:
				{
					const auto elements = vectorElements(4);
					return glm::vec4{ elements[0], elements[1], elements[2], elements[3] };
				}
				case ScriptFieldType::Enum:
					return ParseInteger<int64_t>(value, "Enum defaultValue");
				case ScriptFieldType::Entity:
					return ParseInteger<uint64_t>(value, "Entity defaultValue");
				case ScriptFieldType::AssetRef:
					return ParseInteger<uint64_t>(value, "AssetRef defaultValue");
			}
			throw std::runtime_error("unsupported defaultValue type");
		}

		bool OptionalBoolean(const JsonValue& object, std::string_view name,
			bool fallback = false)
		{
			const JsonValue* value = object.Find(name);
			if (!value)
				return fallback;
			if (value->Type != JsonType::Boolean)
				throw std::runtime_error("property '" + std::string(name) + "' must be boolean");
			return value->Text == "true";
		}

		std::string OptionalString(const JsonValue& object, std::string_view name)
		{
			const JsonValue* value = object.Find(name);
			if (!value || value->Type == JsonType::Null)
				return {};
			if (value->Type != JsonType::String)
				throw std::runtime_error("property '" + std::string(name) + "' must be a string");
			return value->Text;
		}

		std::optional<double> OptionalReal(const JsonValue& object, std::string_view name)
		{
			const JsonValue* value = object.Find(name);
			if (!value || value->Type == JsonType::Null)
				return std::nullopt;
			return ParseReal(*value, std::string(name).c_str());
		}

	}

	ScriptFieldValue ScriptMetadataDefaultValue(
		const EditorScriptFieldMetadata& metadata)
	{
		if (metadata.DefaultValue &&
			IsScriptFieldValueCompatible(metadata.Type, *metadata.DefaultValue))
			return *metadata.DefaultValue;
		return DefaultScriptFieldValue(metadata.Type);
	}

	bool ReconcileScriptEntryFields(CSharpScriptEntry& entry,
		const EditorScriptMetadata& metadata)
	{
		bool changed = false;
		std::vector<bool> claimed(entry.Fields.size(), false);
		for (const EditorScriptFieldMetadata& current : metadata.Fields)
		{
			size_t matched = entry.Fields.size();
			auto findCompatible = [&](bool requireID)
			{
				for (size_t index = 0; index < entry.Fields.size(); ++index)
				{
					if (index < claimed.size() && claimed[index])
						continue;
					const ScriptField& stored = entry.Fields[index];
					if (!IsScriptFieldValueCompatible(current.Type, stored.Value))
						continue;
					const bool idMatches = !current.FieldID.empty()
						&& stored.FieldID == current.FieldID;
					const bool nameMatches = stored.Name == current.Name ||
						std::find(current.FormerNames.begin(), current.FormerNames.end(),
							stored.Name) != current.FormerNames.end();
					if ((requireID && idMatches) || (!requireID && nameMatches))
					{
						matched = index;
						return;
					}
				}
			};
			findCompatible(true);
			if (matched == entry.Fields.size())
				findCompatible(false);

			// A type change keeps the incompatible value as an orphan, but the
			// current field must retain its stable manifest ID. Re-key the orphan
			// deterministically so the serialized field map remains valid and can
			// still migrate back by name if the source type is later restored.
			for (size_t index = 0; index < entry.Fields.size(); ++index)
			{
				if (index != matched && entry.Fields[index].FieldID == current.FieldID)
				{
					entry.Fields[index].FieldID = MakeUniqueOrphanFieldID(entry, index);
					changed = true;
				}
			}

			if (matched == entry.Fields.size())
			{
				entry.Fields.emplace_back(current.FieldID, current.Name, current.Type,
					ScriptMetadataDefaultValue(current), current.TypeName);
				claimed.push_back(true);
				changed = true;
				continue;
			}

			claimed[matched] = true;
			ScriptField& stored = entry.Fields[matched];
			if (stored.FieldID != current.FieldID || stored.Name != current.Name ||
				stored.Type != current.Type || stored.TypeName != current.TypeName)
			{
				stored.FieldID = current.FieldID;
				stored.Name = current.Name;
				stored.Type = current.Type;
				stored.TypeName = current.TypeName;
				changed = true;
			}
		}
		return changed;
	}

	bool ScriptMetadataCache::ParseAndReplace(std::string_view manifestJson,
		std::string& errorMessage)
	{
		errorMessage.clear();
		try
		{
			const JsonValue root = JsonParser(manifestJson).Parse();
			if (ParseInteger<uint32_t>(Required(root, "version", JsonType::Number),
				"manifest version") != 1)
				throw std::runtime_error("unsupported script manifest version");

			std::unordered_map<uint64_t, EditorScriptMetadata> parsedScripts;
			for (const JsonValue& scriptValue :
				Required(root, "scripts", JsonType::Array).Array)
			{
				if (scriptValue.Type != JsonType::Object)
					throw std::runtime_error("script entry must be an object");
				EditorScriptMetadata script;
				const uint64_t rawHandle = ParseInteger<uint64_t>(
					Required(scriptValue, "assetHandle", JsonType::Number), "assetHandle");
				if (rawHandle == 0)
					throw std::runtime_error("assetHandle must be nonzero");
				script.ScriptAsset = AssetHandle(rawHandle);
				script.TypeName = Required(scriptValue, "typeName", JsonType::String).Text;
				script.ExecutionOrder = ParseInteger<int32_t>(
					Required(scriptValue, "executionOrder", JsonType::Number),
					"executionOrder");
				script.DisallowMultiple = OptionalBoolean(scriptValue, "disallowMultiple");

				std::unordered_set<std::string> fieldIDs;
				for (const JsonValue& fieldValue :
					Required(scriptValue, "fields", JsonType::Array).Array)
				{
					if (fieldValue.Type != JsonType::Object)
						throw std::runtime_error("field entry must be an object");
					EditorScriptFieldMetadata field;
					field.FieldID = Required(fieldValue, "id", JsonType::String).Text;
					field.Name = Required(fieldValue, "name", JsonType::String).Text;
					if (field.FieldID.empty() || !fieldIDs.insert(field.FieldID).second)
						throw std::runtime_error("field IDs must be nonempty and unique");
					const std::string type =
						Required(fieldValue, "type", JsonType::String).Text;
					if (!TryParseScriptFieldType(type, field.Type))
						throw std::runtime_error("unsupported field type '" + type + "'");
					field.TypeName = OptionalString(fieldValue, "typeName");
					field.Header = OptionalString(fieldValue, "header");
					field.Tooltip = OptionalString(fieldValue, "tooltip");
					field.RangeMinimum = OptionalReal(fieldValue, "rangeMin");
					field.RangeMaximum = OptionalReal(fieldValue, "rangeMax");
					field.Hidden = OptionalBoolean(fieldValue, "hidden");
					if (const JsonValue* defaultValue = fieldValue.Find("defaultValue"))
						field.DefaultValue = ParseDefaultValue(*defaultValue, field.Type);
					if (const JsonValue* formerNames = fieldValue.Find("formerNames"))
					{
						if (formerNames->Type != JsonType::Array)
							throw std::runtime_error("formerNames must be an array");
						for (const JsonValue& formerName : formerNames->Array)
						{
							if (formerName.Type != JsonType::String)
								throw std::runtime_error("formerNames entries must be strings");
							field.FormerNames.push_back(formerName.Text);
						}
					}
					script.Fields.push_back(std::move(field));
				}

				if (!parsedScripts.emplace(rawHandle, std::move(script)).second)
					throw std::runtime_error("duplicate script assetHandle");
			}
			m_Scripts = std::move(parsedScripts);
			return true;
		}
		catch (const std::exception& error)
		{
			errorMessage = error.what();
			return false;
		}
	}

	std::optional<EditorScriptMetadata> ScriptMetadataCache::Find(AssetHandle handle) const
	{
		const auto found = m_Scripts.find(static_cast<uint64_t>(handle));
		return found == m_Scripts.end()
			? std::nullopt
			: std::optional<EditorScriptMetadata>(found->second);
	}

}
