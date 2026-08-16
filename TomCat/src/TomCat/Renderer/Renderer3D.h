#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Camera.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Renderer/Mesh.h"
#include "TomCat/Renderer/Texture.h"

#include <glm/glm.hpp>

namespace TomCat {

	class Model;

	class Renderer3D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const EditorCamera& camera);
		static void BeginScene(const Camera& camera, const glm::mat4& cameraTransform);
		static void EndScene();
		static void Flush();

		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
			const Ref<Texture2D>& albedoTexture, const glm::vec4& color, bool useTexture, int entityID = -1);

		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID = -1);

		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t MeshCount = 0;
		};

		static void ResetStats();
		static Statistics GetStats();
	};

}
