		// Unity-style Scene orientation control. TomCat's authoring basis is
		// +X right, +Y up and +Z forward; clicking an endpoint places the editor
		// camera on that side of the focus and looks back toward it.
		if (m_Is2DMode)
		{
			m_SceneOrientationGizmoHovered = false;
			m_SceneOrientationGizmoBounds[0] = {};
			m_SceneOrientationGizmoBounds[1] = {};
			m_SceneOrientationPressedTarget = -2;
			return;
		}

		const float uiScale = std::clamp(ImGui::GetFontSize() / 16.0f, 0.85f, 1.50f);
		const ImVec2 controlSize(126.0f * uiScale, 126.0f * uiScale);
		const float margin = 8.0f * uiScale;
		const glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		if (viewportSize.x < controlSize.x + margin * 2.0f
			|| viewportSize.y < controlSize.y + margin * 2.0f)
		{
			m_SceneOrientationGizmoHovered = false;
			m_SceneOrientationGizmoBounds[0] = {};
			m_SceneOrientationGizmoBounds[1] = {};
			m_SceneOrientationPressedTarget = -2;
			return;
		}

		const ImVec2 topLeft(m_ViewportBounds[1].x - controlSize.x - margin,
			m_ViewportBounds[0].y + margin);
		const ImVec2 bottomRight(topLeft.x + controlSize.x,
			topLeft.y + controlSize.y);
		m_SceneOrientationGizmoBounds[0] = { topLeft.x, topLeft.y };
		m_SceneOrientationGizmoBounds[1] = { bottomRight.x, bottomRight.y };

		const ImVec2 previousCursor = ImGui::GetCursorScreenPos();
		ImGui::SetCursorScreenPos(topLeft);
		const bool pressed = ImGui::InvisibleButton("##SceneOrientationGizmo",
			controlSize, ImGuiButtonFlags_MouseButtonLeft
				| ImGuiButtonFlags_MouseButtonRight
				| ImGuiButtonFlags_MouseButtonMiddle);
		m_SceneOrientationGizmoHovered = ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const ImVec2 mouse = ImGui::GetMousePos();

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y),
			ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);

		const ImVec2 center(topLeft.x + controlSize.x * 0.5f,
			topLeft.y + 51.0f * uiScale);
		const float axisLength = 38.0f * uiScale;
		const float coneLength = 9.0f * uiScale;
		const float coneHalfWidth = 5.0f * uiScale;
		const float hitRadius = 10.0f * uiScale;
		const glm::vec3 cameraRight = glm::normalize(m_EditorCamera.GetRightDirection());
		const glm::vec3 cameraUp = glm::normalize(m_EditorCamera.GetUpDirection());
		const glm::vec3 cameraSide = glm::normalize(
			m_EditorCamera.GetPosition() - m_EditorCamera.GetFocalPoint());

		struct AxisHandle
		{
			EditorCamera::AxisView View;
			glm::vec3 Axis;
			ImU32 Color;
			const char* Label;
			const char* Tooltip;
			ImVec2 End;
			float Depth;
			float ProjectedLength;
			bool Positive;
		};

		std::array<AxisHandle, 6> handles = { {
			{ EditorCamera::AxisView::PositiveX, { 1.0f, 0.0f, 0.0f }, IM_COL32(224, 77, 70, 255), "x", "+X  Right", {}, 0.0f, 0.0f, true },
			{ EditorCamera::AxisView::NegativeX, { -1.0f, 0.0f, 0.0f }, IM_COL32(157, 157, 157, 235), "", "-X  Left", {}, 0.0f, 0.0f, false },
			{ EditorCamera::AxisView::PositiveY, { 0.0f, 1.0f, 0.0f }, IM_COL32(111, 196, 83, 255), "y", "+Y  Top", {}, 0.0f, 0.0f, true },
			{ EditorCamera::AxisView::NegativeY, { 0.0f, -1.0f, 0.0f }, IM_COL32(157, 157, 157, 235), "", "-Y  Bottom", {}, 0.0f, 0.0f, false },
			{ EditorCamera::AxisView::PositiveZ, { 0.0f, 0.0f, 1.0f }, IM_COL32(72, 112, 220, 255), "z", "+Z  Forward", {}, 0.0f, 0.0f, true },
			{ EditorCamera::AxisView::NegativeZ, { 0.0f, 0.0f, -1.0f }, IM_COL32(157, 157, 157, 235), "", "-Z  Back", {}, 0.0f, 0.0f, false }
		} };

		for (AxisHandle& handle : handles)
		{
			const float screenX = glm::dot(handle.Axis, cameraRight);
			const float screenY = -glm::dot(handle.Axis, cameraUp);
			handle.ProjectedLength = std::sqrt(screenX * screenX + screenY * screenY);
			handle.End = ImVec2(center.x + screenX * axisLength,
				center.y + screenY * axisLength);
			handle.Depth = glm::dot(handle.Axis, cameraSide);
		}

		std::array<size_t, 6> drawOrder = { 0, 1, 2, 3, 4, 5 };
		std::sort(drawOrder.begin(), drawOrder.end(), [&](size_t left, size_t right)
		{
			return handles[left].Depth < handles[right].Depth;
		});

		int hoveredHandle = -1;
		float nearestDistanceSquared = std::numeric_limits<float>::max();
		float nearestDepth = -std::numeric_limits<float>::max();
		if (m_SceneOrientationGizmoHovered)
		{
			for (size_t index = 0; index < handles.size(); ++index)
			{
				const AxisHandle& handle = handles[index];
				const float deltaX = mouse.x - handle.End.x;
				const float deltaY = mouse.y - handle.End.y;
				const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
				if (distanceSquared <= hitRadius * hitRadius
					&& (distanceSquared < nearestDistanceSquared - 0.25f
						|| (std::abs(distanceSquared - nearestDistanceSquared) <= 0.25f
							&& handle.Depth > nearestDepth)))
				{
					hoveredHandle = static_cast<int>(index);
					nearestDistanceSquared = distanceSquared;
					nearestDepth = handle.Depth;
				}
			}
		}

		const ImVec2 labelMin(topLeft.x + 24.0f * uiScale,
			topLeft.y + 100.0f * uiScale);
		const ImVec2 labelMax(topLeft.x + 102.0f * uiScale,
			topLeft.y + 123.0f * uiScale);
		const bool labelHovered = m_SceneOrientationGizmoHovered
			&& mouse.x >= labelMin.x && mouse.x <= labelMax.x
			&& mouse.y >= labelMin.y && mouse.y <= labelMax.y;
		const float centerDeltaX = mouse.x - center.x;
		const float centerDeltaY = mouse.y - center.y;
		const bool centerHovered = m_SceneOrientationGizmoHovered
			&& hoveredHandle < 0
			&& centerDeltaX * centerDeltaX + centerDeltaY * centerDeltaY
				<= 8.0f * uiScale * 8.0f * uiScale;

		if (m_SceneOrientationGizmoHovered)
			draw->AddRectFilled(topLeft, bottomRight,
				IM_COL32(33, 33, 33, 82), 5.0f * uiScale);

		const float diamondRadius = 5.0f * uiScale;
		const ImU32 centerColor = centerHovered
			? IM_COL32(236, 236, 236, 255) : IM_COL32(190, 190, 190, 235);
		draw->AddQuadFilled(ImVec2(center.x, center.y - diamondRadius),
			ImVec2(center.x + diamondRadius, center.y),
			ImVec2(center.x, center.y + diamondRadius),
			ImVec2(center.x - diamondRadius, center.y), centerColor);

		for (size_t orderedIndex : drawOrder)
		{
			const AxisHandle& handle = handles[orderedIndex];
			const bool hovered = hoveredHandle == static_cast<int>(orderedIndex);
			ImU32 color = handle.Color;
			if (handle.Depth < -0.05f)
				color = handle.Positive
					? IM_COL32((handle.Color >> IM_COL32_R_SHIFT) & 0xff,
						(handle.Color >> IM_COL32_G_SHIFT) & 0xff,
						(handle.Color >> IM_COL32_B_SHIFT) & 0xff, 145)
					: IM_COL32(125, 125, 125, 145);
			if (hovered)
				color = IM_COL32(255, 244, 176, 255);

			if (handle.ProjectedLength < 0.08f)
			{
				const float capRadius = (handle.Depth >= 0.0f ? 6.5f : 4.5f) * uiScale;
				draw->AddCircleFilled(center, capRadius, color, 18);
				draw->AddCircle(center, capRadius,
					IM_COL32(36, 36, 36, 220), 18, 1.0f * uiScale);
				continue;
			}

			const float inverseLength = 1.0f / handle.ProjectedLength;
			const float directionX = (handle.End.x - center.x)
				/ axisLength * inverseLength;
			const float directionY = (handle.End.y - center.y)
				/ axisLength * inverseLength;
			const ImVec2 coneBase(handle.End.x - directionX * coneLength,
				handle.End.y - directionY * coneLength);
			const ImVec2 perpendicular(-directionY * coneHalfWidth,
				directionX * coneHalfWidth);
			draw->AddLine(center, coneBase, color,
				(hovered ? 3.0f : 2.0f) * uiScale);
			draw->AddTriangleFilled(handle.End,
				ImVec2(coneBase.x + perpendicular.x, coneBase.y + perpendicular.y),
				ImVec2(coneBase.x - perpendicular.x, coneBase.y - perpendicular.y),
				color);

			if (handle.Positive && handle.Label[0] != '\0')
			{
				const ImVec2 labelSize = ImGui::CalcTextSize(handle.Label);
				const ImVec2 labelPosition(handle.End.x - labelSize.x * 0.5f,
					handle.End.y - labelSize.y * 0.5f);
				draw->AddText(labelPosition, IM_COL32(246, 246, 246, 255),
					handle.Label);
			}
		}

		if (labelHovered)
			draw->AddRectFilled(labelMin, labelMax, IM_COL32(80, 80, 80, 150),
				3.0f * uiScale);
		const char* projectionLabel = m_EditorCamera.IsOrthographic()
			? "< Ortho" : "< Persp";
		const ImVec2 projectionTextSize = ImGui::CalcTextSize(projectionLabel);
		draw->AddText(ImVec2((labelMin.x + labelMax.x - projectionTextSize.x) * 0.5f,
			(labelMin.y + labelMax.y - projectionTextSize.y) * 0.5f),
			IM_COL32(222, 222, 222, 255), projectionLabel);

		if (ImGui::IsItemActivated())
		{
			m_SceneOrientationPressedTarget = ImGui::IsMouseDown(ImGuiMouseButton_Left)
				? ((labelHovered || centerHovered) ? -1
					: (hoveredHandle >= 0 ? hoveredHandle : -2))
				: -2;
		}
		if (pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (m_SceneOrientationPressedTarget == -1)
				m_EditorCamera.SetOrthographic(!m_EditorCamera.IsOrthographic());
			else if (m_SceneOrientationPressedTarget >= 0)
				m_EditorCamera.SnapToAxis(handles[static_cast<size_t>(
					m_SceneOrientationPressedTarget)].View);
			m_SceneOrientationPressedTarget = -2;
		}
		else if (!ImGui::IsItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_SceneOrientationPressedTarget = -2;

		if (m_SceneOrientationGizmoHovered)
		{
			if (hoveredHandle >= 0)
				ImGui::SetTooltip("%s", handles[static_cast<size_t>(hoveredHandle)].Tooltip);
			else if (labelHovered || centerHovered)
				ImGui::SetTooltip("Toggle perspective / orthographic projection");
		}

		draw->PopClipRect();
		ImGui::SetCursorScreenPos(previousCursor);
