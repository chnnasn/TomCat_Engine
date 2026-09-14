#include "tcpch.h"
#include "RuntimeUI.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Renderer/Camera.h"
#include "TomCat/Renderer/RenderCommand.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scripting/ScriptEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <optional>
#include <set>

#include <glm/gtc/matrix_transform.hpp>

namespace TomCat {

	namespace {

		std::atomic_bool s_GameplayInputCaptured = false;
		Scene* s_PointerCaptureScene = nullptr;
		std::optional<UUID> s_PointerCaptureTarget;
		Scene* s_ControlOwnershipScene = nullptr;
		uint32_t s_ControlOwnership = 0;
		Scene* s_FixedCaptureStateScene = nullptr;
		bool s_PendingFixedGameplayInputCapture = false;
		bool s_FixedCapturePrepared = false;

		constexpr uint32_t KeyboardMoveNextOwner = 1u << 0;
		constexpr uint32_t KeyboardMovePreviousOwner = 1u << 1;
		constexpr uint32_t KeyboardSubmitOwner = 1u << 2;
		constexpr uint32_t GamepadMoveNextOwner = 1u << 3;
		constexpr uint32_t GamepadMovePreviousOwner = 1u << 4;
		constexpr uint32_t GamepadSubmitOwner = 1u << 5;

		void ClearPointerCapture()
		{
			s_PointerCaptureScene = nullptr;
			s_PointerCaptureTarget.reset();
		}

		bool IsVisible(Scene& scene, Entity entity,
			RuntimeUIVisibilityMode visibility)
		{
			return visibility == RuntimeUIVisibilityMode::Editor
				? scene.IsVisibleInEditorHierarchy(entity)
				: scene.IsActiveInHierarchy(entity);
		}

		void ClearControlOwnership()
		{
			s_ControlOwnershipScene = nullptr;
			s_ControlOwnership = 0;
		}

		void NormalizeControlOwnership()
		{
			if (s_ControlOwnership == 0)
				s_ControlOwnershipScene = nullptr;
		}

		void ClearFixedCaptureState()
		{
			s_FixedCaptureStateScene = nullptr;
			s_PendingFixedGameplayInputCapture = false;
			s_FixedCapturePrepared = false;
		}

		bool Finite(float value) { return std::isfinite(value); }
		bool Finite(const glm::vec2& value) { return Finite(value.x) && Finite(value.y); }
		bool Finite(const glm::vec4& value)
		{
			return Finite(value.x) && Finite(value.y) && Finite(value.z) && Finite(value.w);
		}

		float CalculateCanvasScale(const Canvas& canvas, uint32_t width,
			uint32_t height, float dpi)
		{
			if (width == 0 || height == 0 || !Finite(dpi) || dpi <= 0.0f
				|| !Finite(canvas.ScaleFactor) || canvas.ScaleFactor <= 0.0f
				|| !Finite(canvas.ReferenceDPI) || canvas.ReferenceDPI <= 0.0f)
				return 1.0f;
			float screenScale = 1.0f;
			if (canvas.ScaleMode == CanvasScaleMode::ScaleWithScreenSize
				&& Finite(canvas.ReferenceResolution)
				&& canvas.ReferenceResolution.x > 0.0f
				&& canvas.ReferenceResolution.y > 0.0f)
			{
				const float widthScale = static_cast<float>(width)
					/ canvas.ReferenceResolution.x;
				const float heightScale = static_cast<float>(height)
					/ canvas.ReferenceResolution.y;
				const float match = std::clamp(canvas.MatchWidthOrHeight, 0.0f, 1.0f);
				screenScale = std::exp(std::log(std::max(widthScale, 1.0e-6f))
					* (1.0f - match) + std::log(std::max(heightScale, 1.0e-6f)) * match);
			}
			return std::clamp(screenScale * canvas.ScaleFactor
				* (dpi / canvas.ReferenceDPI), 0.01f, 100.0f);
		}

		UIRect ResolveAnchors(const RectTransform& transform,
			const UIRect& parent, float scale)
		{
			const glm::vec2 parentMinimum(parent.X, parent.Y);
			const glm::vec2 parentSize(parent.Width, parent.Height);
			const glm::vec2 anchorMinimum = parentMinimum
				+ transform.AnchorMin * parentSize;
			const glm::vec2 anchorMaximum = parentMinimum
				+ transform.AnchorMax * parentSize;
			const glm::vec2 size = glm::max(glm::vec2(0.0f),
				anchorMaximum - anchorMinimum + transform.SizeDelta * scale);
			// For stretched anchors, AnchoredPosition is measured from the point in
			// the anchor rectangle selected by Pivot. Using the anchor midpoint here
			// shifts every non-centred pivot by part of the parent's size.
			const glm::vec2 anchorReference = anchorMinimum
				+ (anchorMaximum - anchorMinimum) * transform.Pivot;
			const glm::vec2 minimum = anchorReference
				+ transform.AnchoredPosition * scale - transform.Pivot * size;
			return { minimum.x, minimum.y, size.x, size.y };
		}

		bool ClipQuad(const UIRect& original, const UIRect& clip,
			const glm::vec2& uvMin, const glm::vec2& uvMax, UIRect& clipped,
			glm::vec2& clippedUVMin, glm::vec2& clippedUVMax)
		{
			clipped = UIRect::Intersect(original, clip);
			if (original.IsEmpty() || clipped.IsEmpty())
				return false;
			const float x0 = (clipped.X - original.X) / original.Width;
			const float y0 = (clipped.Y - original.Y) / original.Height;
			const float x1 = (clipped.X + clipped.Width - original.X) / original.Width;
			const float y1 = (clipped.Y + clipped.Height - original.Y) / original.Height;
			clippedUVMin = glm::mix(uvMin, uvMax, glm::vec2(x0, y0));
			clippedUVMax = glm::mix(uvMin, uvMax, glm::vec2(x1, y1));
			return true;
		}

		glm::mat4 QuadTransform(const UIRect& rectangle, float z = 0.0f)
		{
			return glm::translate(glm::mat4(1.0f), {
				rectangle.X + rectangle.Width * 0.5f,
				rectangle.Y + rectangle.Height * 0.5f, z })
				* glm::scale(glm::mat4(1.0f), {
					rectangle.Width, rectangle.Height, 1.0f });
		}

		glm::vec4 MultiplyColor(const glm::vec4& first, const glm::vec4& second)
		{
			return first * second;
		}

		void DrawScreenText(const UIText& text, const UIRect& rectangle,
			const UIRect& clip, float canvasScale, int entityID)
		{
			if (!text.Enabled || text.Text.empty() || !Finite(text.FontSize)
				|| text.FontSize <= 0.0f || !Finite(canvasScale)
				|| canvasScale <= 0.0f || !Finite(text.Color))
				return;
			Ref<RuntimeFont> font = FontManager::Get().Load(text.Font, text.Text,
				text.FallbackFont, text.EmojiFont);
			if (!font || !font->GetTexture())
				return;
			const TextLayoutResult layout = TextLayoutEngine::Build(font->GetAtlas(),
				text.Text, text.FontSize * canvasScale,
				text.Wrap ? rectangle.Width : 0.0f,
				text.Alignment, text.LineSpacing);
			const glm::vec2 origin(rectangle.X,
				rectangle.Y + std::max(0.0f, rectangle.Height - layout.Height));
			for (const TextGlyphQuad& glyph : layout.Glyphs)
			{
				const UIRect original{ origin.x + glyph.Rect.X,
					origin.y + glyph.Rect.Y, glyph.Rect.Width, glyph.Rect.Height };
				UIRect visible;
				glm::vec2 uvMin, uvMax;
				if (!ClipQuad(original, clip, glyph.UVMin, glyph.UVMax,
					visible, uvMin, uvMax))
					continue;
				Renderer2D::DrawTexturedQuadRegion(QuadTransform(visible),
					font->GetTexture(), uvMin, uvMax, text.Color, entityID);
			}
		}

		struct LayoutBuilder
		{
			Scene& SceneValue;
			entt::registry& Registry;
			RuntimeUILayoutSnapshot& Snapshot;
			RuntimeUIVisibilityMode Visibility;
			std::set<uint64_t> Visited;

			void LayoutChildren(Entity parent, const UIRect& parentRect,
				const UIRect& inheritedClip, float scale)
			{
				UILayoutGroup* group = parent.HasComponent<UILayoutGroup>()
					? &parent.GetComponent<UILayoutGroup>() : nullptr;
				float horizontalCursor = parentRect.X
					+ (group ? group->Padding.x * scale : 0.0f);
				float verticalCursor = parentRect.Y + parentRect.Height
					- (group ? group->Padding.w * scale : 0.0f);
				for (UUID childID : SceneValue.GetChildrenUUIDs(parent))
				{
					Entity child = SceneValue.FindEntityByUUID(childID);
					if (!child || !child.HasComponent<RectTransform>()
						|| child.HasComponent<Canvas>()
						|| !IsVisible(SceneValue, child, Visibility)
						|| !Visited.emplace(static_cast<uint64_t>(childID)).second)
						continue;
					auto& transform = child.GetComponent<RectTransform>();
					UIRect rectangle = ResolveAnchors(transform, parentRect, scale);
					if (group && group->Enabled)
					{
						glm::vec2 controlled(rectangle.Width, rectangle.Height);
						if (group->ControlChildSize && Finite(group->ChildSize))
							controlled = glm::max(glm::vec2(0.0f), group->ChildSize * scale);
						if (group->Direction == UILayoutDirection::Horizontal)
						{
							rectangle = { horizontalCursor,
								parentRect.Y + group->Padding.y * scale,
								controlled.x, controlled.y };
							horizontalCursor += controlled.x + group->Spacing * scale;
						}
						else
						{
							verticalCursor -= controlled.y;
							rectangle = { parentRect.X + group->Padding.x * scale,
								verticalCursor, controlled.x, controlled.y };
							verticalCursor -= group->Spacing * scale;
						}
					}
					const UIRect clip = UIRect::Intersect(rectangle, inheritedClip);
					transform.RuntimeRect = rectangle.ToVector();
					transform.RuntimeClipRect = clip.ToVector();
					Snapshot.Rectangles[childID] = rectangle;
					Snapshot.Clips[childID] = clip;
					Snapshot.Scales[childID] = scale;
					Snapshot.RenderOrder.push_back(childID);
					const UIRect childClip = transform.ClipChildren ? clip : inheritedClip;
					LayoutChildren(child, rectangle, childClip, scale);
				}
			}
		};

		bool WouldCaptureGameplayInput(Scene& scene, entt::registry& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
			const RuntimeUIInputFrame& input)
		{
			UIEventSystem* eventSystem = nullptr;
			uint64_t eventSystemID = (std::numeric_limits<uint64_t>::max)();
			for (const entt::entity value : registry.view<UIEventSystem, ID>())
			{
				Entity entity(value, &scene);
				auto& candidate = registry.get<UIEventSystem>(value);
				const uint64_t id = static_cast<uint64_t>(entity.GetUUID());
				if (candidate.Enabled && scene.IsActiveInHierarchy(entity)
					&& id < eventSystemID)
				{
					eventSystem = &candidate;
					eventSystemID = id;
				}
			}
			if (!eventSystem || !eventSystem->ConsumeGameplayInput
				|| !input.WindowFocused)
				return false;

			const RuntimeUILayoutSnapshot layout = RuntimeUISystem::BuildLayout(
				scene, registry, viewportWidth, viewportHeight, dpi);
			std::vector<Entity> buttons;
			for (UUID id : layout.RenderOrder)
			{
				Entity entity = scene.FindEntityByUUID(id);
				if (!entity || !entity.HasComponent<UIButton>()
					|| !entity.HasComponent<RectTransform>())
					continue;
				const auto& button = entity.GetComponent<UIButton>();
				if (button.Enabled && button.Interactable
					&& scene.IsActiveInHierarchy(entity))
					buttons.push_back(entity);
			}

			const glm::vec2 mousePoint(input.PointerPosition.x,
				static_cast<float>(viewportHeight) - input.PointerPosition.y);
			bool pointerHandled = false;
			for (auto item = layout.RenderOrder.rbegin();
				item != layout.RenderOrder.rend(); ++item)
			{
				Entity target = scene.FindEntityByUUID(*item);
				const auto rectangle = layout.Rectangles.find(*item);
				const auto clip = layout.Clips.find(*item);
				if (!target || rectangle == layout.Rectangles.end()
					|| clip == layout.Clips.end()
					|| !UIRect::Intersect(rectangle->second, clip->second)
						.Contains(mousePoint))
					continue;
				const bool imageTarget = target.HasComponent<UIImage>()
					&& target.GetComponent<UIImage>().Enabled
					&& target.GetComponent<UIImage>().RaycastTarget;
				const bool textTarget = target.HasComponent<UIText>()
					&& target.GetComponent<UIText>().Enabled
					&& target.GetComponent<UIText>().RaycastTarget;
				if (!imageTarget && !textTarget)
					continue;
				pointerHandled = true;
				break;
			}

			const bool hadPressed = std::any_of(buttons.begin(), buttons.end(),
				[](Entity entity)
				{ return entity.GetComponent<UIButton>().RuntimePressed; });
			bool validPointerCapture = false;
			if (s_PointerCaptureScene == &scene && s_PointerCaptureTarget)
			{
				Entity captured = scene.FindEntityByUUID(*s_PointerCaptureTarget);
				validPointerCapture = captured && scene.IsActiveInHierarchy(captured)
					&& ((captured.HasComponent<UIImage>()
							&& captured.GetComponent<UIImage>().Enabled
							&& captured.GetComponent<UIImage>().RaycastTarget)
						|| (captured.HasComponent<UIText>()
							&& captured.GetComponent<UIText>().Enabled
							&& captured.GetComponent<UIText>().RaycastTarget));
			}
			// Mirror UpdateWithInput's capture transition without mutating it:
			// a new press replaces the old target, while an idle frame releases it.
			if (input.MousePressed)
				validPointerCapture = pointerHandled;
			else if (!input.MouseHeld && !input.MouseReleased)
				validPointerCapture = false;
			bool handled = (input.MousePressed && pointerHandled)
				|| (input.MouseHeld && (validPointerCapture || hadPressed))
				|| (input.MouseReleased
					&& (pointerHandled || validPointerCapture || hadPressed));

			struct ControlState
			{
				uint32_t Ownership;
				bool Pressed;
				bool Held;
				bool Released;
			};
			const std::array<ControlState, 6> controls = {{
				{ KeyboardMoveNextOwner, input.KeyboardMoveNext,
					input.KeyboardMoveNextHeld, input.KeyboardMoveNextReleased },
				{ KeyboardMovePreviousOwner, input.KeyboardMovePrevious,
					input.KeyboardMovePreviousHeld,
					input.KeyboardMovePreviousReleased },
				{ KeyboardSubmitOwner, input.KeyboardSubmit,
					input.KeyboardSubmitHeld, input.KeyboardSubmitReleased },
				{ GamepadMoveNextOwner, input.GamepadMoveNext,
					input.GamepadMoveNextHeld, input.GamepadMoveNextReleased },
				{ GamepadMovePreviousOwner, input.GamepadMovePrevious,
					input.GamepadMovePreviousHeld, input.GamepadMovePreviousReleased },
				{ GamepadSubmitOwner, input.GamepadSubmit,
					input.GamepadSubmitHeld, input.GamepadSubmitReleased }
			}};
			if (s_ControlOwnershipScene == &scene)
			{
				for (const ControlState& control : controls)
				{
					if ((s_ControlOwnership & control.Ownership) != 0
						&& (control.Pressed || control.Held || control.Released))
					{
						handled = true;
						break;
					}
				}
			}

			const bool navigationPressed = input.KeyboardMoveNext
				|| input.KeyboardMovePrevious || input.GamepadMoveNext
				|| input.GamepadMovePrevious;
			const bool submitPressed = input.KeyboardSubmit || input.GamepadSubmit;
			return handled || (!buttons.empty()
				&& (navigationPressed || submitPressed));
		}

		void PublishDisplayGameplayInputCapture(Scene& scene, bool captured)
		{
			if (s_FixedCaptureStateScene != &scene)
			{
				s_FixedCaptureStateScene = &scene;
				s_PendingFixedGameplayInputCapture = false;
				s_FixedCapturePrepared = false;
			}
			if (!s_FixedCapturePrepared)
				s_PendingFixedGameplayInputCapture =
					s_PendingFixedGameplayInputCapture || captured;
			s_FixedCapturePrepared = false;
			s_GameplayInputCaptured.store(captured, std::memory_order_release);
		}

	}

	bool UIRect::Contains(const glm::vec2& point) const
	{
		return !IsEmpty() && point.x >= X && point.y >= Y
			&& point.x <= X + Width && point.y <= Y + Height;
	}

	UIRect UIRect::Intersect(const UIRect& first, const UIRect& second)
	{
		const float minimumX = std::max(first.X, second.X);
		const float minimumY = std::max(first.Y, second.Y);
		const float maximumX = std::min(first.X + first.Width,
			second.X + second.Width);
		const float maximumY = std::min(first.Y + first.Height,
			second.Y + second.Height);
		return { minimumX, minimumY, std::max(0.0f, maximumX - minimumX),
			std::max(0.0f, maximumY - minimumY) };
	}

	TextLayoutResult TextLayoutEngine::Build(const FontAtlasData& atlas,
		std::string_view utf8, float fontSize, float maximumWidth,
		TextAlignment alignment, float lineSpacing)
	{
		TextLayoutResult result;
		if (atlas.PixelHeight <= 0.0f || !Finite(fontSize) || fontSize <= 0.0f
			|| !Finite(maximumWidth) || maximumWidth < 0.0f
			|| !Finite(lineSpacing) || lineSpacing <= 0.0f)
			return result;
		const float scale = fontSize / atlas.PixelHeight;
		const float lineHeight = atlas.LineHeight * scale * lineSpacing;
		struct PendingGlyph { const FontGlyph* Glyph = nullptr; float X = 0.0f; };
		struct Line { std::vector<PendingGlyph> Glyphs; float Width = 0.0f; };
		std::vector<Line> lines(1);
		for (uint32_t codepoint : FontAtlasBuilder::DecodeUTF8(utf8))
		{
			if (codepoint == '\r')
				continue;
			if (codepoint == '\n')
			{
				lines.emplace_back();
				continue;
			}
			const FontGlyph* glyph = atlas.Find(codepoint);
			if (!glyph)
				continue;
			const float advance = glyph->Advance * scale;
			Line* line = &lines.back();
			if (maximumWidth > 0.0f && !line->Glyphs.empty()
				&& line->Width + advance > maximumWidth)
			{
				lines.emplace_back();
				line = &lines.back();
			}
			line->Glyphs.push_back({ glyph, line->Width });
			line->Width += advance;
		}
		result.LineCount = static_cast<uint32_t>(lines.size());
		result.Height = std::max(lineHeight,
			static_cast<float>(lines.size()) * lineHeight);
		for (const Line& line : lines)
			result.Width = std::max(result.Width, line.Width);
		const float alignmentWidth = maximumWidth > 0.0f ? maximumWidth : result.Width;
		for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
		{
			const Line& line = lines[lineIndex];
			float alignmentOffset = 0.0f;
			if (alignment == TextAlignment::Center)
				alignmentOffset = (alignmentWidth - line.Width) * 0.5f;
			else if (alignment == TextAlignment::Right)
				alignmentOffset = alignmentWidth - line.Width;
			const float baseline = result.Height - atlas.Ascent * scale
				- static_cast<float>(lineIndex) * lineHeight;
			for (const PendingGlyph& pending : line.Glyphs)
			{
				const FontGlyph& glyph = *pending.Glyph;
				if (glyph.Width <= 0.0f || glyph.Height <= 0.0f)
					continue;
				TextGlyphQuad quad;
				quad.Codepoint = glyph.Codepoint;
				quad.Rect = { alignmentOffset + pending.X + glyph.OffsetX * scale,
					baseline + glyph.OffsetY * scale, glyph.Width * scale,
					glyph.Height * scale };
				quad.UVMin = glyph.UVMin;
				quad.UVMax = glyph.UVMax;
				quad.UsesFallback = glyph.UsesFallback;
				result.Glyphs.push_back(quad);
			}
		}
		return result;
	}

	RuntimeUILayoutSnapshot RuntimeUISystem::BuildLayout(Scene& scene,
		entt::registry& registry, uint32_t viewportWidth, uint32_t viewportHeight,
		float dpi, RuntimeUIVisibilityMode visibility)
	{
		RuntimeUILayoutSnapshot snapshot;
		snapshot.ViewportWidth = viewportWidth;
		snapshot.ViewportHeight = viewportHeight;
		snapshot.DPI = dpi;
		if (viewportWidth == 0 || viewportHeight == 0)
			return snapshot;
		struct CanvasItem { Entity Value; int32_t Order = 0; uint64_t ID = 0; };
		std::vector<CanvasItem> canvases;
		for (const entt::entity value : registry.view<Canvas, ID>())
		{
			Entity entity(value, &scene);
			const Canvas& canvas = registry.get<Canvas>(value);
			if (canvas.Enabled && IsVisible(scene, entity, visibility))
				canvases.push_back({ entity, canvas.SortingOrder,
					static_cast<uint64_t>(entity.GetUUID()) });
		}
		std::sort(canvases.begin(), canvases.end(), [](const CanvasItem& left,
			const CanvasItem& right)
		{
			return left.Order != right.Order ? left.Order < right.Order
				: left.ID < right.ID;
		});
		LayoutBuilder builder{ scene, registry, snapshot, visibility };
		const UIRect viewport{ 0.0f, 0.0f, static_cast<float>(viewportWidth),
			static_cast<float>(viewportHeight) };
		for (const CanvasItem& item : canvases)
		{
			builder.Visited.emplace(item.ID);
			const float scale = CalculateCanvasScale(item.Value.GetComponent<Canvas>(),
				viewportWidth, viewportHeight, dpi);
			snapshot.Rectangles[item.Value.GetUUID()] = viewport;
			snapshot.Clips[item.Value.GetUUID()] = viewport;
			snapshot.Scales[item.Value.GetUUID()] = scale;
			snapshot.RenderOrder.push_back(item.Value.GetUUID());
			builder.LayoutChildren(item.Value, viewport, viewport, scale);
		}
		return snapshot;
	}

	RuntimeUILayoutSnapshot RuntimeUISystem::BuildLayout(Scene& scene,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		RuntimeUIVisibilityMode visibility)
	{
		return BuildLayout(scene, scene.m_Registry, viewportWidth, viewportHeight,
			dpi, visibility);
	}

	glm::vec2 RuntimeUISystem::MapPointerToViewport(
		const glm::vec2& screenPosition, const glm::vec2& viewportOrigin,
		const glm::vec2& screenToFramebufferScale)
	{
		if (!Finite(screenPosition) || !Finite(viewportOrigin)
			|| !Finite(screenToFramebufferScale)
			|| glm::any(glm::lessThanEqual(screenToFramebufferScale,
				glm::vec2(0.0f))))
			return { -1.0f, -1.0f };
		return (screenPosition - viewportOrigin) * screenToFramebufferScale;
	}

	bool RuntimeUISystem::BuildImageGeometry(const UIRect& rectangle,
		const UIRect& clip, float sourceAspect, const glm::vec2& uvMin,
		const glm::vec2& uvMax, bool preserveAspect, UIImageGeometry& output)
	{
		output = {};
		if (rectangle.IsEmpty() || clip.IsEmpty() || !Finite(uvMin)
			|| !Finite(uvMax) || glm::any(glm::lessThanEqual(uvMax, uvMin)))
			return false;
		UIRect fitted = rectangle;
		if (preserveAspect)
		{
			if (!Finite(sourceAspect) || sourceAspect <= 0.0f)
				return false;
			const float targetAspect = rectangle.Width / rectangle.Height;
			if (targetAspect > sourceAspect)
			{
				fitted.Width = rectangle.Height * sourceAspect;
				fitted.X += (rectangle.Width - fitted.Width) * 0.5f;
			}
			else if (targetAspect < sourceAspect)
			{
				fitted.Height = rectangle.Width / sourceAspect;
				fitted.Y += (rectangle.Height - fitted.Height) * 0.5f;
			}
		}
		return ClipQuad(fitted, clip, uvMin, uvMax, output.Rect,
			output.UVMin, output.UVMax);
	}

	void RuntimeUISystem::Reset(entt::registry& registry)
	{
		for (const entt::entity entity : registry.view<UIButton>())
		{
			auto& button = registry.get<UIButton>(entity);
			button.RuntimeHovered = false;
			button.RuntimePressed = false;
			button.RuntimeFocused = false;
			button.RuntimeClickedThisFrame = false;
			button.RuntimeClickSerial = 0;
		}
		for (const entt::entity entity : registry.view<RectTransform>())
		{
			auto& rectangle = registry.get<RectTransform>(entity);
			rectangle.RuntimeRect = glm::vec4(0.0f);
			rectangle.RuntimeClipRect = glm::vec4(0.0f);
		}
		s_GameplayInputCaptured.store(false, std::memory_order_release);
		ClearPointerCapture();
		ClearControlOwnership();
		ClearFixedCaptureState();
	}

	void RuntimeUISystem::PrepareFixedInputCapture(Scene& scene,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		glm::vec2 viewportOrigin, glm::vec2 screenToFramebufferScale)
	{
		auto& source = Scripting::ScriptEngine::Get();
		const auto mouse = source.GetMousePosition();
		RuntimeUIInputFrame input;
		input.PointerPosition = MapPointerToViewport({ mouse.X, mouse.Y },
			viewportOrigin, screenToFramebufferScale);
		input.WindowFocused = source.IsWindowFocused();
		input.MousePressed = source.WasMouseButtonPressed(0);
		input.MouseHeld = source.IsMouseButtonHeld(0);
		input.MouseReleased = source.WasMouseButtonReleased(0);
		input.KeyboardMoveNext = source.WasKeyPressed(258)
			|| source.WasKeyPressed(264) || source.WasKeyPressed(262);
		input.KeyboardMoveNextHeld = source.IsKeyHeld(258)
			|| source.IsKeyHeld(264) || source.IsKeyHeld(262);
		input.KeyboardMoveNextReleased = source.WasKeyReleased(258)
			|| source.WasKeyReleased(264) || source.WasKeyReleased(262);
		input.KeyboardMovePrevious = source.WasKeyPressed(265)
			|| source.WasKeyPressed(263);
		input.KeyboardMovePreviousHeld = source.IsKeyHeld(265)
			|| source.IsKeyHeld(263);
		input.KeyboardMovePreviousReleased = source.WasKeyReleased(265)
			|| source.WasKeyReleased(263);
		input.KeyboardSubmit = source.WasKeyPressed(257)
			|| source.WasKeyPressed(32);
		input.KeyboardSubmitHeld = source.IsKeyHeld(257)
			|| source.IsKeyHeld(32);
		input.KeyboardSubmitReleased = source.WasKeyReleased(257)
			|| source.WasKeyReleased(32);
		input.GamepadMoveNext = source.WasGamepadButtonPressed(0, 13)
			|| source.WasGamepadButtonPressed(0, 12);
		input.GamepadMoveNextHeld = source.IsGamepadButtonHeld(0, 13)
			|| source.IsGamepadButtonHeld(0, 12);
		input.GamepadMoveNextReleased = source.WasGamepadButtonReleased(0, 13)
			|| source.WasGamepadButtonReleased(0, 12);
		input.GamepadMovePrevious = source.WasGamepadButtonPressed(0, 11)
			|| source.WasGamepadButtonPressed(0, 14);
		input.GamepadMovePreviousHeld = source.IsGamepadButtonHeld(0, 11)
			|| source.IsGamepadButtonHeld(0, 14);
		input.GamepadMovePreviousReleased = source.WasGamepadButtonReleased(0, 11)
			|| source.WasGamepadButtonReleased(0, 14);
		input.GamepadSubmit = source.WasGamepadButtonPressed(0, 0);
		input.GamepadSubmitHeld = source.IsGamepadButtonHeld(0, 0);
		input.GamepadSubmitReleased = source.WasGamepadButtonReleased(0, 0);
		PrepareFixedInputCaptureWithInput(scene, viewportWidth, viewportHeight, dpi,
			input);
	}

	void RuntimeUISystem::PrepareFixedInputCaptureWithInput(Scene& scene,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		const RuntimeUIInputFrame& input)
	{
		if (s_FixedCaptureStateScene == &scene && s_FixedCapturePrepared)
			return;
		if (s_FixedCaptureStateScene != &scene)
		{
			s_FixedCaptureStateScene = &scene;
			s_PendingFixedGameplayInputCapture = false;
			s_FixedCapturePrepared = false;
		}

		const bool captured = s_PendingFixedGameplayInputCapture
			|| WouldCaptureGameplayInput(scene, scene.m_Registry, viewportWidth,
				viewportHeight, dpi, input);
		s_PendingFixedGameplayInputCapture = false;
		s_FixedCapturePrepared = true;
		s_GameplayInputCaptured.store(captured, std::memory_order_release);
	}

	void RuntimeUISystem::Update(Scene& scene, entt::registry& registry,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		glm::vec2 viewportOrigin, glm::vec2 screenToFramebufferScale)
	{
		auto& source = Scripting::ScriptEngine::Get();
		const auto mouse = source.GetMousePosition();
		RuntimeUIInputFrame input;
		input.PointerPosition = MapPointerToViewport({ mouse.X, mouse.Y },
			viewportOrigin, screenToFramebufferScale);
		input.WindowFocused = source.IsWindowFocused();
		input.MousePressed = source.WasMouseButtonPressed(0);
		input.MouseHeld = source.IsMouseButtonHeld(0);
		input.MouseReleased = source.WasMouseButtonReleased(0);
		input.KeyboardMoveNext = source.WasKeyPressed(258)
			|| source.WasKeyPressed(264) || source.WasKeyPressed(262);
		input.KeyboardMoveNextHeld = source.IsKeyHeld(258)
			|| source.IsKeyHeld(264) || source.IsKeyHeld(262);
		input.KeyboardMoveNextReleased = source.WasKeyReleased(258)
			|| source.WasKeyReleased(264) || source.WasKeyReleased(262);
		input.KeyboardMovePrevious = source.WasKeyPressed(265)
			|| source.WasKeyPressed(263);
		input.KeyboardMovePreviousHeld = source.IsKeyHeld(265)
			|| source.IsKeyHeld(263);
		input.KeyboardMovePreviousReleased = source.WasKeyReleased(265)
			|| source.WasKeyReleased(263);
		input.KeyboardSubmit = source.WasKeyPressed(257)
			|| source.WasKeyPressed(32);
		input.KeyboardSubmitHeld = source.IsKeyHeld(257)
			|| source.IsKeyHeld(32);
		input.KeyboardSubmitReleased = source.WasKeyReleased(257)
			|| source.WasKeyReleased(32);
		input.GamepadMoveNext = source.WasGamepadButtonPressed(0, 13)
			|| source.WasGamepadButtonPressed(0, 12);
		input.GamepadMoveNextHeld = source.IsGamepadButtonHeld(0, 13)
			|| source.IsGamepadButtonHeld(0, 12);
		input.GamepadMoveNextReleased = source.WasGamepadButtonReleased(0, 13)
			|| source.WasGamepadButtonReleased(0, 12);
		input.GamepadMovePrevious = source.WasGamepadButtonPressed(0, 11)
			|| source.WasGamepadButtonPressed(0, 14);
		input.GamepadMovePreviousHeld = source.IsGamepadButtonHeld(0, 11)
			|| source.IsGamepadButtonHeld(0, 14);
		input.GamepadMovePreviousReleased = source.WasGamepadButtonReleased(0, 11)
			|| source.WasGamepadButtonReleased(0, 14);
		input.GamepadSubmit = source.WasGamepadButtonPressed(0, 0);
		input.GamepadSubmitHeld = source.IsGamepadButtonHeld(0, 0);
		input.GamepadSubmitReleased = source.WasGamepadButtonReleased(0, 0);
		UpdateWithInput(scene, registry, viewportWidth, viewportHeight, dpi, input);
	}

	void RuntimeUISystem::UpdateWithInput(Scene& scene, entt::registry& registry,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		const RuntimeUIInputFrame& input)
	{
		if (s_PointerCaptureScene != &scene)
			ClearPointerCapture();
		if (s_ControlOwnershipScene != &scene)
			ClearControlOwnership();
		const RuntimeUILayoutSnapshot layout = BuildLayout(scene, registry,
			viewportWidth, viewportHeight, dpi);
		UIEventSystem* eventSystem = nullptr;
		uint64_t eventSystemID = (std::numeric_limits<uint64_t>::max)();
		for (const entt::entity value : registry.view<UIEventSystem, ID>())
		{
			Entity entity(value, &scene);
			auto& candidate = registry.get<UIEventSystem>(value);
			const uint64_t id = static_cast<uint64_t>(entity.GetUUID());
			if (candidate.Enabled && scene.IsActiveInHierarchy(entity)
				&& id < eventSystemID)
			{
				eventSystem = &candidate;
				eventSystemID = id;
			}
		}
		for (const entt::entity value : registry.view<UIButton>())
		{
			auto& button = registry.get<UIButton>(value);
			button.RuntimeHovered = false;
			button.RuntimeClickedThisFrame = false;
		}
		if (!eventSystem)
		{
			for (const entt::entity value : registry.view<UIButton>())
			{
				auto& button = registry.get<UIButton>(value);
				button.RuntimePressed = false;
				button.RuntimeFocused = false;
			}
			PublishDisplayGameplayInputCapture(scene, false);
			ClearPointerCapture();
			ClearControlOwnership();
			return;
		}
		if (!input.WindowFocused)
		{
			for (const entt::entity value : registry.view<UIButton>())
				registry.get<UIButton>(value).RuntimePressed = false;
			PublishDisplayGameplayInputCapture(scene, false);
			ClearPointerCapture();
			ClearControlOwnership();
			return;
		}
		struct Candidate { Entity Value; UIRect Rect; };
		std::vector<Candidate> buttons;
		for (size_t index = 0; index < layout.RenderOrder.size(); ++index)
		{
			const UUID id = layout.RenderOrder[index];
			Entity entity = scene.FindEntityByUUID(id);
			if (!entity || !entity.HasComponent<UIButton>()
				|| !entity.HasComponent<RectTransform>())
				continue;
			auto& button = entity.GetComponent<UIButton>();
			if (!button.Enabled || !button.Interactable
				|| !scene.IsActiveInHierarchy(entity))
				continue;
			buttons.push_back({ entity, layout.Rectangles.at(id) });
		}
		for (const entt::entity value : registry.view<UIButton>())
		{
			const auto found = std::find_if(buttons.begin(), buttons.end(),
				[value](const Candidate& item)
				{
					return static_cast<entt::entity>(item.Value) == value;
				});
			if (found == buttons.end())
			{
				auto& button = registry.get<UIButton>(value);
				button.RuntimePressed = false;
				button.RuntimeFocused = false;
			}
		}
		auto focused = buttons.end();
		bool retainedPressed = false;
		for (auto current = buttons.begin(); current != buttons.end(); ++current)
		{
			auto& button = current->Value.GetComponent<UIButton>();
			if (button.RuntimeFocused && focused == buttons.end())
				focused = current;
			else if (button.RuntimeFocused)
				button.RuntimeFocused = false;
			if (button.RuntimePressed
				&& (!button.RuntimeFocused || retainedPressed))
				button.RuntimePressed = false;
			else if (button.RuntimePressed)
				retainedPressed = true;
		}
		if (!buttons.empty() && focused == buttons.end())
		{
			buttons.front().Value.GetComponent<UIButton>().RuntimeFocused = true;
			focused = buttons.begin();
		}

		const glm::vec2 mousePoint(input.PointerPosition.x,
			static_cast<float>(viewportHeight) - input.PointerPosition.y);
		Candidate* hovered = nullptr;
		bool pointerHandled = false;
		std::optional<UUID> pointerTarget;
		for (auto item = layout.RenderOrder.rbegin();
			item != layout.RenderOrder.rend(); ++item)
		{
			Entity target = scene.FindEntityByUUID(*item);
			const auto rectangle = layout.Rectangles.find(*item);
			const auto clip = layout.Clips.find(*item);
			if (!target || rectangle == layout.Rectangles.end()
				|| clip == layout.Clips.end()
				|| !UIRect::Intersect(rectangle->second, clip->second).Contains(mousePoint))
				continue;
			const bool imageTarget = target.HasComponent<UIImage>()
				&& target.GetComponent<UIImage>().Enabled
				&& target.GetComponent<UIImage>().RaycastTarget;
			const bool textTarget = target.HasComponent<UIText>()
				&& target.GetComponent<UIText>().Enabled
				&& target.GetComponent<UIText>().RaycastTarget;
			if (!imageTarget && !textTarget)
				continue;

			// The first graphic in reverse render order owns the pointer. Bubble the
			// hit to an interactable ancestor button; a non-button overlay still
			// consumes the pointer and deliberately blocks buttons underneath.
			pointerHandled = true;
			pointerTarget = target.GetUUID();
			for (Entity current = target; current; current = scene.GetParent(current))
			{
				auto button = std::find_if(buttons.begin(), buttons.end(),
					[current](const Candidate& candidate)
					{ return candidate.Value == current; });
				if (button != buttons.end())
				{
					hovered = &*button;
					break;
				}
			}
			break;
		}
		if (hovered)
			hovered->Value.GetComponent<UIButton>().RuntimeHovered = true;
		// Hover only updates visual state. Gameplay is captured for the current
		// frame only when the UI actually consumes a pointer action, navigation,
		// or submit; a stationary cursor over a HUD must not suppress WASD.
		const bool hadPressed = std::any_of(buttons.begin(), buttons.end(),
			[](const Candidate& item)
			{ return item.Value.GetComponent<UIButton>().RuntimePressed; });
		if (input.MousePressed)
		{
			if (pointerHandled && pointerTarget)
			{
				s_PointerCaptureScene = &scene;
				s_PointerCaptureTarget = pointerTarget;
			}
			else
				ClearPointerCapture();
		}
		else if (!input.MouseHeld && !input.MouseReleased)
		{
			// Recover cleanly if focus loss prevented a release transition.
			ClearPointerCapture();
		}
		bool validPointerCapture = false;
		if (s_PointerCaptureScene == &scene && s_PointerCaptureTarget)
		{
			Entity captured = scene.FindEntityByUUID(*s_PointerCaptureTarget);
			validPointerCapture = captured && scene.IsActiveInHierarchy(captured)
				&& ((captured.HasComponent<UIImage>()
						&& captured.GetComponent<UIImage>().Enabled
						&& captured.GetComponent<UIImage>().RaycastTarget)
					|| (captured.HasComponent<UIText>()
						&& captured.GetComponent<UIText>().Enabled
						&& captured.GetComponent<UIText>().RaycastTarget));
			if (!validPointerCapture)
				ClearPointerCapture();
		}
		bool handledInteraction = (input.MousePressed && pointerHandled)
			|| (input.MouseHeld && (validPointerCapture || hadPressed));

		if (input.MousePressed && hovered)
		{
			for (Candidate& item : buttons)
				item.Value.GetComponent<UIButton>().RuntimeFocused = false;
			auto& button = hovered->Value.GetComponent<UIButton>();
			button.RuntimeFocused = true;
			button.RuntimePressed = true;
			focused = std::find_if(buttons.begin(), buttons.end(),
				[hovered](const Candidate& item) { return item.Value == hovered->Value; });
		}
		if (input.MouseReleased)
		{
			handledInteraction = handledInteraction || pointerHandled || hadPressed
				|| validPointerCapture;
			for (Candidate& item : buttons)
			{
				auto& button = item.Value.GetComponent<UIButton>();
				if (button.RuntimePressed && hovered && item.Value == hovered->Value)
				{
					button.RuntimeClickedThisFrame = true;
					++button.RuntimeClickSerial;
				}
				button.RuntimePressed = false;
			}
			ClearPointerCapture();
		}

		struct ControlState
		{
			uint32_t Ownership;
			bool Pressed;
			bool Held;
			bool Released;
		};
		const std::array<ControlState, 6> controls = {{
			{ KeyboardMoveNextOwner, input.KeyboardMoveNext,
				input.KeyboardMoveNextHeld, input.KeyboardMoveNextReleased },
			{ KeyboardMovePreviousOwner, input.KeyboardMovePrevious,
				input.KeyboardMovePreviousHeld, input.KeyboardMovePreviousReleased },
			{ KeyboardSubmitOwner, input.KeyboardSubmit,
				input.KeyboardSubmitHeld, input.KeyboardSubmitReleased },
			{ GamepadMoveNextOwner, input.GamepadMoveNext,
				input.GamepadMoveNextHeld, input.GamepadMoveNextReleased },
			{ GamepadMovePreviousOwner, input.GamepadMovePrevious,
				input.GamepadMovePreviousHeld, input.GamepadMovePreviousReleased },
			{ GamepadSubmitOwner, input.GamepadSubmit,
				input.GamepadSubmitHeld, input.GamepadSubmitReleased }
		}};
		bool ownedControlHandled = false;
		for (const ControlState& control : controls)
		{
			if ((s_ControlOwnership & control.Ownership) == 0)
				continue;
			if (!control.Pressed && !control.Held && !control.Released)
				s_ControlOwnership &= ~control.Ownership;
			else
				ownedControlHandled = true;
		}
		NormalizeControlOwnership();
		handledInteraction = handledInteraction || ownedControlHandled;

		uint32_t pressedOwnership = 0;
		const bool moveNext = input.KeyboardMoveNext || input.GamepadMoveNext;
		const bool movePrevious = input.KeyboardMovePrevious
			|| input.GamepadMovePrevious;
		if ((moveNext || movePrevious) && focused != buttons.end())
		{
			handledInteraction = true;
			if (input.KeyboardMoveNext)
				pressedOwnership |= KeyboardMoveNextOwner;
			if (input.KeyboardMovePrevious)
				pressedOwnership |= KeyboardMovePreviousOwner;
			if (input.GamepadMoveNext)
				pressedOwnership |= GamepadMoveNextOwner;
			if (input.GamepadMovePrevious)
				pressedOwnership |= GamepadMovePreviousOwner;
			size_t index = static_cast<size_t>(std::distance(buttons.begin(), focused));
			if (moveNext)
				index = index + 1 < buttons.size() ? index + 1
					: eventSystem->WrapNavigation ? 0 : index;
			else
				index = index > 0 ? index - 1
					: eventSystem->WrapNavigation ? buttons.size() - 1 : index;
			for (Candidate& item : buttons)
				item.Value.GetComponent<UIButton>().RuntimeFocused = false;
			buttons[index].Value.GetComponent<UIButton>().RuntimeFocused = true;
			focused = buttons.begin() + static_cast<std::ptrdiff_t>(index);
		}
		const bool submit = input.KeyboardSubmit || input.GamepadSubmit;
		if (submit && focused != buttons.end())
		{
			handledInteraction = true;
			if (input.KeyboardSubmit)
				pressedOwnership |= KeyboardSubmitOwner;
			if (input.GamepadSubmit)
				pressedOwnership |= GamepadSubmitOwner;
			auto& button = focused->Value.GetComponent<UIButton>();
			button.RuntimeClickedThisFrame = true;
			++button.RuntimeClickSerial;
		}
		if (pressedOwnership != 0 && focused != buttons.end())
		{
			s_ControlOwnership |= pressedOwnership;
			s_ControlOwnershipScene = &scene;
		}
		PublishDisplayGameplayInputCapture(scene,
			eventSystem->ConsumeGameplayInput && handledInteraction);
		for (const ControlState& control : controls)
		{
			if ((s_ControlOwnership & control.Ownership) != 0
				&& control.Released && !control.Held)
				s_ControlOwnership &= ~control.Ownership;
		}
		NormalizeControlOwnership();
	}

	void RuntimeUISystem::UpdateWithInput(Scene& scene, uint32_t viewportWidth,
		uint32_t viewportHeight, float dpi, const RuntimeUIInputFrame& input)
	{
		UpdateWithInput(scene, scene.m_Registry, viewportWidth, viewportHeight,
			dpi, input);
	}

	void RuntimeUISystem::RenderWorldText(Scene& scene, entt::registry& registry,
		RuntimeUIVisibilityMode visibility)
	{
		for (const entt::entity value : registry.view<Transform, TextRenderer>())
		{
			Entity entity(value, &scene);
			auto [transform, text] = registry.get<Transform, TextRenderer>(value);
			if (!text.Enabled || text.Text.empty()
				|| !IsVisible(scene, entity, visibility)
				|| !Finite(text.FontSize) || text.FontSize <= 0.0f)
				continue;
			Ref<RuntimeFont> font = FontManager::Get().Load(text.Font, text.Text,
				text.FallbackFont, text.EmojiFont);
			if (!font || !font->GetTexture())
				continue;
			const TextLayoutResult layout = TextLayoutEngine::Build(font->GetAtlas(),
				text.Text, text.FontSize, std::max(0.0f, text.MaxWidth),
				text.Alignment, text.LineSpacing);
			const glm::mat4 world = transform.GetTransform();
			for (const TextGlyphQuad& glyph : layout.Glyphs)
			{
				const glm::mat4 local = glm::translate(glm::mat4(1.0f), {
					glyph.Rect.X + glyph.Rect.Width * 0.5f,
					glyph.Rect.Y + glyph.Rect.Height * 0.5f, 0.0f })
					* glm::scale(glm::mat4(1.0f), {
						glyph.Rect.Width, glyph.Rect.Height, 1.0f });
				Renderer2D::DrawTexturedQuadRegion(world * local,
					font->GetTexture(), glyph.UVMin, glyph.UVMax, text.Color,
					static_cast<int>(value));
			}
		}
	}

	void RuntimeUISystem::RenderScreen(Scene& scene, entt::registry& registry,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		RuntimeUIVisibilityMode visibility)
	{
		if (viewportWidth == 0 || viewportHeight == 0)
			return;
		const RuntimeUILayoutSnapshot layout = BuildLayout(scene, registry,
			viewportWidth, viewportHeight, dpi, visibility);
		// Script-only and otherwise UI-free scenes must not touch the graphics
		// backend. This also keeps server/headless runtime updates valid before a
		// Renderer2D context exists.
		if (layout.RenderOrder.empty())
			return;
		Camera screenCamera(glm::ortho(0.0f, static_cast<float>(viewportWidth),
			0.0f, static_cast<float>(viewportHeight), -1.0f, 1.0f));
		RenderCommand::SetDepthTest(false);
		Renderer2D::BeginScene(screenCamera, glm::mat4(1.0f));
		for (UUID id : layout.RenderOrder)
		{
			Entity entity = scene.FindEntityByUUID(id);
			if (!entity || !IsVisible(scene, entity, visibility))
				continue;
			const auto rectangle = layout.Rectangles.find(id);
			const auto clip = layout.Clips.find(id);
			if (rectangle == layout.Rectangles.end() || clip == layout.Clips.end())
				continue;
			if (entity.HasComponent<UIImage>())
			{
				auto& image = entity.GetComponent<UIImage>();
				if (image.Enabled && Finite(image.Color))
				{
					Ref<Texture2D> texture;
					glm::vec2 sourceUVMin(0.0f), sourceUVMax(1.0f);
					float sourceAspect = rectangle->second.Width
						/ std::max(rectangle->second.Height, 1.0e-6f);
					if (static_cast<uint64_t>(image.Image) != 0)
					{
						AssetManager& assets = AssetManager::Get();
						texture = assets.LoadTexture(image.Image);
						if (texture && texture->GetHeight() > 0)
							sourceAspect = static_cast<float>(texture->GetWidth())
								/ texture->GetHeight();
						ResolvedSpriteAsset resolved;
						SpriteRenderGeometry spriteGeometry;
						if (texture && assets.ResolveSpriteAsset(image.Image, resolved)
							&& resolved.IsSubAsset
							&& BuildSpriteRenderGeometry(resolved.Data,
								texture->GetWidth(), texture->GetHeight(), spriteGeometry))
						{
							sourceUVMin = { spriteGeometry.UMin, spriteGeometry.VMin };
							sourceUVMax = { spriteGeometry.UMax, spriteGeometry.VMax };
							sourceAspect = spriteGeometry.Width
								/ std::max(spriteGeometry.Height, 1.0e-6f);
						}
					}
					UIImageGeometry geometry;
					if (BuildImageGeometry(rectangle->second, clip->second,
						sourceAspect, sourceUVMin, sourceUVMax,
						image.PreserveAspect, geometry))
					{
						glm::vec4 color = image.Color;
						if (entity.HasComponent<UIButton>()
							&& entity.GetComponent<UIButton>().Enabled)
						{
							const auto& button = entity.GetComponent<UIButton>();
							const glm::vec4 state = button.RuntimePressed ? button.PressedColor
								: button.RuntimeHovered ? button.HoverColor
								: button.RuntimeFocused ? button.SelectedColor
								: button.NormalColor;
							color = MultiplyColor(color, state);
						}
						if (static_cast<uint64_t>(image.Image) == 0)
							Renderer2D::DrawQuad(QuadTransform(geometry.Rect), color,
								static_cast<int>(static_cast<entt::entity>(entity)));
						else if (texture)
							Renderer2D::DrawTexturedQuadRegion(QuadTransform(geometry.Rect),
								texture, geometry.UVMin, geometry.UVMax,
								color, static_cast<int>(static_cast<entt::entity>(entity)));
					}
				}
			}
			if (entity.HasComponent<UIText>())
				DrawScreenText(entity.GetComponent<UIText>(), rectangle->second,
					clip->second, layout.Scales.at(id),
					static_cast<int>(static_cast<entt::entity>(entity)));
		}
		Renderer2D::EndScene();
		RenderCommand::SetDepthTest(true);
	}

	void RuntimeUISystem::RenderScreen(Scene& scene, uint32_t viewportWidth,
		uint32_t viewportHeight, float dpi, RuntimeUIVisibilityMode visibility)
	{
		RenderScreen(scene, scene.m_Registry, viewportWidth, viewportHeight, dpi,
			visibility);
	}

	bool RuntimeUISystem::IsGameplayInputCaptured()
	{
		return s_GameplayInputCaptured.load(std::memory_order_acquire);
	}

	bool RuntimeUISystem::WasButtonClicked(Entity entity)
	{
		return entity && entity.HasComponent<UIButton>()
			&& entity.GetComponent<UIButton>().RuntimeClickedThisFrame;
	}

	uint64_t RuntimeUISystem::GetButtonClickSerial(Entity entity)
	{
		return entity && entity.HasComponent<UIButton>()
			? entity.GetComponent<UIButton>().RuntimeClickSerial : 0;
	}

	bool RuntimeUISystem::FocusButton(Scene& scene, entt::registry& registry,
		Entity entity)
	{
		if (!entity || !entity.HasComponent<UIButton>())
			return false;
		auto& target = entity.GetComponent<UIButton>();
		if (!target.Enabled || !target.Interactable || !scene.IsActiveInHierarchy(entity))
			return false;
		for (const entt::entity value : registry.view<UIButton>())
			registry.get<UIButton>(value).RuntimeFocused = value == static_cast<entt::entity>(entity);
		return true;
	}

	bool RuntimeUISystem::FocusButton(Scene& scene, Entity entity)
	{
		return FocusButton(scene, scene.m_Registry, entity);
	}

}
