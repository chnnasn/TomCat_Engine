#include "ConsolePanel.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "TomCat/Utils/PathUtils.h"

namespace TomCat {

	namespace {

		constexpr size_t kMaximumConsoleMessages = 4096;

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

		if (ImGui::Button("Clear"))
			Clear();
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &m_ShowTrace);
		ImGui::SameLine();
		ImGui::Checkbox("Info", &m_ShowInfo);
		ImGui::SameLine();
		ImGui::Checkbox("Warnings", &m_ShowWarnings);
		ImGui::SameLine();
		ImGui::Checkbox("Errors", &m_ShowErrors);
		ImGui::SameLine();
		ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
		ImGui::Separator();

		const std::vector<ConsoleMessage> messages = Snapshot();
		ImGui::BeginChild("##ConsoleMessages", ImVec2(0.0f, 0.0f), false,
			ImGuiWindowFlags_HorizontalScrollbar);
		for (const ConsoleMessage& message : messages)
		{
			if (!IsVisible(message.Severity))
				continue;

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

			ImGui::TextColored(SeverityColor(message.Severity), "%s", prefix.c_str());
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
