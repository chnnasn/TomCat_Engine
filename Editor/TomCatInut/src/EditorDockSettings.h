#pragma once
#include <charconv>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace TomCat {
// A persisted window may refer to a node that later became a split node.
// ImGui eagerly rebinds previously visible windows during project switching;
// invalid references must be removed before it creates tab bars on those nodes.
inline std::string SanitizeDockWindowReferences(const std::string& settings) {
    auto idAfter = [](std::string_view line, std::string_view key) -> uint32_t {
        const auto offset = line.find(key);
        if (offset == std::string_view::npos) return 0;
        const char* begin = line.data() + offset + key.size();
        uint32_t id = 0;
        const auto parsed = std::from_chars(begin, line.data() + line.size(), id, 16);
        return parsed.ec == std::errc{} ? id : 0;
    };
    std::set<uint32_t> nodes, parents;
    bool docking = false;
    std::istringstream input(settings);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() == '[') docking = line == "[Docking][Data]";
        if (!docking) continue;
        if (const auto id = idAfter(line, "ID=0x")) nodes.insert(id);
        if (const auto parent = idAfter(line, "Parent=0x")) parents.insert(parent);
        if (line.find("Split=") != std::string::npos)
            if (const auto id = idAfter(line, "ID=0x")) parents.insert(id);
    }
    input.clear(); input.seekg(0);
    bool window = false;
    std::string result;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line.front() == '[') window = line.starts_with("[Window][");
        if (window && line.starts_with("DockId=")) {
            const auto id = idAfter(line, "DockId=0x");
            if (id && (!nodes.contains(id) || parents.contains(id))) line = "DockId=0x00000000";
        }
        result += line; result += '\n';
    }
    return result;
}
}
