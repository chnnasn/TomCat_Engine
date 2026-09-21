#include "TomCat/Debug/FrameProfiler.h"
#include "platform/OpenGL/OpenGLProfiler.h"
#include <glad/glad.h>

#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

// Exercise the real query-ring implementation without a GPU or window. A query
// result getter before availability is an error, just as it would stall a driver.
PFNGLGENQUERIESPROC glad_glGenQueries = nullptr;
PFNGLBEGINQUERYPROC glad_glBeginQuery = nullptr;
PFNGLENDQUERYPROC glad_glEndQuery = nullptr;
PFNGLGETQUERYOBJECTIVPROC glad_glGetQueryObjectiv = nullptr;
PFNGLGETQUERYOBJECTUI64VPROC glad_glGetQueryObjectui64v = nullptr;
PFNGLDELETEQUERIESPROC glad_glDeleteQueries = nullptr;

namespace {
	void Require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}
	GLuint nextQuery = 0;
	uint32_t begins = 0, ends = 0, results = 0, deletes = 0;
	bool ready = false;
	void APIENTRY GenQueries(GLsizei count, GLuint* queries) { while (count--) *queries++ = ++nextQuery; }
	void APIENTRY BeginQuery(GLenum, GLuint) { ++begins; }
	void APIENTRY EndQuery(GLenum) { ++ends; }
	void APIENTRY Available(GLuint, GLenum, GLint* value) { *value = ready ? GL_TRUE : GL_FALSE; }
	void APIENTRY Result(GLuint, GLenum, GLuint64* value)
	{
		Require(ready, "GPU result read without availability would block the renderer");
		++results; *value = 2500000;
	}
	void APIENTRY DeleteQueries(GLsizei count, const GLuint*) { deletes += static_cast<uint32_t>(count); }
	void TestGpuRing()
	{
		auto& profiler = TomCat::FrameProfiler::Get();
		Require(!TomCat::OpenGLProfiler::IsSupported(), "absent GPU query API reported supported");
		TomCat::OpenGLProfiler::BeginFrame(0);
		glad_glGenQueries = GenQueries; glad_glBeginQuery = BeginQuery;
		glad_glEndQuery = EndQuery; glad_glGetQueryObjectiv = Available;
		glad_glGetQueryObjectui64v = Result; glad_glDeleteQueries = DeleteQueries;
		Require(TomCat::OpenGLProfiler::IsSupported(), "available GPU query API not detected");
		profiler.SetRecording(true);
		std::vector<uint64_t> frames;
		for (int i = 0; i < 5; ++i)
		{
			const auto id = profiler.BeginFrame(0.016);
			frames.push_back(id);
			TomCat::OpenGLProfiler::BeginFrame(id);
			TomCat::OpenGLProfiler::EndFrame();
			profiler.EndFrame(id);
		}
		Require(begins == 4 && ends == 4 && results == 0, "full GPU ring did not skip without blocking");
		ready = true;
		TomCat::OpenGLProfiler::BeginFrame(0);
		Require(results == 4 && begins == 4, "pending GPU queries failed to drain while capture is paused");
		TomCat::FrameProfile frame;
		for (size_t i = 0; i < frames.size(); ++i)
		{
			profiler.ReadFrame(frames[i], frame);
			Require(i == 4 ? frame.GpuMilliseconds < 0 : frame.GpuMilliseconds == 2.5, "GPU sample attached to wrong frame");
		}
		TomCat::OpenGLProfiler::Shutdown();
		Require(deletes == 4, "GPU queries leaked at shutdown");
		profiler.Clear();
		profiler.SetRecording(false);
	}
}

int main()
{
	try
	{
		auto& profiler = TomCat::FrameProfiler::Get();
		profiler.Clear();
		Require(profiler.BeginFrame(0.016) == 0, "capture must be opt in");
		profiler.Record(0, "disabled", 0, 0, 0, 0);
		Require(profiler.Summaries().empty(), "disabled capture retained an event");
		profiler.SetRecording(true);
		const uint64_t first = profiler.BeginFrame(0.016);
		const double start = TomCat::FrameProfiler::NowMicroseconds();
		std::thread worker([&] {
			for (size_t i = 0; i < TomCat::FrameProfiler::MaxSamplesPerFrame + 7; ++i)
				profiler.Record(first, std::string(300, 'x'), start, 250, 2, 1);
		});
		worker.join();
		profiler.RecordDraw(6);
		profiler.RecordDraw(12);
		profiler.EndFrame(first);
		TomCat::FrameProfile frame;
		Require(profiler.ReadFrame(first, frame), "completed frame missing");
		Require(frame.Samples.size() == TomCat::FrameProfiler::MaxSamplesPerFrame, "scope storage is not bounded");
		Require(frame.Samples.front().Name.size() == TomCat::FrameProfiler::MaxNameBytes, "scope names are not bounded");
		Require(frame.DroppedSamples == 7 && frame.DrawCalls == 2 && frame.SubmittedVertices == 18, "frame counters incorrect");
		Require(frame.GpuMilliseconds < 0, "missing GPU query must not appear as zero");
		profiler.SetGpuTime(first, 3.25);
		profiler.ReadFrame(first, frame);
		Require(frame.GpuMilliseconds == 3.25, "asynchronous GPU result not joined to frame");
		profiler.SetGpuTime(first, std::numeric_limits<double>::quiet_NaN());
		profiler.ReadFrame(first, frame);
		Require(frame.GpuMilliseconds == 3.25, "invalid GPU result accepted");

		const uint64_t second = profiler.BeginFrame(0.032);
		profiler.Record(first, "late worker", start, 900, 2, 0);
		profiler.Record(second, "quoted\" scope\\line\n", start, 50, 1, 0);
		profiler.EndFrame(first);
		Require(profiler.ActiveFrame() == second, "stale end closed a different frame");
		profiler.EndFrame(second);
		profiler.ReadFrame(second, frame);
		Require(frame.Samples.size() == 1, "late worker event attributed to next frame");
		const auto trace = std::filesystem::temp_directory_path() / ("tomcat-profiler-" + std::to_string(first) + "-" + std::to_string(static_cast<uint64_t>(start)) + ".json");
		Require(profiler.ExportTrace(trace), "trace export failed");
		std::ifstream input(trace, std::ios::binary);
		const std::string json((std::istreambuf_iterator<char>(input)), {});
		input.close();
		std::filesystem::remove(trace);
		Require(json.find("quoted\\\" scope\\\\line\\u000a") != std::string::npos, "trace names are not JSON escaped");
		Require(json.starts_with("{\"displayTimeUnit\":") && json.ends_with("]}"), "trace container malformed");

		for (size_t i = 0; i < TomCat::FrameProfiler::MaxFrames + 2; ++i)
		{
			const auto id = profiler.BeginFrame(0.016);
			profiler.EndFrame(id);
		}
		Require(profiler.Summaries().size() == TomCat::FrameProfiler::MaxFrames, "frame history is not bounded");
		Require(!profiler.ReadFrame(first, frame), "old frames not evicted");
		const uint64_t beforeClear = profiler.BeginFrame(0.016);
		profiler.Clear();
		const uint64_t afterClear = profiler.BeginFrame(0.016);
		Require(afterClear > beforeClear, "clear reused frame identifiers");
		profiler.Record(beforeClear, "stale after clear", start, 1, 1, 0);
		profiler.EndFrame(afterClear);
		profiler.ReadFrame(afterClear, frame);
		Require(frame.Samples.empty(), "cleared capture accepted stale event");
		profiler.SetRecording(false);
		Require(profiler.BeginFrame(0.016) == 0, "pause did not disable new frames");

		auto& resources = TomCat::ProfileResourceTracker::Get();
		const auto baseline = resources.Snapshot();
		resources.Texture(1, 1024); resources.Buffer(1, 2048); resources.Framebuffer(1, 4096);
		auto allocation = resources.Snapshot();
		Require(allocation.TextureBytes == baseline.TextureBytes + 1024 && allocation.BufferBytes == baseline.BufferBytes + 2048 && allocation.FramebufferBytes == baseline.FramebufferBytes + 4096, "resource allocation counts incorrect");
		resources.Framebuffer(0, 4096);
		resources.Texture(-1, -1024); resources.Buffer(-1, -2048); resources.Framebuffer(-1, -8192);
		allocation = resources.Snapshot();
		Require(allocation.TextureCount == baseline.TextureCount && allocation.BufferCount == baseline.BufferCount && allocation.FramebufferCount == baseline.FramebufferCount && allocation.FramebufferBytes == baseline.FramebufferBytes, "resource unload/resize accounting did not return to baseline");
		profiler.Clear();
		TestGpuRing();
		std::cout << "PASS Profiler: bounded capture, concurrent scopes, stale-frame isolation, nonblocking GPU query ring, trace escaping, resource release accounting\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAIL Profiler: " << error.what() << '\n';
		return 1;
	}
}
