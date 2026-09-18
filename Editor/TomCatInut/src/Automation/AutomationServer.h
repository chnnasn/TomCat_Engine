#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace TomCat {

// Loopback HTTP transport only. Every handler runs in Pump(), on the Editor
// thread; the network thread never observes Scene or EditorLayer state.
class AutomationServer final
{
public:
    ~AutomationServer();
    bool StartFromEnvironment(std::string& error);
    void Stop();
    void Pump(const std::function<std::string(const std::string&)>& handler);
private:
    struct Request
    {
        std::string Body;
        std::promise<std::string> Response;
        bool Cancelled = false; // protected by m_Mutex
    };
    void Serve();
    std::atomic<bool> m_Running{false};
    std::thread m_Thread;
    std::mutex m_Mutex;
    std::deque<std::shared_ptr<Request>> m_Requests;
    uintptr_t m_Listener = ~uintptr_t(0);
    std::string m_Token;
    bool m_WinsockStarted = false;
};

}
