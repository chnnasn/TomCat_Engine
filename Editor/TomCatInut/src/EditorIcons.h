#pragma once

#include "TomCat/Renderer/Texture.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <utility>

namespace TomCat {

	enum class EditorIcon : size_t
	{
		AssetsRoot = 0,
		FolderClosed,
		FolderOpen,
		GenericFile,
		SceneClosed,
		SceneOpen,
		Texture,
		Material,
		Shader,
		Script,
		Mesh,
		Audio,
		Font,
		Project,
		Package,
		Missing,
		ReadOnly,
		Link,
		Search,
		Add,
		SceneView,
		GameView,
		Hierarchy,
		Inspector,
		ProjectBrowser,
		Entity,
		Camera,
		Sprite,
		Rigidbody2D,
		BoxCollider2D,
		Hand,
		Move,
		Rotate,
		Scale,
		RectTool,
		Play,
		Pause,
		Step,
		Stop,
		EyeOn,
		EyeOff,
		Lock,
		Count
	};

	// Owns the editor-only icon textures for the lifetime of EditorLayer. Keeping
	// the cache on the layer avoids repeated decoding while also guaranteeing the
	// OpenGL textures are released with the editor rather than from a process-wide
	// static after the graphics context has gone away.
	class EditorIconSet
	{
	public:
		bool Load()
		{
			bool loadedAll = true;
			for (size_t index = 0; index < m_Icons.size(); ++index)
			{
				const EditorIcon icon = static_cast<EditorIcon>(index);
				Ref<Texture2D> texture = Texture2D::Create(GetPath(icon));
				if (!texture || !texture->IsLoaded())
				{
					texture.reset();
					loadedAll = false;
				}
				m_Icons[index] = std::move(texture);
			}
			return loadedAll;
		}

		const Ref<Texture2D>& Get(EditorIcon icon) const
		{
			const size_t index = static_cast<size_t>(icon);
			return index < m_Icons.size() ? m_Icons[index] : EmptyIcon();
		}

	private:
		static const Ref<Texture2D>& EmptyIcon()
		{
			static const Ref<Texture2D> empty;
			return empty;
		}

		static std::filesystem::path GetPath(EditorIcon icon)
		{
			constexpr const char* assetRoot = "Packages/Resources/Icons/TomCat/Asset/";
			constexpr const char* editorRoot = "Packages/Resources/Icons/TomCat/Editor/";
			switch (icon)
			{
				case EditorIcon::AssetsRoot: return std::filesystem::path(assetRoot) / "assets-root.png";
				case EditorIcon::FolderClosed: return std::filesystem::path(assetRoot) / "folder-closed.png";
				case EditorIcon::FolderOpen: return std::filesystem::path(assetRoot) / "folder-open.png";
				case EditorIcon::GenericFile: return std::filesystem::path(assetRoot) / "generic-file.png";
				case EditorIcon::SceneClosed: return std::filesystem::path(assetRoot) / "scene-closed.png";
				case EditorIcon::SceneOpen: return std::filesystem::path(assetRoot) / "scene-open.png";
				case EditorIcon::Texture: return std::filesystem::path(assetRoot) / "texture.png";
				case EditorIcon::Material: return std::filesystem::path(assetRoot) / "material.png";
				case EditorIcon::Shader: return std::filesystem::path(assetRoot) / "shader.png";
				case EditorIcon::Script: return std::filesystem::path(assetRoot) / "script.png";
				case EditorIcon::Mesh: return std::filesystem::path(assetRoot) / "mesh.png";
				case EditorIcon::Audio: return std::filesystem::path(assetRoot) / "audio.png";
				case EditorIcon::Font: return std::filesystem::path(assetRoot) / "font.png";
				case EditorIcon::Project: return std::filesystem::path(assetRoot) / "project.png";
				case EditorIcon::Package: return std::filesystem::path(assetRoot) / "package.png";
				case EditorIcon::Missing: return std::filesystem::path(assetRoot) / "missing.png";
				case EditorIcon::ReadOnly: return std::filesystem::path(assetRoot) / "read-only.png";
				case EditorIcon::Link: return std::filesystem::path(assetRoot) / "link.png";
				case EditorIcon::Search: return std::filesystem::path(assetRoot) / "search.png";
				case EditorIcon::Add: return std::filesystem::path(assetRoot) / "add.png";
				case EditorIcon::SceneView: return std::filesystem::path(editorRoot) / "scene-view.png";
				case EditorIcon::GameView: return std::filesystem::path(editorRoot) / "game-view.png";
				case EditorIcon::Hierarchy: return std::filesystem::path(editorRoot) / "hierarchy.png";
				case EditorIcon::Inspector: return std::filesystem::path(editorRoot) / "inspector.png";
				case EditorIcon::ProjectBrowser: return std::filesystem::path(editorRoot) / "project-browser.png";
				case EditorIcon::Entity: return std::filesystem::path(editorRoot) / "entity.png";
				case EditorIcon::Camera: return std::filesystem::path(editorRoot) / "camera.png";
				case EditorIcon::Sprite: return std::filesystem::path(editorRoot) / "sprite.png";
				case EditorIcon::Rigidbody2D: return std::filesystem::path(editorRoot) / "rigidbody-2d.png";
				case EditorIcon::BoxCollider2D: return std::filesystem::path(editorRoot) / "box-collider-2d.png";
				case EditorIcon::Hand: return std::filesystem::path(editorRoot) / "hand.png";
				case EditorIcon::Move: return std::filesystem::path(editorRoot) / "move.png";
				case EditorIcon::Rotate: return std::filesystem::path(editorRoot) / "rotate.png";
				case EditorIcon::Scale: return std::filesystem::path(editorRoot) / "scale.png";
				case EditorIcon::RectTool: return std::filesystem::path(editorRoot) / "rect-tool.png";
				case EditorIcon::Play: return std::filesystem::path(editorRoot) / "play.png";
				case EditorIcon::Pause: return std::filesystem::path(editorRoot) / "pause.png";
				case EditorIcon::Step: return std::filesystem::path(editorRoot) / "step.png";
				case EditorIcon::Stop: return std::filesystem::path(editorRoot) / "stop.png";
				case EditorIcon::EyeOn: return std::filesystem::path(editorRoot) / "eye-on.png";
				case EditorIcon::EyeOff: return std::filesystem::path(editorRoot) / "eye-off.png";
				case EditorIcon::Lock: return std::filesystem::path(editorRoot) / "lock.png";
				case EditorIcon::Count: break;
			}
			return {};
		}

		std::array<Ref<Texture2D>, static_cast<size_t>(EditorIcon::Count)> m_Icons;
	};

}
