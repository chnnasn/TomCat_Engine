#pragma once

#include "TomCat/Core/Log.h"
#include "TomCat/Debug/Instrumentor.h"
#include <filesystem>
#include <future>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace TomCat {
// Worker-owned filesystem snapshots. Rendering never performs a stat or directory
// walk. Mutation commands still validate native paths immediately before use.
class ProjectBrowserCache {
public:
    struct Entry {
        std::filesystem::path Path;
        bool Directory = false, Link = false, ReadOnly = false;
        std::filesystem::file_time_type Modified{};
    };
    using Listing = std::vector<Entry>;
    using Snapshot = std::map<std::filesystem::path, Listing>;
    void Reset() {
        if (m_Job.valid()) m_Job.wait();
        m_Job = {}; m_Directories.clear(); m_Snapshot.clear(); m_NextScan = 0;
    }
    const Listing& Read(const std::filesystem::path& directory) {
        if (m_Directories.insert(directory).second) m_NextScan = 0;
        auto it = m_Snapshot.find(directory);
        static const Listing empty;
        return it == m_Snapshot.end() ? empty : it->second;
    }
    const Entry* Find(const std::filesystem::path& path) {
        for (const auto& entry : Read(path.parent_path())) if (entry.Path == path) return &entry;
        return nullptr;
    }
    void Invalidate() { m_NextScan = 0; }
    void SetReadOnlyRoot(std::filesystem::path root) { m_ReadOnlyRoot = std::move(root); }
    void Tick(double now) {
        if (m_Job.valid()) {
            auto status = m_Job.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready || status == std::future_status::deferred) {
                try { m_Snapshot = m_Job.get(); } catch (...) { m_Job = {}; }
            }
        }
        if (!m_Job.valid() && now >= m_NextScan && !m_Directories.empty()) {
            m_NextScan = now + 1.0;
            const auto directories = m_Directories;
            const auto readOnlyRoot = m_ReadOnlyRoot;
#ifdef __EMSCRIPTEN__
            constexpr auto policy = std::launch::deferred;
#else
            constexpr auto policy = std::launch::async;
#endif
            m_Job = std::async(policy, [directories, readOnlyRoot] { return Scan(directories, readOnlyRoot); });
        }
    }
    static Snapshot Scan(const std::set<std::filesystem::path>& directories, const std::filesystem::path& readOnlyRoot = {}) {
        TC_PROFILE_SCOPE("Project directory scan");
        Snapshot result;
        for (const auto& directory : directories) {
            auto& entries = result[directory];
            std::error_code error;
#ifdef _WIN32
            // The packaged filesystem implements FindFirst/FindNext. MSVC's
            // directory_iterator uses a different enumeration path that can
            // return the virtual root itself as a child. Copy each Win32 name
            // before querying metadata, and reject dot entries before joining.
            std::vector<std::filesystem::directory_entry> children;
            WIN32_FIND_DATAW data{};
            const HANDLE search = FindFirstFileW((directory / L"*").c_str(), &data);
            if (search != INVALID_HANDLE_VALUE) {
                struct SearchGuard { HANDLE Value; ~SearchGuard() { FindClose(Value); } } guard{search};
                do {
                    const std::wstring name(data.cFileName);
                    if (name == L"." || name == L"..") continue;
                    std::filesystem::directory_entry child(directory / name, error);
                    if (!error) children.push_back(std::move(child));
                    error.clear();
                } while (FindNextFileW(search, &data));
            }
            for (const auto& item : children) {
#else
            for (std::filesystem::directory_iterator it(directory, error), end;
                !error && it != end; it.increment(error)) {
                const auto& item = *it;
#endif
                if (item.path().extension() == ".tcmeta") continue;
                Entry entry; entry.Path = item.path().lexically_normal();
                if (entry.Path == directory.lexically_normal() || entry.Path == directory.parent_path().lexically_normal()
                    || entry.Path.filename() == "." || entry.Path.filename() == "..") continue;
                auto status = item.symlink_status(error);
                if (error) { error.clear(); continue; }
                entry.Link = std::filesystem::is_symlink(status);
                entry.Directory = std::filesystem::is_directory(status);
                // Packaged read-only resources are redirected by the virtual filesystem.
                // Apply native containment to project folders, not virtual Packages.
                const auto relative = entry.Path.lexically_relative(readOnlyRoot);
                const bool readOnlyPackage = !readOnlyRoot.empty() && !relative.empty() && !relative.is_absolute()
                    && std::none_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; });
                if (entry.Directory && !readOnlyPackage) {
                    auto native = std::filesystem::weakly_canonical(entry.Path, error);
                    auto parent = std::filesystem::weakly_canonical(directory, error);
                    if (error || native.parent_path() != parent) { error.clear(); entry.Directory = false; entry.Link = true; }
                }
                constexpr auto write = std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write;
                entry.ReadOnly = (status.permissions() & write) == std::filesystem::perms::none;
                entry.Modified = item.last_write_time(error); error.clear();
                entries.push_back(std::move(entry));
            }
            std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
                if (a.Directory != b.Directory) return a.Directory;
                return a.Path.filename() < b.Path.filename();
            });
        }
        return result;
    }
private:
    std::filesystem::path m_ReadOnlyRoot;
    std::set<std::filesystem::path> m_Directories;
    Snapshot m_Snapshot;
    std::future<Snapshot> m_Job;
    double m_NextScan = 0;
};
}
