#include "tcpch.h"
#include "OpenGLProfiler.h"
#include "OpenGLApi.h"
#include "TomCat/Debug/FrameProfiler.h"

namespace TomCat {
#ifndef TC_PLATFORM_WEB
	namespace {
		struct QuerySlot { GLuint Query = 0; uint64_t Frame = 0; bool Pending = false; };
		std::array<QuerySlot, 4> s_Queries;
		int s_Active = -1;
	}
#endif
	bool OpenGLProfiler::IsSupported()
	{
#ifdef TC_PLATFORM_WEB
		return false;
#else
		return glGenQueries && glBeginQuery && glEndQuery && glGetQueryObjectiv && glGetQueryObjectui64v;
#endif
	}

	void OpenGLProfiler::BeginFrame(uint64_t frame)
	{
#ifndef TC_PLATFORM_WEB
		if (!IsSupported()) return;
		for (auto& slot : s_Queries)
		{
			if (!slot.Pending) continue;
			GLint ready = GL_FALSE;
			glGetQueryObjectiv(slot.Query, GL_QUERY_RESULT_AVAILABLE, &ready);
			if (!ready) continue;
			GLuint64 nanoseconds = 0;
			glGetQueryObjectui64v(slot.Query, GL_QUERY_RESULT, &nanoseconds);
			FrameProfiler::Get().SetGpuTime(slot.Frame, static_cast<double>(nanoseconds) / 1000000.0);
			slot.Pending = false;
		}
		if (!frame || s_Active >= 0) return;
		for (size_t i = 0; i < s_Queries.size(); ++i)
		{
			auto& slot = s_Queries[i];
			if (slot.Pending) continue;
			if (!slot.Query) glGenQueries(1, &slot.Query);
			if (!slot.Query) return;
			slot.Frame = frame;
			glBeginQuery(GL_TIME_ELAPSED, slot.Query);
			s_Active = static_cast<int>(i);
			break;
		}
#endif
	}

	void OpenGLProfiler::EndFrame()
	{
#ifndef TC_PLATFORM_WEB
		if (s_Active < 0) return;
		glEndQuery(GL_TIME_ELAPSED);
		s_Queries[s_Active].Pending = true;
		s_Active = -1;
#endif
	}

	void OpenGLProfiler::Shutdown()
	{
#ifndef TC_PLATFORM_WEB
		EndFrame();
		for (auto& slot : s_Queries)
		{
			if (slot.Query) glDeleteQueries(1, &slot.Query);
			slot = {};
		}
#endif
	}
}
