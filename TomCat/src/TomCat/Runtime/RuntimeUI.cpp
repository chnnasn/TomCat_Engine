#include "tcpch.h"
#include "RuntimeUI.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/Input.h"
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
#include <yaml-cpp/yaml.h>

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

		template<typename Component>
		Component* FindScope(Scene& scene, Entity entity)
		{
			for (Entity current = entity; current; current = scene.GetParent(current))
				if (current.HasComponent<Component>() && current.GetComponent<Component>().Enabled)
					return &current.GetComponent<Component>();
			return nullptr;
		}

		bool IsRaycastTarget(Entity entity)
		{
			return (entity.HasComponent<UIImage>() && entity.GetComponent<UIImage>().Enabled
				&& entity.GetComponent<UIImage>().RaycastTarget)
				|| (entity.HasComponent<UIText>() && entity.GetComponent<UIText>().Enabled
					&& entity.GetComponent<UIText>().RaycastTarget)
				|| (entity.HasComponent<UISlider>() && entity.GetComponent<UISlider>().Enabled)
				|| (entity.HasComponent<UIInputField>() && entity.GetComponent<UIInputField>().Enabled)
				|| (entity.HasComponent<UIScrollView>() && entity.GetComponent<UIScrollView>().Enabled);
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

		struct UIClipVertex
		{
			glm::vec2 Position{ 0.0f };
			glm::vec2 UV{ 0.0f };
		};

		bool TransformUIPosition(const glm::mat4& transform,
			const glm::vec2& position, glm::vec2& output)
		{
			const glm::vec4 transformed = transform
				* glm::vec4(position, 0.0f, 1.0f);
			if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y)
				|| !std::isfinite(transformed.w)
				|| std::abs(transformed.w) <= 0.000001f)
				return false;
			output = glm::vec2(transformed) / transformed.w;
			return Finite(output);
		}

		bool InvertUITransform(const glm::mat4& transform, glm::mat4& inverse)
		{
			const float determinant = glm::determinant(transform);
			if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
				return false;
			inverse = glm::inverse(transform);
			for (glm::length_t column = 0; column < 4; ++column)
				for (glm::length_t row = 0; row < 4; ++row)
					if (!std::isfinite(inverse[column][row]))
						return false;
			return true;
		}

		std::vector<UIClipVertex> ClipPolygonToRectangle(
			std::vector<UIClipVertex> polygon, const UIRect& rectangle)
		{
			if (polygon.size() < 3 || rectangle.IsEmpty())
				return {};
			for (int edge = 0; edge < 4 && polygon.size() >= 3; ++edge)
			{
				const bool xAxis = edge < 2;
				const bool lowerEdge = (edge % 2) == 0;
				const float boundary = xAxis
					? (lowerEdge ? rectangle.X : rectangle.X + rectangle.Width)
					: (lowerEdge ? rectangle.Y : rectangle.Y + rectangle.Height);
				auto coordinate = [xAxis](const UIClipVertex& vertex)
				{
					return xAxis ? vertex.Position.x : vertex.Position.y;
				};
				auto inside = [lowerEdge, boundary, &coordinate](
					const UIClipVertex& vertex)
				{
					return lowerEdge ? coordinate(vertex) >= boundary
						: coordinate(vertex) <= boundary;
				};

				std::vector<UIClipVertex> output;
				output.reserve(polygon.size() + 1);
				UIClipVertex previous = polygon.back();
				bool previousInside = inside(previous);
				for (const UIClipVertex& current : polygon)
				{
					const bool currentInside = inside(current);
					if (currentInside != previousInside)
					{
						const float denominator = coordinate(current)
							- coordinate(previous);
						if (std::isfinite(denominator)
							&& std::abs(denominator) > 0.000001f)
						{
							const float amount = std::clamp((boundary
								- coordinate(previous)) / denominator,
								0.0f, 1.0f);
							output.push_back({ glm::mix(previous.Position,
								current.Position, amount),
								glm::mix(previous.UV, current.UV, amount) });
						}
					}
					if (currentInside)
						output.push_back(current);
					previous = current;
					previousInside = currentInside;
				}
				polygon = std::move(output);
			}
			return polygon.size() >= 3 ? polygon : std::vector<UIClipVertex>{};
		}

		std::vector<UIClipVertex> BuildClippedUIPolygon(
			const UIRect& rectangle, const glm::mat4& elementTransform,
			const std::vector<RuntimeUIClipRegion>& clipRegions,
			const glm::vec2& uvMin, const glm::vec2& uvMax)
		{
			if (rectangle.IsEmpty() || !Finite(uvMin) || !Finite(uvMax))
				return {};
			const glm::vec2 localPositions[4] = {
				{ rectangle.X, rectangle.Y },
				{ rectangle.X + rectangle.Width, rectangle.Y },
				{ rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height },
				{ rectangle.X, rectangle.Y + rectangle.Height }
			};
			const glm::vec2 textureCoordinates[4] = {
				{ uvMin.x, uvMin.y }, { uvMax.x, uvMin.y },
				{ uvMax.x, uvMax.y }, { uvMin.x, uvMax.y }
			};
			std::vector<UIClipVertex> polygon;
			polygon.reserve(8);
			for (size_t index = 0; index < 4; ++index)
			{
				glm::vec2 transformed;
				if (!TransformUIPosition(elementTransform, localPositions[index],
					transformed))
					return {};
				polygon.push_back({ transformed, textureCoordinates[index] });
			}

			for (const RuntimeUIClipRegion& region : clipRegions)
			{
				glm::mat4 inverse(1.0f);
				if (!InvertUITransform(region.Transform, inverse))
					return {};
				for (UIClipVertex& vertex : polygon)
				{
					glm::vec2 local;
					if (!TransformUIPosition(inverse, vertex.Position, local))
						return {};
					vertex.Position = local;
				}
				polygon = ClipPolygonToRectangle(std::move(polygon),
					region.Rectangle);
				if (polygon.empty())
					return {};
				for (UIClipVertex& vertex : polygon)
				{
					glm::vec2 transformed;
					if (!TransformUIPosition(region.Transform, vertex.Position,
						transformed))
						return {};
					vertex.Position = transformed;
				}
			}
			return polygon;
		}

		void DrawClippedUIQuad(const UIRect& rectangle,
			const glm::mat4& elementTransform,
			const std::vector<RuntimeUIClipRegion>& clipRegions,
			const Ref<Texture2D>& texture, const glm::vec2& uvMin,
			const glm::vec2& uvMax, const glm::vec4& color, int entityID)
		{
			const std::vector<UIClipVertex> polygon = BuildClippedUIPolygon(
				rectangle, elementTransform, clipRegions, uvMin, uvMax);
			for (size_t index = 1; index + 1 < polygon.size(); ++index)
			{
				const std::array<glm::vec3, 4> positions = {{
					{ polygon[0].Position, 0.0f },
					{ polygon[index].Position, 0.0f },
					{ polygon[index + 1].Position, 0.0f },
					{ polygon[0].Position, 0.0f }
				}};
				const std::array<glm::vec2, 4> textureCoordinates = {{
					polygon[0].UV, polygon[index].UV,
					polygon[index + 1].UV, polygon[0].UV
				}};
				Renderer2D::DrawTexturedQuadVertices(positions, texture,
					textureCoordinates, color, entityID);
			}
		}

		glm::vec4 MultiplyColor(const glm::vec4& first, const glm::vec4& second)
		{
			return first * second;
		}

		void DrawScreenText(const UIText& text, const UIRect& rectangle,
			const std::vector<RuntimeUIClipRegion>& inheritedClipRegions,
			float canvasScale,
			const glm::mat4& elementTransform, int entityID, float horizontalOffset = 0.0f)
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
			const glm::vec2 origin(rectangle.X - horizontalOffset,
				rectangle.Y + std::max(0.0f, rectangle.Height - layout.Height));
			std::vector<RuntimeUIClipRegion> clipRegions = inheritedClipRegions;
			clipRegions.push_back({ rectangle, elementTransform });
			for (const TextGlyphQuad& glyph : layout.Glyphs)
			{
				const UIRect original{ origin.x + glyph.Rect.X,
					origin.y + glyph.Rect.Y, glyph.Rect.Width, glyph.Rect.Height };
				DrawClippedUIQuad(original, elementTransform, clipRegions,
					font->GetTexture(), glyph.UVMin, glyph.UVMax,
					text.Color, entityID);
			}
		}

		void RenderUILayout(Scene& scene, const RuntimeUILayoutSnapshot& layout,
			RuntimeUIVisibilityMode visibility, const glm::mat4& viewProjection)
		{
			// Script-only and otherwise UI-free scenes must not touch the graphics
			// backend. This also keeps server/headless updates valid before a
			// Renderer2D context exists.
			if (layout.RenderOrder.empty())
				return;

			Camera renderCamera(viewProjection);
			RenderCommand::SetDepthTest(false);
			Renderer2D::BeginScene(renderCamera, glm::mat4(1.0f));
			for (UUID id : layout.RenderOrder)
			{
				Entity entity = scene.FindEntityByUUID(id);
				if (!entity || !IsVisible(scene, entity, visibility))
					continue;
				const auto rectangle = layout.Rectangles.find(id);
				const auto clip = layout.Clips.find(id);
				const auto elementTransform = layout.Transforms.find(id);
				const auto clipRegions = layout.ClipRegions.find(id);
				if (rectangle == layout.Rectangles.end()
					|| clip == layout.Clips.end()
					|| elementTransform == layout.Transforms.end()
					|| clipRegions == layout.ClipRegions.end())
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
						if (RuntimeUISystem::BuildImageGeometry(rectangle->second,
							rectangle->second, sourceAspect, sourceUVMin, sourceUVMax,
							image.PreserveAspect, geometry))
						{
							glm::vec4 color = image.Color;
							if (const auto* theme = FindScope<UITheme>(scene, entity))
								color *= theme->ImageColor;
							if (entity.HasComponent<UIButton>()
								&& entity.GetComponent<UIButton>().Enabled)
							{
								const auto& button = entity.GetComponent<UIButton>();
								glm::vec4 state = !button.Interactable
									? button.DisabledColor
									: button.RuntimePressed ? button.PressedColor
									: button.RuntimeHovered ? button.HoverColor
									: button.RuntimeFocused ? button.SelectedColor
									: button.NormalColor;
								const float multiplier = std::isfinite(
									button.ColorMultiplier)
									? std::clamp(button.ColorMultiplier, 0.0f, 5.0f)
									: 1.0f;
								color = MultiplyColor(color, state * multiplier);
							}
							if (static_cast<uint64_t>(image.Image) == 0 || texture)
								DrawClippedUIQuad(geometry.Rect,
									elementTransform->second, clipRegions->second,
									texture, geometry.UVMin, geometry.UVMax, color,
									static_cast<int>(entity));
						}
					}
				}

				if (entity.HasComponent<UISlider>() && entity.GetComponent<UISlider>().Enabled)
				{
					const auto& slider = entity.GetComponent<UISlider>();
					UIRect fill = rectangle->second;
					const float amount = slider.Maximum > slider.Minimum
						? std::clamp((slider.Value - slider.Minimum) / (slider.Maximum - slider.Minimum), 0.0f, 1.0f) : 0.0f;
					if (slider.Vertical) fill.Height *= amount; else fill.Width *= amount;
					glm::vec4 accent = slider.FillColor;
					if (const auto* theme = FindScope<UITheme>(scene, entity)) accent = theme->AccentColor;
					if (!slider.Interactable) accent.a *= 0.5f;
					DrawClippedUIQuad(rectangle->second, elementTransform->second, clipRegions->second,
						{}, { 0.0f, 0.0f }, { 1.0f, 1.0f }, slider.TrackColor, static_cast<int>(entity));
					DrawClippedUIQuad(fill, elementTransform->second, clipRegions->second,
						{}, { 0.0f, 0.0f }, { 1.0f, 1.0f }, accent, static_cast<int>(entity));
				}
				if (entity.HasComponent<UIText>() || entity.HasComponent<UIInputField>())
				{
					UIText text = entity.HasComponent<UIText>() ? entity.GetComponent<UIText>() : UIText{};
					float horizontalOffset = 0.0f;
					std::optional<UIRect> caretRectangle;
					std::vector<RuntimeUIClipRegion> inputClipRegions = clipRegions->second;
					inputClipRegions.push_back({ rectangle->second, elementTransform->second });
					text.Text = RuntimeUISystem::ResolveText(scene, entity);
					if (const auto* theme = FindScope<UITheme>(scene, entity))
					{
						text.Color *= theme->TextColor;
						text.FontSize *= theme->FontScale;
						if (static_cast<uint64_t>(theme->Font) != 0) text.Font = theme->Font;
					}
					if (entity.HasComponent<UIInputField>())
					{
						const auto& field = entity.GetComponent<UIInputField>();
						text.Enabled = text.Enabled && field.Enabled;
						text.Wrap = false;
						text.Text = field.Password ? std::string(FontAtlasBuilder::DecodeUTF8(field.Text).size(), '*') : field.Text;
						if (field.RuntimeFocused && text.Enabled)
						{
							size_t caret = std::min<size_t>(field.RuntimeCaret, field.Text.size());
							size_t anchor = std::min<size_t>(field.RuntimeSelectionAnchor, field.Text.size());
							auto repairBoundary = [&field](size_t value)
							{
								while (value > 0 && value < field.Text.size()
									&& (static_cast<unsigned char>(field.Text[value]) & 0xc0) == 0x80) --value;
								return value;
							};
							caret = repairBoundary(caret);
							anchor = repairBoundary(anchor);
							if (field.Password)
							{
								caret = FontAtlasBuilder::DecodeUTF8(std::string_view(field.Text).substr(0, caret)).size();
								anchor = FontAtlasBuilder::DecodeUTF8(std::string_view(field.Text).substr(0, anchor)).size();
							}
							if (const auto font = FontManager::Get().Load(text.Font, text.Text, text.FallbackFont, text.EmojiFont))
							{
								const float scale = layout.Scales.at(id);
								auto measurePrefix = [&](size_t length)
								{
									return TextLayoutEngine::Build(font->GetAtlas(),
										std::string_view(text.Text).substr(0, length), text.FontSize * scale,
										0.0f, TextAlignment::Left, text.LineSpacing);
								};
								const TextLayoutResult caretLayout = measurePrefix(caret);
								const float caretX = caretLayout.Width;
								const float anchorX = measurePrefix(anchor).Width;
								horizontalOffset = std::max(0.0f, caretX - rectangle->second.Width + 8.0f * scale);
								const float bottom = rectangle->second.Y + std::max(0.0f, rectangle->second.Height - caretLayout.Height);
								if (caret != anchor)
								{
									glm::vec4 selectionColor{ 0.3f, 0.6f, 1.0f, 1.0f };
									if (const auto* theme = FindScope<UITheme>(scene, entity)) selectionColor = theme->AccentColor;
									selectionColor.a *= 0.45f;
									const UIRect selection{ rectangle->second.X + std::min(caretX, anchorX) - horizontalOffset,
										bottom, std::abs(caretX - anchorX), caretLayout.Height };
									DrawClippedUIQuad(selection, elementTransform->second, inputClipRegions,
										{}, { 0.0f, 0.0f }, { 1.0f, 1.0f }, selectionColor, static_cast<int>(entity));
								}
								caretRectangle = UIRect{ rectangle->second.X + caretX - horizontalOffset,
									bottom, std::max(1.0f, scale), caretLayout.Height };
							}
						}
						else if (field.Text.empty()) { text.Text = field.Placeholder; text.Color.a *= 0.5f; }
					}
					DrawScreenText(text, rectangle->second,
						clipRegions->second, layout.Scales.at(id),
						elementTransform->second,
						static_cast<int>(entity), horizontalOffset);
					if (caretRectangle)
						DrawClippedUIQuad(*caretRectangle, elementTransform->second, inputClipRegions,
							{}, { 0.0f, 0.0f }, { 1.0f, 1.0f }, text.Color, static_cast<int>(entity));
				}
			}
			Renderer2D::EndScene();
			RenderCommand::SetDepthTest(true);
		}

		struct LayoutBuilder
		{
			Scene& SceneValue;
			SceneWorld& Registry;
			RuntimeUILayoutSnapshot& Snapshot;
			RuntimeUIVisibilityMode Visibility;
			bool WriteRuntimeRectangles = true;
			std::set<uint64_t> Visited;

			void LayoutChildren(Entity parent, const UIRect& parentRect,
				const UIRect& inheritedClip, float scale,
				const glm::mat4& parentTransform,
				const std::vector<RuntimeUIClipRegion>& inheritedClipRegions)
			{
				UIRect contentRect = parentRect;
				if (parent.HasComponent<UIScrollView>() && parent.GetComponent<UIScrollView>().Enabled)
				{
					auto& scroll = parent.GetComponent<UIScrollView>();
					const glm::vec2 content = glm::max(glm::vec2(parentRect.Width, parentRect.Height), scroll.ContentSize * scale);
					const glm::vec2 maximum = glm::max(glm::vec2(0.0f), content / scale - glm::vec2(parentRect.Width, parentRect.Height) / scale);
					const glm::vec2 offset = glm::clamp(scroll.Offset, glm::vec2(0.0f), maximum);
					contentRect = { parentRect.X - (scroll.Horizontal ? offset.x * scale : 0.0f),
						parentRect.Y + parentRect.Height - content.y + (scroll.Vertical ? offset.y * scale : 0.0f), content.x, content.y };
				}
				UILayoutGroup* group = parent.HasComponent<UILayoutGroup>()
					? &parent.GetComponent<UILayoutGroup>() : nullptr;
				float horizontalCursor = contentRect.X
					+ (group ? group->Padding.x * scale : 0.0f);
				float verticalCursor = contentRect.Y + contentRect.Height
					- (group ? group->Padding.w * scale : 0.0f);
				for (UUID childID : SceneValue.GetChildrenUUIDs(parent))
				{
					Entity child = SceneValue.FindEntityByUUID(childID);
					if (!child || !child.HasComponent<RectTransform>()
						|| child.HasComponent<Canvas>()
						|| !IsVisible(SceneValue, child, Visibility)
						|| !Visited.emplace(static_cast<uint64_t>(childID)).second)
						continue;
					auto& rectTransform = child.GetComponent<RectTransform>();
					UIRect rectangle = ResolveAnchors(rectTransform, contentRect, scale);
					if (group && group->Enabled)
					{
						glm::vec2 controlled(rectangle.Width, rectangle.Height);
						if (group->ControlChildSize && Finite(group->ChildSize))
							controlled = glm::max(glm::vec2(0.0f), group->ChildSize * scale);
						if (group->Direction == UILayoutDirection::Horizontal)
						{
							rectangle = { horizontalCursor,
								contentRect.Y + group->Padding.y * scale,
								controlled.x, controlled.y };
							horizontalCursor += controlled.x + group->Spacing * scale;
						}
						else
						{
							verticalCursor -= controlled.y;
							rectangle = { contentRect.X + group->Padding.x * scale,
								verticalCursor, controlled.x, controlled.y };
							verticalCursor -= group->Spacing * scale;
						}
					}
					const UIRect clip = UIRect::Intersect(rectangle, inheritedClip);
					if (WriteRuntimeRectangles)
					{
						rectTransform.RuntimeRect = rectangle.ToVector();
						rectTransform.RuntimeClipRect = clip.ToVector();
					}
					Snapshot.Rectangles[childID] = rectangle;
					Snapshot.Clips[childID] = clip;
					Snapshot.Scales[childID] = scale;
					glm::mat4 localTransform(1.0f);
					if (child.HasComponent<Transform>())
					{
						const Transform& authored = child.GetComponent<Transform>();
						const float rotation = std::isfinite(authored._LocalRotation.z)
							? authored._LocalRotation.z : 0.0f;
						const float scaleX = std::isfinite(authored._LocalScale.x)
							? authored._LocalScale.x : 1.0f;
						const float scaleY = std::isfinite(authored._LocalScale.y)
							? authored._LocalScale.y : 1.0f;
						const glm::vec2 pivot{
							rectangle.X + rectangle.Width * rectTransform.Pivot.x,
							rectangle.Y + rectangle.Height * rectTransform.Pivot.y };
						localTransform = glm::translate(glm::mat4(1.0f),
							glm::vec3(pivot, 0.0f))
							* glm::rotate(glm::mat4(1.0f), rotation,
								glm::vec3(0.0f, 0.0f, 1.0f))
							* glm::scale(glm::mat4(1.0f),
								glm::vec3(scaleX, scaleY, 1.0f))
							* glm::translate(glm::mat4(1.0f),
								glm::vec3(-pivot, 0.0f));
					}
					const glm::mat4 accumulatedTransform = parentTransform
						* localTransform;
					Snapshot.Transforms[childID] = accumulatedTransform;
					Snapshot.ClipRegions[childID] = inheritedClipRegions;
					Snapshot.RenderOrder.push_back(childID);
					const bool clipsChildren = rectTransform.ClipChildren
						|| (child.HasComponent<UIScrollView>() && child.GetComponent<UIScrollView>().Enabled);
					const UIRect childClip = clipsChildren
						? clip : inheritedClip;
					std::vector<RuntimeUIClipRegion> childClipRegions =
						inheritedClipRegions;
					if (clipsChildren)
						childClipRegions.push_back({ rectangle,
							accumulatedTransform });
					LayoutChildren(child, rectangle, childClip, scale,
						accumulatedTransform, childClipRegions);
				}
			}
		};

		bool ContainsTransformedPoint(const RuntimeUILayoutSnapshot& layout,
			UUID id, const glm::vec2& point)
		{
			const auto rectangle = layout.Rectangles.find(id);
			const auto transform = layout.Transforms.find(id);
			const auto clipRegions = layout.ClipRegions.find(id);
			if (rectangle == layout.Rectangles.end()
				|| transform == layout.Transforms.end()
				|| clipRegions == layout.ClipRegions.end())
				return false;
			glm::mat4 inverse(1.0f);
			if (!InvertUITransform(transform->second, inverse))
				return false;
			glm::vec2 local;
			if (!TransformUIPosition(inverse, point, local)
				|| !rectangle->second.Contains(local))
				return false;
			for (const RuntimeUIClipRegion& region : clipRegions->second)
			{
				if (!InvertUITransform(region.Transform, inverse)
					|| !TransformUIPosition(inverse, point, local)
					|| !region.Rectangle.Contains(local))
					return false;
			}
			return true;
		}


		bool CanFocus(Entity entity)
		{
			return (entity.HasComponent<UIButton>() && entity.GetComponent<UIButton>().Enabled && entity.GetComponent<UIButton>().Interactable)
				|| (entity.HasComponent<UISlider>() && entity.GetComponent<UISlider>().Enabled && entity.GetComponent<UISlider>().Interactable)
				|| (entity.HasComponent<UIInputField>() && entity.GetComponent<UIInputField>().Enabled && entity.GetComponent<UIInputField>().Interactable);
		}

		bool HasFocus(Entity entity)
		{
			return (entity.HasComponent<UIButton>() && entity.GetComponent<UIButton>().RuntimeFocused)
				|| (entity.HasComponent<UISlider>() && entity.GetComponent<UISlider>().RuntimeFocused)
				|| (entity.HasComponent<UIInputField>() && entity.GetComponent<UIInputField>().RuntimeFocused);
		}

		void SetFocus(Entity entity, bool focused)
		{
			if (entity.HasComponent<UIButton>()) entity.GetComponent<UIButton>().RuntimeFocused = focused;
			if (entity.HasComponent<UISlider>()) entity.GetComponent<UISlider>().RuntimeFocused = focused;
			if (entity.HasComponent<UIInputField>())
			{
				auto& field = entity.GetComponent<UIInputField>();
				if (focused && !field.RuntimeFocused)
					field.RuntimeCaret = field.RuntimeSelectionAnchor = static_cast<uint32_t>(field.Text.size());
				field.RuntimeFocused = focused;
			}
		}

		size_t PreviousCharacter(const std::string& text, size_t position)
		{
			position = std::min(position, text.size());
			if (position > 0) --position;
			while (position > 0 && (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) --position;
			return position;
		}

		size_t NextCharacter(const std::string& text, size_t position)
		{
			if (position < text.size()) ++position;
			while (position < text.size() && (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80) ++position;
			return position;
		}

		void EditInputField(UIInputField& field, const RuntimeUIInputFrame& input)
		{
			if (input.DisplayFrame != 0 && field.RuntimeLastInputFrame == input.DisplayFrame) return;
			field.RuntimeLastInputFrame = input.DisplayFrame;
			field.RuntimeCaret = static_cast<uint32_t>(std::min<size_t>(field.RuntimeCaret, field.Text.size()));
			field.RuntimeSelectionAnchor = static_cast<uint32_t>(std::min<size_t>(field.RuntimeSelectionAnchor, field.Text.size()));
			// C# may replace Text while focused. Repair indices to UTF-8 boundaries.
			while (field.RuntimeCaret < field.Text.size() && (static_cast<unsigned char>(field.Text[field.RuntimeCaret]) & 0xc0) == 0x80) --field.RuntimeCaret;
			while (field.RuntimeSelectionAnchor < field.Text.size() && (static_cast<unsigned char>(field.Text[field.RuntimeSelectionAnchor]) & 0xc0) == 0x80) --field.RuntimeSelectionAnchor;
			if (input.SelectAll) { field.RuntimeSelectionAnchor = 0; field.RuntimeCaret = static_cast<uint32_t>(field.Text.size()); }
			if (input.CaretHome) field.RuntimeCaret = 0;
			if (input.CaretEnd) field.RuntimeCaret = static_cast<uint32_t>(field.Text.size());
			if (input.CaretLeft) field.RuntimeCaret = static_cast<uint32_t>(PreviousCharacter(field.Text, field.RuntimeCaret));
			if (input.CaretRight) field.RuntimeCaret = static_cast<uint32_t>(NextCharacter(field.Text, field.RuntimeCaret));
			if (!input.ExtendSelection && (input.CaretHome || input.CaretEnd || input.CaretLeft || input.CaretRight))
				field.RuntimeSelectionAnchor = field.RuntimeCaret;
			bool cutSelection = false;
			if ((input.Copy || input.Cut) && !field.Password && input.WriteClipboard
				&& field.RuntimeCaret != field.RuntimeSelectionAnchor)
			{
				const size_t first = std::min(field.RuntimeCaret, field.RuntimeSelectionAnchor);
				const size_t last = std::max(field.RuntimeCaret, field.RuntimeSelectionAnchor);
				const bool copied = input.WriteClipboard(field.Text.substr(first, last - first));
				cutSelection = copied && input.Cut && !field.ReadOnly;
			}
			if (field.ReadOnly) return;
			const std::string before = field.Text;
			const std::string& insertedText = input.Paste ? input.ClipboardText : input.TextInput;
			bool valid = false;
			FontAtlasBuilder::DecodeUTF8(insertedText, &valid);
			if (input.Backspace || input.Delete || cutSelection || (valid && !insertedText.empty()))
			{
				size_t first = std::min(field.RuntimeCaret, field.RuntimeSelectionAnchor);
				size_t last = std::max(field.RuntimeCaret, field.RuntimeSelectionAnchor);
				if (first == last && input.Backspace) first = PreviousCharacter(field.Text, first);
				else if (first == last && input.Delete) last = NextCharacter(field.Text, last);
				field.Text.erase(first, last - first);
				field.RuntimeCaret = field.RuntimeSelectionAnchor = static_cast<uint32_t>(first);
				if (valid)
				{
					size_t count = FontAtlasBuilder::DecodeUTF8(field.Text).size();
					std::string insertion;
					for (size_t pos = 0; pos < insertedText.size();)
					{
						const size_t next = NextCharacter(insertedText, pos);
						const unsigned char firstByte = static_cast<unsigned char>(insertedText[pos]);
						if (firstByte >= 32 && firstByte != 127 && count < field.CharacterLimit
							&& field.Text.size() + insertion.size() + next - pos <= 65536)
						{ insertion.append(insertedText, pos, next - pos); ++count; }
						pos = next;
					}
					field.Text.insert(field.RuntimeCaret, insertion);
					field.RuntimeCaret += static_cast<uint32_t>(insertion.size());
					field.RuntimeSelectionAnchor = field.RuntimeCaret;
				}
			}
			if (before != field.Text) ++field.RuntimeChangeSerial;
		}

		struct ExtendedInteraction { bool Handled = false; bool OwnsKeyboard = false; };

		ExtendedInteraction UpdateExtendedControls(Scene& scene, SceneWorld& registry,
			const RuntimeUILayoutSnapshot& layout, RuntimeUIInputFrame& input,
			bool enabled, bool wrapNavigation)
		{
			ExtendedInteraction result;
			std::vector<Entity> focusable;
			for (UUID id : layout.RenderOrder)
			{
				Entity entity = scene.FindEntityByUUID(id);
				if (entity && CanFocus(entity)) focusable.push_back(entity);
			}
			for (auto value : registry.View<UIInputField>())
			{
				Entity entity(value, &scene);
				if (!enabled || !layout.Rectangles.contains(entity.GetUUID()) || !CanFocus(entity))
					entity.GetComponent<UIInputField>().RuntimeFocused = false;
			}
			for (auto value : registry.View<UISlider>())
			{
				Entity entity(value, &scene);
				auto& slider = entity.GetComponent<UISlider>();
				if (!enabled || !layout.Rectangles.contains(entity.GetUUID()) || !CanFocus(entity))
					slider.RuntimeFocused = slider.RuntimeDragging = false;
			}
			if (!enabled) return result;
			const glm::vec2 point(input.PointerPosition.x, static_cast<float>(layout.ViewportHeight) - input.PointerPosition.y);
			Entity hit;
			for (auto item = layout.RenderOrder.rbegin(); item != layout.RenderOrder.rend(); ++item)
			{
				Entity entity = scene.FindEntityByUUID(*item);
				if (entity && IsRaycastTarget(entity) && ContainsTransformedPoint(layout, *item, point)) { hit = entity; break; }
			}
			if (input.MousePressed)
			{
				Entity target;
				for (Entity current = hit; current; current = scene.GetParent(current))
					if (CanFocus(current)) { target = current; break; }
				for (Entity entity : focusable) SetFocus(entity, entity == target);
				if (target && target.HasComponent<UISlider>()) target.GetComponent<UISlider>().RuntimeDragging = true;
			}
			if ((input.FocusNext || input.FocusPrevious) && !focusable.empty())
			{
				auto found = std::find_if(focusable.begin(), focusable.end(), HasFocus);
				size_t index = found == focusable.end() ? (input.FocusPrevious ? focusable.size() - 1 : 0)
					: static_cast<size_t>(std::distance(focusable.begin(), found));
				if (found != focusable.end())
				{
					if (input.FocusPrevious) index = index > 0 ? index - 1 : wrapNavigation ? focusable.size() - 1 : index;
					else index = index + 1 < focusable.size() ? index + 1 : wrapNavigation ? 0 : index;
				}
				for (size_t i = 0; i < focusable.size(); ++i) SetFocus(focusable[i], i == index);
				input.KeyboardMoveNext = input.KeyboardMovePrevious = false;
				result.Handled = true;
			}
			if (Finite(input.ScrollDelta) && input.ScrollDelta != glm::vec2(0.0f))
			{
				for (Entity current = hit; current; current = scene.GetParent(current))
				{
					if (!current.HasComponent<UIScrollView>() || !layout.Rectangles.contains(current.GetUUID())) continue;
					auto& scroll = current.GetComponent<UIScrollView>();
					if (!scroll.Enabled) continue;
					result.Handled = true;
					const auto& rect = layout.Rectangles.at(current.GetUUID());
					const float scale = layout.Scales.at(current.GetUUID());
					const glm::vec2 maximum = glm::max(glm::vec2(0.0f), scroll.ContentSize - glm::vec2(rect.Width, rect.Height) / scale);
					glm::vec2 delta(scroll.Horizontal ? -input.ScrollDelta.x : 0.0f, scroll.Vertical ? -input.ScrollDelta.y : 0.0f);
					if (scroll.Horizontal && !scroll.Vertical && delta.x == 0.0f) delta.x = -input.ScrollDelta.y;
					const glm::vec2 next = glm::clamp(scroll.Offset + delta * scroll.ScrollSpeed, glm::vec2(0.0f), maximum);
					if (next != scroll.Offset) { scroll.Offset = next; result.Handled = true; break; }
				}
			}
			for (Entity entity : focusable)
			{
				if (entity.HasComponent<UISlider>())
				{
					auto& slider = entity.GetComponent<UISlider>();
					if (slider.RuntimeDragging)
					{
						if (input.MousePressed || input.MouseHeld || input.MouseReleased)
						{
							glm::mat4 inverse;
							glm::vec2 local;
							const auto& rect = layout.Rectangles.at(entity.GetUUID());
							if (InvertUITransform(layout.Transforms.at(entity.GetUUID()), inverse) && TransformUIPosition(inverse, point, local))
							{
								const float amount = slider.Vertical ? (local.y - rect.Y) / std::max(rect.Height, 0.001f) : (local.x - rect.X) / std::max(rect.Width, 0.001f);
								RuntimeUISystem::SetSliderValue(entity, slider.Minimum + std::clamp(amount, 0.0f, 1.0f) * (slider.Maximum - slider.Minimum));
							}
							result.Handled = true;
						}
						if (input.MouseReleased || (!input.MouseHeld && !input.MousePressed)) slider.RuntimeDragging = false;
					}
					if (slider.RuntimeFocused)
					{
						result.OwnsKeyboard = true;
						result.Handled = result.Handled || input.KeyboardMoveNextHeld || input.KeyboardMovePreviousHeld
							|| input.GamepadMoveNextHeld || input.GamepadMovePreviousHeld;
						const float step = slider.WholeNumbers ? std::max(1.0f, slider.Step) : slider.Step > 0.0f ? slider.Step : (slider.Maximum - slider.Minimum) * 0.01f;
						if (input.KeyboardMoveNext || input.GamepadMoveNext) { RuntimeUISystem::SetSliderValue(entity, slider.Value + step); result.Handled = true; }
						if (input.KeyboardMovePrevious || input.GamepadMovePrevious) { RuntimeUISystem::SetSliderValue(entity, slider.Value - step); result.Handled = true; }
					}
				}
				if (entity.HasComponent<UIInputField>() && entity.GetComponent<UIInputField>().RuntimeFocused)
				{
					auto& field = entity.GetComponent<UIInputField>();
					EditInputField(field, input);
					result.OwnsKeyboard = true;
					// All gameplay input is suppressed while typing, including held movement keys.
					result.Handled = true;
					if (input.Cancel) field.RuntimeFocused = false;
				}
			}
			if (result.OwnsKeyboard)
			{
				input.KeyboardMoveNext = input.KeyboardMovePrevious = input.KeyboardSubmit = false;
				input.GamepadMoveNext = input.GamepadMovePrevious = input.GamepadSubmit = false;
			}
			return result;
		}

		bool WouldCaptureGameplayInput(Scene& scene, SceneWorld& registry,
			uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
			const RuntimeUIInputFrame& input)
		{
			UIEventSystem* eventSystem = nullptr;
			uint64_t eventSystemID = (std::numeric_limits<uint64_t>::max)();
			for (const ekit::Entity value : registry.View<UIEventSystem, ID>())
			{
				Entity entity(value, &scene);
				auto& candidate = registry.Get<UIEventSystem>(value);
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

			for (UUID id : layout.RenderOrder)
			{
				Entity entity = scene.FindEntityByUUID(id);
				if (!entity || !CanFocus(entity)) continue;
				if (entity.HasComponent<UIInputField>() && entity.GetComponent<UIInputField>().RuntimeFocused)
					return true;
				if (entity.HasComponent<UISlider>())
				{
					const auto& slider = entity.GetComponent<UISlider>();
					if ((slider.RuntimeDragging && (input.MouseHeld || input.MouseReleased))
						|| (slider.RuntimeFocused && (input.KeyboardMoveNext || input.KeyboardMovePrevious
							|| input.GamepadMoveNext || input.GamepadMovePrevious || input.KeyboardMoveNextHeld
							|| input.KeyboardMovePreviousHeld || input.GamepadMoveNextHeld || input.GamepadMovePreviousHeld))) return true;
				}
				if (input.FocusNext || input.FocusPrevious) return true;
			}
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
				if (!target || !ContainsTransformedPoint(layout, *item, mousePoint))
					continue;
				if (!IsRaycastTarget(target))
					continue;
				if (input.ScrollDelta != glm::vec2(0.0f) && FindScope<UIScrollView>(scene, target))
					return true;
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
					&& IsRaycastTarget(captured);
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
		SceneWorld& registry, uint32_t viewportWidth, uint32_t viewportHeight,
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
		for (const ekit::Entity value : registry.View<Canvas, ID>())
		{
			Entity entity(value, &scene);
			const Canvas& canvas = registry.Get<Canvas>(value);
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
			snapshot.Transforms[item.Value.GetUUID()] = glm::mat4(1.0f);
			const std::vector<RuntimeUIClipRegion> viewportClipRegions = {
				{ viewport, glm::mat4(1.0f) }
			};
			snapshot.ClipRegions[item.Value.GetUUID()] = viewportClipRegions;
			snapshot.RenderOrder.push_back(item.Value.GetUUID());
			builder.LayoutChildren(item.Value, viewport, viewport, scale,
				glm::mat4(1.0f), viewportClipRegions);
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

	glm::mat4 RuntimeUISystem::GetEditorCanvasTransform(
		const glm::vec2&)
	{
		const float worldUnitsPerPixel = 1.0f / EditorCanvasPixelsPerUnit;
		// Canvas authoring coordinates match runtime layout coordinates: local
		// (0, 0, 0) is the lower-left corner and the plane grows toward +X/+Y.
		return glm::scale(glm::mat4(1.0f), glm::vec3(
				worldUnitsPerPixel, worldUnitsPerPixel, 1.0f));
	}

	RuntimeUILayoutSnapshot RuntimeUISystem::BuildEditorLayout(Scene& scene,
		SceneWorld& registry, RuntimeUIVisibilityMode visibility)
	{
		RuntimeUILayoutSnapshot snapshot;
		snapshot.DPI = 96.0f;
		struct CanvasItem { Entity Value; int32_t Order = 0; uint64_t ID = 0; };
		std::vector<CanvasItem> canvases;
		for (const ekit::Entity value : registry.View<Canvas, ID>())
		{
			Entity entity(value, &scene);
			const Canvas& canvas = registry.Get<Canvas>(value);
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

		LayoutBuilder builder{ scene, registry, snapshot, visibility, false };
		for (const CanvasItem& item : canvases)
		{
			const Canvas& canvas = item.Value.GetComponent<Canvas>();
			const glm::vec2 referenceResolution = Finite(canvas.ReferenceResolution)
				&& glm::all(glm::greaterThan(canvas.ReferenceResolution,
					glm::vec2(0.0f)))
				? canvas.ReferenceResolution : glm::vec2(1920.0f, 1080.0f);
			const UIRect canvasRectangle{ 0.0f, 0.0f,
				referenceResolution.x, referenceResolution.y };
			const float scale = std::clamp(Finite(canvas.ScaleFactor)
				? canvas.ScaleFactor : 1.0f, 0.01f, 100.0f);
			const glm::mat4 canvasTransform = GetEditorCanvasTransform(
				referenceResolution);

			snapshot.ViewportWidth = std::max(snapshot.ViewportWidth,
				static_cast<uint32_t>(std::ceil(referenceResolution.x)));
			snapshot.ViewportHeight = std::max(snapshot.ViewportHeight,
				static_cast<uint32_t>(std::ceil(referenceResolution.y)));
			builder.Visited.emplace(item.ID);
			snapshot.Rectangles[item.Value.GetUUID()] = canvasRectangle;
			snapshot.Clips[item.Value.GetUUID()] = canvasRectangle;
			snapshot.Scales[item.Value.GetUUID()] = scale;
			snapshot.Transforms[item.Value.GetUUID()] = canvasTransform;
			const std::vector<RuntimeUIClipRegion> canvasClipRegions = {
				{ canvasRectangle, canvasTransform }
			};
			snapshot.ClipRegions[item.Value.GetUUID()] = canvasClipRegions;
			snapshot.RenderOrder.push_back(item.Value.GetUUID());
			builder.LayoutChildren(item.Value, canvasRectangle, canvasRectangle,
				scale, canvasTransform, canvasClipRegions);
		}
		return snapshot;
	}

	RuntimeUILayoutSnapshot RuntimeUISystem::BuildEditorLayout(Scene& scene,
		RuntimeUIVisibilityMode visibility)
	{
		return BuildEditorLayout(scene, scene.m_Registry, visibility);
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

	void RuntimeUISystem::Reset(SceneWorld& registry)
	{
		for (const auto entity : registry.View<UISlider>())
		{
			auto& slider = registry.Get<UISlider>(entity);
			slider.RuntimeDragging = slider.RuntimeFocused = false;
			slider.RuntimeChangeSerial = 0;
		}
		for (const auto entity : registry.View<UIInputField>())
		{
			auto& field = registry.Get<UIInputField>(entity);
			field.RuntimeFocused = false;
			field.RuntimeCaret = field.RuntimeSelectionAnchor = 0;
			field.RuntimeChangeSerial = 0;
			field.RuntimeLastInputFrame = 0;
		}

		for (const ekit::Entity entity : registry.View<UIButton>())
		{
			auto& button = registry.Get<UIButton>(entity);
			button.RuntimeHovered = false;
			button.RuntimePressed = false;
			button.RuntimeFocused = false;
			button.RuntimeClickedThisFrame = false;
			button.RuntimeClickSerial = 0;
		}
		for (const ekit::Entity entity : registry.View<RectTransform>())
		{
			auto& rectangle = registry.Get<RectTransform>(entity);
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
		const auto scroll = source.GetScrollDelta();
		input.ScrollDelta = { scroll.X, scroll.Y };
		input.TextInput = Input::GetTextInput();
		input.DisplayFrame = Input::GetFrameSnapshot().FrameNumber;
		const bool control = source.IsKeyHeld(341) || source.IsKeyHeld(345);
		input.Copy = control && source.WasKeyPressed(67);
		input.Cut = control && source.WasKeyPressed(88);
		input.Paste = control && source.WasKeyPressed(86);
		if (input.Paste) input.ClipboardText = Input::GetClipboardText();
		input.WriteClipboard = [](const std::string& value) { return Input::SetClipboardText(value); };
		const auto pressedOrRepeated = [&source](uint32_t key)
		{
			if (source.WasKeyPressed(key)) return true;
			const auto& events = Input::GetFrameSnapshot().Events;
			return std::any_of(events.begin(), events.end(), [key](const InputEventQueue::Event& event)
			{ return event.Source == InputEventQueue::Device::Keyboard && event.Code == key
				&& event.Transition == InputEventQueue::Action::Repeated; });
		};
		input.Backspace = pressedOrRepeated(259);
		input.Delete = pressedOrRepeated(261);
		input.CaretLeft = pressedOrRepeated(263);
		input.CaretRight = pressedOrRepeated(262);
		input.CaretHome = pressedOrRepeated(268);
		input.CaretEnd = pressedOrRepeated(269);
		input.ExtendSelection = source.IsKeyHeld(340) || source.IsKeyHeld(344);
		input.SelectAll = source.WasKeyPressed(65) && (source.IsKeyHeld(341) || source.IsKeyHeld(345));
		input.Cancel = source.WasKeyPressed(256);
		input.FocusNext = source.WasKeyPressed(258) && !input.ExtendSelection;
		input.FocusPrevious = source.WasKeyPressed(258) && input.ExtendSelection;
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

	void RuntimeUISystem::Update(Scene& scene, SceneWorld& registry,
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
		const auto scroll = source.GetScrollDelta();
		input.ScrollDelta = { scroll.X, scroll.Y };
		input.TextInput = Input::GetTextInput();
		input.DisplayFrame = Input::GetFrameSnapshot().FrameNumber;
		const bool control = source.IsKeyHeld(341) || source.IsKeyHeld(345);
		input.Copy = control && source.WasKeyPressed(67);
		input.Cut = control && source.WasKeyPressed(88);
		input.Paste = control && source.WasKeyPressed(86);
		if (input.Paste) input.ClipboardText = Input::GetClipboardText();
		input.WriteClipboard = [](const std::string& value) { return Input::SetClipboardText(value); };
		const auto pressedOrRepeated = [&source](uint32_t key)
		{
			if (source.WasKeyPressed(key)) return true;
			const auto& events = Input::GetFrameSnapshot().Events;
			return std::any_of(events.begin(), events.end(), [key](const InputEventQueue::Event& event)
			{ return event.Source == InputEventQueue::Device::Keyboard && event.Code == key
				&& event.Transition == InputEventQueue::Action::Repeated; });
		};
		input.Backspace = pressedOrRepeated(259);
		input.Delete = pressedOrRepeated(261);
		input.CaretLeft = pressedOrRepeated(263);
		input.CaretRight = pressedOrRepeated(262);
		input.CaretHome = pressedOrRepeated(268);
		input.CaretEnd = pressedOrRepeated(269);
		input.ExtendSelection = source.IsKeyHeld(340) || source.IsKeyHeld(344);
		input.SelectAll = source.WasKeyPressed(65) && (source.IsKeyHeld(341) || source.IsKeyHeld(345));
		input.Cancel = source.WasKeyPressed(256);
		input.FocusNext = source.WasKeyPressed(258) && !input.ExtendSelection;
		input.FocusPrevious = source.WasKeyPressed(258) && input.ExtendSelection;
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

	void RuntimeUISystem::UpdateWithInput(Scene& scene, SceneWorld& registry,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		const RuntimeUIInputFrame& sourceInput)
	{
		RuntimeUIInputFrame input = sourceInput;
		if (s_PointerCaptureScene != &scene)
			ClearPointerCapture();
		if (s_ControlOwnershipScene != &scene)
			ClearControlOwnership();
		const RuntimeUILayoutSnapshot layout = BuildLayout(scene, registry,
			viewportWidth, viewportHeight, dpi);
		UIEventSystem* eventSystem = nullptr;
		uint64_t eventSystemID = (std::numeric_limits<uint64_t>::max)();
		for (const ekit::Entity value : registry.View<UIEventSystem, ID>())
		{
			Entity entity(value, &scene);
			auto& candidate = registry.Get<UIEventSystem>(value);
			const uint64_t id = static_cast<uint64_t>(entity.GetUUID());
			if (candidate.Enabled && scene.IsActiveInHierarchy(entity)
				&& id < eventSystemID)
			{
				eventSystem = &candidate;
				eventSystemID = id;
			}
		}
		for (const ekit::Entity value : registry.View<UIButton>())
		{
			auto& button = registry.Get<UIButton>(value);
			button.RuntimeHovered = false;
			button.RuntimeClickedThisFrame = false;
		}
		const ExtendedInteraction extended = UpdateExtendedControls(scene, registry,
			layout, input, eventSystem && input.WindowFocused,
			eventSystem && eventSystem->WrapNavigation);
		if (!eventSystem)
		{
			for (const ekit::Entity value : registry.View<UIButton>())
			{
				auto& button = registry.Get<UIButton>(value);
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
			for (const ekit::Entity value : registry.View<UIButton>())
				registry.Get<UIButton>(value).RuntimePressed = false;
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
		for (const ekit::Entity value : registry.View<UIButton>())
		{
			const auto found = std::find_if(buttons.begin(), buttons.end(),
				[value](const Candidate& item)
				{
					return static_cast<ekit::Entity>(item.Value) == value;
				});
			if (found == buttons.end())
			{
				auto& button = registry.Get<UIButton>(value);
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
		if (!extended.OwnsKeyboard && !buttons.empty() && focused == buttons.end())
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
			if (!target || !ContainsTransformedPoint(layout, *item, mousePoint))
				continue;
			if (!IsRaycastTarget(target))
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
				&& IsRaycastTarget(captured);
			if (!validPointerCapture)
				ClearPointerCapture();
		}
		bool handledInteraction = extended.Handled || (input.MousePressed && pointerHandled)
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

		// Persistent callbacks are dispatched after all focus/click state for this
		// UI frame is committed and before Scene invokes the normal OnUpdate phase.
		// Copy listeners first because a callback may destroy its own Button or any
		// later target through the managed deferred-command transaction.
		struct PendingButtonClick
		{
			UUID Button;
			std::vector<UIButtonOnClickListener> Listeners;
		};
		std::vector<PendingButtonClick> pendingClicks;
		for (const Candidate& item : buttons)
		{
			if (!item.Value || !item.Value.HasComponent<UIButton>())
				continue;
			const UIButton& button = item.Value.GetComponent<UIButton>();
			if (button.RuntimeClickedThisFrame && !button.OnClick.empty())
				pendingClicks.push_back({ item.Value.GetUUID(), button.OnClick });
		}
		for (const PendingButtonClick& click : pendingClicks)
		{
			for (const UIButtonOnClickListener& listener : click.Listeners)
			{
				const bool assigned = static_cast<uint64_t>(listener.TargetEntity) != 0
					&& static_cast<uint64_t>(listener.TargetAttachmentID) != 0
					&& static_cast<uint64_t>(listener.ScriptAsset) != 0
					&& !listener.MethodName.empty();
				if (!listener.Enabled || !assigned)
					continue;
				const Scripting::ScriptStatus status =
					Scripting::ScriptEngine::Get().InvokeMethod(scene,
						listener.TargetEntity, listener.TargetAttachmentID,
						static_cast<uint64_t>(listener.ScriptAsset),
						listener.MethodName);
				if (status != Scripting::ScriptStatus::Success)
				{
					TC_Core_Warn("UIButton {0} OnClick listener {1} failed with status {2}",
						static_cast<uint64_t>(click.Button), listener.MethodName,
						static_cast<int32_t>(status));
				}
			}
		}
	}

	void RuntimeUISystem::UpdateWithInput(Scene& scene, uint32_t viewportWidth,
		uint32_t viewportHeight, float dpi, const RuntimeUIInputFrame& input)
	{
		UpdateWithInput(scene, scene.m_Registry, viewportWidth, viewportHeight,
			dpi, input);
	}

	void RuntimeUISystem::RenderWorldText(Scene& scene, SceneWorld& registry,
		RuntimeUIVisibilityMode visibility)
	{
		for (const ekit::Entity value : registry.View<Transform, TextRenderer>())
		{
			Entity entity(value, &scene);
			auto& text = registry.Get<TextRenderer>(value);
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
			const glm::mat4 world = scene.GetRuntimeRenderTransform(entity.GetUUID());
			for (const TextGlyphQuad& glyph : layout.Glyphs)
			{
				const glm::mat4 local = glm::translate(glm::mat4(1.0f), {
					glyph.Rect.X + glyph.Rect.Width * 0.5f,
					glyph.Rect.Y + glyph.Rect.Height * 0.5f, 0.0f })
					* glm::scale(glm::mat4(1.0f), {
						glyph.Rect.Width, glyph.Rect.Height, 1.0f });
				Renderer2D::DrawTexturedQuadRegion(world * local,
					font->GetTexture(), glyph.UVMin, glyph.UVMax, text.Color,
					registry.GetPickingID(value));
			}
		}
	}

	void RuntimeUISystem::RenderWorldText(Scene& scene,
		RuntimeUIVisibilityMode visibility)
	{
		RenderWorldText(scene, scene.m_Registry, visibility);
	}

	void RuntimeUISystem::RenderScreen(Scene& scene, SceneWorld& registry,
		uint32_t viewportWidth, uint32_t viewportHeight, float dpi,
		RuntimeUIVisibilityMode visibility)
	{
		if (viewportWidth == 0 || viewportHeight == 0)
			return;
		const RuntimeUILayoutSnapshot layout = BuildLayout(scene, registry,
			viewportWidth, viewportHeight, dpi, visibility);
		RenderUILayout(scene, layout, visibility,
			glm::ortho(0.0f, static_cast<float>(viewportWidth), 0.0f,
				static_cast<float>(viewportHeight), -1.0f, 1.0f));
	}

	void RuntimeUISystem::RenderScreen(Scene& scene, uint32_t viewportWidth,
		uint32_t viewportHeight, float dpi, RuntimeUIVisibilityMode visibility)
	{
		RenderScreen(scene, scene.m_Registry, viewportWidth, viewportHeight, dpi,
			visibility);
	}

	void RuntimeUISystem::RenderEditorCanvas(Scene& scene,
		SceneWorld& registry, const glm::mat4& editorViewProjection,
		RuntimeUIVisibilityMode visibility)
	{
		const RuntimeUILayoutSnapshot layout = BuildEditorLayout(scene, registry,
			visibility);
		RenderUILayout(scene, layout, visibility, editorViewProjection);
	}

	void RuntimeUISystem::RenderEditorCanvas(Scene& scene,
		const glm::mat4& editorViewProjection,
		RuntimeUIVisibilityMode visibility)
	{
		RenderEditorCanvas(scene, scene.m_Registry, editorViewProjection,
			visibility);
	}


	std::string RuntimeUISystem::ResolveText(Scene& scene, Entity entity)
	{
		if (!entity) return {};
		const std::string fallback = entity.HasComponent<UIText>() ? entity.GetComponent<UIText>().Text : std::string{};
		if (!entity.HasComponent<UILocalizedText>() || !entity.GetComponent<UILocalizedText>().Enabled) return fallback;
		const auto& key = entity.GetComponent<UILocalizedText>().Key;
		auto* localization = FindScope<UILocalization>(scene, entity);
		if (!localization || key.empty()) return fallback;
		if (localization->RuntimeTableSource != localization->Table)
		{
			localization->RuntimeTranslations.clear();
			localization->RuntimeTableSource = localization->Table;
			try
			{
				if (localization->Table.size() <= 65536)
				{
					const auto table = YAML::Load(localization->Table);
					if (table.IsMap())
						for (const auto& locale : table)
							if (locale.first.IsScalar() && locale.second.IsMap())
								for (const auto& entry : locale.second)
									if (entry.first.IsScalar() && entry.second.IsScalar())
										localization->RuntimeTranslations[locale.first.as<std::string>()][entry.first.as<std::string>()] = entry.second.as<std::string>();
				}
			}
			catch (const std::exception&) { localization->RuntimeTranslations.clear(); }
		}
		for (const auto& locale : { localization->Locale, localization->FallbackLocale })
		{
			const auto language = localization->RuntimeTranslations.find(locale);
			if (language == localization->RuntimeTranslations.end()) continue;
			const auto translated = language->second.find(key);
			if (translated != language->second.end()) return translated->second;
		}
		return fallback;
	}

	bool RuntimeUISystem::SetSliderValue(Entity entity, float value)
	{
		if (!entity || !entity.HasComponent<UISlider>() || !Finite(value)) return false;
		auto& slider = entity.GetComponent<UISlider>();
		if (!Finite(slider.Minimum) || !Finite(slider.Maximum) || slider.Maximum < slider.Minimum) return false;
		value = std::clamp(value, slider.Minimum, slider.Maximum);
		if (slider.WholeNumbers) value = std::round(value);
		else if (Finite(slider.Step) && slider.Step > 0.0f)
			value = slider.Minimum + std::round((value - slider.Minimum) / slider.Step) * slider.Step;
		value = std::clamp(value, slider.Minimum, slider.Maximum);
		if (slider.Value != value) { slider.Value = value; ++slider.RuntimeChangeSerial; }
		return true;
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

	bool RuntimeUISystem::FocusButton(Scene& scene, SceneWorld& registry,
		Entity entity)
	{
		if (!entity || !entity.HasComponent<UIButton>())
			return false;
		auto& target = entity.GetComponent<UIButton>();
		if (!target.Enabled || !target.Interactable || !scene.IsActiveInHierarchy(entity))
			return false;
		for (const ekit::Entity value : registry.View<UIButton>())
			registry.Get<UIButton>(value).RuntimeFocused = value == static_cast<ekit::Entity>(entity);
		return true;
	}

	bool RuntimeUISystem::FocusButton(Scene& scene, Entity entity)
	{
		return FocusButton(scene, scene.m_Registry, entity);
	}

}
