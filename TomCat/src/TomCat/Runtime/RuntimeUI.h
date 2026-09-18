#pragma once

#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Components.h"

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include <entt.hpp>
#include <glm/glm.hpp>

namespace TomCat {

	class Entity;
	class Scene;

	struct UIRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;

		bool IsEmpty() const { return Width <= 0.0f || Height <= 0.0f; }
		bool Contains(const glm::vec2& point) const;
		glm::vec4 ToVector() const { return { X, Y, Width, Height }; }
		static UIRect Intersect(const UIRect& first, const UIRect& second);
	};

	struct TextGlyphQuad
	{
		uint32_t Codepoint = 0;
		UIRect Rect;
		glm::vec2 UVMin{ 0.0f };
		glm::vec2 UVMax{ 0.0f };
		bool UsesFallback = false;
	};

	struct UIImageGeometry
	{
		UIRect Rect;
		glm::vec2 UVMin{ 0.0f };
		glm::vec2 UVMax{ 1.0f };
	};

	struct TextLayoutResult
	{
		float Width = 0.0f;
		float Height = 0.0f;
		uint32_t LineCount = 0;
		std::vector<TextGlyphQuad> Glyphs;
	};

	class TextLayoutEngine final
	{
	public:
		static TextLayoutResult Build(const FontAtlasData& atlas,
			std::string_view utf8, float fontSize, float maximumWidth,
			TextAlignment alignment, float lineSpacing = 1.0f);
	};

	struct RuntimeUILayoutSnapshot
	{
		uint32_t ViewportWidth = 0;
		uint32_t ViewportHeight = 0;
		float DPI = 96.0f;
		std::map<UUID, UIRect> Rectangles;
		std::map<UUID, UIRect> Clips;
		std::map<UUID, float> Scales;
		std::vector<UUID> RenderOrder;
	};

	enum class RuntimeUIVisibilityMode : uint8_t
	{
		Gameplay = 0,
		Editor
	};

	// Deterministic, viewport-local input used by both the live ScriptEngine
	// snapshot and headless regressions. PointerPosition uses a top-left origin.
	struct RuntimeUIInputFrame
	{
		glm::vec2 PointerPosition{ -1.0f, -1.0f };
		bool WindowFocused = true;
		bool MousePressed = false;
		bool MouseHeld = false;
		bool MouseReleased = false;
		bool KeyboardMoveNext = false;
		bool KeyboardMoveNextHeld = false;
		bool KeyboardMoveNextReleased = false;
		bool KeyboardMovePrevious = false;
		bool KeyboardMovePreviousHeld = false;
		bool KeyboardMovePreviousReleased = false;
		bool KeyboardSubmit = false;
		bool KeyboardSubmitHeld = false;
		bool KeyboardSubmitReleased = false;
		bool GamepadMoveNext = false;
		bool GamepadMoveNextHeld = false;
		bool GamepadMoveNextReleased = false;
		bool GamepadMovePrevious = false;
		bool GamepadMovePreviousHeld = false;
		bool GamepadMovePreviousReleased = false;
		bool GamepadSubmit = false;
		bool GamepadSubmitHeld = false;
		bool GamepadSubmitReleased = false;
	};

	class RuntimeUISystem final
	{
	public:
		static RuntimeUILayoutSnapshot BuildLayout(Scene& scene,
			entt::registry& registry, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static RuntimeUILayoutSnapshot BuildLayout(Scene& scene,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static glm::vec2 MapPointerToViewport(const glm::vec2& screenPosition,
			const glm::vec2& viewportOrigin,
			const glm::vec2& screenToFramebufferScale = glm::vec2(1.0f));
		static bool BuildImageGeometry(const UIRect& rectangle,
			const UIRect& clip, float sourceAspect, const glm::vec2& uvMin,
			const glm::vec2& uvMax, bool preserveAspect, UIImageGeometry& output);
		static void Reset(entt::registry& registry);
		// Freeze whether Runtime UI owns the fixed input batch that is active on
		// ScriptEngine. Call this after BeginFixedStep and before managed actions.
		// Repeated calls before the next display Update are intentionally idempotent
		// so every catch-up substep observes the same capture decision.
		static void PrepareFixedInputCapture(Scene& scene,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			glm::vec2 viewportOrigin = glm::vec2(0.0f),
			glm::vec2 screenToFramebufferScale = glm::vec2(1.0f));
		// Deterministic overload for headless regression coverage. This only
		// publishes the fixed capture snapshot; it never dispatches UI events or
		// mutates focus, pressed, hover, click, or control ownership state.
		static void PrepareFixedInputCaptureWithInput(Scene& scene,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
			const RuntimeUIInputFrame& input);
		static void Update(Scene& scene, entt::registry& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			glm::vec2 viewportOrigin = glm::vec2(0.0f),
			glm::vec2 screenToFramebufferScale = glm::vec2(1.0f));
		static void UpdateWithInput(Scene& scene, entt::registry& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
			const RuntimeUIInputFrame& input);
		static void UpdateWithInput(Scene& scene, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi, const RuntimeUIInputFrame& input);
		static void RenderWorldText(Scene& scene, entt::registry& registry,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderWorldText(Scene& scene,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderScreen(Scene& scene, entt::registry& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderScreen(Scene& scene, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);

		static bool IsGameplayInputCaptured();
		static bool WasButtonClicked(Entity entity);
		static uint64_t GetButtonClickSerial(Entity entity);
		static bool FocusButton(Scene& scene, entt::registry& registry, Entity entity);
		static bool FocusButton(Scene& scene, Entity entity);
	};

}
