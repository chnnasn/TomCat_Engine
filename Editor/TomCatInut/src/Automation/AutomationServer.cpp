#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "AutomationServer.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <sstream>

namespace TomCat {
namespace {
    constexpr size_t MaximumBody = 1024 * 1024;
    constexpr size_t MaximumHeader = 8192;

    std::string Environment(const char* name)
    {
        char* value = nullptr;
        size_t length = 0;
        _dupenv_s(&value, &length, name);
        std::string result = value ? value : "";
        free(value);
        return result;
    }

    void Reply(SOCKET socket, int status, const std::string& body)
    {
        const std::string message = "HTTP/1.1 " + std::to_string(status)
            + " Response\r\nContent-Type: application/json; charset=utf-8\r\n"
              "Connection: close\r\nCache-Control: no-store\r\nContent-Length: "
            + std::to_string(body.size()) + "\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < message.size())
        {
            const int count = send(socket, message.data() + sent,
                static_cast<int>(message.size() - sent), 0);
            if (count <= 0) break;
            sent += count;
        }
    }
}

AutomationServer::~AutomationServer() { Stop(); }

bool AutomationServer::StartFromEnvironment(std::string& error)
{
    const auto portText = Environment("TOMCAT_AUTOMATION_PORT");
    if (portText.empty()) return true; // opt-in, disabled by default
    unsigned port = 0;
    const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    m_Token = Environment("TOMCAT_AUTOMATION_TOKEN");
    if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size()
        || port == 0 || port > 65535 || m_Token.size() < 16
        || m_Token.find_first_of("\r\n") != std::string::npos)
    {
        error = "Automation requires port 1..65535 and a token of at least 16 characters.";
        return false;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    { error = "Automation WSAStartup failed."; return false; }
    m_WinsockStarted = true;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    m_Listener = listener;
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    u_long nonblocking = 1;
    if (listener == INVALID_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || listen(listener, 8) != 0 || ioctlsocket(listener, FIONBIO, &nonblocking) != 0)
    {
        error = "Automation could not bind the loopback port (it may already be in use).";
        Stop();
        return false;
    }
    m_Running = true;
    m_Thread = std::thread(&AutomationServer::Serve, this);
    return true;
}

void AutomationServer::Stop()
{
    m_Running = false;
    if (m_Thread.joinable()) m_Thread.join();
    if (m_Listener != INVALID_SOCKET) closesocket(m_Listener);
    m_Listener = INVALID_SOCKET;
    if (m_WinsockStarted) WSACleanup();
    m_WinsockStarted = false;
    std::lock_guard lock(m_Mutex);
    m_Requests.clear();
}

void AutomationServer::Pump(const std::function<std::string(const std::string&)>& handler)
{
    std::shared_ptr<Request> request;
    {
        std::lock_guard lock(m_Mutex);
        if (m_Requests.empty()) return;
        request = m_Requests.front();
        m_Requests.pop_front();
        if (request->Cancelled) return;
    }
    try { request->Response.set_value(handler(request->Body)); }
    catch (const std::exception&)
    { request->Response.set_value(R"({"ok":false,"error":{"code":"INTERNAL_ERROR","message":"Editor operation failed; query state before retrying."}})"); }
}

void AutomationServer::Serve()
{
    using namespace std::chrono;
    while (m_Running)
    {
        SOCKET client = accept(m_Listener, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        { std::this_thread::sleep_for(milliseconds(10)); continue; }
        u_long blocking = 0;
        ioctlsocket(client, FIONBIO, &blocking);
        DWORD timeout = 1000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        std::string input;
        char buffer[4096];
        const auto deadline = steady_clock::now() + seconds(5);
        auto receive = [&]() {
            if (!m_Running || steady_clock::now() >= deadline) return false;
            const int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) return false;
            input.append(buffer, count);
            return true;
        };
        while (input.find("\r\n\r\n") == std::string::npos && input.size() < MaximumHeader && receive()) {}
        const size_t boundary = input.find("\r\n\r\n");
        if (boundary == std::string::npos || boundary > MaximumHeader)
        { Reply(client, 400, R"({"error":"Invalid HTTP headers"})"); closesocket(client); continue; }
        std::istringstream headers(input.substr(0, boundary));
        std::string method, path, version, line;
        headers >> method >> path >> version;
        std::getline(headers, line);
        std::map<std::string, std::string> fields;
        bool valid = version == "HTTP/1.1";
        while (std::getline(headers, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto colon = line.find(':');
            if (colon == std::string::npos) { valid = false; break; }
            std::string key = line.substr(0, colon);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            if (!fields.emplace(key, value).second) valid = false;
        }
        if (!valid || fields.contains("transfer-encoding") || fields.contains("origin")
            || fields["authorization"] != "Bearer " + m_Token)
        { Reply(client, 403, R"({"error":"Unauthorized or unsupported request"})"); closesocket(client); continue; }
        std::string body;
        if (method == "GET" && path == "/health")
            body = R"({"tool":"editor_get_status","arguments":{}})";
        else if (method == "POST" && path == "/call")
        {
            size_t length = 0;
            const auto& value = fields["content-length"];
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || length > MaximumBody
                || fields["content-type"] != "application/json")
            { Reply(client, 400, R"({"error":"Expected bounded JSON body"})"); closesocket(client); continue; }
            while (input.size() < boundary + 4 + length && receive()) {}
            if (input.size() != boundary + 4 + length)
            { Reply(client, 400, R"({"error":"Incomplete or oversized body"})"); closesocket(client); continue; }
            body = input.substr(boundary + 4);
        }
        else
        { Reply(client, 404, R"({"error":"Unknown endpoint"})"); closesocket(client); continue; }
        auto request = std::make_shared<Request>();
        request->Body = std::move(body);
        auto result = request->Response.get_future();
        { std::lock_guard lock(m_Mutex); m_Requests.push_back(request); }
        const auto responseDeadline = steady_clock::now() + seconds(60);
        while (m_Running && steady_clock::now() < responseDeadline
            && result.wait_for(milliseconds(100)) != std::future_status::ready) {}
        if (result.wait_for(milliseconds(0)) == std::future_status::ready)
            Reply(client, 200, result.get());
        else
        {
            { std::lock_guard lock(m_Mutex); request->Cancelled = true; }
            Reply(client, 504, R"({"error":"Editor timeout; outcome may be unknown. Reuse the request ID."})");
        }
        closesocket(client);
    }
}
}
