#pragma once

#if defined(__linux__) && !defined(__ANDROID__)
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace TomCat::Linux {
bool StartProcess(const std::vector<std::string>& arguments,
    const std::filesystem::path& directory, std::string& error);
struct ProcessResult {
    int ExitCode = -1;
    std::string Output;
    std::string Error;
};

// argv is passed directly to posix_spawnp; filenames never become shell code.
// A zero timeout allows interactive dialogs to remain open until dismissed.
ProcessResult RunProcess(const std::vector<std::string>& arguments,
    const std::filesystem::path& directory = {},
    std::chrono::milliseconds timeout = std::chrono::seconds(30), bool mergeStderr = true);
}
#endif
