#include "EditorLayer.h"
#include "EditorPlayToolbar.h"
#include "SceneToolbarDrawing.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Editor/EditorShortcutRouter.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptDiagnosticSink.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Runtime/RuntimeUI.h"

#include "Player/PlayerBuilder.h"
#include "ImGuizmo.h"

#include "EditorLayerDetail.h"
#include "EditorViewportHandles.h"

#include "EditorLayer.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Runtime/RuntimeUI.h"
#include "Player/PlayerBuilder.h"
#include "ImGuizmo.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace TomCat {

using namespace EditorLayerDetail;

	void EditorViewportHandles::FrameSceneEntity(Entity root)
	{
		glm::vec3 minimum, maximum;
		if (!GetEntityBounds(root, minimum, maximum)) return;
		m_Layer.m_EditorCamera.FrameBounds(minimum, maximum);
		m_Layer.FocusEditorPanel("Scene", m_Layer.m_ShowScenePanel);
	}

	bool EditorViewportHandles::GetEntityBounds(Entity root, glm::vec3& minimum, glm::vec3& maximum, bool includeChildren)
	{
		if (!m_Layer.m_ActiveScene || !root || !root.HasComponent<ID>())
			return false;
		Entity activeRoot = m_Layer.m_ActiveScene->FindEntityByUUID(root.GetUUID());
		if (!activeRoot)
			return false;

		struct FocusBounds
		{
			glm::vec3 Minimum{ (std::numeric_limits<float>::max)() };
			glm::vec3 Maximum{ (std::numeric_limits<float>::lowest)() };
			bool HasGeometry = false;
			std::vector<glm::vec3> Pivots;

			void Add(const glm::vec3& point)
			{
				if (!std::isfinite(point.x) || !std::isfinite(point.y)
					|| !std::isfinite(point.z))
					return;
				Minimum = glm::min(Minimum, point);
				Maximum = glm::max(Maximum, point);
				HasGeometry = true;
			}

			void AddUnitQuad(const glm::mat4& transform)
			{
				constexpr glm::vec4 corners[] = {
					{ -0.5f, -0.5f, 0.0f, 1.0f },
					{  0.5f, -0.5f, 0.0f, 1.0f },
					{  0.5f,  0.5f, 0.0f, 1.0f },
					{ -0.5f,  0.5f, 0.0f, 1.0f }
				};
				for (const glm::vec4& corner : corners)
					Add(glm::vec3(transform * corner));
			}
		};

		FocusBounds bounds;
		Entity canvasOwner;
		for (Entity current = activeRoot; current;
			current = m_Layer.m_ActiveScene->GetParent(current))
		{
			if (current.HasComponent<Canvas>())
			{
				canvasOwner = current;
				break;
			}
		}
		if (canvasOwner)
		{
			const RuntimeUILayoutSnapshot layout =
				RuntimeUISystem::BuildEditorLayout(*m_Layer.m_ActiveScene,
					RuntimeUIVisibilityMode::Editor);
			std::unordered_set<uint64_t> visitedUI;
			std::function<void(Entity, bool)> collectUI =
				[&](Entity entity, bool isRoot)
			{
				if (!entity || !entity.HasComponent<ID>()
					|| (!isRoot
						&& !m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(entity)))
					return;
				const UUID id = entity.GetUUID();
				if (!visitedUI.emplace(static_cast<uint64_t>(id)).second)
					return;
				const auto rectangleIt = layout.Rectangles.find(id);
				const auto transformIt = layout.Transforms.find(id);
				if (rectangleIt != layout.Rectangles.end()
					&& transformIt != layout.Transforms.end())
				{
					const UIRect& rectangle = rectangleIt->second;
					const glm::vec2 corners[4] = {
						{ rectangle.X, rectangle.Y },
						{ rectangle.X + rectangle.Width, rectangle.Y },
						{ rectangle.X + rectangle.Width,
							rectangle.Y + rectangle.Height },
						{ rectangle.X, rectangle.Y + rectangle.Height }
					};
					for (const glm::vec2& corner : corners)
						bounds.Add(glm::vec3(transformIt->second
							* glm::vec4(corner, 0.0f, 1.0f)));
				}
				if (includeChildren) for (UUID childID : m_Layer.m_ActiveScene->GetChildrenUUIDs(entity))
				{
					Entity child = m_Layer.m_ActiveScene->FindEntityByUUID(childID);
					if (child && m_Layer.m_ActiveScene->GetParent(child) == entity)
						collectUI(child, false);
				}
			};
			collectUI(activeRoot, true);
			if (bounds.HasGeometry)
			{
				minimum = bounds.Minimum; maximum = bounds.Maximum;
			}
			return bounds.HasGeometry;
		}

		std::unordered_set<uint64_t> visited;
		std::unordered_set<uint64_t> framedIDs;
		std::function<void(Entity, bool)> collect = [&](Entity entity, bool isRoot)
		{
			if (!entity || !entity.HasComponent<ID>()
				|| (!isRoot && !m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(entity)))
				return;
			const UUID id = entity.GetUUID();
			const uint64_t rawID = static_cast<uint64_t>(id);
			if (!visited.emplace(rawID).second)
				return;
			framedIDs.emplace(rawID);

			glm::mat4 world(1.0f);
			if (entity.HasComponent<Transform>())
			{
				world = m_Layer.m_ActiveScene->GetRuntimeRenderTransform(id);
				bounds.Pivots.push_back(glm::vec3(world * glm::vec4(0, 0, 0, 1)));
			}

			if (entity.HasComponent<SpriteRenderer>()
				&& entity.GetComponent<SpriteRenderer>().Enabled)
				bounds.AddUnitQuad(world);

			if (entity.HasComponent<Tilemap2D>())
			{
				const Tilemap2D& tilemap = entity.GetComponent<Tilemap2D>();
				const Entity parent = m_Layer.m_ActiveScene->GetParent(entity);
				const Grid2D* grid = parent && parent.HasComponent<Grid2D>()
					? &parent.GetComponent<Grid2D>() : nullptr;
				if (tilemap.Enabled)
				{
					for (const TilemapCell& cell : tilemap.Cells)
					{
						if (static_cast<uint64_t>(cell.SpriteHandle) != 0)
							bounds.AddUnitQuad(world
								* Tilemap2DRuntime::GetCellTransform(tilemap, cell, grid));
					}
				}
			}

			if (entity.HasComponent<LineRenderer>())
			{
				const LineRenderer& line = entity.GetComponent<LineRenderer>();
				if (line.Enabled)
				{
					bounds.Add(glm::vec3(world * glm::vec4(line.Start, 1.0f)));
					bounds.Add(glm::vec3(world * glm::vec4(line.End, 1.0f)));
				}
			}

			if (entity.HasComponent<TextRenderer>())
			{
				const TextRenderer& text = entity.GetComponent<TextRenderer>();
				if (text.Enabled && !text.Text.empty() && std::isfinite(text.FontSize)
					&& text.FontSize > 0.0f)
				{
					const Ref<RuntimeFont> font = FontManager::Get().Load(text.Font,
						text.Text, text.FallbackFont, text.EmojiFont);
					if (font)
					{
						const TextLayoutResult layout = TextLayoutEngine::Build(
							font->GetAtlas(), text.Text, text.FontSize,
							std::max(0.0f, text.MaxWidth), text.Alignment,
							text.LineSpacing);
						for (const TextGlyphQuad& glyph : layout.Glyphs)
						{
							bounds.AddUnitQuad(world
								* glm::translate(glm::mat4(1.0f), {
									glyph.Rect.X + glyph.Rect.Width * 0.5f,
									glyph.Rect.Y + glyph.Rect.Height * 0.5f, 0.0f })
								* glm::scale(glm::mat4(1.0f), {
									glyph.Rect.Width, glyph.Rect.Height, 1.0f }));
						}
					}
				}
			}

			if (entity.HasComponent<ParticleSystem2D>())
			{
				const ParticleSystem2D& system = entity.GetComponent<ParticleSystem2D>();
				if (system.Enabled)
				{
					for (const Particle2D& particle : system.RuntimeParticles)
					{
						const float size = ParticleSystem2DRuntime::EvaluateSize(particle);
						if (std::isfinite(size) && size > 0.0f)
							bounds.AddUnitQuad(world
								* glm::translate(glm::mat4(1.0f),
									glm::vec3(particle.Position, 0.0f))
								* glm::scale(glm::mat4(1.0f),
									glm::vec3(size, size, 1.0f)));
					}
				}
			}

			if (entity.HasComponent<Light2D>())
			{
				const Light2D& light = entity.GetComponent<Light2D>();
				if (light.Enabled && light.Type == Light2DType::Point
					&& std::isfinite(light.Radius) && light.Radius > 0.0f)
					bounds.AddUnitQuad(world * glm::scale(glm::mat4(1.0f),
						glm::vec3(light.Radius * 2.0f, light.Radius * 2.0f, 1.0f)));
			}

			if (includeChildren) for (UUID childID : m_Layer.m_ActiveScene->GetChildrenUUIDs(entity))
			{
				Entity child = m_Layer.m_ActiveScene->FindEntityByUUID(childID);
				if (child && m_Layer.m_ActiveScene->GetParent(child) == entity)
					collect(child, false);
			}
		};
		collect(activeRoot, true);

		for (const ColliderDebugShape& shape : m_Layer.m_ActiveScene->GetColliderDebugShapes(
			m_Layer.m_SceneState != EditorLayer::SceneState::Edit))
		{
			if (framedIDs.contains(static_cast<uint64_t>(shape.EntityID)))
				bounds.AddUnitQuad(shape.Transform);
		}

		if (!bounds.HasGeometry)
		{
			for (const glm::vec3& pivot : bounds.Pivots)
				bounds.Add(pivot);
		}
		if (!bounds.HasGeometry)
			return false;

		const glm::vec3 center = (bounds.Minimum + bounds.Maximum) * 0.5f;
		const glm::vec3 extent = bounds.Maximum - bounds.Minimum;
		if (glm::dot(extent, extent) < 0.000001f)
		{
			bounds.Minimum = center - glm::vec3(0.5f);
			bounds.Maximum = center + glm::vec3(0.5f);
		}
		minimum = bounds.Minimum; maximum = bounds.Maximum;
		return true;
	}

	void EditorViewportHandles::RenderSceneColliderOverlays()
	{
		if (!m_Layer.m_ActiveScene)
			return;

		const Entity selectedEntity = m_Layer.m_SceneHierarchyPanel.GetSelectedEntity();
		if (!selectedEntity || !selectedEntity.HasComponent<ID>()
			|| !m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(selectedEntity)
			|| (!selectedEntity.HasComponent<BoxCollider2D>()
				&& !selectedEntity.HasComponent<CircleCollider2D>()))
		{
			return;
		}
		const UUID selectedUUID = selectedEntity.GetUUID();

		const SceneHierarchyPanel::ColliderEditMode editMode =
			m_Layer.m_SceneState == EditorLayer::SceneState::Edit
			? m_Layer.m_SceneHierarchyPanel.GetColliderEditMode()
			: SceneHierarchyPanel::ColliderEditMode::None;

		const std::vector<ColliderDebugShape> shapes =
			m_Layer.m_ActiveScene->GetColliderDebugShapes(m_Layer.m_SceneState != EditorLayer::SceneState::Edit);
		if (shapes.empty())
			return;

		const float previousLineWidth = Renderer2D::GetLineWidth();
		Renderer2D::SetLineWidth(2.0f);
		RenderCommand::SetDepthTest(false);
		Renderer2D::BeginScene(m_Layer.m_EditorCamera);

		for (const ColliderDebugShape& shape : shapes)
		{
			if (shape.EntityID != selectedUUID)
				continue;
			const bool edited =
				((editMode == SceneHierarchyPanel::ColliderEditMode::Box &&
					shape.Type == ColliderDebugShapeType::Box) ||
				 (editMode == SceneHierarchyPanel::ColliderEditMode::Circle &&
					shape.Type == ColliderDebugShapeType::Circle));

			const glm::vec4 color = !shape.Enabled
				? glm::vec4(0.55f, 0.58f, 0.55f, 0.9f)
				: edited
				? glm::vec4(0.45f, 1.0f, 0.35f, 1.0f)
				: glm::vec4(0.32f, 0.95f, 0.48f, 1.0f);

			if (shape.Type == ColliderDebugShapeType::Box)
				Renderer2D::DrawRect(shape.Transform, color, -1);
			else if (shape.Type == ColliderDebugShapeType::Circle)
				Renderer2D::DrawCircle(shape.Transform, color, 0.045f, 0.005f, -1);
		}

		Renderer2D::EndScene();
		RenderCommand::SetDepthTest(true);
		Renderer2D::SetLineWidth(previousLineWidth);
	}

	void EditorViewportHandles::RenderSceneCameraOverlay()
	{
		if (!m_Layer.m_ActiveScene)
			return;

		const Entity selected = m_Layer.m_SceneHierarchyPanel.GetSelectedEntity();
		std::vector<Entity> cameraEntities;
		Entity selectedCamera;
		for (const entt::entity value
			: m_Layer.m_ActiveScene->m_Registry.view<Transform, C_Camera, ID>())
		{
			Entity entity(value, m_Layer.m_ActiveScene.get());
			if (selected && selected == entity)
				selectedCamera = entity;
			else
				cameraEntities.push_back(entity);
		}
		// Keep the selected outline legible when multiple cameras overlap.
		if (selectedCamera)
			cameraEntities.push_back(selectedCamera);

		struct CameraOverlayGeometry
		{
			int EntityID = -1;
			bool Selected = false;
			bool HasFrustum = false;
			SceneCamera::ProjectionType Projection =
				SceneCamera::ProjectionType::Perspective;
			float OrthographicSize = 1.0f;
			glm::mat4 CameraWorld{ 1.0f };
			std::array<glm::vec3, 8> LocalCorners{};
			std::array<glm::vec3, 8> WorldCorners{};
		};
		std::vector<CameraOverlayGeometry> geometries;
		geometries.reserve(cameraEntities.size());
		for (Entity entity : cameraEntities)
		{
			const bool isSelected = selectedCamera && selectedCamera == entity;
			if (!m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(entity))
				continue;
			const SceneCamera& camera = entity.GetComponent<C_Camera>()._Camera;
			CameraOverlayGeometry geometry;
			geometry.EntityID = static_cast<int>(
				static_cast<entt::entity>(entity));
			geometry.Selected = isSelected;
			geometry.Projection = camera.GetProjectionType();
			geometry.OrthographicSize = camera.GetOrthographicSize();
			geometry.CameraWorld =
				m_Layer.m_ActiveScene->GetRuntimeCameraTransform(entity.GetUUID());

			// The icon is the Camera entity's stable selection target. Keep the
			// entry even when an extreme Far/FOV cannot be represented as finite
			// frustum vertices; only the optional line geometry depends on corners.
			const glm::vec3 iconPosition = glm::vec3(geometry.CameraWorld[3]);
			if (!std::isfinite(iconPosition.x) || !std::isfinite(iconPosition.y)
				|| !std::isfinite(iconPosition.z))
				continue;

			if (camera.TryGetLocalFrustumCorners(geometry.LocalCorners))
			{
				bool valid = true;
				for (size_t index = 0; index < geometry.LocalCorners.size(); ++index)
				{
					const glm::vec4 world = geometry.CameraWorld
						* glm::vec4(geometry.LocalCorners[index], 1.0f);
					if (!std::isfinite(world.x) || !std::isfinite(world.y)
						|| !std::isfinite(world.z) || !std::isfinite(world.w))
					{
						valid = false;
						break;
					}
					geometry.WorldCorners[index] = glm::vec3(world);
				}
				geometry.HasFrustum = valid;
			}
			geometries.push_back(std::move(geometry));
		}
		if (geometries.empty())
			return;

		// The overlay gets an infinite far plane. The normal Scene render keeps
		// its finite projection and depth precision, while authored Camera Far has
		// no editor-only visualization ceiling.
		Camera overlayCamera(m_Layer.m_EditorCamera.GetInfiniteFarViewProjection());
		const float previousLineWidth = Renderer2D::GetLineWidth();
		Renderer2D::SetLineWidth(1.5f);
		RenderCommand::SetDepthTest(false);
		Renderer2D::BeginScene(overlayCamera, glm::mat4(1.0f));
		for (const CameraOverlayGeometry& geometry : geometries)
		{
			if (!geometry.HasFrustum)
				continue;
			const auto& corners = geometry.WorldCorners;

			const glm::vec4 color = geometry.Selected
				? glm::vec4(0.28f, 0.68f, 1.0f, 1.0f)
				: glm::vec4(0.78f, 0.78f, 0.78f, 0.72f);
			auto drawLoop = [&](size_t first)
			{
				for (size_t index = 0; index < 4; ++index)
					Renderer2D::DrawLine(corners[first + index],
						corners[first + ((index + 1) % 4)], color,
						geometry.EntityID);
			};
			drawLoop(0);
			drawLoop(4);
			for (size_t index = 0; index < 4; ++index)
				Renderer2D::DrawLine(corners[index], corners[index + 4],
					color, geometry.EntityID);
			if (geometry.Selected && geometry.Projection
				== SceneCamera::ProjectionType::Orthographic)
			{
				const float markerSize = geometry.OrthographicSize * 0.035f;
				for (size_t index = 0; index < 4; ++index)
				{
					const glm::vec3 center = (geometry.LocalCorners[index]
						+ geometry.LocalCorners[(index + 1) % 4]) * 0.5f;
					const glm::mat4 markerTransform = geometry.CameraWorld
						* glm::translate(glm::mat4(1.0f), center)
						* glm::scale(glm::mat4(1.0f), glm::vec3(
							markerSize, markerSize, 1.0f));
					Renderer2D::DrawQuad(markerTransform, color,
						geometry.EntityID);
				}
			}
		}
		Renderer2D::EndScene();

		// Renderer2D flushes lines after quads. Draw icons in a second pass so the
		// frustum cannot slice through the camera silhouette.
		const Ref<Texture2D> cameraIcon = m_Layer.m_EditorIcons
			? m_Layer.m_EditorIcons->Get(EditorIcon::Camera) : Ref<Texture2D>{};
		if (cameraIcon)
		{
			Renderer2D::BeginScene(overlayCamera, glm::mat4(1.0f));
			for (const CameraOverlayGeometry& geometry : geometries)
			{
				// Give the billboard a world-space size so dollying toward a camera
				// visibly enlarges it. A constant pixel size hid all zoom feedback in
				// the initial empty scene, whose distant frustum is viewed end-on.
				// Retain a minimum pixel size for picking distant cameras.
				const glm::vec3 iconPosition = glm::vec3(geometry.CameraWorld[3]);
				const float viewDepth = glm::dot(iconPosition
					- m_Layer.m_EditorCamera.GetPosition(),
					m_Layer.m_EditorCamera.GetForwardDirection());
				if (!std::isfinite(viewDepth) || viewDepth <= 0.0001f)
					continue;
				const float projectionY = std::abs(
					m_Layer.m_EditorCamera.GetProjection()[1][1]);
				const float viewportHeight = std::max(1.0f,
					m_Layer.m_ViewportBounds[1].y - m_Layer.m_ViewportBounds[0].y);
				const float depthScale = m_Layer.m_EditorCamera.IsOrthographic()
					? 1.0f : viewDepth;
				const float minimumPickSize = 24.0f * 2.0f * depthScale
					/ (projectionY * viewportHeight);
				const float iconWorldSize = std::max(0.5f, minimumPickSize);
				if (!std::isfinite(iconWorldSize) || iconWorldSize <= 0.0f)
					continue;

				glm::mat4 iconTransform(1.0f);
				iconTransform[0] = glm::vec4(
					m_Layer.m_EditorCamera.GetRightDirection() * iconWorldSize, 0.0f);
				iconTransform[1] = glm::vec4(
					m_Layer.m_EditorCamera.GetUpDirection() * iconWorldSize, 0.0f);
				iconTransform[2] = glm::vec4(
					m_Layer.m_EditorCamera.GetForwardDirection(), 0.0f);
				iconTransform[3] = glm::vec4(iconPosition, 1.0f);
				Renderer2D::DrawQuad(iconTransform, cameraIcon, 1.0f,
					geometry.Selected
						? glm::vec4(0.56f, 0.82f, 1.0f, 1.0f)
						: glm::vec4(1.0f),
					geometry.EntityID, false);
			}
			Renderer2D::EndScene();
		}
		RenderCommand::SetDepthTest(true);
		Renderer2D::SetLineWidth(previousLineWidth);
	}

	void EditorViewportHandles::RenderSceneCanvasOverlay()
	{
		if (!m_Layer.m_ActiveScene)
			return;
		const RuntimeUILayoutSnapshot layout = RuntimeUISystem::BuildEditorLayout(
			*m_Layer.m_ActiveScene, RuntimeUIVisibilityMode::Editor);
		if (layout.RenderOrder.empty())
			return;

		Entity selectedCanvas;
		for (Entity current = m_Layer.m_SceneHierarchyPanel.GetSelectedEntity(); current;
			current = m_Layer.m_ActiveScene->GetParent(current))
		{
			if (current.HasComponent<Canvas>())
			{
				selectedCanvas = current;
				break;
			}
		}

		const float previousLineWidth = Renderer2D::GetLineWidth();
		Renderer2D::SetLineWidth(1.5f);
		RenderCommand::SetDepthTest(false);
		Renderer2D::BeginScene(m_Layer.m_EditorCamera);
		std::vector<Entity> canvasEntities;
		for (const entt::entity value : m_Layer.m_ActiveScene->m_Registry.view<Canvas, ID>())
		{
			Entity canvas(value, m_Layer.m_ActiveScene.get());
			if (!selectedCanvas || selectedCanvas != canvas)
				canvasEntities.push_back(canvas);
		}
		// Multiple screen-space Canvases share an authoring origin by default, so
		// render the selected owner last to preserve its blue outline.
		if (selectedCanvas)
			canvasEntities.push_back(selectedCanvas);
		for (Entity canvas : canvasEntities)
		{
			const int entityID = static_cast<int>(
				static_cast<entt::entity>(canvas));
			const bool isSelected = selectedCanvas && selectedCanvas == canvas;
			if (!canvas.GetComponent<Canvas>().Enabled
				|| !m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(canvas))
				continue;
			const auto rectangleIt = layout.Rectangles.find(canvas.GetUUID());
			const auto transformIt = layout.Transforms.find(canvas.GetUUID());
			if (rectangleIt == layout.Rectangles.end()
				|| transformIt == layout.Transforms.end())
				continue;

			const UIRect& rectangle = rectangleIt->second;
			const glm::vec2 localCorners[4] = {
				{ rectangle.X, rectangle.Y },
				{ rectangle.X + rectangle.Width, rectangle.Y },
				{ rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height },
				{ rectangle.X, rectangle.Y + rectangle.Height }
			};
			glm::vec3 worldCorners[4]{};
			bool valid = true;
			for (size_t index = 0; index < std::size(localCorners); ++index)
			{
				const glm::vec4 world = transformIt->second
					* glm::vec4(localCorners[index], 0.0f, 1.0f);
				valid &= std::isfinite(world.x) && std::isfinite(world.y)
					&& std::isfinite(world.z) && std::isfinite(world.w);
				worldCorners[index] = glm::vec3(world);
			}
			if (!valid)
				continue;

			const glm::vec4 color = isSelected
				? glm::vec4(0.24f, 0.64f, 1.0f, 1.0f)
				: glm::vec4(0.78f, 0.78f, 0.78f, 0.5f);
			for (size_t index = 0; index < std::size(worldCorners); ++index)
				Renderer2D::DrawLine(worldCorners[index],
					worldCorners[(index + 1) % std::size(worldCorners)], color,
					entityID);

			if (isSelected)
			{
				const float width = glm::length(worldCorners[1] - worldCorners[0]);
				const float height = glm::length(worldCorners[3] - worldCorners[0]);
				const float markerSize = std::clamp(
					std::min(width, height) * 0.018f, 0.08f, 0.24f);
				for (const glm::vec3& corner : worldCorners)
				{
					const glm::mat4 markerTransform = glm::translate(
						glm::mat4(1.0f), corner)
						* glm::scale(glm::mat4(1.0f), glm::vec3(
							markerSize, markerSize, 1.0f));
					Renderer2D::DrawCircle(markerTransform, color, 1.0f,
						0.01f, entityID);
				}
			}
		}
		Renderer2D::EndScene();
		RenderCommand::SetDepthTest(true);
		Renderer2D::SetLineWidth(previousLineWidth);
	}

	bool EditorViewportHandles::WorldToScreen(const glm::vec3& worldPosition, glm::vec2& screenPosition) const
	{
		const glm::vec2 viewportSize = m_Layer.m_ViewportBounds[1] - m_Layer.m_ViewportBounds[0];
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		const glm::vec4 clip = m_Layer.m_EditorCamera.GetViewProjection() * glm::vec4(worldPosition, 1.0f);
		if (!std::isfinite(clip.w) || clip.w <= 0.000001f)
			return false;
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y))
			return false;

		screenPosition.x = m_Layer.m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
		screenPosition.y = m_Layer.m_ViewportBounds[0].y + (0.5f - ndc.y * 0.5f) * viewportSize.y;
		return std::isfinite(screenPosition.x) && std::isfinite(screenPosition.y);
	}

	bool EditorViewportHandles::ScreenToWorldOnPlane(const glm::vec2& screenPosition, float worldZ,
		glm::vec2& worldPosition) const
	{
		const glm::vec2 viewportSize = m_Layer.m_ViewportBounds[1] - m_Layer.m_ViewportBounds[0];
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
			return false;

		const float ndcX = ((screenPosition.x - m_Layer.m_ViewportBounds[0].x) / viewportSize.x) * 2.0f - 1.0f;
		const float ndcY = 1.0f - ((screenPosition.y - m_Layer.m_ViewportBounds[0].y) / viewportSize.y) * 2.0f;
		const glm::mat4 inverseViewProjection = glm::inverse(m_Layer.m_EditorCamera.GetViewProjection());
		glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		if (std::abs(nearPoint.w) <= 0.000001f || std::abs(farPoint.w) <= 0.000001f)
			return false;
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		const glm::vec3 ray = glm::vec3(farPoint - nearPoint);
		if (!std::isfinite(ray.z) || std::abs(ray.z) <= 0.000001f)
			return false;
		const float distance = (worldZ - nearPoint.z) / ray.z;
		const glm::vec3 intersection = glm::vec3(nearPoint) + ray * distance;
		if (!std::isfinite(intersection.x) || !std::isfinite(intersection.y))
			return false;

		worldPosition = { intersection.x, intersection.y };
		return true;
	}

	void EditorViewportHandles::ResetRectTransformEditState()
	{
		if (m_ViewportState.UIRectTransactionActive)
		{
			m_ViewportState.UIRectTransactionActive = false;
			m_Layer.CommitSceneTransaction();
		}
		m_ViewportState.UIRectDragActive = false;
		m_ViewportState.UIRectHandleHovered = false;
		m_ViewportState.UIRectEditEntity = UUID(0);
	}

	bool EditorViewportHandles::UI_RectTransformHandles()
	{
		Entity selected = m_Layer.m_SceneHierarchyPanel.GetSelectedEntity();
		if (!m_Layer.m_ActiveScene || !selected
			|| !m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(selected))
		{
			ResetRectTransformEditState();
			return false;
		}

		// A screen-space Canvas owns the reference-resolution plane drawn by
		// RenderSceneCanvasOverlay. Its ordinary Transform does not move runtime UI.
		if (selected.HasComponent<Canvas>())
		{
			ResetRectTransformEditState();
			return true;
		}
		if (!selected.HasComponent<RectTransform>())
		{
			ResetRectTransformEditState();
			return false;
		}

		const glm::vec2 viewportDisplaySize = m_Layer.m_ViewportBounds[1] - m_Layer.m_ViewportBounds[0];
		if (viewportDisplaySize.x <= 0.0f || viewportDisplaySize.y <= 0.0f)
		{
			ResetRectTransformEditState();
			return true;
		}

		const RuntimeUILayoutSnapshot layout = RuntimeUISystem::BuildEditorLayout(
			*m_Layer.m_ActiveScene, RuntimeUIVisibilityMode::Editor);
		const UUID selectedID = selected.GetUUID();
		const auto rectangleIt = layout.Rectangles.find(selectedID);
		const auto scaleIt = layout.Scales.find(selectedID);
		const auto uiTransformIt = layout.Transforms.find(selectedID);
		if (rectangleIt == layout.Rectangles.end() || scaleIt == layout.Scales.end()
			|| uiTransformIt == layout.Transforms.end()
			|| !std::isfinite(scaleIt->second) || scaleIt->second <= 0.0f)
		{
			ResetRectTransformEditState();
			return true;
		}

		const UIRect& rectangle = rectangleIt->second;
		auto toSceneScreen = [&](const glm::vec2& point, ImVec2& output)
		{
			const glm::vec4 transformed = uiTransformIt->second
				* glm::vec4(point, 0.0f, 1.0f);
			glm::vec2 screen;
			if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y)
				|| !std::isfinite(transformed.z)
				|| !WorldToScreen(glm::vec3(transformed), screen))
				return false;
			output = ImVec2(screen.x, screen.y);
			return true;
		};
		const glm::vec2 localCorners[4] = {
			{ rectangle.X, rectangle.Y },
			{ rectangle.X + rectangle.Width, rectangle.Y },
			{ rectangle.X + rectangle.Width, rectangle.Y + rectangle.Height },
			{ rectangle.X, rectangle.Y + rectangle.Height }
		};
		ImVec2 rectCorners[4]{};
		bool visible = true;
		for (size_t index = 0; index < std::size(localCorners); ++index)
			visible &= toSceneScreen(localCorners[index], rectCorners[index]);
		const glm::vec2 pivotPosition{
			rectangle.X + rectangle.Width * selected.GetComponent<RectTransform>().Pivot.x,
			rectangle.Y + rectangle.Height * selected.GetComponent<RectTransform>().Pivot.y };
		glm::vec2 centerPosition{ rectangle.X + rectangle.Width * 0.5f,
			rectangle.Y + rectangle.Height * 0.5f };
		if (!m_Layer.m_ActiveScene->GetChildrenUUIDs(selected).empty()) {
			if (m_Layer.m_GizmoPivotMode == EditorLayer::GizmoPivotMode::Pivot)
				centerPosition = pivotPosition;
			else {
				glm::vec3 minimum, maximum;
				if (GetEntityBounds(selected, minimum, maximum)
					&& std::abs(glm::determinant(uiTransformIt->second)) > 0.000001f)
					centerPosition = glm::vec2(glm::inverse(uiTransformIt->second)
						* glm::vec4((minimum + maximum) * 0.5f, 1.0f));
			}
		}
		ImVec2 pivotScreen{};
		visible &= toSceneScreen(centerPosition, pivotScreen);
		if (!visible)
		{
			ResetRectTransformEditState();
			return true;
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImGui::PushClipRect(ImVec2(m_Layer.m_ViewportBounds[0].x, m_Layer.m_ViewportBounds[0].y),
			ImVec2(m_Layer.m_ViewportBounds[1].x, m_Layer.m_ViewportBounds[1].y), true);
		const ImU32 outline = IM_COL32(72, 166, 255, 255);
		draw->AddPolyline(rectCorners, 4, outline, ImDrawFlags_Closed, 1.5f);
		draw->AddCircleFilled(pivotScreen, 4.0f, outline);
		draw->AddLine(ImVec2(pivotScreen.x - 9.0f, pivotScreen.y),
			ImVec2(pivotScreen.x + 9.0f, pivotScreen.y), outline, 1.5f);
		draw->AddLine(ImVec2(pivotScreen.x, pivotScreen.y - 9.0f),
			ImVec2(pivotScreen.x, pivotScreen.y + 9.0f), outline, 1.5f);
		ImGui::PopClipRect();

		Entity parent = m_Layer.m_ActiveScene->GetParent(selected);
		const bool layoutControlled = parent && parent.HasComponent<UILayoutGroup>()
			&& parent.GetComponent<UILayoutGroup>().Enabled;
		glm::mat4 parentCanvasTransform(1.0f);
		if (parent && parent.HasComponent<ID>())
		{
			const auto parentTransformIt = layout.Transforms.find(parent.GetUUID());
			if (parentTransformIt != layout.Transforms.end())
				parentCanvasTransform = parentTransformIt->second;
		}
		const float parentDeterminant = glm::determinant(parentCanvasTransform);
		const bool parentTransformInvertible = std::isfinite(parentDeterminant)
			&& std::abs(parentDeterminant) > 0.000001f;
		const glm::mat4 inverseParentCanvasTransform = parentTransformInvertible
			? glm::inverse(parentCanvasTransform) : glm::mat4(1.0f);
		const bool translateTool = m_Layer.m_GizmoType == ImGuizmo::OPERATION::TRANSLATE;
		const bool rotateTool = m_Layer.m_GizmoType == ImGuizmo::OPERATION::ROTATE;
		const bool scaleTool = m_Layer.m_GizmoType == ImGuizmo::OPERATION::SCALE;
		const bool supportedTool = translateTool || rotateTool || scaleTool;
		const bool transformToolAvailable = translateTool
			|| selected.HasComponent<Transform>();
		// A layout group authors its children's positions. Rotation and scale remain
		// independent, matching Unity's driven RectTransform behaviour.
		const bool canManipulate = m_Layer.m_SceneState == EditorLayer::SceneState::Edit
			&& supportedTool && transformToolAvailable
			&& parentTransformInvertible && (!translateTool || !layoutControlled)
			&& (!m_Layer.IsSceneOrientationGizmoPointerInside() || m_ViewportState.UIRectDragActive);

		if (m_ViewportState.UIRectTransactionActive && m_ViewportState.UIRectEditEntity != selectedID)
			ResetRectTransformEditState();

		const ImVec2 mouse = ImGui::GetMousePos();
		const bool rectangleHovered = m_Layer.m_ViewportCanvasHovered
			&& (ImTriangleContainsPoint(rectCorners[0], rectCorners[1],
				rectCorners[2], mouse)
				|| ImTriangleContainsPoint(rectCorners[0], rectCorners[2],
					rectCorners[3], mouse));
		bool gizmoHovered = false;
		bool gizmoUsing = false;
		bool manipulated = false;
		glm::mat4 gizmoTransform(1.0f);
		glm::vec2 gizmoPosition = pivotPosition;
		if (canManipulate)
		{
			glm::vec3 gizmoRotation(0.0f);
			glm::vec3 gizmoScale(1.0f);
			if (selected.HasComponent<Transform>())
			{
				const auto& authoredTransform = selected.GetComponent<Transform>();
				gizmoRotation.z = authoredTransform._LocalRotation.z;
				gizmoScale.x = std::abs(authoredTransform._LocalScale.x) > 0.0001f
					? authoredTransform._LocalScale.x : 0.0001f;
				gizmoScale.y = std::abs(authoredTransform._LocalScale.y) > 0.0001f
					? authoredTransform._LocalScale.y : 0.0001f;
			}
			// Runtime UI composes each RectTransform in canvas space. Include the
			// accumulated parent frame so nested rotated/scaled controls receive a
			// gizmo at the visible rectangle center and with the same visible axes.
			// Single UI handles use the rectangle's geometric center. Anchors and
			// the authored layout pivot still determine placement in the canvas.
			gizmoPosition = glm::vec2(inverseParentCanvasTransform * uiTransformIt->second
				* glm::vec4(centerPosition, 0.0f, 1.0f));
			gizmoTransform = parentCanvasTransform * Math::ComposeTransform(
				{ gizmoPosition.x, gizmoPosition.y, 0.0f },
				gizmoRotation, gizmoScale);

			// Use the real Scene camera for both Canvas content and its gizmo. In 2D
			// mode the depth axis is edge-on, leaving the planar X/Y controls visible
			// without changing ImGuizmo itself.
			glm::mat4 gizmoView(1.0f);
			glm::mat4 gizmoProjection(1.0f);
			m_Layer.m_EditorCamera.GetRightHandedToolMatrices(gizmoView,
				gizmoProjection);
			ImGuizmo::AllowAxisFlip(false);
			ImGuizmo::SetOrthographic(m_Layer.m_EditorCamera.IsOrthographic());
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(m_Layer.m_ViewportBounds[0].x, m_Layer.m_ViewportBounds[0].y,
				viewportDisplaySize.x, viewportDisplaySize.y);
			ImGuizmo::SetID(static_cast<int>(static_cast<uint64_t>(selectedID)
				& 0x7fffffffULL));

			float snapValues[3] = { 0.0f, 0.0f, 0.0f };
			float* snap = nullptr;
			if (ImGui::GetIO().KeyCtrl)
			{
				if (translateTool)
				{
					const float canvasPixel = scaleIt->second
						/ RuntimeUISystem::EditorCanvasPixelsPerUnit;
					snapValues[0] = canvasPixel;
					snapValues[1] = canvasPixel;
					snapValues[2] = canvasPixel;
				}
				else if (rotateTool)
					snapValues[0] = snapValues[1] = snapValues[2] = 15.0f;
				else
					snapValues[0] = snapValues[1] = snapValues[2] = 0.1f;
				snap = snapValues;
			}

			manipulated = ImGuizmo::Manipulate(glm::value_ptr(gizmoView),
				glm::value_ptr(gizmoProjection),
				static_cast<ImGuizmo::OPERATION>(m_Layer.m_GizmoType),
				m_Layer.m_GizmoSpaceMode == EditorLayer::GizmoSpaceMode::Local
					? ImGuizmo::LOCAL : ImGuizmo::WORLD,
				glm::value_ptr(gizmoTransform), nullptr, snap);
			gizmoUsing = ImGuizmo::IsUsing();
			gizmoHovered = ImGuizmo::IsOver(
				static_cast<ImGuizmo::OPERATION>(m_Layer.m_GizmoType));
		}

		// The native mouse event is dispatched before this ImGui pass. Preserve the
		// result for the next event so transparent text/image pixels cannot select
		// world geometry underneath the RectTransform or its ImGuizmo handles.
		m_ViewportState.UIRectHandleHovered = rectangleHovered
			|| (canManipulate && (gizmoHovered || gizmoUsing));
		if (rectangleHovered && translateTool && layoutControlled)
			ImGui::SetTooltip("Position is controlled by the parent UI Layout Group");

		if (gizmoUsing && !m_ViewportState.UIRectTransactionActive)
		{
			const char* label = translateTool ? "Move UI Element"
				: (rotateTool ? "Rotate UI Element" : "Scale UI Element");
			m_Layer.BeginSceneTransaction(label);
			m_ViewportState.UIRectTransactionActive = m_Layer.m_SceneHistory.HasActiveTransaction();
			m_ViewportState.UIRectEditEntity = selectedID;
		}
		m_ViewportState.UIRectDragActive = gizmoUsing;

		if (manipulated && m_ViewportState.UIRectTransactionActive
			&& m_ViewportState.UIRectEditEntity == selectedID)
		{
			glm::vec3 translation{}, rotation{}, scale{};
			const glm::mat4 localGizmoTransform = inverseParentCanvasTransform
				* gizmoTransform;
			if (Math::DecomposeTransform(localGizmoTransform,
				translation, rotation, scale))
			{
				bool changed = false;
				if (translateTool)
				{
					glm::vec2 position = selected.GetComponent<RectTransform>()
						.AnchoredPosition
						+ (glm::vec2(translation) - gizmoPosition) / scaleIt->second;
					if (ImGui::GetIO().KeyCtrl)
						position = glm::round(position);
					auto& rectTransform = selected.GetComponent<RectTransform>();
					if (rectTransform.AnchoredPosition != position)
					{
						rectTransform.AnchoredPosition = position;
						changed = true;
					}
				}
				else
				{
					auto& authoredTransform = selected.GetComponent<Transform>();
					glm::vec3 localRotation = authoredTransform._LocalRotation;
					glm::vec3 localScale = authoredTransform._LocalScale;
					if (rotateTool)
						localRotation.z = rotation.z;
					else
					{
						localScale.x = scale.x;
						localScale.y = scale.y;
					}
					const glm::mat4 localTransform = Math::ComposeTransform(
						authoredTransform._LocalTranslation, localRotation, localScale);
					changed = m_Layer.m_ActiveScene->SetLocalTransform(selected, localTransform);
					if (changed) {
						// Keep the visible center stationary when rotating/scaling an
						// off-center pivot: center = pivot + rotationScale * offset.
						const glm::vec2 offset = centerPosition - pivotPosition;
						const glm::vec2 rotatedOffset = glm::vec2(Math::ComposeTransform(
							glm::vec3(0.0f), localRotation, localScale) * glm::vec4(offset, 0.0f, 0.0f));
						selected.GetComponent<RectTransform>().AnchoredPosition +=
							(glm::vec2(translation) - rotatedOffset - pivotPosition) / scaleIt->second;
					}
				}
				if (changed)
					m_Layer.UpdateSceneTransaction();
			}
		}

		if (m_ViewportState.UIRectTransactionActive && !gizmoUsing)
			ResetRectTransformEditState();
		return true;
	}

	void EditorViewportHandles::ResetColliderEditState()
	{
		if (m_ViewportState.ColliderTransactionActive)
		{
			m_ViewportState.ColliderTransactionActive = false;
			m_Layer.CommitSceneTransaction();
		}
		m_ViewportState.ActiveColliderHandle = ColliderEditHandle::None;
		m_ViewportState.ColliderEditEntity = UUID(0);
		m_ViewportState.ColliderDragStartMouseWorld = { 0.0f, 0.0f };
		m_ViewportState.ColliderDragStartCenter = { 0.0f, 0.0f };
		m_ViewportState.ColliderDragStartHalfSize = { 0.0f, 0.0f };
		m_ViewportState.ColliderDragStartRadius = 0.0f;
		m_ViewportState.ColliderDragPlaneZ = 0.0f;
		m_ViewportState.ColliderHandleHovered = false;
	}

	void EditorViewportHandles::UI_ColliderEditHandles()
	{
		m_ViewportState.ColliderHandleHovered = false;
		const SceneHierarchyPanel::ColliderEditMode editMode = m_Layer.m_SceneHierarchyPanel.GetColliderEditMode();
		Entity selectedEntity = m_Layer.m_SceneHierarchyPanel.GetSelectedEntity();
		if (m_Layer.m_SceneState != EditorLayer::SceneState::Edit ||
			editMode == SceneHierarchyPanel::ColliderEditMode::None ||
			!m_Layer.m_ActiveScene ||
			!selectedEntity || !selectedEntity.HasComponent<Transform>() ||
			!selectedEntity.HasComponent<ID>() ||
			!m_Layer.m_ActiveScene->IsVisibleInEditorHierarchy(selectedEntity))
		{
			ResetColliderEditState();
			return;
		}

		const UUID selectedUUID = selectedEntity.GetUUID();
		if (m_ViewportState.ActiveColliderHandle != ColliderEditHandle::None &&
			m_ViewportState.ColliderEditEntity != selectedUUID)
			ResetColliderEditState();

		const ColliderDebugShapeType expectedType =
			editMode == SceneHierarchyPanel::ColliderEditMode::Box
			? ColliderDebugShapeType::Box : ColliderDebugShapeType::Circle;
		const std::vector<ColliderDebugShape> shapes = m_Layer.m_ActiveScene->GetColliderDebugShapes(false);
		const auto shapeIt = std::find_if(shapes.begin(), shapes.end(),
			[&](const ColliderDebugShape& shape)
			{
				return shape.EntityID == selectedUUID && shape.Type == expectedType;
			});
		if (shapeIt == shapes.end())
		{
			ResetColliderEditState();
			return;
		}

		const ColliderDebugShape& shape = *shapeIt;
		const Transform& transform = selectedEntity.GetComponent<Transform>();
		const float scaleX = std::abs(transform._Scale.x);
		const float scaleY = std::abs(transform._Scale.y);
		const float maximumScale = std::max(scaleX, scaleY);
		if (scaleX <= 0.000001f || scaleY <= 0.000001f ||
			(editMode == SceneHierarchyPanel::ColliderEditMode::Circle && maximumScale <= 0.000001f))
		{
			ResetColliderEditState();
			return;
		}

		const float cosine = std::cos(shape.Rotation);
		const float sine = std::sin(shape.Rotation);
		const glm::vec2 right(cosine, sine);
		const glm::vec2 up(-sine, cosine);
		const glm::vec2 center = shape.Center;
		const float handleRadius = 6.0f;
		const bool orientationBlocksActivation =
			m_Layer.IsSceneOrientationGizmoPointerInside()
			&& m_ViewportState.ActiveColliderHandle == ColliderEditHandle::None;
		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImGui::PushClipRect(ImVec2(m_Layer.m_ViewportBounds[0].x, m_Layer.m_ViewportBounds[0].y),
			ImVec2(m_Layer.m_ViewportBounds[1].x, m_Layer.m_ViewportBounds[1].y), true);
		ImGui::PushID("ColliderEditHandles");
		ImGui::PushID(static_cast<int>(selectedEntity));

		auto submitHandle = [&](ColliderEditHandle handle, const char* id,
			const glm::vec2& worldPosition, ImGuiMouseCursor cursor, bool offsetHandle)
		{
			glm::vec2 screenPosition;
			if (!WorldToScreen(glm::vec3(worldPosition, transform._Translation.z), screenPosition))
				return;
			if (screenPosition.x < m_Layer.m_ViewportBounds[0].x - handleRadius ||
				screenPosition.x > m_Layer.m_ViewportBounds[1].x + handleRadius ||
				screenPosition.y < m_Layer.m_ViewportBounds[0].y - handleRadius ||
				screenPosition.y > m_Layer.m_ViewportBounds[1].y + handleRadius)
				return;

			const ImVec2 minimum(screenPosition.x - handleRadius, screenPosition.y - handleRadius);
			const ImVec2 maximum(screenPosition.x + handleRadius, screenPosition.y + handleRadius);
			ImGui::PushID(id);
			bool hovered = false;
			bool active = false;
			if (!orientationBlocksActivation)
			{
				ImGui::SetCursorScreenPos(minimum);
				ImGui::InvisibleButton("##handle",
					ImVec2(handleRadius * 2.0f, handleRadius * 2.0f));
				hovered = ImGui::IsItemHovered();
				active = m_ViewportState.ActiveColliderHandle == handle && ImGui::IsItemActive();
			}
			m_ViewportState.ColliderHandleHovered = m_ViewportState.ColliderHandleHovered || hovered || active;
			if (hovered || active)
				ImGui::SetMouseCursor(cursor);

			if (!orientationBlocksActivation && ImGui::IsItemActivated())
			{
				glm::vec2 mouseWorld;
				const ImVec2 mouse = ImGui::GetMousePos();
				if (ScreenToWorldOnPlane({ mouse.x, mouse.y }, transform._Translation.z, mouseWorld))
				{
					m_Layer.BeginSceneTransaction("Collider Drag");
					m_ViewportState.ColliderTransactionActive =
						m_Layer.m_SceneHistory.HasActiveTransaction();
					m_ViewportState.ActiveColliderHandle = handle;
					m_ViewportState.ColliderEditEntity = selectedUUID;
					m_ViewportState.ColliderDragStartMouseWorld = mouseWorld;
					m_ViewportState.ColliderDragStartCenter = shape.Center;
					m_ViewportState.ColliderDragStartHalfSize = shape.HalfSize;
					m_ViewportState.ColliderDragStartRadius = shape.Radius;
					m_ViewportState.ColliderDragPlaneZ = transform._Translation.z;
				}
			}

			const ImU32 handleColor = active
				? IM_COL32(255, 176, 65, 255)
				: hovered ? IM_COL32(220, 255, 185, 255) : IM_COL32(115, 235, 110, 255);
			if (offsetHandle)
			{
				const ImVec2 top(screenPosition.x, screenPosition.y - handleRadius);
				const ImVec2 rightPoint(screenPosition.x + handleRadius, screenPosition.y);
				const ImVec2 bottom(screenPosition.x, screenPosition.y + handleRadius);
				const ImVec2 leftPoint(screenPosition.x - handleRadius, screenPosition.y);
				draw->AddQuadFilled(top, rightPoint, bottom, leftPoint, IM_COL32(25, 25, 25, 255));
				draw->AddQuadFilled(ImVec2(top.x, top.y + 1.5f),
					ImVec2(rightPoint.x - 1.5f, rightPoint.y),
					ImVec2(bottom.x, bottom.y - 1.5f),
					ImVec2(leftPoint.x + 1.5f, leftPoint.y), handleColor);
			}
			else
			{
				draw->AddRectFilled(minimum, maximum, IM_COL32(25, 25, 25, 255), 1.0f);
				draw->AddRectFilled(ImVec2(minimum.x + 1.5f, minimum.y + 1.5f),
					ImVec2(maximum.x - 1.5f, maximum.y - 1.5f), handleColor, 1.0f);
			}
			ImGui::PopID();
		};

		if (editMode == SceneHierarchyPanel::ColliderEditMode::Box)
		{
			const glm::vec2 halfSize = shape.HalfSize;
			submitHandle(ColliderEditHandle::BoxLeft, "Left", center - right * halfSize.x,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::BoxRight, "Right", center + right * halfSize.x,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::BoxBottom, "Bottom", center - up * halfSize.y,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::BoxTop, "Top", center + up * halfSize.y,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::BoxBottomLeft, "BottomLeft",
				center - right * halfSize.x - up * halfSize.y, ImGuiMouseCursor_ResizeNESW, false);
			submitHandle(ColliderEditHandle::BoxBottomRight, "BottomRight",
				center + right * halfSize.x - up * halfSize.y, ImGuiMouseCursor_ResizeNWSE, false);
			submitHandle(ColliderEditHandle::BoxTopLeft, "TopLeft",
				center - right * halfSize.x + up * halfSize.y, ImGuiMouseCursor_ResizeNWSE, false);
			submitHandle(ColliderEditHandle::BoxTopRight, "TopRight",
				center + right * halfSize.x + up * halfSize.y, ImGuiMouseCursor_ResizeNESW, false);
		}
		else
		{
			submitHandle(ColliderEditHandle::CircleLeft, "CircleLeft", center - right * shape.Radius,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::CircleRight, "CircleRight", center + right * shape.Radius,
				ImGuiMouseCursor_ResizeEW, false);
			submitHandle(ColliderEditHandle::CircleBottom, "CircleBottom", center - up * shape.Radius,
				ImGuiMouseCursor_ResizeNS, false);
			submitHandle(ColliderEditHandle::CircleTop, "CircleTop", center + up * shape.Radius,
				ImGuiMouseCursor_ResizeNS, false);
		}

		// Submit the offset handle last so it remains reachable for very small
		// colliders whose resize handles overlap the center.
		submitHandle(ColliderEditHandle::Offset, "Offset", center, ImGuiMouseCursor_ResizeAll, true);

		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopClipRect();
		ImGui::SetCursorScreenPos(savedCursor);

		if (m_ViewportState.ActiveColliderHandle == ColliderEditHandle::None)
			return;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (m_ViewportState.ColliderTransactionActive)
			{
				m_ViewportState.ColliderTransactionActive = false;
				m_Layer.CommitSceneTransaction();
			}
			m_ViewportState.ActiveColliderHandle = ColliderEditHandle::None;
			return;
		}

		const ImVec2 mouse = ImGui::GetMousePos();
		glm::vec2 mouseWorld;
		if (!ScreenToWorldOnPlane({ mouse.x, mouse.y }, m_ViewportState.ColliderDragPlaneZ, mouseWorld))
			return;
		const glm::vec2 mouseDelta = mouseWorld - m_ViewportState.ColliderDragStartMouseWorld;

		auto centerToOffset = [&](const glm::vec2& worldCenter, glm::vec2& offset)
		{
			const glm::vec2 relative = worldCenter - glm::vec2(transform._Translation);
			const float transformCosine = std::cos(transform._Rotation.z);
			const float transformSine = std::sin(transform._Rotation.z);
			const glm::vec2 scaledLocal(
				transformCosine * relative.x + transformSine * relative.y,
				-transformSine * relative.x + transformCosine * relative.y);
			if (std::abs(transform._Scale.x) <= 0.000001f ||
				std::abs(transform._Scale.y) <= 0.000001f)
				return false;
			offset = { scaledLocal.x / transform._Scale.x, scaledLocal.y / transform._Scale.y };
			return std::isfinite(offset.x) && std::isfinite(offset.y);
		};

		constexpr float minimumComponentExtent = 0.001f;
		if (editMode == SceneHierarchyPanel::ColliderEditMode::Box &&
			selectedEntity.HasComponent<BoxCollider2D>())
		{
			glm::vec2 newCenter = m_ViewportState.ColliderDragStartCenter;
			glm::vec2 newHalfSize = m_ViewportState.ColliderDragStartHalfSize;
			bool resizeX = false;
			bool resizeY = false;
			float signX = 0.0f;
			float signY = 0.0f;

			switch (m_ViewportState.ActiveColliderHandle)
			{
			case ColliderEditHandle::BoxLeft: resizeX = true; signX = -1.0f; break;
			case ColliderEditHandle::BoxRight: resizeX = true; signX = 1.0f; break;
			case ColliderEditHandle::BoxBottom: resizeY = true; signY = -1.0f; break;
			case ColliderEditHandle::BoxTop: resizeY = true; signY = 1.0f; break;
			case ColliderEditHandle::BoxBottomLeft:
				resizeX = resizeY = true; signX = signY = -1.0f; break;
			case ColliderEditHandle::BoxBottomRight:
				resizeX = resizeY = true; signX = 1.0f; signY = -1.0f; break;
			case ColliderEditHandle::BoxTopLeft:
				resizeX = resizeY = true; signX = -1.0f; signY = 1.0f; break;
			case ColliderEditHandle::BoxTopRight:
				resizeX = resizeY = true; signX = signY = 1.0f; break;
			default: break;
			}

			if (m_ViewportState.ActiveColliderHandle == ColliderEditHandle::Offset)
				newCenter += mouseDelta;
			if (resizeX)
			{
				const glm::vec2 outward = right * signX;
				const float requestedHalfSize = m_ViewportState.ColliderDragStartHalfSize.x +
					glm::dot(mouseDelta, outward) * 0.5f;
				newHalfSize.x = std::max(scaleX * minimumComponentExtent, requestedHalfSize);
				newCenter += outward * (newHalfSize.x - m_ViewportState.ColliderDragStartHalfSize.x);
			}
			if (resizeY)
			{
				const glm::vec2 outward = up * signY;
				const float requestedHalfSize = m_ViewportState.ColliderDragStartHalfSize.y +
					glm::dot(mouseDelta, outward) * 0.5f;
				newHalfSize.y = std::max(scaleY * minimumComponentExtent, requestedHalfSize);
				newCenter += outward * (newHalfSize.y - m_ViewportState.ColliderDragStartHalfSize.y);
			}

			auto& collider = selectedEntity.GetComponent<BoxCollider2D>();
			glm::vec2 newOffset;
			if (centerToOffset(newCenter, newOffset))
			{
				const glm::vec2 newSize(newHalfSize.x / scaleX, newHalfSize.y / scaleY);
				if (glm::length(collider.Offset - newOffset) > 0.000001f ||
					glm::length(collider.Size - newSize) > 0.000001f)
				{
					collider.Offset = newOffset;
					collider.Size = newSize;
					m_Layer.UpdateSceneTransaction();
				}
			}
		}
		else if (editMode == SceneHierarchyPanel::ColliderEditMode::Circle &&
			selectedEntity.HasComponent<CircleCollider2D>())
		{
			auto& collider = selectedEntity.GetComponent<CircleCollider2D>();
			if (m_ViewportState.ActiveColliderHandle == ColliderEditHandle::Offset)
			{
				glm::vec2 newOffset;
				if (centerToOffset(m_ViewportState.ColliderDragStartCenter + mouseDelta, newOffset) &&
					glm::length(collider.Offset - newOffset) > 0.000001f)
				{
					collider.Offset = newOffset;
					m_Layer.UpdateSceneTransaction();
				}
			}
			else
			{
				glm::vec2 outward(0.0f);
				switch (m_ViewportState.ActiveColliderHandle)
				{
				case ColliderEditHandle::CircleLeft: outward = -right; break;
				case ColliderEditHandle::CircleRight: outward = right; break;
				case ColliderEditHandle::CircleBottom: outward = -up; break;
				case ColliderEditHandle::CircleTop: outward = up; break;
				default: break;
				}
				if (glm::dot(outward, outward) > 0.0f)
				{
					const float worldRadius = std::max(maximumScale * minimumComponentExtent,
						m_ViewportState.ColliderDragStartRadius + glm::dot(mouseDelta, outward));
					const float newRadius = worldRadius / maximumScale;
					if (std::abs(collider.Radius - newRadius) > 0.000001f)
					{
						collider.Radius = newRadius;
						m_Layer.UpdateSceneTransaction();
					}
				}
			}
		}
	}


}
