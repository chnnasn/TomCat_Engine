#pragma once

#include "TomCat/Core/Base.h"
#include "TomCat/Renderer/Camera.h"
#include "TomCat/Renderer/EditorCamera.h"
#include "TomCat/Renderer/Mesh.h"
#include "TomCat/Renderer/Texture.h"

#include <glm/glm.hpp>
#include <vector>

namespace TomCat {

	class Model;

	class Renderer3D
	{
	public:
		struct Light
		{
			glm::vec3 Position{0}, Direction{0, -1, 0}, Color{1};
			int Type = 0;
			float Intensity = 3, Range = 10, InnerAngle = 20, OuterAngle = 30;
			bool CastShadows = true;
			float ShadowBias = 0.002f, ShadowExtent = 30;
		};
		struct Environment
		{
			bool ShowSky = false;
			Ref<Texture2D> Panorama;
			glm::vec3 SkyColor{0.3f, 0.5f, 0.8f}, GroundColor{0.08f, 0.07f, 0.06f};
			float Intensity = 1, AmbientIntensity = 0.3f, Rotation = 0, Exposure = 1;
		};
		struct Surface
		{
			float Metallic = 0, Roughness = 0.5f, AmbientOcclusion = 1;
			glm::vec3 Emission{0};
			bool CastShadows = true, ReceiveShadows = true;
		};
		// Call between BeginScene and submission. An empty list intentionally disables direct light.
		static void SetLighting(const std::vector<Light>& lights, const Environment& environment);
		static void SetSurface(const Surface& surface);
		static void Init();
		static void Shutdown();

		static void BeginScene(const EditorCamera& camera);
		static void BeginScene(const Camera& camera, const glm::mat4& cameraTransform);
		static void EndScene();
		static void Flush();

		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
			const Ref<Texture2D>& albedoTexture, const glm::vec4& color, bool useTexture, int entityID = -1);

		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID = -1, const glm::vec4& tint = glm::vec4(1.0f), const Ref<Texture2D>& texture = nullptr);

		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t MeshCount = 0;
		};

		static void ResetStats();
		static Statistics GetStats();
	};

}
