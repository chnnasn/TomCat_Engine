#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace TomCat::ModulePackage {
    inline constexpr uint64_t Handle = UINT64_MAX - 1;
    inline constexpr uint16_t EntryFlag = 2;
    inline constexpr uint32_t EntryTag = 0x31444f4d; // MOD1
    inline constexpr size_t MaximumBytes = 256u * 1024u * 1024u;
    bool Build(const std::filesystem::path& project, std::vector<uint8_t>& bytes,
        std::string& error);
    bool Validate(std::span<const uint8_t> bytes, std::string& error);
    bool Extract(std::span<const uint8_t> bytes, const std::filesystem::path& root,
        std::string& error);
}
