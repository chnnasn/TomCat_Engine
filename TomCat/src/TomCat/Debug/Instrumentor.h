#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace TomCat {

	using FloatingPointMicroseconds = std::chrono::duration<double, std::micro>;

	struct ProfileResult
	{
		std::string Name;

		FloatingPointMicroseconds Start;
		std::chrono::microseconds ElapsedTime;
		std::thread::id ThreadID;
	};

	struct InstrumentationSession
	{
		std::string Name;
	};

	class Instrumentor
	{
	public:
		Instrumentor(const Instrumentor&) = delete;
		Instrumentor(Instrumentor&&) = delete;

		void BeginSession(const std::string& name,
			const std::filesystem::path& filepath = "results.json")
		{
			m_SessionActive.store(false, std::memory_order_release);
			std::lock_guard lock(m_Mutex);
			if (m_CurrentSession)
			{
				// If there is already a current session, then close it before beginning new one.
				// Subsequent profiling output meant for the original session will end up in the
				// newly opened session instead.  That's better than having badly formatted
				// profiling output.
				if (Log::GetCoreLogger()) // Edge case: BeginSession() might be before Log::Init()
				{
					TC_Core_Error("Instrumentor::BeginSession('{0}') when session '{1}' already open.", name, m_CurrentSession->Name);
				}
				InternalEndSession();
			}
			m_OutputStream.open(filepath);

			if (m_OutputStream.is_open())
			{
				m_CurrentSession = new InstrumentationSession({ name });
				m_WrittenBytes = 0;
				m_BytesSinceFlush = 0;
				WriteHeader();
				m_SessionActive.store(true, std::memory_order_release);
			}
			else
			{
				if (Log::GetCoreLogger()) // Edge case: BeginSession() might be before Log::Init()
				{
					TC_Core_Error("Instrumentor could not open results file '{0}'.",
						filepath.string());
				}
			}
		}

		void EndSession()
		{
			m_SessionActive.store(false, std::memory_order_release);
			std::lock_guard lock(m_Mutex);
			InternalEndSession();
		}

		[[nodiscard]] bool IsSessionActive() const noexcept
		{
			return m_SessionActive.load(std::memory_order_acquire);
		}

		void WriteProfile(const ProfileResult& result)
		{
			// The macros remain compiled into development and Release builds so a
			// shipped game can opt in to profiling. The inactive path must stay cheap:
			// do not format JSON or take the global mutex unless a session is open.
			if (!IsSessionActive())
				return;

			std::stringstream json;

			json << std::setprecision(3) << std::fixed;
			json << ",{";
			json << "\"cat\":\"function\",";
			json << "\"dur\":" << (result.ElapsedTime.count()) << ',';
			json << "\"name\":\"" << result.Name << "\",";
			json << "\"ph\":\"X\",";
			json << "\"pid\":0,";
			json << "\"tid\":" << result.ThreadID << ",";
			json << "\"ts\":" << result.Start.count();
			json << "}";

			const std::string record = json.str();
			std::lock_guard lock(m_Mutex);
			if (m_CurrentSession && IsSessionActive())
			{
				// Always leave room for the JSON footer. Reaching the hard limit closes
				// the trace as a valid document and atomically disables further events.
				if (m_WrittenBytes + record.size() + FooterSize > MaxSessionBytes)
				{
					InternalEndSession();
					return;
				}
				m_OutputStream << record;
				m_WrittenBytes += record.size();
				m_BytesSinceFlush += record.size();
				if (m_BytesSinceFlush >= FlushIntervalBytes)
				{
					m_OutputStream.flush();
					m_BytesSinceFlush = 0;
				}
			}
		}

		static Instrumentor& Get()
		{
			static Instrumentor instance;
			return instance;
		}
	private:
		Instrumentor()
			: m_CurrentSession(nullptr)
		{
		}

		~Instrumentor()
		{
			EndSession();
		}

		void WriteHeader()
		{
			constexpr std::string_view header = "{\"otherData\": {},\"traceEvents\":[{}";
			m_OutputStream << header;
			m_WrittenBytes += header.size();
			m_OutputStream.flush();
		}

		void WriteFooter()
		{
			m_OutputStream << "]}";
			m_WrittenBytes += FooterSize;
			m_OutputStream.flush();
		}

		// Note: you must already own lock on m_Mutex before
		// calling InternalEndSession()
		void InternalEndSession()
		{
			m_SessionActive.store(false, std::memory_order_release);
			if (m_CurrentSession)
			{
				WriteFooter();
				m_OutputStream.close();
				delete m_CurrentSession;
				m_CurrentSession = nullptr;
			}
		}
	private:
		static constexpr size_t MaxSessionBytes = 64ull * 1024ull * 1024ull;
		static constexpr size_t FlushIntervalBytes = 256ull * 1024ull;
		static constexpr size_t FooterSize = 2;
		std::mutex m_Mutex;
		std::atomic<bool> m_SessionActive{ false };
		InstrumentationSession* m_CurrentSession;
		std::ofstream m_OutputStream;
		size_t m_WrittenBytes = 0;
		size_t m_BytesSinceFlush = 0;
	};

	class InstrumentationTimer
	{
	public:
		InstrumentationTimer(const char* name)
			: m_Name(name),
			  m_Enabled(Instrumentor::Get().IsSessionActive()),
			  m_Stopped(!m_Enabled)
		{
			if (m_Enabled)
				m_StartTimepoint = std::chrono::steady_clock::now();
		}

		~InstrumentationTimer()
		{
			if (!m_Stopped)
				Stop();
		}

		void Stop()
		{
			if (m_Stopped)
				return;
			auto endTimepoint = std::chrono::steady_clock::now();
			auto highResStart = FloatingPointMicroseconds{ m_StartTimepoint.time_since_epoch() };
			auto elapsedTime = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch() - std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch();

			Instrumentor::Get().WriteProfile({ m_Name, highResStart, elapsedTime, std::this_thread::get_id() });

			m_Stopped = true;
		}
	private:
		const char* m_Name;
		std::chrono::time_point<std::chrono::steady_clock> m_StartTimepoint;
		bool m_Enabled;
		bool m_Stopped;
	};

	namespace InstrumentorUtils {

		template <size_t N>
		struct ChangeResult
		{
			char Data[N];
		};

		template <size_t N, size_t K>
		constexpr auto CleanupOutputString(const char(&expr)[N], const char(&remove)[K])
		{
			ChangeResult<N> result = {};

			size_t srcIndex = 0;
			size_t dstIndex = 0;
			while (srcIndex < N)
			{
				size_t matchIndex = 0;
				while (matchIndex < K - 1 && srcIndex + matchIndex < N - 1 && expr[srcIndex + matchIndex] == remove[matchIndex])
					matchIndex++;
				if (matchIndex == K - 1)
					srcIndex += matchIndex;
				result.Data[dstIndex++] = expr[srcIndex] == '"' ? '\'' : expr[srcIndex];
				srcIndex++;
			}
			return result;
		}
	}
}

#ifndef TC_PROFILE
	#if defined(TC_DIST)
		#define TC_PROFILE 0
	#else
		#define TC_PROFILE 1
	#endif
#endif
#if TC_PROFILE
// Resolve which function signature macro will be used. Note that this only
// is resolved when the (pre)compiler starts, so the syntax highlighting
// could mark the wrong one in your editor!
#if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) || (defined(__ICC) && (__ICC >= 600)) || defined(__ghs__)
#define TC_FUNC_SIG __PRETTY_FUNCTION__
#elif defined(__DMC__) && (__DMC__ >= 0x810)
#define TC_FUNC_SIG __PRETTY_FUNCTION__
#elif (defined(__FUNCSIG__) || (_MSC_VER))
#define TC_FUNC_SIG __FUNCSIG__
#elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
#define TC_FUNC_SIG __FUNCTION__
#elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
#define TC_FUNC_SIG __FUNC__
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
#define TC_FUNC_SIG __func__
#elif defined(__cplusplus) && (__cplusplus >= 201103)
#define TC_FUNC_SIG __func__
#else
#define TC_FUNC_SIG "TC_FUNC_SIG unknown!"
#endif

#define TC_PROFILE_BEGIN_SESSION(name, filepath) ::TomCat::Instrumentor::Get().BeginSession(name, filepath)
#define TC_PROFILE_END_SESSION() ::TomCat::Instrumentor::Get().EndSession()
#define TC_PROFILE_SCOPE_LINE2(name, line) constexpr auto fixedName##line = ::TomCat::InstrumentorUtils::CleanupOutputString(name, "__cdecl ");\
											   ::TomCat::InstrumentationTimer timer##line(fixedName##line.Data)
#define TC_PROFILE_SCOPE_LINE(name, line) TC_PROFILE_SCOPE_LINE2(name, line)
#define TC_PROFILE_SCOPE(name) TC_PROFILE_SCOPE_LINE(name, __LINE__)
#define TC_PROFILE_FUNCTION() TC_PROFILE_SCOPE(TC_FUNC_SIG)
#else
#define TC_PROFILE_BEGIN_SESSION(name, filepath)
#define TC_PROFILE_END_SESSION()
#define TC_PROFILE_SCOPE(name)
#define TC_PROFILE_FUNCTION()
#endif
