#include "tcpch.h"
#include "SceneArchiveCodec.h"

#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"

namespace TomCat {

	bool SceneArchiveCodec::Encode(const Ref<Scene>& scene, std::string& document,
		std::string& error)
	{
		if (!scene)
		{
			document.clear();
			error = "Cannot encode a null Scene";
			return false;
		}
		return SceneSerializer(scene).SerializeDocument(document, error);
	}

	bool SceneArchiveCodec::Decode(const std::vector<uint8_t>& bytes,
		const Ref<Scene>& destination,
		const std::filesystem::path& diagnosticPath, bool resolveAssets)
	{
		if (!destination)
			return false;
		return SceneSerializer(destination).DeserializeDocument(bytes,
			diagnosticPath, resolveAssets);
	}

}
