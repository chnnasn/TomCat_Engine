#include "tcpch.h"
#include "LinuxProcess.h"
#if defined(__linux__) && !defined(__ANDROID__)
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace TomCat::Linux {
bool StartProcess(const std::vector<std::string>& arguments,
    const std::filesystem::path& directory, std::string& message)
{
    if (arguments.empty()) { message = "Empty process command"; return false; }
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) { message = std::strerror(error); return false; }
    if (!directory.empty()) error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    pid_t child = -1;
    if (!error) error = posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) { message = std::strerror(error); return false; }
    // Reap the child without blocking the Editor/Hub UI thread.
    try {
        std::thread([child] { int status; while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }).detach();
    } catch (...) {
        kill(child, SIGTERM);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
        message = "Could not start child process monitor";
        return false;
    }
    message.clear();
    return true;
}

ProcessResult RunProcess(const std::vector<std::string>& arguments,
    const std::filesystem::path& directory, std::chrono::milliseconds timeout, bool mergeStderr)
{
    ProcessResult result;
    if (arguments.empty()) { result.Error = "Empty process command"; return result; }
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    int pipes[2];
    if (pipe2(pipes, O_CLOEXEC) != 0) { result.Error = std::strerror(errno); return result; }
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        close(pipes[0]); close(pipes[1]); result.Error = std::strerror(error); return result;
    }
    error = posix_spawnattr_init(&attributes);
    if (error) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipes[0]); close(pipes[1]); result.Error = std::strerror(error); return result;
    }
    error = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
    if (!error && mergeStderr) error = posix_spawn_file_actions_adddup2(&actions, pipes[1], STDERR_FILENO);
    if (!error && !directory.empty())
        error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    if (!error) error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (!error) error = posix_spawnattr_setpgroup(&attributes, 0);
    pid_t child = -1;
    if (!error) error = posix_spawnp(&child, argv.front(), &actions, &attributes, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close(pipes[1]);
    if (error) { close(pipes[0]); result.Error = std::strerror(error); return result; }
    fcntl(pipes[0], F_SETFL, O_NONBLOCK);
    const auto started = std::chrono::steady_clock::now();
    bool exited = false, eof = false;
    int status = 0;
    while (!exited || !eof) {
        pollfd descriptor{pipes[0], POLLIN, 0};
        poll(&descriptor, 1, 20);
        char buffer[4096];
        // Bound each drain so continuous output cannot bypass the deadline.
        for (int chunk = 0; chunk < 64 && !eof; ++chunk) {
            const auto count = read(pipes[0], buffer, sizeof(buffer));
            if (count > 0) {
                if (result.Output.size() < 16 * 1024 * 1024)
                    result.Output.append(buffer, std::min<size_t>(count, 16 * 1024 * 1024 - result.Output.size()));
            } else {
                eof = count == 0 || (errno != EAGAIN && errno != EINTR);
                break;
            }
        }
        if (!exited) {
            const auto waited = waitpid(child, &status, WNOHANG);
            exited = waited == child;
            if (waited < 0 && errno != EINTR) { result.Error = std::strerror(errno); break; }
        }
        if (timeout.count() > 0 && std::chrono::steady_clock::now() - started >= timeout) {
            result.Error = "Process timed out";
            kill(-child, SIGKILL);
            if (!exited) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
    }
    close(pipes[0]);
    if (result.Error.empty())
        result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}
}
#endif
