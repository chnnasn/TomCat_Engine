#pragma once

#include "TomCat/Debug/FrameProfiler.h"

namespace TomCat {

	class ProfilerPanel
	{
	public:
		void OnImGuiRender(bool* open);
	private:
		uint64_t m_SelectedFrame = 0;
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
