#pragma once
#include <chrono>
#include <coroutine>
#include <exception>
#include <utility>
#include <cstdint>

namespace TomCat {
// Cooperative main-thread work. A unit is indivisible: overruns are measured,
// never hidden by pretending that arbitrary native/script calls are preemptible.
class FrameTask {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;
    struct promise_type {
        bool Success = false;
        std::exception_ptr Error;
        FrameTask get_return_object() { return FrameTask(Handle::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(uint32_t) noexcept { return {}; }
        void return_value(bool value) { Success = value; }
        void unhandled_exception() { Error = std::current_exception(); }
    };
    FrameTask() = default;
    explicit FrameTask(Handle handle) : m_Handle(handle) {}
    FrameTask(const FrameTask&) = delete;
    FrameTask& operator=(const FrameTask&) = delete;
    FrameTask(FrameTask&& other) noexcept { *this = std::move(other); }
    FrameTask& operator=(FrameTask&& other) noexcept {
        if (this != &other) { if (m_Handle) m_Handle.destroy(); m_Handle = std::exchange(other.m_Handle, {}); LastMilliseconds = other.LastMilliseconds; PeakMilliseconds = other.PeakMilliseconds; OverrunFrames = other.OverrunFrames; }
        return *this;
    }
    ~FrameTask() { if (m_Handle) m_Handle.destroy(); }
    bool Done() const { return !m_Handle || m_Handle.done(); }
    bool Succeeded() const { return m_Handle && Done() && m_Handle.promise().Success && !m_Handle.promise().Error; }
    uint32_t Advance(uint32_t maximumUnits, double milliseconds) {
        const auto start = std::chrono::steady_clock::now();
        uint32_t count = 0;
        while (!Done() && count < maximumUnits) {
            m_Handle.resume(); ++count;
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= milliseconds) break;
        }
        LastMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (LastMilliseconds > PeakMilliseconds) PeakMilliseconds = LastMilliseconds;
        if (LastMilliseconds > milliseconds) ++OverrunFrames;
        return count;
    }
    double LastMilliseconds = 0.0, PeakMilliseconds = 0.0;
    uint64_t OverrunFrames = 0;
private:
    Handle m_Handle{};
};
}
