#include "tcpch.h"
#include "AssetJobSystem.h"

#include <algorithm>
#include <stdexcept>

namespace TomCat {

	namespace {
		thread_local bool s_IsAssetWorker = false;
		thread_local uint64_t s_CurrentAssetReservation = 0;

		class NestedReservationScope final
		{
		public:
			explicit NestedReservationScope(uint64_t reservation) noexcept
				: m_Previous(s_CurrentAssetReservation)
			{
				s_CurrentAssetReservation = reservation;
			}
			~NestedReservationScope()
			{
				s_CurrentAssetReservation = m_Previous;
			}

		private:
			uint64_t m_Previous;
		};

		AssetJobSystem::Limits Normalize(AssetJobSystem::Limits limits)
		{
			const uint32_t hardware = std::thread::hardware_concurrency();
			if (limits.WorkerCount == 0)
				limits.WorkerCount = std::clamp(hardware > 1 ? hardware - 1 : 1u,
					1u, 8u);
			limits.WorkerCount = std::clamp(limits.WorkerCount, 1u, 64u);
			limits.MaximumQueuedJobs = std::max<size_t>(limits.MaximumQueuedJobs, 1);
			limits.MemoryBudgetBytes = std::max<uint64_t>(limits.MemoryBudgetBytes,
				1024ULL * 1024ULL);
			return limits;
		}
	}

	AssetJobSystem& AssetJobSystem::Get()
	{
		static AssetJobSystem instance;
		return instance;
	}

	AssetJobSystem::AssetJobSystem()
		: m_Limits(Normalize({}))
	{
		Start();
	}

	AssetJobSystem::~AssetJobSystem()
	{
		Shutdown();
	}

	void AssetJobSystem::Start()
	{
		std::unique_lock lock(m_Mutex);
		m_Stopping = false;
		try
		{
			m_Workers.reserve(m_Limits.WorkerCount);
			for (uint32_t index = 0; index < m_Limits.WorkerCount; ++index)
				m_Workers.emplace_back([this]() { WorkerLoop(); });
		}
		catch (...)
		{
			// A partially-created pool must be joined before the vector unwinds;
			// otherwise a thread-construction failure would terminate the process.
			m_Stopping = true;
			lock.unlock();
			m_WorkAvailable.notify_all();
			for (std::thread& worker : m_Workers)
				if (worker.joinable())
					worker.join();
			lock.lock();
			m_Workers.clear();
			throw;
		}
	}

	void AssetJobSystem::Configure(Limits limits)
	{
		if (IsWorkerThread())
			throw std::logic_error("asset workers cannot reconfigure their own executor");
		limits = Normalize(limits);
		std::scoped_lock lifecycleLock(m_LifecycleMutex);
		StopAndJoin();
		{
			std::lock_guard lock(m_Mutex);
			m_Limits = limits;
		}
		Start();
	}

	AssetJobSystem::Limits AssetJobSystem::GetLimits() const
	{
		std::lock_guard lock(m_Mutex);
		return m_Limits;
	}

	bool AssetJobSystem::TrySchedule(uint64_t reservationBytes,
		std::function<void()> function)
	{
		if (!function)
			return false;
		if (s_IsAssetWorker)
		{
			uint64_t reservation = 0;
			{
				std::lock_guard lock(m_Mutex);
				reservation = std::min(reservationBytes,
					m_Limits.MemoryBudgetBytes);
			}
			if (reservation > s_CurrentAssetReservation)
				return false;
			NestedReservationScope reservationScope(reservation);
			function();
			return true;
		}
		std::lock_guard lock(m_Mutex);
		if (m_Stopping)
			return false;
		const uint64_t reservation = std::min(reservationBytes,
			m_Limits.MemoryBudgetBytes);
		if (m_Jobs.size() >= m_Limits.MaximumQueuedJobs
			|| reservation > m_Limits.MemoryBudgetBytes - m_ReservedBytes)
			return false;
		m_Jobs.push_back({ reservation, std::move(function) });
		m_ReservedBytes += reservation;
		m_WorkAvailable.notify_one();
		return true;
	}

	void AssetJobSystem::Shutdown()
	{
		if (IsWorkerThread())
			throw std::logic_error("asset workers cannot shut down their own executor");
		std::scoped_lock lifecycleLock(m_LifecycleMutex);
		StopAndJoin();
	}

	void AssetJobSystem::StopAndJoin()
	{
		{
			std::lock_guard lock(m_Mutex);
			m_Stopping = true;
			if (m_Workers.empty())
			{
				m_Jobs.clear();
				m_ReservedBytes = 0;
				return;
			}
		}
		m_WorkAvailable.notify_all();
		m_CapacityAvailable.notify_all();
		for (std::thread& worker : m_Workers)
			if (worker.joinable())
				worker.join();
		std::lock_guard lock(m_Mutex);
		m_Workers.clear();
		m_Jobs.clear();
		m_ReservedBytes = 0;
	}

	void AssetJobSystem::Enqueue(uint64_t reservationBytes,
		std::function<void()> function)
	{
		if (!function)
			throw std::invalid_argument("Asset job function is empty");
		// A worker may synchronously request a dependent load. Execute it inline to
		// avoid a pool-wide dependency deadlock. Nested work shares the parent's
		// reservation and may not silently claim more memory than the parent holds.
		if (s_IsAssetWorker)
		{
			uint64_t reservation = 0;
			{
				std::lock_guard lock(m_Mutex);
				reservation = std::min(reservationBytes,
					m_Limits.MemoryBudgetBytes);
			}
			if (reservation > s_CurrentAssetReservation)
				throw std::runtime_error(
					"nested asset job reservation exceeds its parent reservation");
			NestedReservationScope reservationScope(reservation);
			function();
			return;
		}
		std::unique_lock lock(m_Mutex);
		// An oversized job receives the complete budget and runs exclusively.
		// Clamping is intentional so it cannot wait forever for an impossible
		// reservation, but the caller remains responsible for an honest estimate.
		const uint64_t reservation = std::min(reservationBytes,
			m_Limits.MemoryBudgetBytes);
		m_CapacityAvailable.wait(lock, [this, reservation]()
		{
			return m_Stopping || (m_Jobs.size() < m_Limits.MaximumQueuedJobs
				&& reservation <= m_Limits.MemoryBudgetBytes - m_ReservedBytes);
		});
		if (m_Stopping)
			throw std::runtime_error("Asset job system is shutting down");
		m_Jobs.push_back({ reservation, std::move(function) });
		m_ReservedBytes += reservation;
		lock.unlock();
		m_WorkAvailable.notify_one();
	}

	void AssetJobSystem::WorkerLoop()
	{
		s_IsAssetWorker = true;
		for (;;)
		{
			Job job;
			{
				std::unique_lock lock(m_Mutex);
				m_WorkAvailable.wait(lock,
					[this]() { return m_Stopping || !m_Jobs.empty(); });
				if (m_Stopping && m_Jobs.empty())
					break;
				job = std::move(m_Jobs.front());
				m_Jobs.pop_front();
			}
			// Popping creates queue capacity even though the job's memory remains
			// reserved until completion. Wake producers that were queue-bound now.
			m_CapacityAvailable.notify_all();
			s_CurrentAssetReservation = job.ReservationBytes;
			try { job.Execute(); }
			catch (const std::exception& exception)
			{
				// packaged_task stores Submit exceptions in its future. Only raw
				// fire-and-forget callbacks normally reach this boundary.
				try
				{
					if (const auto logger = Log::GetCoreLogger())
						logger->error("Unhandled asset job exception: {0}", exception.what());
				}
				catch (...) {}
			}
			catch (...)
			{
				try
				{
					if (const auto logger = Log::GetCoreLogger())
						logger->error("Unhandled non-standard asset job exception");
				}
				catch (...) {}
			}
			s_CurrentAssetReservation = 0;
			{
				std::lock_guard lock(m_Mutex);
				m_ReservedBytes -= job.ReservationBytes;
			}
			m_CapacityAvailable.notify_all();
		}
		s_IsAssetWorker = false;
	}

	bool AssetJobSystem::IsWorkerThread() const noexcept
	{
		return s_IsAssetWorker;
	}

}
