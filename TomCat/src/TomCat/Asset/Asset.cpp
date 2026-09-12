#include "tcpch.h"
#include "Asset.h"

#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace TomCat {

	const char* AssetTypeToString(AssetType type)
	{
		switch (type)
		{
			case AssetType::None: return "None";
			case AssetType::Scene: return "Scene";
			case AssetType::Texture2D: return "Texture2D";
			case AssetType::Shader: return "Shader";
			case AssetType::Audio: return "Audio";
			case AssetType::Font: return "Font";
			case AssetType::Mesh: return "Mesh";
			case AssetType::Material: return "Material";
			case AssetType::CSharpScript: return "CSharpScript";
			case AssetType::Other: return "Other";
			case AssetType::Prefab: return "Prefab";
		}
		return "None";
	}

	AssetType AssetTypeFromString(const std::string& value)
	{
		if (value == "Scene") return AssetType::Scene;
		if (value == "Texture2D") return AssetType::Texture2D;
		if (value == "Shader") return AssetType::Shader;
		if (value == "Audio") return AssetType::Audio;
		if (value == "Font") return AssetType::Font;
		if (value == "Mesh") return AssetType::Mesh;
		if (value == "Material") return AssetType::Material;
		if (value == "CSharpScript") return AssetType::CSharpScript;
		// Read old sidecars once; Refresh rewrites them according to the source
		// extension, so .cs becomes CSharpScript and native/Lua source becomes Other.
		if (value == "Script") return AssetType::CSharpScript;
		if (value == "Other") return AssetType::Other;
		if (value == "Prefab") return AssetType::Prefab;
		return AssetType::None;
	}

	AssetType AssetTypeFromPath(const std::filesystem::path& path)
	{
		std::string extension = PathToUTF8(path.extension());
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });

		if (extension == ".tomcat") return AssetType::Scene;
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
			extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
			extension == ".psd" || extension == ".hdr" || extension == ".pic")
			return AssetType::Texture2D;
		if (extension == ".glsl" || extension == ".vert" || extension == ".frag" ||
			extension == ".comp" || extension == ".hlsl")
			return AssetType::Shader;
		if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" ||
			extension == ".flac")
			return AssetType::Audio;
		if (extension == ".ttf" || extension == ".otf" || extension == ".woff" ||
			extension == ".woff2")
			return AssetType::Font;
		if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" ||
			extension == ".glb")
			return AssetType::Mesh;
		if (extension == ".tcmat") return AssetType::Material;
		if (extension == ".cs") return AssetType::CSharpScript;
		if (extension == ".tcprefab") return AssetType::Prefab;
		return AssetType::Other;
	}

}
