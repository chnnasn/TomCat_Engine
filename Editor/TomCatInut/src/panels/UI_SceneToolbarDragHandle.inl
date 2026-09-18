// Shared upstream Scene toolbar body: desktop and Web.
		ImGui::PushID(id);
		ImGui::SetCursorScreenPos(handleMin);
		ImGui::InvisibleButton("##drag", ImVec2(handleMax.x - handleMin.x, handleMax.y - handleMin.y));

		const bool pressing = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		if (!dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			dragging = true;
			if (docked)
			{
				docked = false;
				const ImVec2 mouse = ImGui::GetMousePos();
				offset = { mouse.x - m_ViewportBounds[0].x - tearX,
					m_GizmoModeDockY - m_ViewportBounds[0].y };
			}
		}

		if (dragging && pressing)
		{
			const ImVec2 delta = ImGui::GetIO().MouseDelta;
			offset.x += delta.x;
			offset.y += delta.y;
		}
		else if (dragging)
		{
			const ImVec2 mouse = ImGui::GetMousePos();
			if (canDock &&
				mouse.y >= m_GizmoModeDockY - 6.0f &&
				mouse.y <= m_GizmoModeDockY + m_GizmoModeDockHeight + 6.0f)
			{
				docked = true;
				offset = { 16.0f, 10.0f };
			}
			dragging = false;
		}

		ImGui::PopID();
