// Shared upstream Scene toolbar body: desktop and Web.
		const bool draggingMode = m_GizmoModeToolbarDragging;
		const bool draggingTransform = m_GizmoTransformToolbarDragging;
		if (!draggingMode && !draggingTransform)
			return;

		const ImVec2 mouse = ImGui::GetMousePos();
		if (mouse.y < m_GizmoModeDockY - 5.0f ||
			mouse.y > m_GizmoModeDockY + m_GizmoModeDockHeight + 5.0f)
			return;

		const float dockStartX = m_ViewportBounds[0].x + 8.0f;
		const float previewWidth = draggingMode
			? kSceneModeToolbarWidth : kSceneTransformToolbarWidth;
		const bool otherDocked = draggingMode
			? m_GizmoTransformToolbarDocked : m_GizmoModeToolbarDocked;
		const float otherWidth = draggingMode
			? kSceneTransformToolbarWidth : kSceneModeToolbarWidth;
		float previewX = dockStartX;
		if (otherDocked)
		{
			const bool insertBefore = mouse.x < dockStartX + otherWidth * 0.5f;
			previewX = insertBefore
				? dockStartX : dockStartX + otherWidth + kSceneToolbarDockGap;
		}
		previewX = std::max(dockStartX, std::min(previewX,
			m_ViewportBounds[1].x - previewWidth - 4.0f));

		// Submit this after both toolbar bodies and on the foreground layer. This
		// makes the insertion target equally visible in either drag direction.
		ImDrawList* previewDraw = ImGui::GetForegroundDrawList();
		previewDraw->PushClipRect(
			ImVec2(m_ViewportBounds[0].x, m_GizmoModeDockY),
			ImVec2(m_ViewportBounds[1].x, m_GizmoModeDockY + m_GizmoModeDockHeight), false);
		previewDraw->AddRectFilled(ImVec2(previewX, m_GizmoModeDockY),
			ImVec2(previewX + previewWidth, m_GizmoModeDockY + m_GizmoModeDockHeight),
			IM_COL32(44, 93, 135, 85), 1.0f);
		previewDraw->AddRect(ImVec2(previewX + 1.0f, m_GizmoModeDockY + 1.0f),
			ImVec2(previewX + previewWidth - 1.0f, m_GizmoModeDockY + m_GizmoModeDockHeight - 1.0f),
			IM_COL32(80, 165, 235, 230), 1.0f, 0, 1.0f);
		previewDraw->PopClipRect();
