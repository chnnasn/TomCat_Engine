#include "tcpch.h"
#include "MaterialArtifact.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>

namespace TomCat {

	namespace {

		constexpr std::array<uint8_t, 4> kMagic = { 'T', 'C', 'M', 'A' };
		constexpr uint32_t kVersion = 1;
		constexpr uint32_t kHeaderSize = 40;
		constexpr uint32_t kTextureRecordSize = 16;
		constexpr uint32_t kParameterRecordSize = 24;
		constexpr size_t kMaximumSourceBytes = 16 * 1024 * 1024;
		constexpr uint32_t kMaximumBindings = 4096;

		struct SourceTexture
		{
			std::string Name;
			AssetHandle Handle = AssetHandle(0);
		};

		struct SourceParameter
		{
			std::string Name;
			MaterialParameterType Type = MaterialParameterType::Float;
			std::array<uint32_t, 4> Value{};
		};

		struct SourceMaterial
		{
			AssetHandle Shader = AssetHandle(0);
			std::vector<SourceTexture> Textures;
			std::vector<SourceParameter> Parameters;
		};

		void AppendU32(std::vector<uint8_t>& output, uint32_t value)
		{
			for (uint32_t shift = 0; shift < 32; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		void AppendU64(std::vector<uint8_t>& output, uint64_t value)
		{
			for (uint32_t shift = 0; shift < 64; shift += 8)
				output.push_back(static_cast<uint8_t>(value >> shift));
		}

		bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 4)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 4; ++index)
				value |= static_cast<uint32_t>(bytes[offset + index]) << (index * 8);
			return true;
		}

		bool ReadU64(std::span<const uint8_t> bytes, size_t offset, uint64_t& value)
		{
			if (offset > bytes.size() || bytes.size() - offset < 8)
				return false;
			value = 0;
			for (uint32_t index = 0; index < 8; ++index)
				value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8);
			return true;
		}

		bool IsName(std::string_view name)
		{
			if (name.empty() || name.size() > 127)
				return false;
			const auto alpha = [](unsigned char value)
			{
				return (value >= 'a' && value <= 'z') ||
					(value >= 'A' && value <= 'Z') || value == '_';
			};
			if (!alpha(static_cast<unsigned char>(name.front())))
				return false;
			return std::all_of(name.begin() + 1, name.end(), [&](unsigned char value)
			{
				return alpha(value) || (value >= '0' && value <= '9') || value == '.';
			});
		}

		bool HasExactKeys(const YAML::Node& node,
			std::initializer_list<std::string_view> required,
			std::initializer_list<std::string_view> optional,
			std::string_view context, std::string& error)
		{
			if (!node || !node.IsMap())
			{
				error = std::string(context) + " must be a map";
				return false;
			}
			std::unordered_set<std::string> seen;
			for (const auto& pair : node)
			{
				if (!pair.first.IsScalar())
				{
					error = std::string(context) + " contains a non-scalar key";
					return false;
				}
				const std::string key = pair.first.Scalar();
				if (!seen.emplace(key).second)
				{
					error = std::string(context) + " contains duplicate key '" + key + "'";
					return false;
				}
				const auto matches = [&](std::string_view candidate) { return candidate == key; };
				if (std::find_if(required.begin(), required.end(), matches) == required.end()
					&& std::find_if(optional.begin(), optional.end(), matches) == optional.end())
				{
					error = std::string(context) + " contains unknown key '" + key + "'";
					return false;
				}
			}
			for (const std::string_view key : required)
			{
				if (!node[std::string(key)])
				{
					error = std::string(context) + " is missing key '" + std::string(key) + "'";
					return false;
				}
			}
			return true;
		}

		bool ParseParameterType(std::string_view value, MaterialParameterType& type,
			size_t& components)
		{
			components = 1;
			if (value == "Bool") type = MaterialParameterType::Bool;
			else if (value == "Int") type = MaterialParameterType::Int;
			else if (value == "Float") type = MaterialParameterType::Float;
			else if (value == "Float2") { type = MaterialParameterType::Float2; components = 2; }
			else if (value == "Float3") { type = MaterialParameterType::Float3; components = 3; }
			else if (value == "Float4") { type = MaterialParameterType::Float4; components = 4; }
			else return false;
			return true;
		}

		bool ParseSource(std::span<const uint8_t> source, SourceMaterial& material,
			std::string& error)
		{
			material = {};
			error.clear();
			if (source.empty() || source.size() > kMaximumSourceBytes ||
				std::find(source.begin(), source.end(), uint8_t{ 0 }) != source.end())
			{
				error = "material source is empty, oversized, or contains NUL bytes";
				return false;
			}
			try
			{
				const YAML::Node root = YAML::Load(std::string(source.begin(), source.end()));
				if (!HasExactKeys(root, { "SchemaVersion", "Shader" },
					{ "Textures", "Parameters" }, "material", error))
					return false;
				if (!root["SchemaVersion"].IsScalar()
					|| root["SchemaVersion"].as<uint32_t>() != 1)
				{
					error = "material.SchemaVersion must be 1";
					return false;
				}
				if (!root["Shader"].IsScalar())
				{
					error = "material.Shader must be a non-zero AssetHandle";
					return false;
				}
				material.Shader = AssetHandle(root["Shader"].as<uint64_t>());
				if (static_cast<uint64_t>(material.Shader) == 0)
				{
					error = "material.Shader must be a non-zero AssetHandle";
					return false;
				}

				const YAML::Node textures = root["Textures"];
				if (textures)
				{
					if (!textures.IsMap() || textures.size() > kMaximumBindings)
					{
						error = "material.Textures must be a map with at most 4096 entries";
						return false;
					}
					std::set<std::string> names;
					for (const auto& pair : textures)
					{
						if (!pair.first.IsScalar() || !pair.second.IsScalar())
						{
							error = "material texture bindings must map names to AssetHandles";
							return false;
						}
						SourceTexture texture;
						texture.Name = pair.first.Scalar();
						if (!IsName(texture.Name) || !names.emplace(texture.Name).second)
						{
							error = "material contains an invalid or duplicate texture slot '"
								+ texture.Name + "'";
							return false;
						}
						texture.Handle = AssetHandle(pair.second.as<uint64_t>());
						material.Textures.push_back(std::move(texture));
					}
				}

				const YAML::Node parameters = root["Parameters"];
				if (parameters)
				{
					if (!parameters.IsMap() || parameters.size() > kMaximumBindings)
					{
						error = "material.Parameters must be a map with at most 4096 entries";
						return false;
					}
					std::set<std::string> names;
					for (const auto& pair : parameters)
					{
						if (!pair.first.IsScalar())
						{
							error = "material parameter names must be scalars";
							return false;
						}
						SourceParameter parameter;
						parameter.Name = pair.first.Scalar();
						if (!IsName(parameter.Name) || !names.emplace(parameter.Name).second)
						{
							error = "material contains an invalid or duplicate parameter '"
								+ parameter.Name + "'";
							return false;
						}
						const std::string context = "material.Parameters." + parameter.Name;
						if (!HasExactKeys(pair.second, { "Type", "Value" }, {}, context, error)
							|| !pair.second["Type"].IsScalar())
							return false;
						size_t components = 0;
						if (!ParseParameterType(pair.second["Type"].Scalar(),
							parameter.Type, components))
						{
							error = context + ".Type is unsupported";
							return false;
						}
						const YAML::Node value = pair.second["Value"];
						if (parameter.Type == MaterialParameterType::Bool)
						{
							if (!value.IsScalar() || (value.Scalar() != "true" && value.Scalar() != "false"))
							{
								error = context + ".Value must be true or false";
								return false;
							}
							parameter.Value[0] = value.Scalar() == "true" ? 1U : 0U;
						}
						else if (parameter.Type == MaterialParameterType::Int)
						{
							if (!value.IsScalar())
							{
								error = context + ".Value must be a 32-bit integer";
								return false;
							}
							parameter.Value[0] = std::bit_cast<uint32_t>(value.as<int32_t>());
						}
						else
						{
							if ((components == 1 && !value.IsScalar())
								|| (components > 1 && (!value.IsSequence() || value.size() != components)))
							{
								error = context + ".Value has the wrong component count";
								return false;
							}
							for (size_t index = 0; index < components; ++index)
							{
								const float number = (components == 1 ? value : value[index]).as<float>();
								if (!std::isfinite(number))
								{
									error = context + ".Value contains a non-finite number";
									return false;
								}
								parameter.Value[index] = std::bit_cast<uint32_t>(number);
							}
						}
						material.Parameters.push_back(std::move(parameter));
					}
				}
				std::sort(material.Textures.begin(), material.Textures.end(),
					[](const SourceTexture& left, const SourceTexture& right)
					{ return left.Name < right.Name; });
				std::sort(material.Parameters.begin(), material.Parameters.end(),
					[](const SourceParameter& left, const SourceParameter& right)
					{ return left.Name < right.Name; });
				return true;
			}
			catch (const std::exception& exception)
			{
				error = std::string("could not parse material source: ") + exception.what();
				return false;
			}
		}

		bool ReadName(std::span<const uint8_t> bytes, uint32_t stringTable,
			uint32_t offset, std::string_view& value)
		{
			if (offset < stringTable || offset >= bytes.size())
				return false;
			const auto begin = bytes.begin() + offset;
			const auto end = std::find(begin, bytes.end(), uint8_t{ 0 });
			if (end == bytes.end())
				return false;
			value = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset),
				static_cast<size_t>(end - begin));
			return IsName(value);
		}

	}

	bool MaterialParameterView::AsBool() const noexcept
	{
		return Type == MaterialParameterType::Bool && RawValue[0] != 0;
	}

	int32_t MaterialParameterView::AsInt() const noexcept
	{
		return Type == MaterialParameterType::Int ? std::bit_cast<int32_t>(RawValue[0]) : 0;
	}

	float MaterialParameterView::AsFloat(size_t component) const noexcept
	{
		return component < ComponentCount() ? std::bit_cast<float>(RawValue[component]) : 0.0f;
	}

	size_t MaterialParameterView::ComponentCount() const noexcept
	{
		switch (Type)
		{
			case MaterialParameterType::Bool:
			case MaterialParameterType::Int:
			case MaterialParameterType::Float: return 1;
			case MaterialParameterType::Float2: return 2;
			case MaterialParameterType::Float3: return 3;
			case MaterialParameterType::Float4: return 4;
		}
		return 0;
	}

	bool MaterialParameter::AsBool() const noexcept
	{
		return RawValue[0] != 0;
	}

	int32_t MaterialParameter::AsInt() const noexcept
	{
		return static_cast<int32_t>(RawValue[0]);
	}

	float MaterialParameter::AsFloat(size_t component) const noexcept
	{
		return component < ComponentCount()
			? std::bit_cast<float>(RawValue[component]) : 0.0f;
	}

	size_t MaterialParameter::ComponentCount() const noexcept
	{
		switch (Type)
		{
			case MaterialParameterType::Bool:
			case MaterialParameterType::Int:
			case MaterialParameterType::Float: return 1;
			case MaterialParameterType::Float2: return 2;
			case MaterialParameterType::Float3: return 3;
			case MaterialParameterType::Float4: return 4;
		}
		return 0;
	}

	bool IsMaterialArtifact(std::span<const uint8_t> bytes)
	{
		return bytes.size() >= kMagic.size()
			&& std::equal(kMagic.begin(), kMagic.end(), bytes.begin());
	}

	bool ParseMaterialArtifact(std::span<const uint8_t> bytes,
		MaterialArtifactView& artifact, std::string& error)
	{
		artifact = {};
		error.clear();
		uint32_t version = 0, textureCount = 0, parameterCount = 0;
		uint32_t textureOffset = 0, parameterOffset = 0, stringOffset = 0, totalSize = 0;
		uint64_t shader = 0;
		if (bytes.size() < kHeaderSize || !IsMaterialArtifact(bytes)
			|| !ReadU32(bytes, 4, version) || version != kVersion
			|| !ReadU64(bytes, 8, shader) || shader == 0
			|| !ReadU32(bytes, 16, textureCount) || textureCount > kMaximumBindings
			|| !ReadU32(bytes, 20, parameterCount) || parameterCount > kMaximumBindings
			|| !ReadU32(bytes, 24, textureOffset)
			|| !ReadU32(bytes, 28, parameterOffset)
			|| !ReadU32(bytes, 32, stringOffset)
			|| !ReadU32(bytes, 36, totalSize) || totalSize != bytes.size())
		{
			error = "material artifact header is invalid or unsupported";
			return false;
		}
		const uint64_t expectedParameters = static_cast<uint64_t>(kHeaderSize)
			+ static_cast<uint64_t>(textureCount) * kTextureRecordSize;
		const uint64_t expectedStrings = expectedParameters
			+ static_cast<uint64_t>(parameterCount) * kParameterRecordSize;
		if (textureOffset != kHeaderSize || parameterOffset != expectedParameters
			|| stringOffset != expectedStrings || stringOffset > bytes.size())
		{
			error = "material artifact table layout is invalid";
			return false;
		}

		artifact.Shader = AssetHandle(shader);
		std::string previousName;
		for (uint32_t index = 0; index < textureCount; ++index)
		{
			const size_t record = textureOffset + static_cast<size_t>(index) * kTextureRecordSize;
			uint32_t nameOffset = 0, reserved = 0;
			uint64_t handle = 0;
			MaterialTextureBindingView binding;
			if (!ReadU32(bytes, record, nameOffset) || !ReadU32(bytes, record + 4, reserved)
				|| reserved != 0 || !ReadU64(bytes, record + 8, handle)
				|| !ReadName(bytes, stringOffset, nameOffset, binding.Name)
				|| (!previousName.empty() && binding.Name <= previousName))
			{
				error = "material artifact contains an invalid texture binding";
				artifact = {};
				return false;
			}
			previousName.assign(binding.Name);
			binding.Texture = AssetHandle(handle);
			artifact.Textures.push_back(binding);
		}
		previousName.clear();
		for (uint32_t index = 0; index < parameterCount; ++index)
		{
			const size_t record = parameterOffset + static_cast<size_t>(index) * kParameterRecordSize;
			uint32_t nameOffset = 0, type = 0;
			MaterialParameterView parameter;
			if (!ReadU32(bytes, record, nameOffset) || !ReadU32(bytes, record + 4, type)
				|| type < static_cast<uint32_t>(MaterialParameterType::Bool)
				|| type > static_cast<uint32_t>(MaterialParameterType::Float4)
				|| !ReadName(bytes, stringOffset, nameOffset, parameter.Name)
				|| (!previousName.empty() && parameter.Name <= previousName))
			{
				error = "material artifact contains an invalid parameter";
				artifact = {};
				return false;
			}
			parameter.Type = static_cast<MaterialParameterType>(type);
			for (size_t component = 0; component < parameter.RawValue.size(); ++component)
				(void)ReadU32(bytes, record + 8 + component * 4, parameter.RawValue[component]);
			for (size_t component = 0; component < parameter.ComponentCount(); ++component)
			{
				if (parameter.Type != MaterialParameterType::Bool
					&& parameter.Type != MaterialParameterType::Int
					&& !std::isfinite(parameter.AsFloat(component)))
				{
					error = "material artifact contains a non-finite parameter";
					artifact = {};
					return false;
				}
			}
			if (parameter.Type == MaterialParameterType::Bool && parameter.RawValue[0] > 1)
			{
				error = "material artifact contains an invalid boolean parameter";
				artifact = {};
				return false;
			}
			previousName.assign(parameter.Name);
			artifact.Parameters.push_back(parameter);
		}
		return true;
	}

	bool DecodeMaterialArtifact(std::span<const uint8_t> bytes,
		MaterialArtifact& artifact, std::string& error)
	{
		artifact = {};
		MaterialArtifactView view;
		if (!ParseMaterialArtifact(bytes, view, error))
			return false;

		MaterialArtifact decoded;
		decoded.Shader = view.Shader;
		decoded.Textures.reserve(view.Textures.size());
		for (const MaterialTextureBindingView& binding : view.Textures)
			decoded.Textures.push_back({ std::string(binding.Name), binding.Texture });
		decoded.Parameters.reserve(view.Parameters.size());
		for (const MaterialParameterView& parameter : view.Parameters)
		{
			decoded.Parameters.push_back({ std::string(parameter.Name),
				parameter.Type, parameter.RawValue });
		}
		artifact = std::move(decoded);
		return true;
	}

	bool ParseMaterialSourceDependencies(std::span<const uint8_t> source,
		std::vector<TypedAssetDependency>& dependencies, std::string& error)
	{
		dependencies.clear();
		SourceMaterial material;
		if (!ParseSource(source, material, error))
			return false;
		dependencies.push_back({ "Shader", material.Shader, AssetType::Shader });
		for (const SourceTexture& texture : material.Textures)
		{
			if (static_cast<uint64_t>(texture.Handle) != 0)
				dependencies.push_back({ texture.Name, texture.Handle, AssetType::Texture2D });
		}
		return true;
	}

	bool BuildMaterialArtifact(std::span<const uint8_t> source,
		std::vector<uint8_t>& artifact, std::string& error)
	{
		artifact.clear();
		SourceMaterial material;
		if (!ParseSource(source, material, error))
			return false;
		const uint64_t stringTableOffset64 = static_cast<uint64_t>(kHeaderSize)
			+ static_cast<uint64_t>(material.Textures.size()) * kTextureRecordSize
			+ static_cast<uint64_t>(material.Parameters.size()) * kParameterRecordSize;
		if (stringTableOffset64 > UINT32_MAX)
		{
			error = "material artifact tables are too large";
			return false;
		}
		const uint32_t stringTableOffset = static_cast<uint32_t>(stringTableOffset64);
		std::vector<uint8_t> strings;
		std::vector<uint32_t> textureNames, parameterNames;
		for (const SourceTexture& texture : material.Textures)
		{
			textureNames.push_back(stringTableOffset + static_cast<uint32_t>(strings.size()));
			strings.insert(strings.end(), texture.Name.begin(), texture.Name.end());
			strings.push_back(0);
		}
		for (const SourceParameter& parameter : material.Parameters)
		{
			parameterNames.push_back(stringTableOffset + static_cast<uint32_t>(strings.size()));
			strings.insert(strings.end(), parameter.Name.begin(), parameter.Name.end());
			strings.push_back(0);
		}
		const uint64_t totalSize64 = stringTableOffset64 + strings.size();
		if (totalSize64 > UINT32_MAX)
		{
			error = "material artifact is too large";
			return false;
		}
		artifact.reserve(static_cast<size_t>(totalSize64));
		artifact.insert(artifact.end(), kMagic.begin(), kMagic.end());
		AppendU32(artifact, kVersion);
		AppendU64(artifact, static_cast<uint64_t>(material.Shader));
		AppendU32(artifact, static_cast<uint32_t>(material.Textures.size()));
		AppendU32(artifact, static_cast<uint32_t>(material.Parameters.size()));
		AppendU32(artifact, kHeaderSize);
		AppendU32(artifact, kHeaderSize + static_cast<uint32_t>(material.Textures.size()) * kTextureRecordSize);
		AppendU32(artifact, stringTableOffset);
		AppendU32(artifact, static_cast<uint32_t>(totalSize64));
		for (size_t index = 0; index < material.Textures.size(); ++index)
		{
			AppendU32(artifact, textureNames[index]);
			AppendU32(artifact, 0);
			AppendU64(artifact, static_cast<uint64_t>(material.Textures[index].Handle));
		}
		for (size_t index = 0; index < material.Parameters.size(); ++index)
		{
			AppendU32(artifact, parameterNames[index]);
			AppendU32(artifact, static_cast<uint32_t>(material.Parameters[index].Type));
			for (uint32_t raw : material.Parameters[index].Value)
				AppendU32(artifact, raw);
		}
		artifact.insert(artifact.end(), strings.begin(), strings.end());
		MaterialArtifactView validation;
		if (!ParseMaterialArtifact(artifact, validation, error))
		{
			artifact.clear();
			return false;
		}
		return true;
	}

}
