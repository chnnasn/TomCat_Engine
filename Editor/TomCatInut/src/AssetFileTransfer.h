#pragma once

#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace TomCat {

// Copy source files, never their identity sidecars. The registry assigns fresh
// handles to copies; existing scenes continue referencing the original assets.
inline bool CopyAssetFiles(const std::filesystem::path& source,
    const std::filesystem::path& directory, std::filesystem::path& result,
    std::string& message)
{
    namespace fs = std::filesystem;
    result.clear(); message.clear();
    try {
        auto isLink = [](const fs::path& path) {
#ifdef _WIN32
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
#endif
            return fs::is_symlink(fs::symlink_status(path));
        };
        auto sidecar = [](const fs::path& path) {
            return path.extension() == ".tcmeta";
        };
        if (source.filename().empty() || isLink(source) || isLink(directory) || !fs::is_directory(directory) || sidecar(source)) {
            message = "Choose a regular source asset and a writable project folder."; return false;
        }
        const bool folder = fs::is_directory(source);
        if (!folder && !fs::is_regular_file(source)) {
            message = "Only files and folders can be imported."; return false;
        }
        std::vector<fs::path> entries;
        if (folder) {
            const auto relative = fs::weakly_canonical(directory).lexically_relative(fs::canonical(source));
            if (!relative.empty() && *relative.begin() != "..") {
                message = "A folder cannot be copied into itself or a descendant."; return false;
            }
            // Validate before copying so a junction never imports outside content.
            for (const auto& entry : fs::recursive_directory_iterator(source)) {
                if (isLink(entry.path()) || (!entry.is_directory() && !entry.is_regular_file())) {
                    message = "Folders containing links or special files cannot be imported."; return false;
                }
                if (!sidecar(entry.path())) entries.push_back(entry.path());
            }
        }
        auto destination = directory / source.filename();
        for (unsigned suffix = 1; fs::exists(destination) || fs::exists(fs::path(destination.wstring() + L".tcmeta")); ++suffix) {
            if (suffix > 10000) { message = "Too many assets with the same name."; return false; }
            const auto stem = folder ? source.filename() : source.stem();
            destination = directory / (stem.wstring() + L" (" + std::to_wstring(suffix) + L")" + (folder ? L"" : source.extension().wstring()));
        }
        if (!folder) fs::copy_file(source, destination, fs::copy_options::none);
        else {
            if (!fs::create_directory(destination)) {
                message = "The destination was created by another operation. Please retry."; return false;
            }
            for (const auto& entry : entries) {
                const auto target = destination / entry.lexically_relative(source);
                if (fs::is_directory(entry)) fs::create_directory(target);
                else fs::copy_file(entry, target, fs::copy_options::none);
            }
        }
        result = destination;
        return true;
    } catch (const fs::filesystem_error& error) {
        message = error.what(); return false;
    }
}

}
