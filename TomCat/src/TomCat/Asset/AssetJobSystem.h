#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace TomCat {

	// One bounded executor for import, load and preload work. Reservations cover
	// queued and running jobs so known working sets are admission-controlled. The
	// memory budget is not an allocator cap: a reservation larger than the budget
	// consumes the whole admission budget and runs alone. Task implementations
	// must provide conservative estimates of their real peak allocation.
	class AssetJobSystem final
	{
	public:
		struct Limits
		{
			uint32_t WorkerCount = 0;
			size_t MaximumQueuedJobs = 64;
			uint64_t MemoryBudgetBytes = 512ULL * 1024ULL * 1024ULL;
		};

		static AssetJobSystem& Get();

		AssetJobSystem(const AssetJobSystem&) = delete;
		AssetJobSystem& operator=(const AssetJobSystem&) = delete;

		void Configure(Limits limits);
		Limits GetLimits() const;
		void Shutdown();
		// Non-blocking submission for latency-sensitive producers such as the
		// render-thread texture streamer. Returns false when either the queue or
		// memory reservation is full; callers can retain the work in a backlog and
		// retry from a later frame. Since this API has no future, an exception that
		// escapes a queued callback is reported at the worker boundary.
		bool TrySchedule(uint64_t reservationBytes, std::function<void()> function);

		template<typename Function>
		auto Submit(uint64_t reservationBytes, Function&& function)
			-> std::future<std::invoke_result_t<std::decay_t<Function>>>
		{
			using Result = std::invoke_result_t<std::decay_t<Function>>;
			auto task = std::make_shared<std::packaged_task<Result()>>(
				std::forward<Function>(function));
			std::future<Result> future = task->get_future();
			Enqueue(reservationBytes, [task]() { (*task)(); });
			return future;
		}

		bool IsWorkerThread() const noexcept;

	private:
		struct Job
		{
			uint64_t ReservationBytes = 0;
			std::function<void()> Execute;
		};

		AssetJobSystem();
		~AssetJobSystem();
		void Start();
		void StopAndJoin();
		void Enqueue(uint64_t reservationBytes, std::function<void()> function);
		void WorkerLoop();

		mutable std::mutex m_Mutex;
		// Serializes Configure/Shutdown across the unlocked join interval.
		std::mutex m_LifecycleMutex;
		std::condition_variable m_WorkAvailable;
		std::condition_variable m_CapacityAvailable;
		Limits m_Limits;
		std::deque<Job> m_Jobs;
		std::vector<std::thread> m_Workers;
		uint64_t m_ReservedBytes = 0;
		bool m_Stopping = false;
	};

}
