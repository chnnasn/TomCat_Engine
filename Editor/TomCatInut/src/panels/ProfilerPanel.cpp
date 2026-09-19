#include "../EditorVisuals.h"
#include "ProfilerPanel.h"

#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Renderer/Renderer.h"
#include "TomCat/Utils/PlatformUtils.h"
#include <imgui/imgui.h>

#include <array>
#include <cfloat>
#include <map>
#include <unordered_map>

#ifdef TC_PLATFORM_WINDOWS
#include <Windows.h>
#include <psapi.h>
#endif

namespace TomCat {
	namespace {
		constexpr double MiB = 1024.0 * 1024.0;
		double DeltaMiB(uint64_t current, uint64_t baseline)
		{
			return (static_cast<double>(current) - static_cast<double>(baseline)) / MiB;
		}
		void DrawTimeline(const FrameProfile& frame)
		{
			std::map<uint64_t, uint32_t> depths;
			for (const auto& sample : frame.Samples)
				depths[sample.Thread] = (std::max)(depths[sample.Thread], (std::min)(sample.Depth, 20u) + 1);
			std::map<uint64_t, float> offsets;
			const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
			float height = 0;
			for (const auto& [thread, depth] : depths)
			{
				offsets[thread] = height + rowHeight;
				height += (depth + 1) * rowHeight + 8;
			}
			ImGui::BeginChild("CPU timeline", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float width = (std::max)(ImGui::GetContentRegionAvail().x, 200.0f);
			const double milliseconds = (std::max)(frame.CpuMilliseconds, 0.001);
			auto* draw = ImGui::GetWindowDrawList();
			for (const auto& [thread, offset] : offsets)
			{
				const std::string label = "Thread " + std::to_string(thread);
				draw->AddText(ImVec2(origin.x, origin.y + offset - rowHeight), ImGui::GetColorU32(ImGuiCol_TextDisabled), label.c_str());
			}
			for (const auto& sample : frame.Samples)
			{
				const float x = static_cast<float>(std::clamp(sample.StartMilliseconds / milliseconds, 0.0, 1.0)) * width;
				const float end = static_cast<float>(std::clamp((sample.StartMilliseconds + sample.DurationMilliseconds) / milliseconds, 0.0, 1.0)) * width;
				const float y = offsets[sample.Thread] + (std::min)(sample.Depth, 20u) * rowHeight;
				const ImVec2 a(origin.x + x, origin.y + y);
				const ImVec2 b(origin.x + (std::max)(end, x + 1), a.y + rowHeight - 2);
				const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(sample.Name));
				const ImU32 color = ImGui::ColorConvertFloat4ToU32(ImColor::HSV(static_cast<float>(hash % 360) / 360.0f, 0.55f, 0.72f));
				draw->AddRectFilled(a, b, color, 2.0f);
				if (b.x - a.x > 35)
				{
					draw->PushClipRect(a, b, true);
					draw->AddText(ImVec2(a.x + 3, a.y + 1), IM_COL32_WHITE, sample.Name.c_str());
					draw->PopClipRect();
				}
				if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(sample.Name.c_str());
					ImGui::Text("Start %.3f ms | Inclusive %.3f ms | Depth %u", sample.StartMilliseconds, sample.DurationMilliseconds, sample.Depth);
					ImGui::EndTooltip();
				}
			}
			ImGui::Dummy(ImVec2(width, (std::max)(height, rowHeight)));
			ImGui::EndChild();
		}

		void DrawScopeTable(const FrameProfile& frame, const char* search)
		{
			struct Aggregate { std::string Name; double Total = 0, Maximum = 0; uint32_t Calls = 0; };
			std::unordered_map<std::string, Aggregate> byName;
			for (const auto& sample : frame.Samples)
			{
				auto& value = byName[sample.Name];
				value.Name = sample.Name;
				value.Total += sample.DurationMilliseconds;
				value.Maximum = (std::max)(value.Maximum, sample.DurationMilliseconds);
				++value.Calls;
			}
			std::vector<Aggregate> sorted;
			for (auto& [name, value] : byName) sorted.push_back(std::move(value));
			std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.Total > b.Total; });
			ImGui::TextDisabled("Inclusive scopes overlap; totals must not be added together.");
			if (ImGui::BeginTable("Scopes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 0)))
			{
				ImGui::TableSetupColumn("Scope"); ImGui::TableSetupColumn("Calls");
				ImGui::TableSetupColumn("Inclusive ms"); ImGui::TableSetupColumn("Max ms");
				ImGui::TableHeadersRow();
				for (const auto& value : sorted)
				{
                    if (search[0] && value.Name.find(search)==std::string::npos) continue;
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(value.Name.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%u", value.Calls);
					ImGui::TableNextColumn(); ImGui::Text("%.3f", value.Total);
					ImGui::TableNextColumn(); ImGui::Text("%.3f", value.Maximum);
				}
				ImGui::EndTable();
			}
		}
	}

    void ProfilerPanel::OnImGuiRender(bool* open)
    {
        auto& profiler=FrameProfiler::Get();
        if(open && !*open) { profiler.SetRecording(false); return; }
        PrepareEditorToolWindow(ImVec2(1120,760),ImVec2(640,440));
        if(!ImGui::Begin("Profiler",open)) { ImGui::End(); return; }
        const float font=ImGui::GetFontSize(), frameHeight=ImGui::GetFrameHeight();
        auto summaries=profiler.Summaries();
        int selected=-1;
        if(!summaries.empty())
        {
            if(m_FollowLatest || !m_SelectedFrame || m_SelectedFrame<summaries.front().ID) m_SelectedFrame=summaries.back().ID;
            for(size_t i=0;i<summaries.size();++i) if(summaries[i].ID==m_SelectedFrame) selected=static_cast<int>(i);
        }
        const char* modules[]={"CPU Usage","GPU Usage","Rendering","Memory"};
        const ImU32 colors[]={IM_COL32(130,185,68,255),IM_COL32(62,163,202,255),IM_COL32(219,159,65,255),IM_COL32(147,129,201,255)};
        const float sidebar=std::clamp(ImGui::GetContentRegionAvail().x*0.23f,font*8,font*13);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::BeginChild("ProfilerToolbar",ImVec2(0,frameHeight+2),false,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(1,0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,0);
        if(ImGui::Button("Profiler Modules",ImVec2(sidebar,frameHeight))) ImGui::OpenPopup("ModuleSelector");
        const ImVec2 modulesAnchor(ImGui::GetItemRectMin().x,ImGui::GetItemRectMax().y);
        PrepareEditorPopup("ModuleSelector",300);
        if(ImGui::IsPopupOpen("ModuleSelector")) ImGui::SetNextWindowPos(modulesAnchor,ImGuiCond_Appearing);
        if(ImGui::BeginPopup("ModuleSelector"))
        {
            for(int module=0;module<4;++module)
            {
                ImGui::Checkbox(modules[module],&m_Modules[module]);
                if(module==1 && !Renderer::SupportsGpuProfiling())
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("GPU timer queries are unavailable on this platform.");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Additional modules");
            for(const char* name:{"Audio","Video","Physics","Physics (2D)","UI","Asset Loading"})
            {
                bool unavailable=false;
                ImGui::BeginDisabled(); ImGui::Checkbox(name,&unavailable); ImGui::EndDisabled();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("No dedicated chart collector yet. Instrumented scopes are available in CPU Usage.");
            }
            ImGui::Separator();
            if(ImGui::Button("Restore Defaults",ImVec2(-1,0))) { m_Modules={true,true,true,true}; m_SelectedModule=0; }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        bool recording=profiler.IsRecording();
        if(ImGui::Button("##Record",ImVec2(frameHeight,frameHeight))) { recording=!recording; profiler.SetRecording(recording); }
        const ImVec2 recordMin=ImGui::GetItemRectMin();
        auto* toolbarDraw=ImGui::GetWindowDrawList();
        const ImVec2 recordCenter(recordMin.x+frameHeight*0.5f,recordMin.y+frameHeight*0.5f);
        toolbarDraw->AddCircle(recordCenter,font*0.42f,ImGui::GetColorU32(ImGuiCol_Text),24,1.5f);
        toolbarDraw->AddCircleFilled(recordCenter,font*0.3f,recording?IM_COL32(229,88,79,255):IM_COL32(116,116,116,255),24);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Record editor frames (including Play Mode)");
        auto moveFrame=[&](const char* label,int offset) {
            ImGui::SameLine(); ImGui::BeginDisabled(selected<0 || selected+offset<0 || selected+offset>=static_cast<int>(summaries.size()));
            if(ImGui::Button(label)) { selected+=offset; m_SelectedFrame=summaries[selected].ID; m_FollowLatest=false; }
            ImGui::EndDisabled();
        };
        moveFrame("<",-1); moveFrame(">",1);
        ImGui::SameLine();
        if(ImGui::Button("Live")) m_FollowLatest=true;
        ImGui::SameLine(); ImGui::AlignTextToFramePadding();
        ImGui::Text("Frame: %d / %zu",selected+1,summaries.size());
        ImGui::SameLine();
        if(ImGui::Button("Clear")) { profiler.Clear(); summaries.clear(); selected=-1; m_SelectedFrame=0; }
        ImGui::SameLine();
        if(ImGui::Button("...")) ImGui::OpenPopup("CaptureOptions");
        if(ImGui::BeginPopup("CaptureOptions"))
        {
            ImGui::MenuItem("Clear on Play",nullptr,&m_ClearOnPlay);
            ImGui::MenuItem("Follow latest frame",nullptr,&m_FollowLatest);
            if(ImGui::MenuItem("Export CPU trace..."))
            {
                const auto path=FileDialogs::SaveFile("JSON trace (*.json)\0*.json\0");
                if(!path.empty()) m_ExportMessage=profiler.ExportTrace(path)?"Trace exported.":"Could not write trace.";
            }
            ImGui::TextDisabled("240 frames / 512 scopes per frame");
            if(!m_ExportMessage.empty()) ImGui::TextWrapped("%s",m_ExportMessage.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2); ImGui::EndChild(); ImGui::PopStyleVar();
        const float available=ImGui::GetContentRegionAvail().y;
        const float overviewHeight=(std::max)(frameHeight,std::floor(available*m_OverviewRatio));
        ImGui::BeginChild("ModuleCharts",ImVec2(0,overviewHeight),true);
        bool any=false;
        for(int module=0;module<4;++module)
        {
            if(!m_Modules[module]) continue;
            any=true; ImGui::PushID(module);
            const float rowHeight=font*6;
            const ImVec2 start=ImGui::GetCursorScreenPos();
            const float width=ImGui::GetContentRegionAvail().x;
            const float labelWidth=(std::min)(sidebar,width*0.46f);
            if(ImGui::Selectable("##Module",m_SelectedModule==module,0,ImVec2(labelWidth,rowHeight))) m_SelectedModule=module;
            auto* draw=ImGui::GetWindowDrawList();
            draw->AddRectFilled({start.x+font*0.3f,start.y+font*0.55f},{start.x+font*0.7f,start.y+font*0.95f},colors[module]);
            draw->PushClipRect(start,{start.x+labelWidth,start.y+rowHeight},true);
            draw->AddText({start.x+font,start.y+font*0.25f},ImGui::GetColorU32(ImGuiCol_Text),modules[module]);
            const char* legend[]={"CPU frame / ms","GPU elapsed / ms","Draw calls","Tracked resources / MiB"};
            draw->AddText({start.x+font*0.4f,start.y+font*1.7f},ImGui::GetColorU32(ImGuiCol_TextDisabled),legend[module]);
            draw->PopClipRect();
            const ImVec2 chartMin(start.x+labelWidth+4,start.y), chartMax(start.x+width,start.y+rowHeight);
            draw->AddRectFilled(chartMin,chartMax,IM_COL32(47,47,47,255));
            for(int line=1;line<4;++line) { const float y=chartMin.y+rowHeight*line/4; draw->AddLine({chartMin.x,y},{chartMax.x,y},IM_COL32(70,70,70,255)); }
            const auto value=[&](const FrameProfileSummary& summary) -> double {
                if(module==0) return summary.CpuMilliseconds;
                if(module==1) return summary.GpuMilliseconds;
                if(module==2) return summary.DrawCalls;
                return (summary.Resources.TextureBytes+summary.Resources.BufferBytes+summary.Resources.FramebufferBytes)/MiB;
            };
            double maximum=module<2?16.67:1;
            bool valid=false;
            for(const auto& summary:summaries) if(value(summary)>=0) { maximum=(std::max)(maximum,value(summary)*1.15); valid=true; }
            ImVec2 previous{}; bool previousValid=false;
            const float plotWidth=(std::max)(1.0f,chartMax.x-chartMin.x-12);
            const auto xFor=[&](size_t i) { return chartMin.x+6+plotWidth*(i+0.5f)/std::max<size_t>(1,summaries.size()); };
            for(size_t i=0;i<summaries.size();++i)
            {
                const double sample=value(summaries[i]);
                if(sample<0) { previousValid=false; continue; } // Missing GPU data is a gap, never a zero.
                const ImVec2 point(xFor(i),chartMax.y-6-static_cast<float>(sample/maximum)*(rowHeight-16));
                if(previousValid) draw->AddLine(previous,point,colors[module],1.5f);
                else draw->AddCircleFilled(point,2,colors[module]);
                previous=point; previousValid=true;
            }
            if(selected>=0) draw->AddLine({xFor(selected),chartMin.y},{xFor(selected),chartMax.y},IM_COL32(230,230,230,180));
            if(!valid) draw->AddText({chartMin.x+font*0.4f,chartMin.y+font*0.4f},ImGui::GetColorU32(ImGuiCol_TextDisabled),summaries.empty()?"No frame data - enable Record":"Sample unavailable");
            if(ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(chartMin,chartMax) && ImGui::IsMouseDown(0) && !summaries.empty())
            {
                const float fraction=std::clamp((ImGui::GetIO().MousePos.x-chartMin.x-6)/plotWidth,0.0f,0.9999f);
                selected=static_cast<int>(fraction*summaries.size()); m_SelectedFrame=summaries[selected].ID; m_FollowLatest=false; m_SelectedModule=module;
            }
            draw->AddLine({start.x,start.y+rowHeight},{chartMax.x,chartMax.y},ImGui::GetColorU32(ImGuiCol_Border));
            ImGui::PopID();
        }
        if(!any) ImGui::TextWrapped("Choose a module from Profiler Modules to show its chart.");
        ImGui::EndChild();
        ImGui::InvisibleButton("ProfilerSplit",ImVec2(-1,5));
        if(ImGui::IsItemActive() && available>1) m_OverviewRatio=std::clamp(m_OverviewRatio+ImGui::GetIO().MouseDelta.y/available,0.2f,0.75f);
        if(ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        ImGui::BeginChild("FrameDetails",ImVec2(0,0),false);
        ImGui::SetNextItemWidth(font*8);
        const char* views[]={"Hierarchy","Timeline","C# Debugger"};
        ImGui::Combo("##DetailsView",&m_DetailsView,views,3);
        ImGui::SameLine(); ImGui::Checkbox("Live",&m_FollowLatest);
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
        EditorSearchField("##ScopeSearch","Search scopes...",m_ScopeSearch,sizeof(m_ScopeSearch));
        ImGui::Separator();
        FrameProfile frame;
        const bool hasFrame=profiler.ReadFrame(m_SelectedFrame,frame);
        if(m_DetailsView==2)
        {
#ifdef TC_PLATFORM_WINDOWS
            ImGui::Text("Attach managed (.NET) debugger to PID %lu",static_cast<unsigned long>(GetCurrentProcessId()));
#endif
            ImGui::TextWrapped("After Play initializes .NET, attach Visual Studio to TomCatInut.exe with managed .NET code selected. Set a breakpoint in the project's script source and enter Play again for OnCreate. See docs/DEBUGGING_AND_PROFILING.md for symbols and source mapping.");
        }
        else if(m_SelectedModule==3)
        {
			const auto current = ProfileResourceTracker::Get().Snapshot();
			if (m_LastMemorySample < 0 || ImGui::GetTime() - m_LastMemorySample > 0.5)
			{
				m_LastMemorySample = ImGui::GetTime();
#ifdef TC_PLATFORM_WINDOWS
				PROCESS_MEMORY_COUNTERS_EX counters{};
				counters.cb = sizeof(counters);
				m_ProcessMemoryAvailable = K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE;
				if (m_ProcessMemoryAvailable) { m_WorkingSet = counters.WorkingSetSize; m_PrivateBytes = counters.PrivateUsage; m_PeakWorkingSet = counters.PeakWorkingSetSize; }
#endif
			}
			if (ImGui::Button("Set memory baseline"))
			{
				m_Baseline = current; m_HasBaseline = true;
				m_BaselineWorkingSet = m_WorkingSet; m_BaselinePrivateBytes = m_PrivateBytes;
			}
			if (m_ProcessMemoryAvailable)
			{
				ImGui::Text("Process working set %.2f MiB | private committed %.2f MiB | peak working set %.2f MiB", m_WorkingSet / MiB, m_PrivateBytes / MiB, m_PeakWorkingSet / MiB);
				if (m_HasBaseline) ImGui::Text("Since baseline: working set %+.2f MiB | private committed %+.2f MiB", DeltaMiB(m_WorkingSet, m_BaselineWorkingSet), DeltaMiB(m_PrivateBytes, m_BaselinePrivateBytes));
			}
			else ImGui::TextDisabled("Process memory counters unavailable.");
			if (ImGui::BeginTable("Resources", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
			{
				ImGui::TableSetupColumn("Resource payload"); ImGui::TableSetupColumn("Live count"); ImGui::TableSetupColumn("MiB"); ImGui::TableSetupColumn("Baseline delta MiB"); ImGui::TableHeadersRow();
				const auto row = [&](const char* name, uint64_t count, uint64_t bytes, uint64_t baseline)
				{
					ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
					ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(count));
					ImGui::TableNextColumn(); ImGui::Text("%.3f", bytes / MiB);
					ImGui::TableNextColumn(); if (m_HasBaseline) ImGui::Text("%+.3f", DeltaMiB(bytes, baseline)); else ImGui::TextUnformatted("--");
				};
				row("Textures (all mips)", current.TextureCount, current.TextureBytes, m_Baseline.TextureBytes);
				row("Vertex / index buffers", current.BufferCount, current.BufferBytes, m_Baseline.BufferBytes);
				row("Framebuffer attachments", current.FramebufferCount, current.FramebufferBytes, m_Baseline.FramebufferBytes);
				ImGui::EndTable();
			}
			const auto textures = AssetManager::Get().GetTextureStreamingStats();
			const auto fonts = FontManager::Get().GetStreamingStats();
			ImGui::Text("Texture staging %.2f MiB | pending %llu | workers %llu", textures.PreparedBytes / MiB, static_cast<unsigned long long>(textures.PendingCount), static_cast<unsigned long long>(textures.JobsInFlight));
			ImGui::Text("Font staging %.2f MiB | atlases %llu | workers %llu", fonts.PreparedBytes / MiB, static_cast<unsigned long long>(fonts.PublishedCount), static_cast<unsigned long long>(fonts.JobsInFlight));
			ImGui::TextWrapped("Resource sizes are allocation estimates, including texture mips/compression and framebuffer samples. They exclude driver overhead, shader programs, audio and managed heap attribution. Process counters include the editor and managed runtime. Compare equivalent load/unload cycles against a baseline to investigate retained resources.");

        }
        else if(!hasFrame) ImGui::TextWrapped("No frame data available. Record frames, then select a frame in the charts above to see its details here.");
        else
        {
            ImGui::Text("Frame %llu | CPU %.3f ms",static_cast<unsigned long long>(frame.ID),frame.CpuMilliseconds);
            if(m_SelectedModule==1)
            {
                if(frame.GpuMilliseconds>=0) ImGui::Text("GPU elapsed %.3f ms",frame.GpuMilliseconds);
                else ImGui::TextWrapped("GPU sample unavailable: unsupported, pending, or the query ring was busy.");
                ImGui::TextWrapped("Main context timing excludes Present and may include queue idle time. Per-pass GPU attribution is not collected.");
            }
            else if(m_SelectedModule==2)
            {
                ImGui::Text("Draw calls: %u",frame.DrawCalls);
                ImGui::Text("Submitted elements: %u",frame.SubmittedVertices);
                ImGui::TextWrapped("Engine submissions exclude the editor's ImGui draws.");
            }
            else
            {
                if(frame.DroppedSamples) ImGui::Text("Omitted scopes: %u",frame.DroppedSamples);
                if(m_DetailsView==1)
                {
                    if(m_ScopeSearch[0]) frame.Samples.erase(std::remove_if(frame.Samples.begin(),frame.Samples.end(),[&](const auto& sample){return sample.Name.find(m_ScopeSearch)==std::string::npos;}),frame.Samples.end());
                    DrawTimeline(frame);
                }
                else DrawScopeTable(frame,m_ScopeSearch);
            }
        }
        ImGui::EndChild(); ImGui::End();
        if(open && !*open) profiler.SetRecording(false);
    }
}
