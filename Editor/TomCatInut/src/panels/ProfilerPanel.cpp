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
			const float rowHeight = 21.0f;
			float height = 0;
			for (const auto& [thread, depth] : depths)
			{
				offsets[thread] = height + rowHeight;
				height += (depth + 1) * rowHeight + 8;
			}
			ImGui::BeginChild("CPU timeline", ImVec2(0, 240), true, ImGuiWindowFlags_HorizontalScrollbar);
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

		void DrawScopeTable(const FrameProfile& frame)
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
			if (ImGui::BeginTable("Scopes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 220)))
			{
				ImGui::TableSetupColumn("Scope"); ImGui::TableSetupColumn("Calls");
				ImGui::TableSetupColumn("Inclusive ms"); ImGui::TableSetupColumn("Max ms");
				ImGui::TableHeadersRow();
				for (const auto& value : sorted)
				{
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
		auto& profiler = FrameProfiler::Get();
		if (open && !*open) { profiler.SetRecording(false); return; }
		ImGui::SetNextWindowSize(ImVec2(800, 680), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Profiler", open)) { ImGui::End(); return; }
		bool recording = profiler.IsRecording();
		if (ImGui::Checkbox("Record", &recording)) profiler.SetRecording(recording);
		ImGui::SameLine();
		ImGui::Checkbox("Follow latest", &m_FollowLatest);
		ImGui::SameLine();
		if (ImGui::Button("Clear")) { profiler.Clear(); m_SelectedFrame = 0; }
		ImGui::SameLine();
		if (ImGui::Button("Export CPU trace"))
		{
			const auto path = FileDialogs::SaveFile("JSON trace (*.json)\0*.json\0");
			if (!path.empty()) m_ExportMessage = profiler.ExportTrace(path) ? "Trace exported." : "Could not write trace.";
		}
		if (!m_ExportMessage.empty()) ImGui::TextUnformatted(m_ExportMessage.c_str());
		ImGui::TextDisabled("240 frames, 512 scopes/frame. Closing this panel stops capture.");
		const auto summaries = profiler.Summaries();
		if (!summaries.empty())
		{
			std::vector<float> cpu, gpu, resources;
			for (const auto& frame : summaries)
			{
				cpu.push_back(static_cast<float>(frame.CpuMilliseconds));
				gpu.push_back(static_cast<float>((std::max)(0.0, frame.GpuMilliseconds)));
				resources.push_back(static_cast<float>((frame.Resources.TextureBytes + frame.Resources.BufferBytes + frame.Resources.FramebufferBytes) / MiB));
			}
			ImGui::PlotLines("CPU frame (ms)", cpu.data(), static_cast<int>(cpu.size()), 0, nullptr, 0, FLT_MAX, ImVec2(0, 65));
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
			{
				const float fraction = std::clamp((ImGui::GetIO().MousePos.x - ImGui::GetItemRectMin().x) / (std::max)(1.0f, ImGui::GetItemRectSize().x), 0.0f, 0.9999f);
				m_SelectedFrame = summaries[static_cast<size_t>(fraction * summaries.size())].ID;
				m_FollowLatest = false;
			}
			ImGui::PlotLines("GPU (ms)", gpu.data(), static_cast<int>(gpu.size()), 0, "Missing samples draw at zero", 0, FLT_MAX, ImVec2(0, 55));
			ImGui::PlotLines("Resources (MiB)", resources.data(), static_cast<int>(resources.size()), 0, "Tracked allocation payload", 0, FLT_MAX, ImVec2(0, 45));
			if (m_FollowLatest || !m_SelectedFrame || m_SelectedFrame < summaries.front().ID) m_SelectedFrame = summaries.back().ID;
			int selected = 0;
			for (size_t i = 0; i < summaries.size(); ++i) if (summaries[i].ID == m_SelectedFrame) selected = static_cast<int>(i);
			if (ImGui::SliderInt("Frame in capture", &selected, 0, static_cast<int>(summaries.size()) - 1))
			{
				m_SelectedFrame = summaries[selected].ID;
				m_FollowLatest = false;
			}
			FrameProfile frame;
			if (profiler.ReadFrame(m_SelectedFrame, frame))
			{
				ImGui::Text("Frame %llu | CPU %.3f ms | Delta %.3f ms | Draws %u | Submitted elements %u", static_cast<unsigned long long>(frame.ID), frame.CpuMilliseconds, frame.DeltaMilliseconds, frame.DrawCalls, frame.SubmittedVertices);
				if (frame.GpuMilliseconds >= 0) ImGui::Text("GPU elapsed %.3f ms (main context, excludes Present)", frame.GpuMilliseconds);
				else ImGui::TextDisabled(Renderer::SupportsGpuProfiling() ? "GPU sample pending or skipped (query ring busy)." : "GPU timer queries are unavailable on this platform.");
				ImGui::TextDisabled("CPU includes Present/VSync; engine draw count excludes ImGui. GPU elapsed can include queue idle time.");
				if (frame.DroppedSamples) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%u scope samples omitted: per-frame limit reached.", frame.DroppedSamples);
				if (ImGui::CollapsingHeader("CPU timeline and scopes", ImGuiTreeNodeFlags_DefaultOpen)) { DrawTimeline(frame); DrawScopeTable(frame); }
			}
		}
		else ImGui::TextWrapped("Enable Record, reproduce the slowdown, then turn Record off to inspect frames. Click the CPU graph or use the frame slider.");

		if (ImGui::CollapsingHeader("Memory and resources", ImGuiTreeNodeFlags_DefaultOpen))
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
		if (ImGui::CollapsingHeader("C# debugger"))
		{
#ifdef TC_PLATFORM_WINDOWS
			ImGui::Text("Attach managed (.NET) debugger to PID %lu", static_cast<unsigned long>(GetCurrentProcessId()));
#endif
			ImGui::TextWrapped("After the first Play initializes .NET, attach Visual Studio to TomCatInut.exe with managed .NET code selected. Open the project's script source, set a breakpoint, and enter Play again for OnCreate. Portable symbols are loaded together with Assembly-CSharp. See docs/DEBUGGING_AND_PROFILING.md for source mapping and symbol troubleshooting.");
		}
		ImGui::End();
		if (open && !*open) profiler.SetRecording(false);
	}
}
