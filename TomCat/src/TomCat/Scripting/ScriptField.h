#pragma once

#include "TomCat/Asset/Asset.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace TomCat {

	enum class ScriptFieldType : uint8_t
	{
		Bool = 0,
		Int32,
		Int64,
		Float,
		Double,
		String,
		Vector2,
		Vector3,
		Vector4,
		Color,
		Enum,
		Entity,
		AssetRef
	};

	inline const char* ScriptFieldTypeToString(ScriptFieldType type)
	{
		switch (type)
		{
			case ScriptFieldType::Bool: return "Bool";
			case ScriptFieldType::Int32: return "Int32";
			case ScriptFieldType::Int64: return "Int64";
			case ScriptFieldType::Float: return "Float";
			case ScriptFieldType::Double: return "Double";
			case ScriptFieldType::String: return "String";
			case ScriptFieldType::Vector2: return "Vector2";
			case ScriptFieldType::Vector3: return "Vector3";
			case ScriptFieldType::Vector4: return "Vector4";
			case ScriptFieldType::Color: return "Color";
			case ScriptFieldType::Enum: return "Enum";
			case ScriptFieldType::Entity: return "Entity";
			case ScriptFieldType::AssetRef: return "AssetRef";
		}
		return "Unknown";
	}

	inline bool TryParseScriptFieldType(std::string_view value,
		ScriptFieldType& result)
	{
		if (value == "Bool") result = ScriptFieldType::Bool;
		else if (value == "Int32") result = ScriptFieldType::Int32;
		else if (value == "Int64") result = ScriptFieldType::Int64;
		else if (value == "Float") result = ScriptFieldType::Float;
		else if (value == "Double") result = ScriptFieldType::Double;
		else if (value == "String") result = ScriptFieldType::String;
		else if (value == "Vector2") result = ScriptFieldType::Vector2;
		else if (value == "Vector3") result = ScriptFieldType::Vector3;
		else if (value == "Vector4") result = ScriptFieldType::Vector4;
		else if (value == "Color") result = ScriptFieldType::Color;
		else if (value == "Enum") result = ScriptFieldType::Enum;
		else if (value == "Entity") result = ScriptFieldType::Entity;
		else if (value == "AssetRef") result = ScriptFieldType::AssetRef;
		else return false;
		return true;
	}

	// Field.Type discriminates shared representations: Vector4/Color use
	// glm::vec4, Enum uses int64_t, and Entity/AssetRef use uint64_t.
	using ScriptFieldValue = std::variant<
		bool,
		int32_t,
		int64_t,
		float,
		double,
		std::string,
		glm::vec2,
		glm::vec3,
		glm::vec4,
		uint64_t>;

	inline ScriptFieldValue DefaultScriptFieldValue(ScriptFieldType type)
	{
		switch (type)
		{
			case ScriptFieldType::Bool: return false;
			case ScriptFieldType::Int32: return int32_t{ 0 };
			case ScriptFieldType::Int64:
			case ScriptFieldType::Enum: return int64_t{ 0 };
			case ScriptFieldType::Float: return 0.0f;
			case ScriptFieldType::Double: return 0.0;
			case ScriptFieldType::String: return std::string{};
			case ScriptFieldType::Vector2: return glm::vec2{ 0.0f };
			case ScriptFieldType::Vector3: return glm::vec3{ 0.0f };
			case ScriptFieldType::Vector4:
			case ScriptFieldType::Color: return glm::vec4{ 0.0f };
			case ScriptFieldType::Entity:
			case ScriptFieldType::AssetRef: return uint64_t{ 0 };
		}
		return false;
	}

	inline bool IsScriptFieldValueCompatible(ScriptFieldType type,
		const ScriptFieldValue& value)
	{
		switch (type)
		{
			case ScriptFieldType::Bool: return std::holds_alternative<bool>(value);
			case ScriptFieldType::Int32: return std::holds_alternative<int32_t>(value);
			case ScriptFieldType::Int64:
			case ScriptFieldType::Enum: return std::holds_alternative<int64_t>(value);
			case ScriptFieldType::Float: return std::holds_alternative<float>(value);
			case ScriptFieldType::Double: return std::holds_alternative<double>(value);
			case ScriptFieldType::String: return std::holds_alternative<std::string>(value);
			case ScriptFieldType::Vector2: return std::holds_alternative<glm::vec2>(value);
			case ScriptFieldType::Vector3: return std::holds_alternative<glm::vec3>(value);
			case ScriptFieldType::Vector4:
			case ScriptFieldType::Color: return std::holds_alternative<glm::vec4>(value);
			case ScriptFieldType::Entity:
			case ScriptFieldType::AssetRef: return std::holds_alternative<uint64_t>(value);
		}
		return false;
	}

	struct ScriptField
	{
		// ScriptGenerator emits a stable 128-bit identifier as 32 lowercase hex
		// characters. Text avoids platform GUID byte-order ambiguity.
		std::string FieldID;
		std::string Name;
		ScriptFieldType Type = ScriptFieldType::Bool;
		ScriptFieldValue Value = false;

		ScriptField() = default;
		ScriptField(std::string fieldID, std::string name, ScriptFieldType type,
			ScriptFieldValue value)
			: FieldID(std::move(fieldID)), Name(std::move(name)), Type(type),
			  Value(std::move(value))
		{
		}
	};

	// Ordered storage preserves manifest/Inspector order and keeps orphaned
	// fields in place when current managed metadata no longer contains them.
	using ScriptFieldMap = std::vector<ScriptField>;

}
