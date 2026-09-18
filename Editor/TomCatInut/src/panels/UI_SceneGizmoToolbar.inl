// Shared upstream Scene toolbar body: desktop and Web.
		// Draw directly over the Scene image. This keeps the palette clipped and
		// owned by the Scene view instead of creating another dockable ImGui window.
		// The native Scene menu bar clips to its own row while it is active; widen
		// this toolbar pass to the Scene bounds so a floating palette below the row
		// remains visible and interactive.
		const float toolbarClipTop = m_GizmoTransformToolbarDragging
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		const float dockPadding = kSceneToolbarPadding;
		const float dockHandleWidth = kSceneToolbarHandleWidth;
		const float dockButtonWidth = kSceneTransformButtonWidth;
		const float dockButtonHeight = 28.0f;
		const float dockGap = kSceneToolbarItemGap;
		const float dockWidth = kSceneTransformToolbarWidth;
		const float modeWidth = kSceneModeToolbarWidth;

		if (m_GizmoTransformToolbarDocked)
		{
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			const float transformDockX = (m_GizmoModeToolbarDocked && m_GizmoModeToolbarFirst)
				? dockStartX + modeWidth + dockGap : dockStartX;
			const float transformHeight = dockButtonHeight + dockPadding * 2.0f;
			const float transformDockOffsetY = (m_GizmoModeDockHeight - transformHeight) * 0.5f;
			ImVec2 topLeft(transformDockX, m_GizmoModeDockY + transformDockOffsetY);
			ImVec2 bottomRight(topLeft.x + dockWidth, topLeft.y + dockButtonHeight + dockPadding * 2.0f);
			// A docked toolbar may be torn off while the cursor leaves the Scene
			// window.  Use the viewport foreground list during the drag so the
			// active bar is not clipped away by the Scene window bounds.
			ImDrawList* draw = m_GizmoTransformToolbarDragging
				? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
			const ImU32 outer = IM_COL32(40, 40, 40, 245);
			const ImU32 normal = IM_COL32(71, 71, 71, 245);
			const ImU32 active = IM_COL32(44, 93, 135, 255);
			const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
			draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

			ImVec2 handleMin(topLeft.x + dockPadding, topLeft.y + dockPadding);
			ImVec2 handleMax(handleMin.x + dockHandleWidth, topLeft.y + dockButtonHeight + dockPadding);
			ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
			for (int i = -1; i <= 1; ++i)
				draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
					ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

			const bool transformWasDragging = m_GizmoTransformToolbarDragging;
			UI_SceneToolbarDragHandle("##scene_transform_toolbar_docked", m_GizmoToolbarOffset,
				m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
				handleMin, handleMax, 0.0f, true);
			if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
			{
				const float x = ImGui::GetMousePos().x;
				m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
					x >= dockStartX + modeWidth * 0.5f;
				SaveSceneToolbarLayout();
			}

			const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
				ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
			const EditorIcon toolIcons[] = { EditorIcon::Hand, EditorIcon::Move,
				EditorIcon::Rotate, EditorIcon::Scale };
			for (int i = 0; i < 4; ++i)
			{
				ImVec2 min(topLeft.x + dockPadding + dockHandleWidth + dockGap + i * (dockButtonWidth + dockGap),
					topLeft.y + dockPadding);
				ImVec2 max(min.x + dockButtonWidth, min.y + dockButtonHeight);
				const bool selected = m_GizmoType == tools[i];
				draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);
				draw->AddRect(min, max, selected ? active : IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
				const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
				constexpr float dockIconRadius = 11.0f;
				DrawSceneToolbarIcon(draw, m_EditorIcons, toolIcons[i],
					ImVec2(center.x - dockIconRadius, center.y - dockIconRadius),
					ImVec2(center.x + dockIconRadius, center.y + dockIconRadius));
				ImGui::SetCursorScreenPos(min);
				ImGui::InvisibleButton((std::string("##scene_docked_tool_") + std::to_string(i)).c_str(),
					ImVec2(max.x - min.x, max.y - min.y));
				if (ImGui::IsItemClicked())
					m_GizmoType = tools[i];
			}
			ImGui::PopClipRect();
			return;
		}

		const float width = 52.0f;
		const float handleHeight = 28.0f;
		const float buttonHeight = 50.0f;
		const float gap = 2.0f;
		const float height = handleHeight + gap + buttonHeight * 4.0f + gap * 3.0f + 5.0f;

		// Never allow the palette to become stranded outside the Scene view.
		// While dragging, leave the offset free so the bar follows the mouse.
		if (!m_GizmoTransformToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoToolbarOffset.x < 4.0f) m_GizmoToolbarOffset.x = 4.0f;
			if (m_GizmoToolbarOffset.y < 4.0f) m_GizmoToolbarOffset.y = 4.0f;
			if (m_GizmoToolbarOffset.x > maxOffsetX) m_GizmoToolbarOffset.x = maxOffsetX;
			if (m_GizmoToolbarOffset.y > maxOffsetY) m_GizmoToolbarOffset.y = maxOffsetY;
		}

		ImVec2 topLeft(m_ViewportBounds[0].x + m_GizmoToolbarOffset.x,
			m_ViewportBounds[0].y + m_GizmoToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
		ImDrawList* draw = m_GizmoTransformToolbarDragging
			? ImGui::GetForegroundDrawList() : ImGui::GetWindowDrawList();
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 active = IM_COL32(44, 93, 135, 255);

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		// The top handle is the only draggable area, matching the reference UI.
		ImVec2 handleMin(topLeft.x + 5.0f, topLeft.y + 4.0f);
		ImVec2 handleMax(topLeft.x + width - 5.0f, topLeft.y + handleHeight);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 12.0f, handleCenter.y + i * 5.0f),
				ImVec2(handleCenter.x + 12.0f, handleCenter.y + i * 5.0f), handleLine, 2.0f);
		const bool transformWasDragging = m_GizmoTransformToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_transform_toolbar", m_GizmoToolbarOffset,
			m_GizmoTransformToolbarDocked, m_GizmoTransformToolbarDragging,
			handleMin, handleMax, 0.0f, true);
		if (transformWasDragging && !m_GizmoTransformToolbarDragging && m_GizmoTransformToolbarDocked)
		{
			const float modeWidth = kSceneModeToolbarWidth;
			const float dockStartX = m_ViewportBounds[0].x + 8.0f;
			m_GizmoModeToolbarFirst = !m_GizmoModeToolbarDocked ||
				ImGui::GetMousePos().x >= dockStartX + modeWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		const int tools[] = { -1, ImGuizmo::OPERATION::TRANSLATE,
			ImGuizmo::OPERATION::ROTATE, ImGuizmo::OPERATION::SCALE };
		const EditorIcon toolIcons[] = { EditorIcon::Hand, EditorIcon::Move,
			EditorIcon::Rotate, EditorIcon::Scale };
		for (int i = 0; i < 4; ++i)
		{
			ImVec2 min(topLeft.x + 5.0f, topLeft.y + handleHeight + gap + i * (buttonHeight + gap));
			ImVec2 max(min.x + width - 10.0f, min.y + buttonHeight);
			bool selected = m_GizmoType == tools[i];
			draw->AddRectFilled(min, max, selected ? active : normal, 2.0f);

			const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			constexpr float floatingIconRadius = 17.0f;
			DrawSceneToolbarIcon(draw, m_EditorIcons, toolIcons[i],
				ImVec2(center.x - floatingIconRadius, center.y - floatingIconRadius),
				ImVec2(center.x + floatingIconRadius, center.y + floatingIconRadius));

			ImGui::SetCursorScreenPos(min);
			ImGui::InvisibleButton((std::string("##scene_tool_") + std::to_string(i)).c_str(),
				ImVec2(max.x - min.x, max.y - min.y));
			if (ImGui::IsItemClicked())
				m_GizmoType = tools[i];
		}
		ImGui::PopClipRect();
