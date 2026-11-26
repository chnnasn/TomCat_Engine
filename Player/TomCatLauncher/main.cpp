#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR arguments, int)
{
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) return 1;
    path.resize(size);
    const fs::path executable(path);
    const fs::path data = executable.parent_path() / (executable.stem().wstring() + L"_Data");
    const fs::path player = data / L"TomCatPlayer.exe";
    std::wstring command = L"\"" + player.wstring() + L"\"";
    if (*arguments) command += std::wstring(L" ") + arguments;

    STARTUPINFOW startup{};
    GetStartupInfoW(&startup);
    // Preserve explicitly redirected streams for build automation without opening a console.
    const BOOL inheritStreams = (startup.dwFlags & STARTF_USESTDHANDLES) != 0;
    startup.dwFlags &= STARTF_USESTDHANDLES;
    startup.lpReserved2 = nullptr;
    startup.cbReserved2 = 0;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(player.c_str(), command.data(), nullptr, nullptr, inheritStreams,
                        DETACHED_PROCESS, nullptr, nullptr, &startup, &process))
    {
        const auto error = GetLastError();
        const auto message = L"Could not start the game (Windows error " + std::to_wstring(error)
            + L").\n\nKeep the executable beside its matching _Data folder.";
        MessageBoxW(nullptr, message.c_str(), executable.stem().c_str(), MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
