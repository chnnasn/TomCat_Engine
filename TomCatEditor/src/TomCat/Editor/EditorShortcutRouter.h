#pragma once

#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Events/InputModifiers.h"

#include <cstdint>

namespace TomCat {

	enum class EditorShortcutAction : uint8_t
	{
		None = 0,
		NewScene,
		OpenScene,
		SaveScene,
		SaveSceneAs,
		Undo,
		Redo,
		NextWindow,
		PreviousWindow,
		CutSelection,
		CopySelection,
		PasteSelection,
		DuplicateSelection,
		RenameSelection,
		DeleteSelection,
		ToolNone,
		ToolTranslate,
		ToolRotate,
		ToolScale,
		FrameSelection,
		ToggleScene2D
	};

	struct EditorShortcutContext
	{
		int KeyCode = 0;
		int RepeatCount = 0;
		InputModifiers Modifiers{};
		bool WantsTextInput = false;
		bool PopupOpen = false;
		bool EntityContextFocused = false;
		bool SceneFocused = false;
		bool HasSelection = false;
		bool EditingScene = false;
		bool TransformDragActive = false;
	};

	// Resolves keyboard input without depending on ImGui or a concrete editor
	// panel. Hosts remain responsible for executing the returned command.
	inline EditorShortcutAction ResolveEditorShortcut(
		const EditorShortcutContext& context)
	{
		if (context.RepeatCount > 0 || context.PopupOpen)
			return EditorShortcutAction::None;

		const bool control = context.Modifiers.Control;
		const bool shift = context.Modifiers.Shift;
		const bool alt = context.Modifiers.Alt;
		const bool super = context.Modifiers.Super;
		const bool controlOnly = control && !shift && !alt && !super;
		const bool controlWithOptionalShift = control && !alt && !super;
		const bool unmodified = !control && !shift && !alt && !super;

		// File and window commands are editor-global, including while a property
		// text field owns input. Entity and history commands below yield to text.
		if (context.KeyCode == Key::N && controlOnly)
			return EditorShortcutAction::NewScene;
		if (context.KeyCode == Key::O && controlOnly)
			return EditorShortcutAction::OpenScene;
		if (context.KeyCode == Key::S && controlWithOptionalShift)
			return shift ? EditorShortcutAction::SaveSceneAs
				: EditorShortcutAction::SaveScene;
		if (context.KeyCode == Key::Tab && controlWithOptionalShift)
			return shift ? EditorShortcutAction::PreviousWindow
				: EditorShortcutAction::NextWindow;

		if (context.WantsTextInput)
			return EditorShortcutAction::None;

		if (context.KeyCode == Key::Key2 && unmodified && context.SceneFocused
			&& !context.TransformDragActive)
			return EditorShortcutAction::ToggleScene2D;

		if (context.EditingScene && controlOnly)
		{
			if (context.KeyCode == Key::Z)
				return EditorShortcutAction::Undo;
			if (context.KeyCode == Key::Y)
				return EditorShortcutAction::Redo;
		}

		if (!context.EntityContextFocused)
			return EditorShortcutAction::None;

		if (controlOnly)
		{
			switch (context.KeyCode)
			{
				case Key::X:
					return context.HasSelection ? EditorShortcutAction::CutSelection
						: EditorShortcutAction::None;
				case Key::C:
					return context.HasSelection ? EditorShortcutAction::CopySelection
						: EditorShortcutAction::None;
				case Key::V:
					return EditorShortcutAction::PasteSelection;
				case Key::D:
					return context.HasSelection ? EditorShortcutAction::DuplicateSelection
						: EditorShortcutAction::None;
				default:
					break;
			}
		}

		if (!unmodified)
			return EditorShortcutAction::None;

		switch (context.KeyCode)
		{
			case Key::F2:
				return context.HasSelection ? EditorShortcutAction::RenameSelection
					: EditorShortcutAction::None;
			case Key::Delete:
				return context.HasSelection ? EditorShortcutAction::DeleteSelection
					: EditorShortcutAction::None;
			case Key::Q:
				return context.TransformDragActive ? EditorShortcutAction::None
					: EditorShortcutAction::ToolNone;
			case Key::W:
				return context.TransformDragActive ? EditorShortcutAction::None
					: EditorShortcutAction::ToolTranslate;
			case Key::E:
				return context.TransformDragActive ? EditorShortcutAction::None
					: EditorShortcutAction::ToolRotate;
			case Key::R:
				return context.TransformDragActive ? EditorShortcutAction::None
					: EditorShortcutAction::ToolScale;
			case Key::F:
				return context.HasSelection && !context.TransformDragActive
					? EditorShortcutAction::FrameSelection
					: EditorShortcutAction::None;
			default:
				return EditorShortcutAction::None;
		}
	}

}
