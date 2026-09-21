#include "../EditorVisuals.h"
#include "ConsolePanel.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <chrono>
#include <ctime>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "TomCat/Utils/PathUtils.h"

namespace TomCat {

	namespace {

		constexpr size_t kMaximumConsoleMessages = 4096;

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

        void DrawSeverityIcon(ConsoleMessageSeverity severity, ImVec2 center, float radius, bool enabled=true)
        {
            auto* draw=ImGui::GetWindowDrawList();
            const int alpha=enabled?255:100;
            ImU32 color=IM_COL32(215,215,215,alpha);
            if(severity==ConsoleMessageSeverity::Warning) color=IM_COL32(244,190,48,alpha);
            if(severity==ConsoleMessageSeverity::Error) color=IM_COL32(220,88,80,alpha);
            if(severity==ConsoleMessageSeverity::Warning)
                draw->AddTriangleFilled({center.x,center.y-radius},{center.x-radius,center.y+radius},{center.x+radius,center.y+radius},color);
            else if(severity==ConsoleMessageSeverity::Error)
            {
                ImVec2 points[8];
                for(int i=0;i<8;++i) { const float angle=3.14159265f*(0.125f+i*0.25f); points[i]={center.x+std::cos(angle)*radius,center.y+std::sin(angle)*radius}; }
                draw->AddConvexPolyFilled(points,8,color);
            }
            else
            {
                draw->AddCircleFilled(center,radius,color,24);
                draw->AddTriangleFilled({center.x+radius*0.25f,center.y+radius*0.65f},
                    {center.x+radius,center.y+radius*1.2f},{center.x+radius*0.8f,center.y+radius*0.2f},color);
            }
            const ImU32 mark=IM_COL32(48,48,48,alpha);
            draw->AddLine({center.x,center.y-radius*0.5f},{center.x,center.y+radius*0.15f},mark,std::max(1.5f,radius*0.16f));
            draw->AddCircleFilled({center.x,center.y+radius*0.5f},radius*0.11f,mark,12);
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
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm time{};
#ifdef _WIN32
        localtime_s(&time,&now);
#else
        localtime_r(&now,&time);
#endif
        char timestamp[16]{};
        std::strftime(timestamp,sizeof(timestamp),"%H:%M:%S",&time);
        message.Timestamp=timestamp;
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

		PrepareEditorToolWindow(ImVec2(960,560),ImVec2(420,280));
        const bool visible = BeginEditorWindow("Console", open);
		m_Docked = ImGui::IsWindowDocked();
		m_Focused = visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			return;
		}

        const auto messages = Snapshot();
        size_t informationCount=0,warningCount=0,errorCount=0;
        for(const auto& message:messages)
        {
            if(message.Severity==ConsoleMessageSeverity::Error)
            {
                ++errorCount;
                if(m_ErrorPause && message.Sequence>m_LastObservedSequence && m_ErrorPauseCallback) m_ErrorPauseCallback();
            }
            else if(message.Severity==ConsoleMessageSeverity::Warning) ++warningCount;
            else ++informationCount;
        }
        if(!messages.empty()) m_LastObservedSequence=messages.back().Sequence;
        const float font=ImGui::GetFontSize();
        const float frame=ImGui::GetFrameHeight();
        const bool wide=ImGui::GetContentRegionAvail().x>font*38;
        bool cleared=false;
        // One clipped toolbar, like Unity: narrow docks keep Clear / Collapse first.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::BeginChild("ConsoleToolbar",ImVec2(0,frame+2),false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(1,0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);
        if(ImGui::Button("Clear")) { Clear(); cleared=true; informationCount=warningCount=errorCount=0; }
        ImGui::SameLine();
        if(ImGui::ArrowButton("ClearOptions",ImGuiDir_Down)) ImGui::OpenPopup("ConsoleOptions");
        if(ImGui::BeginPopup("ConsoleOptions"))
        {
            ImGui::MenuItem("Clear on Play",nullptr,&m_ClearOnPlay);
            ImGui::MenuItem("Auto-scroll",nullptr,&m_AutoScroll);
            ImGui::MenuItem("Error Pause",nullptr,&m_ErrorPause);
            ImGui::Separator();
            ImGui::MenuItem("Log",nullptr,&m_ShowInfo);
            ImGui::MenuItem("Trace",nullptr,&m_ShowTrace);
            ImGui::MenuItem("Warning",nullptr,&m_ShowWarnings);
            ImGui::MenuItem("Error",nullptr,&m_ShowErrors);
            ImGui::SetNextItemWidth(font*18);
            EditorSearchField("Search","Search messages...",m_Search,sizeof(m_Search));
            ImGui::EndPopup();
        }
        auto toggle=[&](const char* label,bool& value) {
            ImGui::SameLine();
            if(value) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            bool pressed=ImGui::Button(label);
            if(value) ImGui::PopStyleColor();
            if(pressed) value=!value;
        };
        toggle("Collapse",m_Collapse);
        if(wide)
        {
            toggle("Error Pause",m_ErrorPause);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(font*5.5f);
            if(ImGui::BeginCombo("##LogTarget","Editor")) { ImGui::Selectable("Editor",true); ImGui::EndCombo(); }
        }
        const auto counterWidth=[&](size_t count) { return frame+ImGui::CalcTextSize(std::to_string(count).c_str()).x+font*0.5f; };
        const float countersWidth=counterWidth(informationCount)+counterWidth(warningCount)+counterWidth(errorCount)+3;
        if(wide)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(font*3,ImGui::GetContentRegionAvail().x-countersWidth-2));
            EditorSearchField("##ConsoleSearch","",m_Search,sizeof(m_Search));
        }
        auto counter=[&](const char* id,ConsoleMessageSeverity severity,size_t count,bool enabled) {
            ImGui::SameLine();
            const bool pressed=ImGui::Button(id,ImVec2(counterWidth(count),frame));
            const auto a=ImGui::GetItemRectMin();
            DrawSeverityIcon(severity,{a.x+frame*0.5f,a.y+frame*0.5f},font*0.42f,enabled);
            ImGui::GetWindowDrawList()->AddText({a.x+frame,a.y+(frame-font)*0.5f},
                ImGui::GetColorU32(enabled?ImGuiCol_Text:ImGuiCol_TextDisabled),std::to_string(count).c_str());
            return pressed;
        };
        if(counter("##Info",ConsoleMessageSeverity::Info,informationCount,m_ShowInfo||m_ShowTrace)) { const bool show=!(m_ShowInfo||m_ShowTrace); m_ShowInfo=m_ShowTrace=show; }
        if(counter("##Warn",ConsoleMessageSeverity::Warning,warningCount,m_ShowWarnings)) m_ShowWarnings=!m_ShowWarnings;
        if(counter("##Error",ConsoleMessageSeverity::Error,errorCount,m_ShowErrors)) m_ShowErrors=!m_ShowErrors;
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::PopStyleVar();
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
			if (cleared || !IsVisible(message.Severity) || (m_Search[0] && (message.Text + message.Source + message.Code).find(m_Search) == std::string::npos))
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

        const float available=ImGui::GetContentRegionAvail().y;
        const float details=std::min(font*7,available*0.3f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(0,0));
        ImGui::BeginChild("##ConsoleMessages",ImVec2(0,std::max(frame,available-details)),false);
        const float rowHeight=font*2+ImGui::GetStyle().FramePadding.y*2+font*0.35f;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(displayMessages.size()),rowHeight);
        while(clipper.Step()) for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index)
        {
            const auto& displayMessage=displayMessages[index];
            const auto& message=*displayMessage.Message;
            ImGui::PushID(static_cast<int>(message.Sequence&0x7fffffff));
            const ImVec2 a=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x;
            if(index%2==0) ImGui::GetWindowDrawList()->AddRectFilled(a,{a.x+width,a.y+rowHeight},IM_COL32(255,255,255,8));
            if(ImGui::Selectable("##Message",m_SelectedSequence==message.Sequence,ImGuiSelectableFlags_AllowDoubleClick,ImVec2(width,rowHeight))) m_SelectedSequence=message.Sequence;
            if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !message.File.empty() && m_OpenSource) m_OpenSource(message.File);
            if(ImGui::BeginPopupContextItem("MessageMenu"))
            {
                if(ImGui::MenuItem("Copy")) ImGui::SetClipboardText((message.Text+"\n"+message.StackTrace).c_str());
                if(ImGui::MenuItem("Open source",nullptr,false,!message.File.empty() && bool(m_OpenSource))) m_OpenSource(message.File);
                ImGui::EndPopup();
            }
            DrawSeverityIcon(message.Severity,{a.x+rowHeight*0.48f,a.y+rowHeight*0.47f},rowHeight*0.31f);
            std::string summary="["+message.Timestamp+"] ";
            if(!message.Source.empty()) summary+="["+message.Source+"] ";
            if(!message.Code.empty()) summary+=message.Code+" ";
            summary+=message.Text.substr(0,message.Text.find('\n'));
            std::string second=FormatLocation(message);
            if(second.empty()) second=message.StackTrace.substr(0,message.StackTrace.find('\n'));
            if(second.empty()) second=message.Source;
            auto* draw=ImGui::GetWindowDrawList();
            const float badge=displayMessage.Count>1?ImGui::CalcTextSize(std::to_string(displayMessage.Count).c_str()).x+font:0;
            const ImVec2 textStart(a.x+rowHeight,a.y+font*0.12f);
            draw->PushClipRect(textStart,{std::max(textStart.x,a.x+width-badge),a.y+rowHeight},true);
            draw->AddText(textStart,ImGui::GetColorU32(ImGuiCol_Text),summary.c_str());
            draw->AddText({textStart.x,textStart.y+font},ImGui::GetColorU32(ImGuiCol_Text),second.c_str());
            draw->PopClipRect();
            if(badge>0)
            {
                draw->AddRectFilled({a.x+width-badge,a.y+font*0.4f},{a.x+width,a.y+font*1.6f},ImGui::GetColorU32(ImGuiCol_Button),font*0.5f);
                draw->AddText({a.x+width-badge+font*0.5f,a.y+font*0.5f},ImGui::GetColorU32(ImGuiCol_Text),std::to_string(displayMessage.Count).c_str());
            }
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
        ImGui::PopStyleVar();
        ImGui::Separator();
        ImGui::BeginChild("ConsoleDetails");
        auto selected = std::find_if(messages.begin(), messages.end(), [&](const auto& message) { return message.Sequence == m_SelectedSequence; });
        if (!cleared && selected != messages.end())
        {
            if (ImGui::SmallButton("Copy details")) ImGui::SetClipboardText((selected->Text + "\n" + selected->StackTrace).c_str());
            if(!selected->File.empty() && m_OpenSource) {ImGui::SameLine();if(ImGui::SmallButton("Open source")) m_OpenSource(selected->File);}
            ImGui::TextDisabled("%s", FormatLocation(*selected).c_str());
            ImGui::TextWrapped("%s", selected->Text.c_str());
            ImGui::TextWrapped("%s", selected->StackTrace.c_str());
        }
        else ImGui::TextDisabled("Select a message to inspect its full details.");
        ImGui::EndChild();
        ImGui::End();
    }

}
