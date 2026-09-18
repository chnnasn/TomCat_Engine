// Shared upstream Scene toolbar body: desktop and Web.
		const float padding = kSceneToolbarPadding;
		const float handleWidth = kSceneToolbarHandleWidth;
		const float buttonWidth = kSceneModeButtonWidth;
		const float buttonHeight = 28.0f;
		const float gap = kSceneToolbarItemGap;
		const float height = buttonHeight + padding * 2.0f;
		const float width = kSceneModeToolbarWidth;
		// The Q/W/E/R toolbar uses the same strip when docked.  Keep these
		// dimensions here (and in its renderer below) so insertion previews and
		// the two bars always agree about their occupied widths.
		const float transformWidth = kSceneTransformToolbarWidth;
		const float dockGap = kSceneToolbarDockGap;

		// While dragging, let the bar follow the mouse freely so it can cover the
		// Scene top strip.  When idle, keep it inside the viewport while still
		// allowing it to sit over that strip if the user parked it there.
		if (!m_GizmoModeToolbarDragging)
		{
			const float maxOffsetX = m_ViewportSize.x > width + 8.0f ? m_ViewportSize.x - width - 4.0f : 4.0f;
			const float minOffsetY = m_GizmoModeDockY - m_ViewportBounds[0].y + 1.0f;
			const float maxOffsetY = m_ViewportSize.y > height + 8.0f ? m_ViewportSize.y - height - 4.0f : 4.0f;
			if (m_GizmoModeToolbarOffset.x < 4.0f) m_GizmoModeToolbarOffset.x = 4.0f;
			if (m_GizmoModeToolbarOffset.y < minOffsetY) m_GizmoModeToolbarOffset.y = minOffsetY;
			if (m_GizmoModeToolbarOffset.x > maxOffsetX) m_GizmoModeToolbarOffset.x = maxOffsetX;
			if (m_GizmoModeToolbarOffset.y > maxOffsetY) m_GizmoModeToolbarOffset.y = maxOffsetY;
		}

		// Dock to the same strip used by the drop preview. Extend the ImGui item
		// clip rectangle as well as the drawing clip so the handle stays interactive.
		const float dockStartX = m_ViewportBounds[0].x + 8.0f;
		const float modeDockX = (m_GizmoModeToolbarDocked && m_GizmoTransformToolbarDocked && !m_GizmoModeToolbarFirst)
			? dockStartX + transformWidth + dockGap : dockStartX;
		const float modeDockOffsetY = (m_GizmoModeDockHeight - height) * 0.5f;
		ImVec2 topLeft = m_GizmoModeToolbarDocked
			? ImVec2(modeDockX, m_GizmoModeDockY + modeDockOffsetY)
			: ImVec2(m_ViewportBounds[0].x + m_GizmoModeToolbarOffset.x,
				m_ViewportBounds[0].y + m_GizmoModeToolbarOffset.y);
		ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);

		const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
		const float toolbarClipTop = (m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
			? ImGui::GetWindowPos().y : m_GizmoModeDockY;
		ImGui::PushClipRect(ImVec2(ImGui::GetWindowPos().x, toolbarClipTop),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), false);
		// Keep the idle toolbar body in the Scene window layer. Only the active
		// toolbar is promoted to the viewport foreground while it is dragged.
		ImDrawList* dockDraw = ImGui::GetWindowDrawList();
		ImDrawList* draw = m_GizmoModeToolbarDragging
			? ImGui::GetForegroundDrawList() : dockDraw;
		const ImU32 outer = IM_COL32(40, 40, 40, 245);
		const ImU32 normal = IM_COL32(71, 71, 71, 245);
		const ImU32 hover = IM_COL32(98, 98, 98, 245);
		const ImU32 line = IM_COL32(196, 196, 196, 255);
		const ImU32 arrow = IM_COL32(137, 137, 137, 255);
		const ImU32 accent = IM_COL32(212, 127, 42, 255);
		// The caller paints the Scene menu-bar overlay before entering this helper.
		// Keep this pass focused on the toolbar body; the insertion preview is a
		// final shared pass so neither toolbar can cover it based on call order.

		draw->AddRectFilled(topLeft, bottomRight, outer, 2.0f);

		ImVec2 handleMin(topLeft.x + padding, topLeft.y + padding);
		ImVec2 handleMax(handleMin.x + handleWidth, topLeft.y + height - padding);
		ImVec2 handleCenter((handleMin.x + handleMax.x) * 0.5f, (handleMin.y + handleMax.y) * 0.5f);
		const ImU32 handleLine = IM_COL32(137, 137, 137, 255);
		for (int i = -1; i <= 1; ++i)
			draw->AddLine(ImVec2(handleCenter.x - 7.0f, handleCenter.y + i * 4.0f),
				ImVec2(handleCenter.x + 7.0f, handleCenter.y + i * 4.0f), handleLine, 2.0f);

		const bool modeWasDragging = m_GizmoModeToolbarDragging;
		UI_SceneToolbarDragHandle("##scene_gizmo_mode", m_GizmoModeToolbarOffset,
			m_GizmoModeToolbarDocked, m_GizmoModeToolbarDragging,
			handleMin, handleMax, 17.0f, true);
		if (modeWasDragging && !m_GizmoModeToolbarDragging && m_GizmoModeToolbarDocked)
		{
			m_GizmoModeToolbarFirst = !m_GizmoTransformToolbarDocked ||
				ImGui::GetMousePos().x < dockStartX + transformWidth * 0.5f;
			SaveSceneToolbarLayout();
		}

		auto DrawFrame = [&](const ImVec2& min, const ImVec2& max, bool hovered)
		{
			// Pivot and space choices are represented by their icons. Keep each button
			// surface neutral after a click; only pointer hover may tint it.
			draw->AddRectFilled(min, max, hovered ? hover : normal, 2.0f);
			draw->AddRect(min, max, IM_COL32(25, 25, 25, 255), 2.0f, 0, 1.0f);
		};

		auto DrawDropArrow = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(max.x - 8.0f, (min.y + max.y) * 0.5f + 1.0f);
			draw->AddTriangleFilled(ImVec2(c.x - 3.0f, c.y - 2.0f), ImVec2(c.x + 3.0f, c.y - 2.0f), ImVec2(c.x, c.y + 2.5f), arrow);
		};

		auto DrawPivotIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(min.x + 14.0f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f),
				ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.5f);
			draw->AddLine(ImVec2(c.x - 6.0f, c.y + 6.0f),
				ImVec2(c.x + 6.0f, c.y - 6.0f), line, 1.5f);
			draw->AddCircleFilled(ImVec2(c.x - 6.0f, c.y + 6.0f), 3.0f, accent);
		};

		auto DrawCenterIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(min.x + 14.0f, (min.y + max.y) * 0.5f);
			draw->AddRect(ImVec2(c.x - 9.0f, c.y - 9.0f),
				ImVec2(c.x + 9.0f, c.y + 9.0f), line, 0.0f, 0, 1.5f);
			draw->AddLine(ImVec2(c.x - 6.0f, c.y + 6.0f),
				ImVec2(c.x + 6.0f, c.y - 6.0f), line, 1.5f);
			draw->AddCircleFilled(c, 3.0f, accent);
		};

		auto DrawLocalIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(min.x + 14.0f, (min.y + max.y) * 0.5f);
			const ImVec2 top(c.x, c.y - 9.0f);
			const ImVec2 upperLeft(c.x - 8.0f, c.y - 4.0f);
			const ImVec2 upperRight(c.x + 8.0f, c.y - 4.0f);
			const ImVec2 middle(c.x, c.y + 1.0f);
			const ImVec2 lowerLeft(c.x - 8.0f, c.y + 5.0f);
			const ImVec2 lowerRight(c.x + 8.0f, c.y + 5.0f);
			const ImVec2 bottom(c.x, c.y + 10.0f);
			draw->AddLine(top, upperLeft, line, 1.5f);
			draw->AddLine(top, upperRight, line, 1.5f);
			draw->AddLine(upperLeft, middle, line, 1.5f);
			draw->AddLine(upperRight, middle, line, 1.5f);
			draw->AddLine(upperLeft, lowerLeft, line, 1.5f);
			draw->AddLine(upperRight, lowerRight, line, 1.5f);
			draw->AddLine(middle, bottom, line, 1.5f);
			draw->AddLine(lowerLeft, bottom, line, 1.5f);
			draw->AddLine(lowerRight, bottom, line, 1.5f);
			draw->AddCircleFilled(lowerLeft, 2.8f, accent);
		};

		auto DrawWorldIcon = [&](const ImVec2& min, const ImVec2& max)
		{
			const ImVec2 c(min.x + 14.0f, (min.y + max.y) * 0.5f);
			draw->AddCircle(c, 9.0f, line, 24, 1.5f);
			draw->AddLine(ImVec2(c.x - 9.0f, c.y),
				ImVec2(c.x + 9.0f, c.y), line, 1.3f);
			draw->AddBezierCubic(ImVec2(c.x, c.y - 9.0f),
				ImVec2(c.x - 6.0f, c.y - 4.5f), ImVec2(c.x - 6.0f, c.y + 4.5f),
				ImVec2(c.x, c.y + 9.0f), line, 1.3f);
			draw->AddBezierCubic(ImVec2(c.x, c.y - 9.0f),
				ImVec2(c.x + 6.0f, c.y - 4.5f), ImVec2(c.x + 6.0f, c.y + 4.5f),
				ImVec2(c.x, c.y + 9.0f), line, 1.3f);
			draw->AddCircleFilled(ImVec2(c.x + 6.0f, c.y + 5.0f), 2.8f, accent);
		};

		auto DrawModeLabel = [&](const ImVec2& min, const ImVec2& max,
			const char* label)
		{
			const ImVec2 textSize = ImGui::CalcTextSize(label);
			draw->AddText(ImVec2(min.x + 28.0f,
				std::round((min.y + max.y - textSize.y) * 0.5f)), line, label);
		};

		const float buttonMinY = topLeft.y + padding;
		const ImVec2 pivotMin(topLeft.x + padding + handleWidth + gap, buttonMinY);
		const ImVec2 pivotMax(pivotMin.x + buttonWidth, pivotMin.y + buttonHeight);
		const ImVec2 spaceMin(pivotMax.x + gap, buttonMinY);
		const ImVec2 spaceMax(spaceMin.x + buttonWidth, spaceMin.y + buttonHeight);

		ImGui::SetCursorScreenPos(pivotMin);
		ImGui::InvisibleButton("##scene_gizmo_pivot_mode", ImVec2(pivotMax.x - pivotMin.x, pivotMax.y - pivotMin.y));
		const bool pivotHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_pivot_popup");
		DrawFrame(pivotMin, pivotMax, pivotHovered);
		if (m_GizmoPivotMode == GizmoPivotMode::Pivot)
			DrawPivotIcon(pivotMin, pivotMax);
		else
			DrawCenterIcon(pivotMin, pivotMax);
		DrawModeLabel(pivotMin, pivotMax,
			m_GizmoPivotMode == GizmoPivotMode::Pivot ? "Pivot" : "Center");
		DrawDropArrow(pivotMin, pivotMax);
		if (ImGui::BeginPopup("##scene_gizmo_pivot_popup"))
		{
			if (ImGui::MenuItem("Pivot", nullptr, m_GizmoPivotMode == GizmoPivotMode::Pivot))
				m_GizmoPivotMode = GizmoPivotMode::Pivot;
			if (ImGui::MenuItem("Center", nullptr, m_GizmoPivotMode == GizmoPivotMode::Center))
				m_GizmoPivotMode = GizmoPivotMode::Center;
			ImGui::EndPopup();
		}

		ImGui::SetCursorScreenPos(spaceMin);
		ImGui::InvisibleButton("##scene_gizmo_space_mode", ImVec2(spaceMax.x - spaceMin.x, spaceMax.y - spaceMin.y));
		const bool spaceHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
			ImGui::OpenPopup("##scene_gizmo_space_popup");
		DrawFrame(spaceMin, spaceMax, spaceHovered);
		if (m_GizmoSpaceMode == GizmoSpaceMode::Local)
			DrawLocalIcon(spaceMin, spaceMax);
		else
			DrawWorldIcon(spaceMin, spaceMax);
		DrawModeLabel(spaceMin, spaceMax,
			m_GizmoSpaceMode == GizmoSpaceMode::Local ? "Local" : "Global");
		DrawDropArrow(spaceMin, spaceMax);
		if (ImGui::BeginPopup("##scene_gizmo_space_popup"))
		{
			if (ImGui::MenuItem("Local", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::Local))
				m_GizmoSpaceMode = GizmoSpaceMode::Local;
			if (ImGui::MenuItem("Global", nullptr, m_GizmoSpaceMode == GizmoSpaceMode::World))
				m_GizmoSpaceMode = GizmoSpaceMode::World;
			ImGui::EndPopup();
		}
		ImGui::PopClipRect();
		ImGui::SetCursorScreenPos(savedCursor);
