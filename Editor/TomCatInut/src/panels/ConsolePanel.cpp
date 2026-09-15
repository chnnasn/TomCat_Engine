#include "ConsolePanel.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "TomCat/Utils/PathUtils.h"

namespace TomCat {

	namespace {

		constexpr size_t kMaximumConsoleMessages = 4096;
		constexpr float kMinimumInlineMessageWidth = 160.0f;
		constexpr float kConsoleCounterMinimumWidth = 48.0f;

		void AppendCollapseKeyField(std::string& key, std::string_view value)
		{
			key += std::to_string(value.size());
			key += ':';
			key.append(value.data(), value.size());
		}

		std::string BuildCollapseKey(const ConsoleMessage& message)
		{
			std::string key(1, static_cast<char>(message.Severity));
			AppendCollapseKeyField(key, message.Source);
			AppendCollapseKeyField(key, message.Code);
			AppendCollapseKeyField(key, message.Text);
			AppendCollapseKeyField(key, PathToUTF8(message.File));
			AppendCollapseKeyField(key, std::to_string(message.Line));
			AppendCollapseKeyField(key, std::to_string(message.Column));
			AppendCollapseKeyField(key, message.StackTrace);
			return key;
		}

		const char* SeverityLabel(ConsoleMessageSeverity severity)
		{
			switch (severity)
			{
				case ConsoleMessageSeverity::Trace: return "Trace";
				case ConsoleMessageSeverity::Info: return "Info";
				case ConsoleMessageSeverity::Warning: return "Warning";
				case ConsoleMessageSeverity::Error: return "Error";
			}
			return "Info";
		}

		ImVec4 SeverityColor(ConsoleMessageSeverity severity)
		{
			switch (severity)
			{
				case ConsoleMessageSeverity::Trace: return ImVec4(0.58f, 0.62f, 0.68f, 1.0f);
				case ConsoleMessageSeverity::Info: return ImVec4(0.78f, 0.84f, 0.92f, 1.0f);
				case ConsoleMessageSeverity::Warning: return ImVec4(1.0f, 0.72f, 0.25f, 1.0f);
				case ConsoleMessageSeverity::Error: return ImVec4(1.0f, 0.34f, 0.34f, 1.0f);
			}
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		std::string FormatLocation(const ConsoleMessage& message)
		{
			if (message.File.empty())
				return {};
			std::string location = PathToUTF8(message.File);
			if (message.Line != 0)
			{
				location += "(" + std::to_string(message.Line);
				if (message.Column != 0)
					location += "," + std::to_string(message.Column);
				location += ")";
			}
			return location;
		}

	}

	void ConsolePanel::Push(ConsoleMessage message)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		message.Sequence = m_NextSequence++;
		m_Messages.push_back(std::move(message));
		if (m_Messages.size() > kMaximumConsoleMessages)
		{
			const size_t overflow = m_Messages.size() - kMaximumConsoleMessages;
			m_Messages.erase(m_Messages.begin(), m_Messages.begin() +
				static_cast<std::ptrdiff_t>(overflow));
		}
		m_ScrollToBottom = true;
	}

	void ConsolePanel::Push(ConsoleMessageSeverity severity, std::string text,
		std::string source)
	{
		ConsoleMessage message;
		message.Severity = severity;
		message.Text = std::move(text);
		message.Source = std::move(source);
		Push(std::move(message));
	}

	void ConsolePanel::Clear()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Messages.clear();
		m_ScrollToBottom = false;
	}

	void ConsolePanel::OnPlayStarted()
	{
		if (m_ClearOnPlay)
			Clear();
	}

	std::vector<ConsoleMessage> ConsolePanel::Snapshot() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Messages;
	}

	bool ConsolePanel::IsVisible(ConsoleMessageSeverity severity) const
	{
		switch (severity)
		{
			case ConsoleMessageSeverity::Trace: return m_ShowTrace;
			case ConsoleMessageSeverity::Info: return m_ShowInfo;
			case ConsoleMessageSeverity::Warning: return m_ShowWarnings;
			case ConsoleMessageSeverity::Error: return m_ShowErrors;
		}
		return true;
	}

	void ConsolePanel::OnImGuiRender(bool* open)
	{
		if (open && !*open)
		{
			m_Focused = false;
			return;
		}

		const bool visible = ImGui::Begin("Console", open);
		m_Docked = ImGui::IsWindowDocked();
		m_Focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			return;
		}

		const ImGuiStyle& style = ImGui::GetStyle();
		std::vector<ConsoleMessage> messages = Snapshot();
		size_t informationCount = 0;
		size_t warningCount = 0;
		size_t errorCount = 0;
		for (const ConsoleMessage& message : messages)
		{
			if (message.Severity == ConsoleMessageSeverity::Warning)
				++warningCount;
			else if (message.Severity == ConsoleMessageSeverity::Error)
				++errorCount;
			else
				++informationCount;
		}

		const auto clearConsole = [&]()
		{
			Clear();
			messages.clear();
			informationCount = warningCount = errorCount = 0;
		};
		const float toolbarStartY = ImGui::GetCursorScreenPos().y;
		if (ImGui::Button("Clear"))
			clearConsole();
		ImGui::SameLine(0.0f, 1.0f);
		if (ImGui::ArrowButton("##ConsoleClearMenuButton", ImGuiDir_Down))
			ImGui::OpenPopup("##ConsoleClearMenu");
		if (ImGui::BeginPopup("##ConsoleClearMenu"))
		{
			if (ImGui::MenuItem("Clear All"))
				clearConsole();
			ImGui::Separator();
			ImGui::MenuItem("Clear on Play", nullptr, &m_ClearOnPlay);
			ImGui::MenuItem("Auto-scroll", nullptr, &m_AutoScroll);
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		const bool collapseWasActive = m_Collapse;
		if (collapseWasActive)
		{
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImGui::GetStyleColorVec4(ImGuiCol_Header));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
		}
		if (ImGui::Button("Collapse"))
			m_Collapse = !m_Collapse;
		if (collapseWasActive)
			ImGui::PopStyleColor(3);

		const auto counterWidth = [](size_t count)
		{
			return std::max(kConsoleCounterMinimumWidth,
				ImGui::CalcTextSize(std::to_string(count).c_str()).x + 31.0f);
		};
		const float informationWidth = counterWidth(informationCount);
		const float warningWidth = counterWidth(warningCount);
		const float errorWidth = counterWidth(errorCount);
		const float countersWidth = informationWidth + warningWidth + errorWidth;
		const float contentLeft = ImGui::GetWindowPos().x +
			ImGui::GetWindowContentRegionMin().x;
		const float contentRight = ImGui::GetWindowPos().x +
			ImGui::GetWindowContentRegionMax().x;
		const float countersX = std::max(contentLeft,
			contentRight - countersWidth);
		if (ImGui::GetItemRectMax().x + style.ItemSpacing.x <= countersX)
		{
			ImGui::SameLine();
			ImGui::SetCursorScreenPos(ImVec2(countersX, toolbarStartY));
		}
		else
			ImGui::SetCursorScreenPos(ImVec2(countersX,
				ImGui::GetItemRectMax().y + style.ItemSpacing.y));

		enum class CounterGlyph { Information, Warning, Error };
		const auto drawCounter = [&](const char* id, CounterGlyph glyph, size_t count,
			float width, bool enabled)
		{
			const ImVec2 size(width, ImGui::GetFrameHeight());
			ImGui::InvisibleButton(id, size);
			const bool pressed = ImGui::IsItemClicked();
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 minimum = ImGui::GetItemRectMin();
			const ImVec2 maximum = ImGui::GetItemRectMax();
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(minimum, maximum,
				ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
			drawList->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border));
			ImU32 glyphColor = IM_COL32(145, 145, 145, enabled ? 255 : 105);
			if (enabled && count > 0)
			{
				if (glyph == CounterGlyph::Information) glyphColor = IM_COL32(110, 170, 220, 255);
				else if (glyph == CounterGlyph::Warning) glyphColor = IM_COL32(235, 175, 65, 255);
				else glyphColor = IM_COL32(225, 90, 90, 255);
			}
			const ImVec2 center(minimum.x + 14.0f,
				(minimum.y + maximum.y) * 0.5f);
			if (glyph == CounterGlyph::Information)
				drawList->AddCircleFilled(center, 8.0f, glyphColor, 18);
			else if (glyph == CounterGlyph::Warning)
				drawList->AddTriangleFilled(ImVec2(center.x, center.y - 9.0f),
					ImVec2(center.x - 9.0f, center.y + 8.0f),
					ImVec2(center.x + 9.0f, center.y + 8.0f), glyphColor);
			else
			{
				ImVec2 points[8];
				for (int index = 0; index < 8; ++index)
				{
					const float angle = 3.14159265f * (0.125f + index * 0.25f);
					points[index] = ImVec2(center.x + std::cos(angle) * 8.5f,
						center.y + std::sin(angle) * 8.5f);
				}
				drawList->AddConvexPolyFilled(points, 8, glyphColor);
			}
			const ImU32 markColor = IM_COL32(55, 55, 55, enabled ? 235 : 90);
			drawList->AddLine(ImVec2(center.x, center.y - 4.0f),
				ImVec2(center.x, center.y + 2.0f), markColor, 1.7f);
			drawList->AddCircleFilled(ImVec2(center.x, center.y + 5.0f), 1.1f,
				markColor, 8);
			const std::string countText = std::to_string(count);
			drawList->AddText(ImVec2(minimum.x + 27.0f,
				std::round(center.y - ImGui::GetTextLineHeight() * 0.5f)),
				ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
				countText.c_str());
			return pressed;
		};

		const bool informationVisible = m_ShowTrace || m_ShowInfo;
		if (drawCounter("##ConsoleInformationCount", CounterGlyph::Information,
			informationCount, informationWidth, informationVisible))
		{
			m_ShowTrace = !informationVisible;
			m_ShowInfo = !informationVisible;
		}
		ImGui::SameLine(0.0f, 0.0f);
		if (drawCounter("##ConsoleWarningCount", CounterGlyph::Warning,
			warningCount, warningWidth, m_ShowWarnings))
			m_ShowWarnings = !m_ShowWarnings;
		ImGui::SameLine(0.0f, 0.0f);
		if (drawCounter("##ConsoleErrorCount", CounterGlyph::Error,
			errorCount, errorWidth, m_ShowErrors))
			m_ShowErrors = !m_ShowErrors;
		ImGui::Separator();

		struct DisplayMessage
		{
			const ConsoleMessage* Message = nullptr;
			size_t Count = 1;
		};
		std::vector<DisplayMessage> displayMessages;
		displayMessages.reserve(messages.size());
		std::unordered_map<std::string, size_t> collapsedIndices;
		for (const ConsoleMessage& message : messages)
		{
			if (!IsVisible(message.Severity))
				continue;
			if (!m_Collapse)
			{
				displayMessages.push_back({ &message, 1 });
				continue;
			}
			const std::string key = BuildCollapseKey(message);
			const auto [iterator, inserted] = collapsedIndices.emplace(
				key, displayMessages.size());
			if (inserted)
				displayMessages.push_back({ &message, 1 });
			else
				++displayMessages[iterator->second].Count;
		}

		ImGui::BeginChild("##ConsoleMessages", ImVec2(0.0f, 0.0f), false,
			ImGuiWindowFlags_HorizontalScrollbar);
		for (const DisplayMessage& displayMessage : displayMessages)
		{
			const ConsoleMessage& message = *displayMessage.Message;

			ImGui::PushID(static_cast<int>(message.Sequence & 0x7fffffff));
			const std::string location = FormatLocation(message);
			std::string prefix = "[";
			prefix += SeverityLabel(message.Severity);
			prefix += "]";
			if (!message.Source.empty())
				prefix += " [" + message.Source + "]";
			if (!message.Code.empty())
				prefix += " " + message.Code;
			if (!location.empty())
				prefix += " " + location;
			if (displayMessage.Count > 1)
				prefix += " (x" + std::to_string(displayMessage.Count) + ")";

			const float inlineMessageWidth = ImGui::GetContentRegionAvail().x -
				ImGui::CalcTextSize(prefix.c_str()).x - style.ItemSpacing.x;
			ImGui::TextColored(SeverityColor(message.Severity), "%s", prefix.c_str());
			if (inlineMessageWidth >= kMinimumInlineMessageWidth)
				ImGui::SameLine();
			ImGui::TextWrapped("%s", message.Text.c_str());
			if (!message.StackTrace.empty() && ImGui::TreeNode("Stack trace"))
			{
				ImGui::TextUnformatted(message.StackTrace.c_str());
				ImGui::TreePop();
			}
			ImGui::Separator();
			ImGui::PopID();
		}

		bool shouldScroll = false;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			shouldScroll = m_ScrollToBottom;
			m_ScrollToBottom = false;
		}
		if (m_AutoScroll && shouldScroll)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();
		ImGui::End();
	}

}
