#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace TomCat {

	struct ProfileResources
	{
		uint64_t TextureCount = 0, TextureBytes = 0;
		uint64_t BufferCount = 0, BufferBytes = 0;
		uint64_t FramebufferCount = 0, FramebufferBytes = 0;
	};

	// Allocation payload estimates, not driver residency. Track all allocations,
	// including those made before capture starts; updates happen on resource changes.
	class ProfileResourceTracker
	{
	public:
		static ProfileResourceTracker& Get() { static ProfileResourceTracker tracker; return tracker; }
		void Texture(int64_t count, int64_t bytes) { Adjust(m_TextureCount, count); Adjust(m_TextureBytes, bytes); }
		void Buffer(int64_t count, int64_t bytes) { Adjust(m_BufferCount, count); Adjust(m_BufferBytes, bytes); }
		void Framebuffer(int64_t count, int64_t bytes) { Adjust(m_FramebufferCount, count); Adjust(m_FramebufferBytes, bytes); }
		ProfileResources Snapshot() const
		{
			return { m_TextureCount.load(), m_TextureBytes.load(), m_BufferCount.load(),
				m_BufferBytes.load(), m_FramebufferCount.load(), m_FramebufferBytes.load() };
		}
	private:
		static void Adjust(std::atomic<uint64_t>& counter, int64_t delta)
		{
			if (delta >= 0) counter.fetch_add(static_cast<uint64_t>(delta), std::memory_order_relaxed);
			else counter.fetch_sub(static_cast<uint64_t>(-delta), std::memory_order_relaxed);
		}
		std::atomic<uint64_t> m_TextureCount{ 0 }, m_TextureBytes{ 0 };
		std::atomic<uint64_t> m_BufferCount{ 0 }, m_BufferBytes{ 0 };
		std::atomic<uint64_t> m_FramebufferCount{ 0 }, m_FramebufferBytes{ 0 };
	};

	struct FrameProfileSample
	{
		std::string Name;
		double StartMilliseconds = 0, DurationMilliseconds = 0;
		uint64_t Thread = 0;
		uint32_t Depth = 0;
	};

	struct FrameProfileSummary
	{
		uint64_t ID = 0;
		double CpuMilliseconds = 0, DeltaMilliseconds = 0;
		// Negative means unsupported, not ready, or query ring exhausted.
		double GpuMilliseconds = -1;
		uint32_t DrawCalls = 0, SubmittedVertices = 0, DroppedSamples = 0;
		ProfileResources Resources;
	};

	struct FrameProfile : FrameProfileSummary
	{
		double StartMicroseconds = 0;
		std::vector<FrameProfileSample> Samples;
	};

	// A bounded, opt-in capture. Scope events are assigned by frame ID, so late
	// worker events cannot be attributed to a subsequent frame or capture session.
	class FrameProfiler
	{
	public:
		static constexpr size_t MaxFrames = 240;
		static constexpr size_t MaxSamplesPerFrame = 512;
		static constexpr size_t MaxNameBytes = 160;
		static FrameProfiler& Get() { static FrameProfiler profiler; return profiler; }
		static double NowMicroseconds()
		{
			return std::chrono::duration<double, std::micro>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
		}
		bool IsRecording() const { return m_Recording.load(std::memory_order_relaxed); }
		uint64_t ActiveFrame() const { return m_ActiveID.load(std::memory_order_relaxed); }
		void SetRecording(bool recording) { m_Recording.store(recording, std::memory_order_relaxed); }
		uint64_t BeginFrame(double deltaSeconds)
		{
			if (!IsRecording()) return 0;
			std::lock_guard lock(m_Mutex);
			m_Current = {};
			m_Current.ID = ++m_NextID;
			m_Current.StartMicroseconds = NowMicroseconds();
			m_Current.DeltaMilliseconds = std::isfinite(deltaSeconds) ? (std::max)(0.0, deltaSeconds * 1000.0) : 0;
			m_Current.Samples.reserve(MaxSamplesPerFrame);
			m_ActiveID.store(m_Current.ID, std::memory_order_relaxed);
			return m_Current.ID;
		}
		void EndFrame(uint64_t id)
		{
			if (!id) return;
			const double end = NowMicroseconds();
			std::lock_guard lock(m_Mutex);
			if (m_Current.ID != id || m_ActiveID.load() != id) return;
			m_ActiveID.store(0, std::memory_order_relaxed);
			m_Current.CpuMilliseconds = (std::max)(0.0, (end - m_Current.StartMicroseconds) / 1000.0);
			m_Current.Resources = ProfileResourceTracker::Get().Snapshot();
			if (m_Frames.size() == MaxFrames) m_Frames.pop_front();
			m_Frames.push_back(std::move(m_Current));
			m_Current = {};
		}
		void Record(uint64_t frame, std::string_view name, double startMicroseconds,
			double durationMicroseconds, uint64_t thread, uint32_t depth)
		{
			if (!frame || frame != ActiveFrame()) return;
			if (!std::isfinite(startMicroseconds) || !std::isfinite(durationMicroseconds) || durationMicroseconds < 0) return;
			std::lock_guard lock(m_Mutex);
			if (frame != m_Current.ID || frame != m_ActiveID.load()) return;
			if (m_Current.Samples.size() >= MaxSamplesPerFrame) { ++m_Current.DroppedSamples; return; }
			m_Current.Samples.push_back({ std::string(name.substr(0, MaxNameBytes)),
				(std::max)(0.0, (startMicroseconds - m_Current.StartMicroseconds) / 1000.0),
				durationMicroseconds / 1000.0, thread, depth });
		}
		void RecordDraw(uint32_t vertices)
		{
			if (!ActiveFrame()) return;
			std::lock_guard lock(m_Mutex);
			if (!m_ActiveID.load()) return;
			++m_Current.DrawCalls;
			m_Current.SubmittedVertices += vertices;
		}
		void SetGpuTime(uint64_t frame, double milliseconds)
		{
			if (!std::isfinite(milliseconds) || milliseconds < 0) return;
			std::lock_guard lock(m_Mutex);
			for (auto it = m_Frames.rbegin(); it != m_Frames.rend(); ++it)
				if (it->ID == frame) { it->GpuMilliseconds = milliseconds; return; }
		}
		std::vector<FrameProfileSummary> Summaries() const
		{
			std::lock_guard lock(m_Mutex);
			std::vector<FrameProfileSummary> result;
			result.reserve(m_Frames.size());
			for (const auto& frame : m_Frames) result.push_back(frame);
			return result;
		}
		bool ReadFrame(uint64_t id, FrameProfile& result) const
		{
			std::lock_guard lock(m_Mutex);
			for (const auto& frame : m_Frames)
				if (frame.ID == id) { result = frame; return true; }
			return false;
		}
		void Clear()
		{
			std::lock_guard lock(m_Mutex);
			m_Frames.clear();
			m_ActiveID.store(0, std::memory_order_relaxed);
			m_Current = {};
		}
		bool ExportTrace(const std::filesystem::path& path) const
		{
			std::deque<FrameProfile> frames;
			{ std::lock_guard lock(m_Mutex); frames = m_Frames; }
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output) return false;
			output.imbue(std::locale::classic());
			output << std::fixed << std::setprecision(3) << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
			bool first = true;
			for (const auto& frame : frames)
			{
				for (const auto& sample : frame.Samples)
				{
					if (!first) output << ',';
					first = false;
					output << "{\"name\":\"";
					WriteEscaped(output, sample.Name);
					output << "\",\"cat\":\"CPU\",\"ph\":\"X\",\"pid\":1,\"tid\":" << sample.Thread
						<< ",\"ts\":" << frame.StartMicroseconds + sample.StartMilliseconds * 1000.0
						<< ",\"dur\":" << sample.DurationMilliseconds * 1000.0 << '}';
				}
				if (!first) output << ',';
				first = false;
				output << "{\"name\":\"Frame\",\"ph\":\"X\",\"pid\":2,\"tid\":0,\"ts\":"
					<< frame.StartMicroseconds << ",\"dur\":" << frame.CpuMilliseconds * 1000.0
					<< ",\"args\":{\"frame\":" << frame.ID << ",\"gpuMs\":" << frame.GpuMilliseconds
					<< ",\"drawCalls\":" << frame.DrawCalls << ",\"droppedScopes\":" << frame.DroppedSamples << "}}";
			}
			output << "]}";
			output.flush();
			return output.good();
		}
	private:
		static void WriteEscaped(std::ostream& output, std::string_view value)
		{
			constexpr char hex[] = "0123456789abcdef";
			for (const unsigned char c : value)
			{
				if (c == '"' || c == '\\') output << '\\' << static_cast<char>(c);
				else if (c < 32) output << "\\u00" << hex[c >> 4] << hex[c & 15];
				else output << static_cast<char>(c);
			}
		}
		mutable std::mutex m_Mutex;
		std::atomic<bool> m_Recording{ false };
		std::atomic<uint64_t> m_ActiveID{ 0 };
		uint64_t m_NextID = 0;
		FrameProfile m_Current;
		std::deque<FrameProfile> m_Frames;
	};

	class ScopedProfileFrame
	{
	public:
		explicit ScopedProfileFrame(double delta) : ID(FrameProfiler::Get().BeginFrame(delta)) {}
		~ScopedProfileFrame() { FrameProfiler::Get().EndFrame(ID); }
		const uint64_t ID;
	};
}
