#include "tcpch.h"
#ifdef TC_PLATFORM_LINUX
#include "TomCat/Utils/PlatformUtils.h"
#include "LinuxProcess.h"

namespace TomCat {
namespace {
std::filesystem::path ShowDialog(const char* filter, bool save, bool folder)
{
    std::vector<std::string> args{"zenity", "--file-selection"};
    if (save) { args.emplace_back("--save"); args.emplace_back("--confirm-overwrite"); }
    if (folder) args.emplace_back("--directory");
    std::string extension;
    if (!folder && filter && *filter) {
        std::string label(filter);
        const auto begin = label.rfind('('), end = label.rfind(')');
        if (begin != std::string::npos && end > begin) {
            auto pattern = label.substr(begin + 1, end - begin - 1);
            if (pattern.rfind("*.", 0) == 0 && pattern.find_first_of(";?*", 2) == std::string::npos)
                extension = pattern.substr(1);
            std::replace(pattern.begin(), pattern.end(), ';', ' ');
            args.emplace_back("--file-filter=" + label + " | " + pattern);
        }
    }
    auto result = Linux::RunProcess(args, {}, std::chrono::milliseconds(0), false);
    if (result.ExitCode != 0) {
        if (!result.Error.empty()) TC_Core_Warn("File dialog unavailable (install zenity): {0}", result.Error);
        return {};
    }
    if (!result.Output.empty() && result.Output.back() == '\n') result.Output.pop_back();
    if (result.Output.find('\n') != std::string::npos) return {};
    std::filesystem::path path(result.Output);
    if (!path.is_absolute()) return {};
    if (save && !path.has_extension() && !extension.empty()) path += extension;
    return path;
}
}
std::filesystem::path FileDialogs::OpenFile(const char* filter) { return ShowDialog(filter, false, false); }
std::filesystem::path FileDialogs::SaveFile(const char* filter) { return ShowDialog(filter, true, false); }
std::filesystem::path FileDialogs::OpenFolder() { return ShowDialog(nullptr, false, true); }
}
#endif
