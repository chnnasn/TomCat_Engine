// Included inside namespace TomCat by ContentBrowserPanel.cpp.
namespace {
    void AssetProperty(const char* label, const std::string& value)
    {
        if (ImGui::BeginTable(label, 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", label);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", value.c_str());
            ImGui::EndTable();
        }
    }

    bool ReadInspectorBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes,
        std::string& error, size_t limit = 64 * 1024 * 1024)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const auto size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
        if (size < 0) { error = "The source file cannot be read."; return false; }
        if (static_cast<uint64_t>(size) > limit)
        { error = "This file exceeds the inline preview limit. Use Open to view it externally."; return false; }
        bytes.resize(static_cast<size_t>(size)); input.seekg(0);
        if (size && !input.read(reinterpret_cast<char*>(bytes.data()), size))
        { error = "The source file changed while being read."; return false; }
        return true;
    }


}

void ContentBrowserPanel::RefreshInspectorDetails(const std::filesystem::path& path,
    AssetType type, bool directory)
{
    m_InspectorDetails.clear(); m_InspectorShaderResources.clear();
    m_InspectorSource.clear(); m_InspectorReadError.clear(); m_InspectorCompileMessage.clear();
    m_InspectorPreview.reset();
    std::error_code error;
    m_InspectorSourceTime = std::filesystem::last_write_time(path, error);
    if (error) { m_InspectorReadError = "The selected asset no longer exists or cannot be accessed."; return; }
    auto row = [&](const char* label, auto value) { std::ostringstream text; text << value; m_InspectorDetails.emplace_back(label, text.str()); };
    if (directory)
    {
        size_t files = 0, folders = 0;
        for (const auto& entry : ReadDirectory(path))
            if (IsManagedEntry(GetRootForPath(path), entry.path()))
            { if (entry.is_directory(error)) ++folders; else ++files; }
        row("Folders", folders); row("Assets", files);
        return;
    }
    if (type == AssetType::Texture2D)
    {
        m_InspectorPreview = LoadEditorPreview(path);
        if (m_InspectorPreview)
        {
            row("Width", m_InspectorPreview->GetWidth()); row("Height", m_InspectorPreview->GetHeight());
            m_InspectorDetails.emplace_back("Shape", "2D");
        }
        else m_InspectorReadError = "Image preview is unavailable (invalid image or preview memory limit).";
        return;
    }
    std::vector<uint8_t> bytes;
    const bool text = type == AssetType::CSharpScript || type == AssetType::Shader ||
        type == AssetType::Scene || type == AssetType::Prefab || type == AssetType::Material ||
        type == AssetType::AnimationClip || type == AssetType::AnimatorController || type == AssetType::TilePalette;
    if (type == AssetType::Other || type == AssetType::None) return;
    if (!ReadInspectorBytes(path, bytes, m_InspectorReadError, text ? 4 * 1024 * 1024 : 64 * 1024 * 1024)) return;
    if (text)
    {
        const size_t length = std::min<size_t>(bytes.size(), 256 * 1024);
        m_InspectorSource.assign(bytes.begin(), bytes.begin() + length);
        if (length < bytes.size()) m_InspectorReadError = "Source preview truncated to 256 KiB. Open the file to see all content.";
    }
    if (type == AssetType::Audio)
    {
        if (auto clip = AudioClip::Decode(bytes, m_InspectorReadError))
        {
            row("Duration (seconds)", clip->GetDurationSeconds()); row("Channels", clip->GetChannels());
            row("Sample rate (Hz)", clip->GetSampleRate()); row("Bits per sample", clip->GetBitsPerSample());
            row("Sample frames", clip->GetFrameCount());
            m_InspectorDetails.emplace_back("Encoding", clip->IsFloatingPoint() ? "Floating point WAV" : "PCM WAV");
        }
    }
    else if (type == AssetType::Mesh)
    {
        std::vector<uint8_t> artifact; MeshArtifactView mesh;
        if (BuildMeshArtifact(bytes, path, artifact, m_InspectorReadError) &&
            ParseMeshArtifact(artifact, mesh, m_InspectorReadError))
        { row("Vertices", mesh.VertexCount); row("Indices", mesh.IndexCount); row("Triangles", mesh.IndexCount / 3); }
    }
    else if (type == AssetType::Material)
    {
        std::vector<uint8_t> artifact; MaterialArtifactView material;
        if (BuildMaterialArtifact(bytes, artifact, m_InspectorReadError) &&
            ParseMaterialArtifact(artifact, material, m_InspectorReadError))
        {
            const auto referenceName = [](AssetHandle handle) {
                if (static_cast<uint64_t>(handle) == 0) return std::string("None");
                const auto* reference = AssetManager::Get().GetRegistry().GetMetadata(handle);
                return reference ? PathToUTF8(reference->FilePath.filename()) :
                    "Missing asset (" + std::to_string(static_cast<uint64_t>(handle)) + ")";
            };
            m_InspectorDetails.emplace_back("Shader", referenceName(material.Shader));
            for (const auto& texture : material.Textures)
                m_InspectorDetails.emplace_back(std::string(texture.Name), referenceName(texture.Texture));
            for (const auto& parameter : material.Parameters)
            {
                std::string value;
                for (size_t i = 0; i < parameter.ComponentCount(); ++i)
                {
                    if (i) value += ", ";
                    value += parameter.Type == MaterialParameterType::Bool ? (parameter.AsBool() ? "true" : "false") :
                        parameter.Type == MaterialParameterType::Int ? std::to_string(parameter.AsInt()) : std::to_string(parameter.AsFloat(i));
                }
                m_InspectorDetails.emplace_back(std::string(parameter.Name), value);
            }
        }
    }
    else if (type == AssetType::Font)
    {
        const auto codepoints = FontAtlasBuilder::DecodeUTF8("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!?");
        FontAtlasData atlas;
        if (FontAtlasBuilder::Build(bytes, codepoints, atlas, 32.0f) && atlas.UsesSourceFont)
        {
            m_InspectorPreview = Texture2D::Create(atlas.Width, atlas.Height);
            m_InspectorPreview->SetData(atlas.PixelsRGBA.data(), static_cast<uint32_t>(atlas.PixelsRGBA.size()));
            row("Preview glyphs", atlas.Glyphs.size()); row("Ascent", atlas.Ascent); row("Descent", atlas.Descent);
            m_InspectorDetails.emplace_back("Preview", "Glyph atlas rendered from this font");
        }
        else m_InspectorReadError = "This font could not be decoded.";
    }
    else if (type == AssetType::Scene || type == AssetType::Prefab || type == AssetType::AnimationClip ||
        type == AssetType::AnimatorController || type == AssetType::TilePalette)
    {
        try
        {
            const auto document = YAML::Load(std::string(bytes.begin(), bytes.end()));
            if (!document.IsMap()) throw std::runtime_error("Expected an asset document.");
            if (type == AssetType::Scene || type == AssetType::Prefab)
            {
                if (document["SceneName"]) m_InspectorDetails.emplace_back("Scene", document["SceneName"].as<std::string>());
                const auto entities = document["Entities"];
                if (entities.IsSequence())
                {
                    row("Entities", entities.size());
                    for (size_t i = 0; i < std::min<size_t>(entities.size(), 12); ++i)
                    {
                        const auto tag = entities[i]["Tag"];
                        if (tag.IsMap() && tag["Tag"]) m_InspectorDetails.emplace_back("Object " + std::to_string(i+1), tag["Tag"].as<std::string>());
                    }
                    if (entities.size() > 12) m_InspectorDetails.emplace_back("More objects", "Open the asset to inspect the complete hierarchy.");
                }
            }
            else
            {
                const char* root = type == AssetType::AnimationClip ? "TomCatAnimationClip" :
                    type == AssetType::AnimatorController ? "TomCatAnimatorController" : "TomCatTilePalette";
                const auto content = document[root];
                if (!content.IsMap()) throw std::runtime_error("The asset document is missing its type header.");
                for (const auto& item : content)
                {
                    const auto key = item.first.as<std::string>();
                    if (key == "Version") continue;
                    std::string label;
                    for (const char c : key) { if (!label.empty() && std::isupper(static_cast<unsigned char>(c))) label += ' '; label += c; }
                    if (item.second.IsScalar()) m_InspectorDetails.emplace_back(label, item.second.Scalar());
                    else if (item.second.IsSequence())
                    {
                        if (key == "CellSize" || key == "CellGap")
                        {
                            std::string value;
                            for (const auto& part : item.second) { if (!value.empty()) value += ", "; value += part.as<std::string>(); }
                            m_InspectorDetails.emplace_back(label, value);
                        }
                        else m_InspectorDetails.emplace_back(label, std::to_string(item.second.size()));
                    }
                }
            }
        }
        catch (const std::exception& exception) { m_InspectorReadError = exception.what(); }
    }
}

void ContentBrowserPanel::OnAssetInspectorRender(bool* open)
{
    if (open && !*open) return;
    PrepareEditorToolWindow(ImVec2(520,660));
    if (BeginEditorWindow("Asset Inspector", open))
        DrawAssetInspector(m_InspectedPath.empty() ? m_SelectedPath : m_InspectedPath);
    ImGui::End();
}

void ContentBrowserPanel::DrawAssetInspector(const std::filesystem::path& requestedPath)
{
    if (requestedPath.empty()) { ImGui::TextWrapped("Select an asset in Project."); return; }
    const auto path = LexicalPath(requestedPath);
    if (GetRootForPath(path).empty()) { ImGui::TextWrapped("This asset is outside the current project."); return; }
    auto& assets = AssetManager::Get();
    const auto* liveMetadata = assets.GetRegistry().GetMetadata(path);
    const AssetMetadata metadata = liveMetadata ? *liveMetadata : AssetMetadata{};
    std::error_code error;
    const bool directory = std::filesystem::is_directory(path, error);
    const auto type = directory ? AssetType::None : liveMetadata ? metadata.Type : AssetTypeFromPath(path);
    const bool changed = m_InspectedPath != path;
    if (changed)
    {
        if (m_AssetSettingsDirty) m_ImportDrafts[m_InspectedPath] = { m_AssetSettingsDraft, m_AssetSettingsBase };
        else m_ImportDrafts.erase(m_InspectedPath);
        m_InspectedPath = path; m_InspectedAsset = metadata.Handle;
        const auto draft = m_ImportDrafts.find(path);
        m_AssetSettingsDraft = draft == m_ImportDrafts.end() ? metadata.ImportSettings : draft->second.Settings;
        m_AssetSettingsBase = draft == m_ImportDrafts.end() ? metadata.ImportSettings : draft->second.Base;
        m_AssetSettingsDirty = m_AssetSettingsDraft != m_AssetSettingsBase;
        m_AssetInspectorMessage.clear();
    }
    if (changed || ImGui::GetTime() >= m_NextInspectorRefresh)
    {
        const auto modified = std::filesystem::last_write_time(path, error);
        if (changed || modified != m_InspectorSourceTime || error) RefreshInspectorDetails(path, type, directory);
        if (!m_AssetSettingsDirty) m_AssetSettingsBase = m_AssetSettingsDraft = metadata.ImportSettings;
        m_NextInspectorRefresh = ImGui::GetTime() + 1.0;
    }
    ImGui::PushID("AssetDetails");
    const float iconSize = ImGui::GetFontSize() * 2.2f;
    auto icon = directory && m_Icons ? m_Icons->Get(EditorIcon::FolderClosed) : GetAssetIcon(path, false);
    if (icon)
    {
        const auto cursor = ImGui::GetCursorScreenPos();
        const float scale = iconSize / std::max(1u, std::max(icon->GetWidth(), icon->GetHeight()));
        const ImVec2 size(icon->GetWidth()*scale, icon->GetHeight()*scale);
        ImGui::Dummy(ImVec2(iconSize, iconSize));
        ImGui::GetWindowDrawList()->AddImage(ToImGuiTextureID(icon), cursor,
            ImVec2(cursor.x+size.x, cursor.y+size.y), ImVec2(0,1), ImVec2(1,0));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
    ImGui::TextWrapped("%s (%s)%s", AssetDisplayName(path, directory).c_str(),
        directory ? "Folder" : AssetTypeToString(type), m_AssetSettingsDirty ? " *" : "");
    ImGui::PopTextWrapPos();
    const bool exists = std::filesystem::exists(path, error);
    ImGui::BeginDisabled(!exists);
    if (ImGui::Button("Open"))
    {
        if (type == AssetType::CSharpScript) OpenCSharpScript(path);
        else if (directory || type == AssetType::Scene || type == AssetType::AnimationClip ||
            type == AssetType::AnimatorController || type == AssetType::TilePalette)
            OpenAsset(path, directory);
        else
        {
#ifdef TC_PLATFORM_WINDOWS
            // Reveal unknown files instead of executing arbitrary file associations.
            if (type == AssetType::Other || type == AssetType::None)
                ShellExecuteW(nullptr, L"open", path.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            else ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
        }
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::Separator();
    if (!exists || metadata.IsMissing) ImGui::TextWrapped("Source file is missing.");
    const bool writable = liveMetadata && m_AssetMutationsEnabled && IsWritablePath(path) && !IsReadOnlyPath(path) && exists;
    if (!directory && !IsWritablePath(path)) ImGui::TextDisabled("Read-only package asset");
    if (type == AssetType::Texture2D || type == AssetType::Shader)
    {
        ImGui::TextUnformatted("Import Settings");
        ImGui::BeginDisabled(!writable);
        auto boolean = [&](const char* label, const char* key, bool fallback) {
            auto it = m_AssetSettingsDraft.find(key);
            const std::string keyName(key);
            const char* alias = keyName == "sRGB" ? "srgb" : keyName == "generateMipmaps" ? "mipmaps" : key;
            if (it == m_AssetSettingsDraft.end()) it = m_AssetSettingsDraft.find(alias);
            bool value = it == m_AssetSettingsDraft.end() ? fallback :
                ToLower(it->second) == "true" || it->second == "1" || ToLower(it->second) == "yes";
            if (keyName == "sRGB")
                if (const auto space = m_AssetSettingsDraft.find("colorSpace"); space != m_AssetSettingsDraft.end())
                    value = ToLower(space->second) == "srgb";
            if (ImGui::Checkbox(label, &value))
            {
                m_AssetSettingsDraft.erase(alias);
                if (keyName == "sRGB") m_AssetSettingsDraft.erase("colorSpace");
                m_AssetSettingsDraft[key] = value ? "true" : "false";
            }
        };
        if (type == AssetType::Texture2D)
        {
            boolean("sRGB (Color Texture)", "sRGB", true);
            boolean("Generate Mipmaps", "generateMipmaps", true);
            const char* choices[] = { "Automatic", "RGBA32 (Uncompressed)", "BC3 / DXT5" };
            const auto it = m_AssetSettingsDraft.find("compression");
            const auto compression = it == m_AssetSettingsDraft.end() ? "auto" : ToLower(it->second);
            int selected = compression == "none" || compression == "rgba8" ? 1 : compression == "bc3" || compression == "dxt5" ? 2 : 0;
            ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.6f));
            if (ImGui::Combo("Compression", &selected, choices, 3))
                m_AssetSettingsDraft["compression"] = selected == 1 ? "none" : selected == 2 ? "bc3" : "auto";
            const auto mode = m_AssetSettingsDraft.find("SpriteMode");
            AssetProperty("Sprite Mode", mode == m_AssetSettingsDraft.end() ? "Single" : mode->second);
            const bool pendingSettings = m_AssetSettingsDraft != m_AssetSettingsBase;
            ImGui::BeginDisabled(pendingSettings);
            if (ImGui::Button("Sprite Editor")) BeginAtlasEditor(path);
            ImGui::EndDisabled();
            if (pendingSettings) ImGui::TextWrapped("Apply or Revert before opening Sprite Editor.");
        }
        else
        {
            boolean("Optimize", "optimize", true);
            boolean("Warnings as Errors", "warningsAsErrors", false);
        }
        m_AssetSettingsDirty = m_AssetSettingsDraft != m_AssetSettingsBase;
        const bool conflict = m_AssetSettingsDirty && metadata.ImportSettings != m_AssetSettingsBase;
        if (conflict) ImGui::TextWrapped("Import settings changed externally. Revert to reload the current settings.");
        ImGui::BeginDisabled(!m_AssetSettingsDirty);
        if (ImGui::Button("Revert"))
        {
            m_AssetSettingsBase = m_AssetSettingsDraft = metadata.ImportSettings;
            m_AssetSettingsDirty = false; m_ImportDrafts.erase(path); m_AssetInspectorMessage.clear();
        }
        ImGui::SameLine(); ImGui::BeginDisabled(conflict);
        if (ImGui::Button("Apply"))
        {
            if (assets.SetImportSettings(metadata.Handle, m_AssetSettingsDraft))
            {
                m_AssetSettingsBase = m_AssetSettingsDraft; m_AssetSettingsDirty = false; m_ImportDrafts.erase(path);
                m_AssetInspectorMessage = "Settings saved. Reimport queued; check Console for diagnostics.";
            }
            else m_AssetInspectorMessage = "Settings could not be saved. Your draft has been retained.";
        }
        ImGui::EndDisabled(); ImGui::EndDisabled(); ImGui::EndDisabled();
        if (!m_AssetInspectorMessage.empty()) ImGui::TextWrapped("%s", m_AssetInspectorMessage.c_str());
        ImGui::Separator();
    }
    if (type == AssetType::CSharpScript)
    {
        ImGui::TextUnformatted("Script Information");
        const auto script = m_ScriptMetadataProvider ? m_ScriptMetadataProvider(metadata.Handle) : std::nullopt;
        if (script)
        {
            AssetProperty("Assembly", "Assembly-CSharp.dll"); AssetProperty("Class", script->TypeName);
            AssetProperty("Execution Order", std::to_string(script->ExecutionOrder));
            AssetProperty("Multiple Components", script->DisallowMultiple ? "Disallowed" : "Allowed");
            if (ImGui::CollapsingHeader("Serialized Fields", ImGuiTreeNodeFlags_DefaultOpen))
                for (const auto& field : script->Fields)
                    if (!field.Hidden) AssetProperty(field.Name.c_str(), field.TypeName.empty() ? ScriptFieldTypeToString(field.Type) : field.TypeName);
            if (!script->EventMethods.empty() && ImGui::CollapsingHeader("Event Methods"))
                for (const auto& method : script->EventMethods) ImGui::BulletText("%s", method.c_str());
            ImGui::TextWrapped("Field values are edited on a component instance. Execution order is declared in the script.");
        }
        else ImGui::TextWrapped("Build project scripts to show compiled class and serialized-field information.");
        ImGui::Separator();
    }
    if (type == AssetType::Shader)
    {
        if (ImGui::Button("Compile and Inspect"))
        {
            std::vector<uint8_t> source, artifact; ShaderArtifactView shader;
            m_InspectorShaderResources.clear(); m_InspectorCompileMessage.clear();
            if (ReadInspectorBytes(path, source, m_InspectorCompileMessage, 4 * 1024 * 1024) &&
                BuildShaderArtifact(source, path, m_AssetSettingsDraft, "opengl", artifact, m_InspectorCompileMessage) &&
                ParseShaderArtifact(artifact, shader, m_InspectorCompileMessage))
            {
                m_InspectorCompileMessage = "Shader compilation succeeded for the active renderer.";
                for (const auto& stage : shader.Stages)
                    m_InspectorShaderResources.emplace_back(stage.Stage == ShaderArtifactStage::Vertex ? "Vertex" : "Fragment", std::string(stage.EntryPoint));
                for (const auto& resource : shader.Resources)
                    m_InspectorShaderResources.emplace_back(std::string(resource.Name),
                        "binding " + (resource.Binding == UINT32_MAX ? std::string("none") : std::to_string(resource.Binding)) +
                        ", array " + std::to_string(resource.ArraySize) + ", bytes " + std::to_string(resource.ByteSize));
            }
        }
        if (!m_InspectorCompileMessage.empty()) ImGui::TextWrapped("%s", m_InspectorCompileMessage.c_str());
        for (const auto& [label, value] : m_InspectorShaderResources) AssetProperty(label.c_str(), value);
    }
    if (!m_InspectorDetails.empty() && ImGui::CollapsingHeader(directory ? "Folder Information" : "Imported Object", ImGuiTreeNodeFlags_DefaultOpen))
        for (const auto& [label, value] : m_InspectorDetails) AssetProperty(label.c_str(), value);
    if (m_InspectorPreview && ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const float scale = std::min(std::max(1.0f, ImGui::GetContentRegionAvail().x) / m_InspectorPreview->GetWidth(),
            ImGui::GetFontSize()*12 / m_InspectorPreview->GetHeight());
        ImGui::Image(ToImGuiTextureID(m_InspectorPreview), ImVec2(m_InspectorPreview->GetWidth()*scale,
            m_InspectorPreview->GetHeight()*scale), ImVec2(0,1), ImVec2(1,0));
    }
    if (!m_InspectorReadError.empty()) ImGui::TextWrapped("%s", m_InspectorReadError.c_str());
    if (!metadata.SubAssets.empty() && ImGui::CollapsingHeader("Sub-assets"))
        for (const auto& child : metadata.SubAssets)
        {
            ImGui::PushID(child.Name.c_str());
            AssetProperty(child.Name.c_str(), std::string(AssetTypeToString(child.Type)) + " / " + std::to_string(static_cast<uint64_t>(child.Handle)));
            if (child.Type == AssetType::Texture2D)
                ImGui::Text("Rect: %u, %u, %u x %u | PPU: %.1f", child.Sprite.X, child.Sprite.Y,
                    child.Sprite.Width, child.Sprite.Height, child.Sprite.PixelsPerUnit);
            ImGui::PopID();
        }
    if (!m_InspectorSource.empty() && ImGui::CollapsingHeader("Source Preview",
        type == AssetType::CSharpScript || type == AssetType::Shader ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::BeginChild("Source", ImVec2(0, ImGui::GetFontSize()*14), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_InspectorSource.c_str());
        ImGui::EndChild();
    }
    if (ImGui::CollapsingHeader("Asset Information"))
    {
        AssetProperty("Path", PathToUTF8(path));
        AssetProperty("Access", IsWritablePath(path) && !IsReadOnlyPath(path) ? "Project asset" : "Read-only");
        if (!directory)
        {
            const auto size = std::filesystem::file_size(path, error);
            if (!error) AssetProperty("Source Size", std::to_string(size) + " bytes");
        }
        if (liveMetadata) AssetProperty("Asset ID", std::to_string(static_cast<uint64_t>(metadata.Handle)));
        if (ImGui::Button("Copy Path")) ImGui::SetClipboardText(PathToUTF8(path).c_str());
    }
    ImGui::PopID();
}
