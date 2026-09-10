#pragma once

#include "TomCat/Renderer/Texture.h"

#include "TomCat/Renderer/Camera.h"

#include "TomCat/Renderer/EditorCamera.h"

#include "TomCat/Scene/Components.h"

namespace TomCat {

	class Renderer2D
	{
	public:
		static void Init();
		static void Shutdown();

		static void BeginScene(const Camera& camera, const glm::mat4& transform);
		static void BeginScene(const EditorCamera& camera);
		static void EndScene();
		static void Flush();

		// Filled rectangles (quads)
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
		static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawQuad(const glm::vec3& position, const glm::vec2& size, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		static void DrawQuad(const glm::mat4& transform, const glm::vec4& color, int entityID = -1);
		static void DrawQuad(const glm::mat4& transform, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f), int entityID = -1);
		static void DrawCircle(const glm::mat4& transform, const glm::vec4& color,
			float thickness = 1.0f, float fade = 0.005f, int entityID = -1);

		static void DrawLine(const glm::vec3& start, const glm::vec3& end,
			const glm::vec4& color, int entityID = -1);
		// Rectangle outlines rendered as four lines.
		static void DrawRect(const glm::vec2& position, const glm::vec2& size,
			const glm::vec4& color, int entityID = -1);
		static void DrawRect(const glm::vec3& position, const glm::vec2& size,
			const glm::vec4& color, int entityID = -1);
		static void DrawRect(const glm::mat4& transform, const glm::vec4& color,
			int entityID = -1);

		static float GetLineWidth();
		static void SetLineWidth(float width);

		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
		static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));
		static void DrawRotatedQuad(const glm::vec3& position, const glm::vec2& size, float rotation, const Ref<Texture2D>& texture, float tilingFactor = 1.0f, const glm::vec4& tintColor = glm::vec4(1.0f));

		// Dispatches to quad or circle rendering according to SpriteRenderer::Shape.
		static void DrawSprite(const glm::mat4& transform, SpriteRenderer& src, int entityID);
		// Stats
		struct Statistics
		{
			uint32_t DrawCalls = 0;
			uint32_t QuadCount = 0;
			uint32_t CircleCount = 0;
			uint32_t LineCount = 0;

			uint32_t GetTotalVertexCount() const { return (QuadCount + CircleCount) * 4 + LineCount * 2; }
			uint32_t GetTotalIndexCount() const { return (QuadCount + CircleCount) * 6; }
		};
		static void ResetStats();
		static Statistics GetStats();
		
	private:
		static void StartBatch();
		static void NextBatch();
	};

}
