#include "tcpch.h"
#include "AssetReferenceVisitor.h"

#include "TomCat/Scripting/ScriptField.h"

#include <yaml-cpp/yaml.h>

#include <exception>

namespace TomCat {

	namespace {

		bool TryResolveScriptAssetType(const YAML::Node& field,
			AssetType& expectedType, std::string& unsupportedTypeName)
		{
			expectedType = AssetType::None;
			unsupportedTypeName.clear();
			const YAML::Node typeName = field["TypeName"];
			if (!typeName || typeName.IsNull())
				return true;
			if (!typeName.IsScalar())
			{
				unsupportedTypeName = "must be a scalar or null";
				return false;
			}
			const std::string value = typeName.as<std::string>();
			if (value.empty())
				return true;
			if (value == "TomCat.SceneAsset"
				|| value == "TomCat.AssetRef<TomCat.SceneAsset>")
			{
				expectedType = AssetType::Scene;
				return true;
			}
			if (value == "TomCat.PrefabAsset"
				|| value == "TomCat.AssetRef<TomCat.PrefabAsset>")
			{
				expectedType = AssetType::Prefab;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.Texture2DAsset>")
			{
				expectedType = AssetType::Texture2D;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.ShaderAsset>")
			{
				expectedType = AssetType::Shader;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.AudioAsset>")
			{
				expectedType = AssetType::Audio;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.FontAsset>")
			{
				expectedType = AssetType::Font;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.MeshAsset>")
			{
				expectedType = AssetType::Mesh;
				return true;
			}
			if (value == "TomCat.AssetRef<TomCat.MaterialAsset>")
			{
				expectedType = AssetType::Material;
				return true;
			}
			unsupportedTypeName = "'" + value + "' is not a supported asset type";
			return false;
		}

		bool VisitHandle(const YAML::Node& owner, const char* key,
			const std::string& propertyPath, AssetType expectedType,
			SerializedAssetReferenceKind kind, bool required,
			const AssetReferenceVisitor::Visitor& visitor,
			std::string& errorMessage)
		{
			const YAML::Node value = owner[key];
			if (!value || !value.IsScalar())
			{
				errorMessage = propertyPath + "." + key
					+ " must be an AssetHandle scalar";
				return false;
			}

			SerializedAssetReference reference;
			reference.Handle = AssetHandle(value.as<uint64_t>());
			reference.ExpectedType = expectedType;
			reference.Kind = kind;
			reference.PropertyPath = propertyPath + "." + key;
			reference.Required = required;
			if (!visitor(reference))
			{
				if (errorMessage.empty())
					errorMessage = "Asset reference was rejected at "
						+ reference.PropertyPath;
				return false;
			}
			return true;
		}

	}

	bool AssetReferenceVisitor::VisitScene(const YAML::Node& sceneDocument,
		const Visitor& visitor, std::string& errorMessage)
	{
		errorMessage.clear();
		if (!visitor)
		{
			errorMessage = "Asset reference visitor callback is empty";
			return false;
		}
		try
		{
			if (!sceneDocument || !sceneDocument.IsMap())
			{
				errorMessage = "Scene document must be a map";
				return false;
			}
			const YAML::Node entities = sceneDocument["Entities"];
			if (!entities)
			{
				errorMessage = "Scene document is missing Entities";
				return false;
			}
			return VisitEntities(entities, "$.Entities", visitor, errorMessage);
		}
		catch (const std::exception& error)
		{
			errorMessage = std::string("Could not inspect scene asset references: ")
				+ error.what();
			return false;
		}
	}

	bool AssetReferenceVisitor::VisitPrefab(const YAML::Node& prefabDocument,
		const Visitor& visitor, std::string& errorMessage)
	{
		errorMessage.clear();
		if (!visitor)
		{
			errorMessage = "Asset reference visitor callback is empty";
			return false;
		}
		try
		{
			if (!prefabDocument || !prefabDocument.IsMap())
			{
				errorMessage = "Prefab document must be a map";
				return false;
			}
			const YAML::Node entities = prefabDocument["Entities"];
			if (!entities)
			{
				errorMessage = "Prefab document is missing Entities";
				return false;
			}
			return VisitEntities(entities, "$.Entities", visitor, errorMessage);
		}
		catch (const std::exception& error)
		{
			errorMessage = std::string("Could not inspect Prefab asset references: ")
				+ error.what();
			return false;
		}
	}

	bool AssetReferenceVisitor::VisitEntities(const YAML::Node& entities,
		const std::string& propertyPath, const Visitor& visitor,
		std::string& errorMessage)
	{
		errorMessage.clear();
		if (!visitor)
		{
			errorMessage = "Asset reference visitor callback is empty";
			return false;
		}
		try
		{
			if (!entities || !entities.IsSequence())
			{
				errorMessage = propertyPath + " must be an entity array";
				return false;
			}

			for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
			{
				const YAML::Node entity = entities[entityIndex];
				const std::string entityPath = propertyPath + "["
					+ std::to_string(entityIndex) + "]";
				if (!entity.IsMap())
				{
					errorMessage = entityPath + " must be a map";
					return false;
				}

				const YAML::Node sprite = entity["SpriteRenderer"];
				if (sprite && (!sprite.IsMap() || !VisitHandle(sprite,
					"SpriteHandle", entityPath + ".SpriteRenderer",
					AssetType::Texture2D, SerializedAssetReferenceKind::Sprite,
					false, visitor, errorMessage)))
					return false;

				const YAML::Node csharpScripts = entity["CSharpScripts"];
				if (!csharpScripts)
					continue;
				if (!csharpScripts.IsMap())
				{
					errorMessage = entityPath + ".CSharpScripts must be a map";
					return false;
				}
				const YAML::Node scripts = csharpScripts["Scripts"];
				if (!scripts || !scripts.IsSequence())
				{
					errorMessage = entityPath
						+ ".CSharpScripts.Scripts must be an array";
					return false;
				}

				for (size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex)
				{
					const YAML::Node script = scripts[scriptIndex];
					const std::string scriptPath = entityPath
						+ ".CSharpScripts.Scripts[" + std::to_string(scriptIndex)
						+ "]";
					if (!script.IsMap() || !VisitHandle(script, "ScriptHandle",
						scriptPath, AssetType::CSharpScript,
						SerializedAssetReferenceKind::CSharpScript, true, visitor,
						errorMessage))
						return false;

					const YAML::Node fields = script["Fields"];
					if (!fields || !fields.IsSequence())
					{
						errorMessage = scriptPath + ".Fields must be an array";
						return false;
					}
					for (size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex)
					{
						const YAML::Node field = fields[fieldIndex];
						const std::string fieldPath = scriptPath + ".Fields["
							+ std::to_string(fieldIndex) + "]";
						if (!field.IsMap() || !field["Type"] ||
							!field["Type"].IsScalar())
						{
							errorMessage = fieldPath + ".Type must be a scalar";
							return false;
						}
						ScriptFieldType fieldType;
						if (!TryParseScriptFieldType(
							field["Type"].as<std::string>(), fieldType))
						{
							errorMessage = fieldPath
								+ ".Type is not a supported script field type";
							return false;
						}
						if (fieldType == ScriptFieldType::AssetRef)
						{
							AssetType expectedType = AssetType::None;
							std::string typeNameError;
							if (!TryResolveScriptAssetType(field, expectedType, typeNameError))
							{
								errorMessage = fieldPath + ".TypeName " + typeNameError;
								return false;
							}
							if (!VisitHandle(field, "Value", fieldPath, expectedType,
								SerializedAssetReferenceKind::ScriptField, false,
								visitor, errorMessage))
								return false;
						}
					}
				}
			}
			return true;
		}
		catch (const std::exception& error)
		{
			errorMessage = std::string("Could not inspect serialized entity asset references: ")
				+ error.what();
			return false;
		}
	}

}
