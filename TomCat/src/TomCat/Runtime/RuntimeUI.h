#pragma once

#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Components.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <vector>

#include "TomCat/Scene/SceneWorld.h"
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

	struct RuntimeUIClipRegion
	{
		UIRect Rectangle;
		glm::mat4 Transform{ 1.0f };
	};

	struct RuntimeUILayoutSnapshot
	{
		uint32_t ViewportWidth = 0;
		uint32_t ViewportHeight = 0;
		float DPI = 96.0f;
		std::map<UUID, UIRect> Rectangles;
		// Layout-space compatibility/diagnostic rectangles. Transformed rendering
		// and hit testing use ClipRegions, where every mask keeps its own transform.
		std::map<UUID, UIRect> Clips;
		std::map<UUID, float> Scales;
		// Maps the authored, unrotated screen rectangle into its accumulated
		// RectTransform rotation/scale space. Translation remains anchor driven.
		std::map<UUID, glm::mat4> Transforms;
		// Ordered ancestor masks in the coordinate space where each mask was
		// authored. Rendering and hit testing apply every region after transforms,
		// so a rotated child remains clipped by its parent's visible rectangle.
		std::map<UUID, std::vector<RuntimeUIClipRegion>> ClipRegions;
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
		glm::vec2 ScrollDelta{ 0.0f };
		std::string TextInput;
		bool Backspace = false;
		bool Delete = false;
		bool CaretLeft = false;
		bool CaretRight = false;
		bool CaretHome = false;
		bool CaretEnd = false;
		bool SelectAll = false;
		bool ExtendSelection = false;
		bool Cancel = false;
		bool FocusNext = false;
		bool FocusPrevious = false;
		bool Copy = false;
		bool Cut = false;
		bool Paste = false;
		std::string ClipboardText;
		// Live input binds the platform clipboard; tests can supply an isolated sink.
		std::function<bool(const std::string&)> WriteClipboard;
		// Zero treats each injected frame as a distinct transaction. Live updates
		// carry Input's display frame ID to avoid replaying text on repeated Update.
		uint64_t DisplayFrame = 0;
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
		// Screen-space Canvas content is authored on a stable plane in Scene view.
		// Keeping the conversion public gives editor gizmos, framing, rendering and
		// regression tests one shared coordinate contract.
		static constexpr float EditorCanvasPixelsPerUnit = 100.0f;
		static glm::mat4 GetEditorCanvasTransform(
			const glm::vec2& referenceResolution);
		static RuntimeUILayoutSnapshot BuildLayout(Scene& scene,
			SceneWorld& registry, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static RuntimeUILayoutSnapshot BuildLayout(Scene& scene,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static RuntimeUILayoutSnapshot BuildEditorLayout(Scene& scene,
			SceneWorld& registry,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Editor);
		static RuntimeUILayoutSnapshot BuildEditorLayout(Scene& scene,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Editor);
		static glm::vec2 MapPointerToViewport(const glm::vec2& screenPosition,
			const glm::vec2& viewportOrigin,
			const glm::vec2& screenToFramebufferScale = glm::vec2(1.0f));
		static bool BuildImageGeometry(const UIRect& rectangle,
			const UIRect& clip, float sourceAspect, const glm::vec2& uvMin,
			const glm::vec2& uvMax, bool preserveAspect, UIImageGeometry& output);
		static void Reset(SceneWorld& registry);
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
		static void Update(Scene& scene, SceneWorld& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			glm::vec2 viewportOrigin = glm::vec2(0.0f),
			glm::vec2 screenToFramebufferScale = glm::vec2(1.0f));
		static void UpdateWithInput(Scene& scene, SceneWorld& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
			const RuntimeUIInputFrame& input);
		static void UpdateWithInput(Scene& scene, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi, const RuntimeUIInputFrame& input);
		static void RenderWorldText(Scene& scene, SceneWorld& registry,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderWorldText(Scene& scene,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderScreen(Scene& scene, SceneWorld& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderScreen(Scene& scene, uint32_t viewportWidth,
			uint32_t viewportHeight, float dpi = 96.0f,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Gameplay);
		static void RenderEditorCanvas(Scene& scene, SceneWorld& registry,
			const glm::mat4& editorViewProjection,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Editor);
		static void RenderEditorCanvas(Scene& scene,
			const glm::mat4& editorViewProjection,
			RuntimeUIVisibilityMode visibility = RuntimeUIVisibilityMode::Editor);

		static bool IsGameplayInputCaptured();
		// Resolves the nearest enabled localization scope, then its fallback locale.
		// Missing keys retain the authored UIText.Text value.
		static std::string ResolveText(Scene& scene, Entity entity);
		static bool SetSliderValue(Entity entity, float value);
		static bool WasButtonClicked(Entity entity);
		static uint64_t GetButtonClickSerial(Entity entity);
		static bool FocusButton(Scene& scene, SceneWorld& registry, Entity entity);
		static bool FocusButton(Scene& scene, Entity entity);
	};

}
