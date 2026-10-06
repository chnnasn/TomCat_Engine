#pragma once

#include "TomCat/Debug/FrameProfiler.h"
#include <array>

namespace TomCat {

	class ProfilerPanel
	{
	public:
		void OnImGuiRender(bool* open, bool externalCapture = false);
        void OnPlayStarted() { if (m_ClearOnPlay) { FrameProfiler::Get().Clear(); m_SelectedFrame=0; } }
	private:
		uint64_t m_SelectedFrame = 0;
        double m_NextChartRefresh = 0;
        std::vector<FrameProfileSummary> m_Summaries;
        FrameProfile m_DisplayedFrame;
        bool m_HasDisplayedFrame = false;
        std::array<bool,4> m_Modules{true,true,true,true};
        int m_SelectedModule=0, m_DetailsView=0;
        float m_OverviewRatio=0.46f;
        bool m_ClearOnPlay=true;
        char m_ScopeSearch[160]{};
		bool m_FollowLatest = true;
		bool m_HasBaseline = false;
		ProfileResources m_Baseline;
		uint64_t m_WorkingSet = 0, m_PrivateBytes = 0, m_PeakWorkingSet = 0;
		uint64_t m_BaselineWorkingSet = 0, m_BaselinePrivateBytes = 0;
		double m_LastMemorySample = -1;
		bool m_ProcessMemoryAvailable = false;
		std::string m_ExportMessage;
	};
}
