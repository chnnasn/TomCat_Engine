// Shared by desktop EditorLayer and the embedded Web host.

		if (!m_ShowProjectSettingsPanel)
			return;
		if (m_ProjectSettingsDraftProject != m_CurrentProject)
			LoadProjectSettingsDraft();

		if (m_FocusProjectSettingsPanel)
		{
			ImGui::SetNextWindowFocus();
			m_FocusProjectSettingsPanel = false;
		}
		PrepareEditorToolWindow(ImVec2(900,680),ImVec2(640,420));
		if (!BeginEditorWindow("Project Settings", &m_ShowProjectSettingsPanel,
			ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		const bool hasProject = m_CurrentProject != nullptr;
		const bool editable = hasProject && !IsSceneRunning();
		if (!hasProject)
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Open a project to edit Player, Tags, Layers, and Physics 2D settings.");
		else if (IsSceneRunning())
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Project settings are read-only while the scene is running. Stop Play Mode to edit them.");

        static char settingsSearch[128]{};
        ImGui::SetNextItemWidth(-1);
        EditorSearchField("##SettingsSearch", "Search settings pages...", settingsSearch, sizeof(settingsSearch));
        auto matchesSettings = [&](const char* terms) {
            std::string query(settingsSearch), text(terms);
            for (char& c : query) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text.find(query) != std::string::npos;
        };
        ImGui::BeginChild("##ProjectSettingsNavigation", ImVec2(std::max(180.0f,ImGui::GetFontSize()*7.8f), 0), true);
        const char* pages[] = { "Tags and Layers", "Physics 2D", "Player" };
        const char* terms[] = { "tags layers names", "physics 2d collision matrix", "player product company version icon display width height window vsync directories" };
        for (int page = 0; page < 3; ++page)
            if (matchesSettings(terms[page]) && ImGui::Selectable(pages[page], m_ProjectSettingsPage == page)) m_ProjectSettingsPage = page;
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##ProjectSettingsPage", ImVec2(0.0f, 0.0f), true);
        auto settingRow = [](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(std::max(165.0f,ImGui::GetFontSize()*6.5f));
            ImGui::SetNextItemWidth(-1);
            return (std::string("##") + label);
        };
		ImGui::BeginDisabled(!editable);
		if (m_ProjectSettingsPage == 0)
		{
			ImGui::TextUnformatted("Tags");
			ImGui::Separator();
			ImGui::TextDisabled("Untagged is reserved and cannot be removed.");
			std::size_t tagToRemove = static_cast<std::size_t>(-1);
			const auto& tags = m_ProjectSettingsDraft.TagsAndLayers.Tags;
			for (std::size_t index = 0; index < tags.size(); ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				ImGui::TextUnformatted(tags[index].empty() ? "<Empty>" : tags[index].c_str());
				if (index == 0)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(Reserved)");
				}
				else
				{
					const float removeWidth = ImGui::CalcTextSize("Remove").x +
						ImGui::GetStyle().FramePadding.x * 2.0f;
					ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
						ImGui::GetWindowContentRegionMax().x - removeWidth));
					if (ImGui::SmallButton("Remove"))
						tagToRemove = index;
				}
				ImGui::PopID();
			}
			if (tagToRemove != static_cast<std::size_t>(-1))
			{
				m_ProjectSettingsDraft.TagsAndLayers.Tags.erase(
					m_ProjectSettingsDraft.TagsAndLayers.Tags.begin() + tagToRemove);
				PersistProjectSettingsDraft();
			}

			ImGui::Spacing();
			const float addButtonWidth = ImGui::CalcTextSize("Add Tag").x +
				ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(std::max(80.0f,
				ImGui::GetContentRegionAvail().x - addButtonWidth - ImGui::GetStyle().ItemSpacing.x));
			bool addTag = ImGui::InputTextWithHint("##NewProjectTag", "New tag",
				m_NewProjectTagBuffer.data(), m_NewProjectTagBuffer.size(),
				ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			addTag |= ImGui::Button("Add Tag");
			if (addTag)
			{
				const std::string newTag = TrimASCIIWhitespace(m_NewProjectTagBuffer.data());
				if (newTag.empty())
				{
					ClearProjectSettingsFeedback();
					m_ProjectSettingsError = "Tag names cannot be empty.";
				}
				else if (std::find(m_ProjectSettingsDraft.TagsAndLayers.Tags.begin(),
					m_ProjectSettingsDraft.TagsAndLayers.Tags.end(), newTag)
					!= m_ProjectSettingsDraft.TagsAndLayers.Tags.end())
				{
					ClearProjectSettingsFeedback();
					m_ProjectSettingsError = "A tag with that name already exists.";
				}
				else
				{
					m_ProjectSettingsDraft.TagsAndLayers.Tags.push_back(newTag);
					if (PersistProjectSettingsDraft())
						m_NewProjectTagBuffer.fill('\0');
				}
			}

			ImGui::Spacing();
			ImGui::TextUnformatted("Layers");
			ImGui::Separator();
			ImGui::TextWrapped("Layer slots are stable. Clear a name to hide that layer from entity menus.");
			for (std::size_t layer = 0; layer < Physics2DLayerCount; ++layer)
			{
				ImGui::PushID(static_cast<int>(layer));
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%02u", static_cast<unsigned int>(layer));
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::BeginDisabled(layer == 0);
				if (ImGui::InputText("##LayerName", m_ProjectLayerNameBuffers[layer].data(),
					m_ProjectLayerNameBuffers[layer].size()))
				{
					m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer] =
						m_ProjectLayerNameBuffers[layer].data();
					PersistProjectSettingsDraft();
				}
				ImGui::EndDisabled();
				if (layer == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Layer 0 is reserved as Default.");
				ImGui::PopID();
			}
		}
		else if (m_ProjectSettingsPage == 1)
		{
			ImGui::TextUnformatted("Physics 2D Layer Collision Matrix");
			ImGui::Separator();
			ImGui::TextWrapped("A checked cell allows the two named entity layers to collide. The matrix is symmetric.");

			std::vector<uint8_t> namedLayers;
			for (uint8_t layer = 0; layer < Physics2DLayerCount; ++layer)
			{
				if (!m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layer].empty())
					namedLayers.push_back(layer);
			}
			if (ImGui::Button("Enable All"))
			{
				for (std::size_t row = 0; row < namedLayers.size(); ++row)
					for (std::size_t column = 0; column <= row; ++column)
						m_ProjectSettingsDraft.Physics2D.SetLayersCollide(
							namedLayers[row], namedLayers[column], true);
				PersistProjectSettingsDraft();
			}
			ImGui::SameLine();
			if (ImGui::Button("Disable All"))
			{
				for (std::size_t row = 0; row < namedLayers.size(); ++row)
					for (std::size_t column = 0; column <= row; ++column)
						m_ProjectSettingsDraft.Physics2D.SetLayersCollide(
							namedLayers[row], namedLayers[column], false);
				PersistProjectSettingsDraft();
			}

			if (namedLayers.empty())
				ImGui::TextDisabled("Define at least one named layer on the Tags and Layers page.");
			else
			{
				const ImGuiTableFlags flags = ImGuiTableFlags_Borders |
					ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
					ImGuiTableFlags_ScrollX;
				const float matrixHeight = std::min(430.0f,
					(static_cast<float>(namedLayers.size()) + 2.0f) * ImGui::GetFrameHeightWithSpacing());
				if (ImGui::BeginTable("##Physics2DLayerMatrix",
					static_cast<int>(namedLayers.size()) + 1, flags, ImVec2(0.0f, matrixHeight)))
				{
					ImGui::TableSetupScrollFreeze(1, 1);
					ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 170.0f);
					std::array<std::string, Physics2DLayerCount> columnLabels;
					for (std::size_t column = 0; column < namedLayers.size(); ++column)
					{
						columnLabels[column] = m_ProjectSettingsDraft.TagsAndLayers.LayerNames[namedLayers[column]];
						ImGui::TableSetupColumn(columnLabels[column].c_str(),
							ImGuiTableColumnFlags_WidthFixed, 88.0f);
					}
					ImGui::TableHeadersRow();

					for (std::size_t row = 0; row < namedLayers.size(); ++row)
					{
						const uint8_t layerA = namedLayers[row];
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						const std::string rowLabel = std::to_string(
							static_cast<unsigned int>(layerA)) + "  " +
							m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerA];
						ImGui::TextUnformatted(rowLabel.c_str());
						for (std::size_t column = 0; column < namedLayers.size(); ++column)
						{
							ImGui::TableSetColumnIndex(static_cast<int>(column) + 1);
							if (column > row)
							{
								ImGui::TextDisabled("-");
								continue;
							}
							const uint8_t layerB = namedLayers[column];
							bool collide = m_ProjectSettingsDraft.Physics2D.CanLayersCollide(layerA, layerB);
							ImGui::PushID(static_cast<int>(layerA) * 32 + layerB);
							if (ImGui::Checkbox("##Collide", &collide))
							{
								m_ProjectSettingsDraft.Physics2D.SetLayersCollide(layerA, layerB, collide);
								PersistProjectSettingsDraft();
							}
							if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
								ImGui::SetTooltip("%s / %s",
									m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerA].c_str(),
									m_ProjectSettingsDraft.TagsAndLayers.LayerNames[layerB].c_str());
							ImGui::PopID();
						}
					}
					ImGui::EndTable();
				}
			}
		}
		else
		{
			ImGui::TextUnformatted("Player Identity");
			ImGui::Separator();
			auto drawString = [&](const char* label, auto& buffer, std::string& value)
			{
				if (ImGui::InputText(settingRow(label).c_str(), buffer.data(), buffer.size()))
					value = buffer.data();
				if (ImGui::IsItemDeactivatedAfterEdit())
					PersistPlayerSettingsDraft();
			};
			drawString("Product Name", m_PlayerProductNameBuffer,
				m_PlayerSettingsDraft.ProductName);
			drawString("Company Name", m_PlayerCompanyNameBuffer,
				m_PlayerSettingsDraft.CompanyName);
			drawString("Version", m_PlayerVersionBuffer,
				m_PlayerSettingsDraft.Version);

            const AssetMetadata* iconMetadata = AssetManager::Get().GetRegistry().GetMetadata(m_PlayerSettingsDraft.Icon);
            const std::string iconName = iconMetadata ? PathToUTF8(iconMetadata->FilePath.filename()) : "None (Texture2D)";
            if (ImGui::BeginCombo(settingRow("Icon").c_str(), iconName.c_str()))
            {
                if (ImGui::Selectable("None")) { m_PlayerSettingsDraft.Icon = AssetHandle(0); PersistPlayerSettingsDraft(); }
                for (const auto& [handle, metadata] : AssetManager::Get().GetRegistry().GetAssets())
                    if (!metadata.IsMissing && metadata.Type == AssetType::Texture2D)
                    {
                        ImGui::PushID(std::to_string(static_cast<uint64_t>(handle)).c_str());
                        if (ImGui::Selectable(PathToUTF8(metadata.FilePath).c_str())) { m_PlayerSettingsDraft.Icon = handle; PersistPlayerSettingsDraft(); }
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(
							*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = AssetManager::Get()
							.GetRegistry().GetMetadata(handle);
						if (metadata && !metadata->IsMissing
							&& metadata->Type == AssetType::Texture2D)
						{
							m_PlayerSettingsDraft.Icon = handle;
							PersistPlayerSettingsDraft();
						}
						else
							m_ProjectSettingsError =
								"Player icon must be a live Texture2D asset.";
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("Select an icon, or drag a Texture2D asset onto the field."); ImGui::PopTextWrapPos();

			ImGui::Spacing();
			ImGui::TextUnformatted("Display");
			ImGui::Separator();
#ifdef __EMSCRIPTEN__
			ImGui::TextWrapped("Desktop Player options are preserved for export; browser preview follows its Game view.");
			ImGui::BeginDisabled();
#endif
			if (ImGui::InputScalar(settingRow("Width").c_str(), ImGuiDataType_U32,
				&m_PlayerSettingsDraft.Width))
				PersistPlayerSettingsDraft();
			if (ImGui::InputScalar(settingRow("Height").c_str(), ImGuiDataType_U32,
				&m_PlayerSettingsDraft.Height))
				PersistPlayerSettingsDraft();
			const char* windowModes[] = {
				"Windowed", "Borderless", "Exclusive Fullscreen"
			};
			int windowMode = static_cast<int>(m_PlayerSettingsDraft.WindowMode);
			if (ImGui::Combo(settingRow("Window Mode").c_str(), &windowMode, windowModes,
				static_cast<int>(std::size(windowModes))))
			{
				m_PlayerSettingsDraft.WindowMode =
					static_cast<PlayerWindowMode>(windowMode);
				PersistPlayerSettingsDraft();
			}
			if (ImGui::Checkbox(settingRow("Resizable").c_str(), &m_PlayerSettingsDraft.Resizable))
				PersistPlayerSettingsDraft();
			if (ImGui::Checkbox(settingRow("VSync").c_str(), &m_PlayerSettingsDraft.VSync))
				PersistPlayerSettingsDraft();

			ImGui::Spacing();
			ImGui::TextUnformatted("Per-user Directories");
			ImGui::Separator();
			auto drawDirectory = [&](const char* label, auto& buffer,
				std::filesystem::path& value)
			{
				if (ImGui::InputText(settingRow(label).c_str(), buffer.data(), buffer.size()))
					value = UTF8ToPath(buffer.data());
				if (ImGui::IsItemDeactivatedAfterEdit())
					PersistPlayerSettingsDraft();
			};
			drawDirectory("Save Directory", m_PlayerSaveDirectoryBuffer,
				m_PlayerSettingsDraft.SaveDirectory);
			drawDirectory("Log Directory", m_PlayerLogDirectoryBuffer,
				m_PlayerSettingsDraft.LogDirectory);
			drawDirectory("Crash Directory", m_PlayerCrashDirectoryBuffer,
				m_PlayerSettingsDraft.CrashDirectory);
			ImGui::TextWrapped("Directories must be relative and remain under the game's per-user data root.");
#ifdef __EMSCRIPTEN__
			ImGui::EndDisabled();
#endif
		}
		ImGui::EndDisabled();

		ImGui::Separator();
#ifdef __EMSCRIPTEN__
		ImGui::TextWrapped("Changes apply immediately. Use File > Save to retain them in this browser, or Export project for a backup.");
#else
		ImGui::TextWrapped(m_ProjectSettingsPage == 2
			? "Valid changes are saved automatically to ProjectSettings/PlayerSettings.json."
			: "Valid changes are saved automatically to ProjectSettings/ProjectSettings.json.");
#endif
		if (!m_ProjectSettingsError.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("Error: %s", m_ProjectSettingsError.c_str());
			ImGui::PopStyleColor();
		}
		else if (!m_ProjectSettingsStatus.empty())
			ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s",
				m_ProjectSettingsStatus.c_str());
		ImGui::EndChild();
		ImGui::End();
