#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Editor/EditorRecoveryService.h"
#include "platform/Linux/LinuxProcess.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Environment {
    std::string Name;
    std::optional<std::string> Previous;
    explicit Environment(const char* name) : Name(name) {
        if (const auto* value = std::getenv(name)) Previous = value;
    }
    ~Environment() {
        if (Previous) setenv(Name.c_str(), Previous->c_str(), 1);
        else unsetenv(Name.c_str());
    }
};
struct TemporaryDirectory {
    std::filesystem::path Path;
    TemporaryDirectory() {
        std::string name = (std::filesystem::temp_directory_path() / "tomcat-linux-XXXXXX").string();
        Check(mkdtemp(name.data()) != nullptr, "mkdtemp failed");
        Path = name;
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(Path, error); }
};
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--echo") { std::cout << argv[2]; return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--sleep") { sleep(5); return 0; }
    try {
        TemporaryDirectory temporary;
        Environment data("XDG_DATA_HOME"), home("HOME");
        setenv("XDG_DATA_HOME", temporary.Path.c_str(), 1);
        Check(TomCat::ApplicationPaths::GetLocalAppDataRoot() == temporary.Path, "XDG data path not used");
        setenv("HOME", temporary.Path.c_str(), 1);
        setenv("XDG_DATA_HOME", "relative-is-invalid", 1);
        Check(TomCat::ApplicationPaths::GetLocalAppDataRoot() == temporary.Path / ".local/share", "Relative XDG path was accepted");
        const auto executable = TomCat::ApplicationPaths::GetExecutablePath();
        Check(executable && std::filesystem::is_regular_file(*executable), "Executable resolution failed");
        const std::string argument = "Unicode 路径 ' \" ; $(touch SHOULD_NOT_EXIST)";
        auto result = TomCat::Linux::RunProcess({executable->string(), "--echo", argument}, temporary.Path);
        Check(result.ExitCode == 0 && result.Output == argument, "Process argv did not round-trip");
        Check(!std::filesystem::exists(temporary.Path / "SHOULD_NOT_EXIST"), "Process arguments reached a shell");
        result = TomCat::Linux::RunProcess({"/tomcat/does-not-exist"});
        Check(result.ExitCode == -1 && !result.Error.empty(), "Missing executable did not report launch failure");
        result = TomCat::Linux::RunProcess({executable->string(), "--sleep"}, {}, std::chrono::milliseconds(50));
        Check(result.ExitCode == -1 && result.Error == "Process timed out", "Process timeout failed");

        TomCat::Log::Init();
        const auto project = temporary.Path / "Project.tcproj";
        std::ofstream(project) << "test";
        const auto root = temporary.Path / "locks";
        std::string error;
        TomCat::EditorProjectLock first, second;
        using Result = TomCat::ProjectLockAcquireResult;
        Check(first.Acquire(project, error, root) == Result::Acquired, "First project lock failed");
        Check(first.GetRecord().ProcessId == static_cast<uint64_t>(getpid()), "Lock recorded the wrong PID");
        Check(second.Acquire(project, error, root) == Result::LiveOwner, "Concurrent project lock was allowed");
        first.Release();
        Check(second.Acquire(project, error, root) == Result::Acquired, "Released project lock could not be reacquired");
        second.Release();
        const pid_t child = fork();
        Check(child >= 0, "fork failed");
        if (child == 0) {
            TomCat::EditorProjectLock abandoned;
            const bool acquired = abandoned.Acquire(project, error, root) == Result::Acquired;
            _exit(acquired ? 0 : 1); // Simulate a crash: deliberately bypass destructors.
        }
        int status = 0;
        Check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Child lock failed");
        Check(first.Acquire(project, error, root) == Result::Acquired, "Crash left the project permanently locked");
        first.Release();
        TomCat::Log::Shutdown();
        std::cout << "Linux platform regressions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
