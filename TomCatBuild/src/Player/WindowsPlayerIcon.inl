// Included in the Windows build implementation only. Icon pixels use the same
// imported mip data as the Player; the input may be any imported Texture2D.
#pragma pack(push, 2)
struct GroupIconEntry
{
    BYTE Width, Height, Colors, Reserved;
    WORD Planes, Bits;
    DWORD Bytes;
    WORD ResourceId;
};
struct PlayerIconGroup
{
    WORD Reserved = 0, Type = 1, Count = 0;
    GroupIconEntry Entries[7]{};
};
#pragma pack(pop)
static_assert(sizeof(GroupIconEntry) == 14);

bool EmbedWindowsPlayerIcon(const std::filesystem::path& executable, AssetHandle icon,
    std::string& error)
{
    if (static_cast<uint64_t>(icon) == 0)
        return true;
    auto loaded = AssetManager::Get().LoadImportedArtifact(icon);
    TextureArtifactView texture;
    if (!loaded.Succeeded()) { error = loaded.Error; return false; }
    if (!ParseTextureArtifact(loaded.Artifact.Bytes, texture, error) || texture.Mips.empty())
        return false;
    const auto& mip = texture.Mips.front();
    std::vector<uint8_t> rgba;
    if (!DecompressTextureMip(mip, texture.Format, rgba, error))
        return false;

    HANDLE update = BeginUpdateResourceW(executable.c_str(), FALSE);
    if (!update) { error = "BeginUpdateResourceW failed: " + std::to_string(GetLastError()); return false; }
    auto write = [&](WORD type, WORD id, void* bytes, DWORD length) {
        if (UpdateResourceW(update, MAKEINTRESOURCEW(type), MAKEINTRESOURCEW(id), 0, bytes, length))
            return true;
        error = "UpdateResourceW failed: " + std::to_string(GetLastError());
        EndUpdateResourceW(update, TRUE);
        return false;
    };
    PlayerIconGroup group;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
    {
        const auto longest = (std::max)(mip.Width, mip.Height);
        const int width = (std::max)(1u, mip.Width * size / longest);
        const int height = (std::max)(1u, mip.Height * size / longest);
        const int stride = ((size + 31) / 32) * 4;
        std::vector<uint8_t> image(sizeof(BITMAPINFOHEADER) + size * size * 4 + stride * size);
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = size;
        header.biHeight = size * 2; // BGRA pixels followed by the 1-bit transparency mask.
        header.biPlanes = 1;
        header.biBitCount = 32;
        std::memcpy(image.data(), &header, sizeof(header));
        auto* pixels = image.data() + sizeof(header);
        auto* mask = pixels + size * size * 4;
        for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int u = x - (size-width)/2, v = y - (size-height)/2;
            auto* dest = pixels + 4 * (y * size + x);
            if (u >= 0 && u < width && v >= 0 && v < height)
            {
                // Both the imported texture and Windows DIB use bottom-up rows.
                const auto* src = rgba.data() + 4 * (size_t(v * mip.Height / height) * mip.Width + u * mip.Width / width);
                dest[0] = src[2]; dest[1] = src[1]; dest[2] = src[0]; dest[3] = src[3];
            }
            if (!dest[3]) mask[y * stride + x/8] |= BYTE(0x80 >> (x%8));
        }
        const WORD id = group.Count + 1;
        const DWORD bytes = static_cast<DWORD>(image.size());
        if (!write(3, id, image.data(), bytes)) return false; // RT_ICON
        group.Entries[group.Count++] = { BYTE(size), BYTE(size), 0, 0, 1, 32, bytes, id };
    }
    if (!write(14, 1, &group, sizeof(group))) return false; // RT_GROUP_ICON
    if (EndUpdateResourceW(update, FALSE)) return true;
    error = "EndUpdateResourceW failed: " + std::to_string(GetLastError());
    return false;
}
