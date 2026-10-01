#include "EditorLayer.h"
#include "EditorPlayToolbar.h"
#include "SceneToolbarDrawing.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef TC_PLATFORM_WINDOWS
	#include <Windows.h>
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Scene/Serialization/SceneArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Editor/EditorShortcutRouter.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptDiagnosticSink.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Runtime/RuntimeUI.h"

#include "Player/PlayerBuilder.h"
#include "ImGuizmo.h"

#include "EditorLayerDetail.h"
#include "EditorBuildController.h"

#include "EditorLayer.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Utils/PlatformUtils.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Project/ProjectManager.h"
#include "TomCat/Scripting/ManagedRuntimeFactory.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Runtime/RuntimeUI.h"
#include "Player/PlayerBuilder.h"
#include "ImGuizmo.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace TomCat {

using namespace EditorLayerDetail;

	void EditorBuildController::UI_BuildSettings()
	{
		if (!m_Layer.m_ShowBuildSettingsPanel)
			return;

		if (m_Layer.m_FocusBuildSettingsPanel)
		{
			ImGui::SetNextWindowFocus();
			m_Layer.m_FocusBuildSettingsPanel = false;
		}
		PrepareEditorToolWindow(ImVec2(920,740),ImVec2(680,500));
		const bool buildSettingsVisible = BeginEditorWindow("Build Settings",
			&m_Layer.m_ShowBuildSettingsPanel, ImGuiWindowFlags_NoDocking);
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			m_Layer.m_EditorPanelCycleIndex = 0;
		if (!buildSettingsVisible)
		{
			ImGui::End();
			return;
		}

		if (!m_Layer.m_CurrentProject)
			ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.25f, 1.0f),
				"Open a project to inspect its scenes and Player settings.");

		ImGui::BeginChild("BuildContent", ImVec2(0, -100.0f));
		ImGui::TextUnformatted("Scenes In Build");
		BuildSettings edited = m_Layer.m_CurrentProject
			? m_Layer.m_CurrentProject->GetBuildSettings() : BuildSettings{};
		const bool buildSettingsEditable = m_Layer.m_CurrentProject && !m_Layer.IsSceneRunning();
		bool buildSettingsChanged = false;
		std::string buildSettingsChangeDescription;
		std::optional<std::size_t> removeScene;
		std::optional<std::pair<std::size_t, std::size_t>> moveScene;
		AssetRegistry& registry = AssetManager::Get().GetRegistry();

		ImGui::BeginChild("##ScenesInBuild", ImVec2(0.0f, 225.0f), true,
			ImGuiWindowFlags_HorizontalScrollbar);
		if (m_Layer.m_CurrentProject && !edited.Scenes.empty())
		{
			const ImGuiTableFlags sceneFlags = ImGuiTableFlags_RowBg |
				ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_ScrollY;
			if (ImGui::BeginTable("##SceneBuildRows", 6, sceneFlags))
			{
				ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 34.0f);
				ImGui::TableSetupColumn("Entry", ImGuiTableColumnFlags_WidthFixed, 42.0f);
				ImGui::TableSetupColumn("Scene / Path Hint", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 42.0f);
				ImGui::TableSetupColumn("Order", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 56.0f);
				ImGui::TableHeadersRow();
				uint32_t enabledIndex = 0;
				for (std::size_t index = 0; index < edited.Scenes.size(); ++index)
				{
					BuildSceneSettings& scene = edited.Scenes[index];
					const AssetMetadata* metadata = registry.GetMetadata(scene.Handle);
					const bool live = metadata && !metadata->IsMissing
						&& metadata->Type == AssetType::Scene;
					const bool hasResolvedPath = metadata && !metadata->IsMissing;
					const std::filesystem::path shownPath = hasResolvedPath
						? metadata->FilePath : scene.PathHint;
					std::string label = PathToUTF8(shownPath);
					if (label.empty())
						label = "Scene " + std::to_string(static_cast<uint64_t>(scene.Handle));

					ImGui::PushID(static_cast<int>(index));
					ImGui::TableNextRow(0, ImGui::GetFrameHeightWithSpacing());
					ImGui::TableSetColumnIndex(0);
					bool enabled = scene.Enabled;
					ImGui::BeginDisabled(!buildSettingsEditable);
					if (ImGui::Checkbox("##Enabled", &enabled))
					{
						scene.Enabled = enabled;
						buildSettingsChanged = true;
						buildSettingsChangeDescription = enabled
							? "Enabled build scene." : "Disabled build scene.";
					}
					ImGui::EndDisabled();

					ImGui::TableSetColumnIndex(1);
					const bool isEntry = edited.EntrySceneHandle == scene.Handle;
					ImGui::BeginDisabled(!buildSettingsEditable);
					if (ImGui::RadioButton("##Entry", isEntry))
					{
						scene.Enabled = true;
						edited.EntrySceneHandle = scene.Handle;
						buildSettingsChanged = true;
						buildSettingsChangeDescription = "Changed Entry Scene.";
					}
					ImGui::EndDisabled();

					ImGui::TableSetColumnIndex(2);
					ImGui::AlignTextToFramePadding();
					if (live)
						ImGui::TextUnformatted(label.c_str());
					else
						ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
							"%s (%s)", label.c_str(), !metadata || metadata->IsMissing
								? "Missing" : "Wrong Type");
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
					{
						const std::string pathHint = PathToUTF8(scene.PathHint);
						ImGui::SetTooltip("Handle: %llu\nPathHint: %s",
							static_cast<unsigned long long>(static_cast<uint64_t>(scene.Handle)),
							pathHint.empty() ? "<no path hint>" : pathHint.c_str());
					}

					ImGui::TableSetColumnIndex(3);
					ImGui::AlignTextToFramePadding();
					if (scene.Enabled)
						ImGui::Text("%u", enabledIndex++);
					else
						ImGui::TextDisabled("-");

					ImGui::TableSetColumnIndex(4);
					ImGui::BeginDisabled(!buildSettingsEditable || index == 0);
					if (ImGui::SmallButton("^"))
						moveScene = std::pair{ index, index - 1 };
					ImGui::EndDisabled();
					ImGui::SameLine(0.0f, 2.0f);
					ImGui::BeginDisabled(!buildSettingsEditable || index + 1 >= edited.Scenes.size());
					if (ImGui::SmallButton("v"))
						moveScene = std::pair{ index, index + 1 };
					ImGui::EndDisabled();

					ImGui::TableSetColumnIndex(5);
					ImGui::BeginDisabled(!buildSettingsEditable);
					if (ImGui::SmallButton("Remove"))
						removeScene = index;
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}
		else
			ImGui::TextDisabled(m_Layer.m_CurrentProject
				? "No scenes are configured. Add the saved open Scene or drag a Scene asset here."
				: "No project is open.");
		ImGui::EndChild();

		if (buildSettingsEditable && ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
				AssetDragDropPayloadID))
			{
				if (payload->DataSize == sizeof(uint64_t))
				{
					const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
					const AssetMetadata* metadata = registry.GetMetadata(handle);
					if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Scene)
					{
						m_BuildState.BuildSettingsSucceeded = false;
						m_BuildState.BuildSettingsStatus = "Only live Scene assets can be added to Scenes In Build.";
					}
					else if (std::any_of(edited.Scenes.begin(), edited.Scenes.end(),
						[handle](const BuildSceneSettings& scene) { return scene.Handle == handle; }))
					{
						m_BuildState.BuildSettingsSucceeded = false;
						m_BuildState.BuildSettingsStatus = "That Scene is already in the build list.";
					}
					else
					{
						edited.Scenes.push_back({ handle, true, metadata->FilePath });
						if (static_cast<uint64_t>(edited.EntrySceneHandle) == 0)
							edited.EntrySceneHandle = handle;
						buildSettingsChanged = true;
						buildSettingsChangeDescription = "Added dropped Scene asset.";
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (moveScene)
		{
			std::swap(edited.Scenes[moveScene->first], edited.Scenes[moveScene->second]);
			buildSettingsChanged = true;
			buildSettingsChangeDescription = "Reordered build scenes.";
		}
		if (removeScene)
		{
			const AssetHandle removedHandle = edited.Scenes[*removeScene].Handle;
			edited.Scenes.erase(edited.Scenes.begin() + static_cast<std::ptrdiff_t>(*removeScene));
			if (edited.EntrySceneHandle == removedHandle)
				edited.EntrySceneHandle = AssetHandle(0);
			buildSettingsChanged = true;
			buildSettingsChangeDescription = "Removed build scene.";
		}
		const auto entryIsEnabled = [&edited]()
		{
			return std::any_of(edited.Scenes.begin(), edited.Scenes.end(),
				[&](const BuildSceneSettings& scene)
				{
					return scene.Enabled && scene.Handle == edited.EntrySceneHandle;
				});
		};
		if (buildSettingsChanged && !entryIsEnabled())
		{
			edited.EntrySceneHandle = AssetHandle(0);
			for (const BuildSceneSettings& scene : edited.Scenes)
			{
				if (scene.Enabled)
				{
					edited.EntrySceneHandle = scene.Handle;
					break;
				}
			}
		}

		const AssetHandle openSceneHandle = m_Layer.GetSavedEditorSceneHandle();
		const bool openSceneAlreadyAdded = std::any_of(edited.Scenes.begin(), edited.Scenes.end(),
			[openSceneHandle](const BuildSceneSettings& scene)
			{
				return scene.Handle == openSceneHandle;
			});
		const bool canAddOpenScene = buildSettingsEditable
			&& static_cast<uint64_t>(openSceneHandle) != 0 && !openSceneAlreadyAdded;
		const float addOpenScenesWidth = ImGui::CalcTextSize("Add Open Scene").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
			ImGui::GetWindowContentRegionMax().x - addOpenScenesWidth));
		ImGui::BeginDisabled(!canAddOpenScene);
		if (ImGui::Button("Add Open Scene"))
		{
			const AssetMetadata* metadata = registry.GetMetadata(openSceneHandle);
			if (metadata && !metadata->IsMissing && metadata->Type == AssetType::Scene)
			{
				edited.Scenes.push_back({ openSceneHandle, true, metadata->FilePath });
				if (static_cast<uint64_t>(edited.EntrySceneHandle) == 0)
					edited.EntrySceneHandle = openSceneHandle;
				buildSettingsChanged = true;
				buildSettingsChangeDescription = "Added the saved open Scene.";
			}
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (!m_Layer.m_CurrentProject)
				ImGui::SetTooltip("Open a project first.");
			else if (m_Layer.IsSceneRunning())
				ImGui::SetTooltip("Stop Play Mode before changing Build Settings.");
			else if (static_cast<uint64_t>(openSceneHandle) == 0)
				ImGui::SetTooltip("Save the current Scene inside the project's Assets directory first.");
			else if (openSceneAlreadyAdded)
				ImGui::SetTooltip("The open Scene is already in the build list.");
		}

		if (buildSettingsChanged && m_Layer.m_CurrentProject)
		{
			if (m_Layer.m_CurrentProject->SetBuildSettings(edited))
			{
				m_BuildState.BuildSettingsSucceeded = true;
				m_BuildState.BuildSettingsStatus = buildSettingsChangeDescription
					+ " Saved to ProjectSettings/BuildSettings.json.";
			}
			else
			{
				m_BuildState.BuildSettingsSucceeded = false;
				m_BuildState.BuildSettingsStatus =
					"Build Settings could not be saved; the previous file and list were preserved.";
			}
		}
		if (!m_BuildState.BuildSettingsStatus.empty())
		{
			const ImVec4 color = m_BuildState.BuildSettingsSucceeded
				? ImVec4(0.45f, 0.86f, 0.48f, 1.0f)
				: ImVec4(0.95f, 0.42f, 0.38f, 1.0f);
			ImGui::TextColored(color, "%s", m_BuildState.BuildSettingsStatus.c_str());
		}

		const BuildSettings& persistedBuildSettings = m_Layer.m_CurrentProject
			? m_Layer.m_CurrentProject->GetBuildSettings() : edited;
		const auto persistedEntry = std::find_if(persistedBuildSettings.Scenes.begin(),
			persistedBuildSettings.Scenes.end(), [&](const BuildSceneSettings& scene)
			{
				return scene.Enabled
					&& scene.Handle == persistedBuildSettings.EntrySceneHandle;
			});
		const AssetMetadata* entrySceneMetadata = persistedEntry != persistedBuildSettings.Scenes.end()
			? registry.GetMetadata(persistedBuildSettings.EntrySceneHandle) : nullptr;
		const bool entrySceneReady = entrySceneMetadata && !entrySceneMetadata->IsMissing
			&& entrySceneMetadata->Type == AssetType::Scene;

		ImGui::TextUnformatted("Platform");
		const float platformHeight = std::clamp(ImGui::GetContentRegionAvail().y - 150.0f,
			245.0f, 355.0f);
		const ImGuiTableFlags platformFlags = ImGuiTableFlags_Resizable |
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##BuildPlatformColumns", 2, platformFlags,
			ImVec2(0.0f, platformHeight)))
		{
			ImGui::TableSetupColumn("Targets", ImGuiTableColumnFlags_WidthStretch, 0.42f);
			ImGui::TableSetupColumn("Options", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::BeginChild("##PlatformList", ImVec2(0.0f, platformHeight), true);

			auto drawPlatformGlyph = [](int kind, const ImVec2& rowMinimum,
				float rowHeight, ImU32 color)
			{
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const float left = rowMinimum.x + 10.0f;
				const float centerY = rowMinimum.y + rowHeight * 0.5f;
				if (kind == 0)
				{
					draw->AddRect(ImVec2(left, centerY - 8.0f), ImVec2(left + 24.0f, centerY + 6.0f),
						color, 2.0f, 0, 1.5f);
					draw->AddLine(ImVec2(left + 12.0f, centerY + 6.0f),
						ImVec2(left + 12.0f, centerY + 10.0f), color, 1.5f);
					draw->AddLine(ImVec2(left + 7.0f, centerY + 10.0f),
						ImVec2(left + 17.0f, centerY + 10.0f), color, 1.5f);
				}
				else if (kind == 1)
				{
					for (int row = -1; row <= 1; ++row)
					{
						const float top = centerY + static_cast<float>(row) * 7.0f - 2.5f;
						draw->AddRect(ImVec2(left, top), ImVec2(left + 24.0f, top + 5.0f),
							color, 1.0f, 0, 1.2f);
						draw->AddCircleFilled(ImVec2(left + 4.0f, top + 2.5f), 1.0f, color);
					}
				}
				else if (kind == 2)
				{
					draw->AddRect(ImVec2(left + 5.0f, centerY - 11.0f),
						ImVec2(left + 19.0f, centerY + 11.0f), color, 2.0f, 0, 1.5f);
					draw->AddCircleFilled(ImVec2(left + 12.0f, centerY + 8.0f), 1.0f, color);
				}
				else if (kind == 3)
				{
					draw->AddCircle(ImVec2(left + 12.0f, centerY), 10.0f, color, 16, 1.3f);
					draw->AddLine(ImVec2(left + 2.0f, centerY),
						ImVec2(left + 22.0f, centerY), color, 1.0f);
					draw->AddLine(ImVec2(left + 12.0f, centerY - 10.0f),
						ImVec2(left + 12.0f, centerY + 10.0f), color, 1.0f);
				}
				else
				{
					draw->AddRect(ImVec2(left + 1.0f, centerY - 7.0f),
						ImVec2(left + 23.0f, centerY + 7.0f), color, 2.0f, 0, 1.3f);
				}
			};

			auto drawPlatformRow = [&](const char* label, bool selected, bool disabled, int glyph)
			{
				ImGui::PushID(label);
				if (disabled)
					ImGui::BeginDisabled();
				ImGui::Selectable("##PlatformTarget", selected, ImGuiSelectableFlags_None,
					ImVec2(0.0f, 39.0f));
				const ImVec2 rowMinimum = ImGui::GetItemRectMin();
				const float rowHeight = ImGui::GetItemRectSize().y;
				const ImVec4 textColor = ImGui::GetStyleColorVec4(disabled
					? ImGuiCol_TextDisabled : ImGuiCol_Text);
				const ImU32 packedColor = ImGui::ColorConvertFloat4ToU32(textColor);
				drawPlatformGlyph(glyph, rowMinimum, rowHeight, packedColor);
				const ImVec2 textSize = ImGui::CalcTextSize(label);
				ImGui::GetWindowDrawList()->AddText(
					ImVec2(rowMinimum.x + 43.0f,
						rowMinimum.y + (rowHeight - textSize.y) * 0.5f), packedColor, label);
				if (disabled)
					ImGui::EndDisabled();
				ImGui::PopID();
			};

            drawPlatformRow("Windows (64-bit)", true, false, 0);
            ImGui::TextWrapped("This build pipeline exports Windows players. Other targets are not available here.");
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(1);
			ImGui::BeginChild("##PlatformOptions", ImVec2(0.0f, platformHeight), false);
			const ImVec2 headerStart = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(34.0f, 28.0f));
			drawPlatformGlyph(0, headerStart, 28.0f,
				ImGui::GetColorU32(ImGuiCol_Text));
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Windows (64-bit)");
			ImGui::Separator();

			const ImGuiTableFlags optionFlags = ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##BuildTargetOptions", 2, optionFlags))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.55f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.45f);
				auto drawDisabledCombo = [](const char* label, const char* id, const char* preview)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::BeginDisabled();
					if (ImGui::BeginCombo(id, preview))
						ImGui::EndCombo();
					ImGui::EndDisabled();
				};
				auto drawDisabledCheckbox = [](const char* label, const char* id, bool muted)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					if (muted)
						ImGui::TextDisabled("%s", label);
					else
						ImGui::TextUnformatted(label);
					ImGui::TableSetColumnIndex(1);
					bool value = false;
					ImGui::BeginDisabled();
					ImGui::Checkbox(id, &value);
					ImGui::EndDisabled();
				};

				drawDisabledCombo("Target Platform", "##TargetPlatform", "Windows");
				drawDisabledCombo("Architecture", "##Architecture", "Intel 64-bit");
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("Development Build", &m_BuildState.DevelopmentBuild);
                ImGui::TextWrapped(m_BuildState.DevelopmentBuild ? "Unoptimized C# with portable symbols" : "Optimized C# with portable symbols");
				ImGui::EndTable();
			}
			ImGui::EndChild();
			ImGui::EndTable();
		}


		ImGui::Separator();
		ImGui::EndChild();
		if (ImGui::Button("Player Settings..."))
			m_Layer.OpenProjectSettingsPanel();

		const float buildWidth = ImGui::CalcTextSize("Build").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f + 22.0f;
		const float buildAndRunWidth = ImGui::CalcTextSize("Build And Run").x
			+ ImGui::GetStyle().FramePadding.x * 2.0f;
		const float buildButtonsWidth = buildWidth + buildAndRunWidth
			+ ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f,
			ImGui::GetWindowContentRegionMax().x - buildButtonsWidth));
		const bool canBuild = m_Layer.m_CurrentProject && entrySceneReady &&
			!m_Layer.IsSceneRunning() && !m_Layer.m_ScriptCompiler.IsCompileInProgress();
		ImGui::BeginDisabled(!canBuild);
		const bool buildPressed = ImGui::Button("Build", ImVec2(buildWidth, 0.0f));
		const bool buildHovered = ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::SameLine();
		const bool buildAndRunPressed = ImGui::Button("Build And Run",
			ImVec2(buildAndRunWidth, 0.0f));
		const bool buildAndRunHovered = ImGui::IsItemHovered(
			ImGuiHoveredFlags_AllowWhenDisabled);
		ImGui::EndDisabled();
		if (!canBuild && (buildHovered || buildAndRunHovered))
		{
			if (!m_Layer.m_CurrentProject)
				ImGui::SetTooltip("Open a project before building a Player.");
			else if (!entrySceneReady)
				ImGui::SetTooltip("Choose an enabled, live Entry Scene in Build Settings.");
			else if (m_Layer.IsSceneRunning())
				ImGui::SetTooltip("Stop Play mode before building a Player.");
			else
				ImGui::SetTooltip("Wait for the background C# compilation to finish.");
		}
		if (buildPressed)
			BuildPlayer(false);
		else if (buildAndRunPressed)
			BuildPlayer(true);

		if (!m_BuildState.PlayerBuildStatus.empty())
		{
			ImGui::Spacing();
			const ImVec4 color = m_BuildState.PlayerBuildSucceeded
				? ImVec4(0.45f, 0.86f, 0.48f, 1.0f)
				: ImVec4(0.95f, 0.42f, 0.38f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, color);
			ImGui::TextWrapped("%s", m_BuildState.PlayerBuildStatus.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::End();
	}

	bool EditorBuildController::BuildPlayer(bool runAfterBuild)
	{
		m_BuildState.PlayerBuildSucceeded = false;
		m_BuildState.PlayerBuildStatus = "Building Player...";
		auto fail = [this](std::string message)
		{
			m_BuildState.PlayerBuildSucceeded = false;
			m_BuildState.PlayerBuildStatus = std::move(message);
			m_Layer.m_ShowConsolePanel = true;
			m_Layer.m_PendingPanelFocus = "Console";
			m_Layer.m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				m_BuildState.PlayerBuildStatus, "Player Build");
			TC_Core_Error("{0}", m_BuildState.PlayerBuildStatus);
			return false;
		};

		if (!m_Layer.m_CurrentProject)
			return fail("Player build failed: no project is open.");
		if (m_Layer.IsSceneRunning())
			return fail("Player build failed: stop Play mode before building.");
		if (m_Layer.m_ScriptCompiler.IsCompileInProgress())
			return fail("Player build failed: the background C# compilation is still running.");

		const BuildSettings& buildSettings = m_Layer.m_CurrentProject->GetBuildSettings();
		const auto entry = std::find_if(buildSettings.Scenes.begin(),
			buildSettings.Scenes.end(), [&](const BuildSceneSettings& scene)
			{
				return scene.Enabled && scene.Handle == buildSettings.EntrySceneHandle;
			});
		if (static_cast<uint64_t>(buildSettings.EntrySceneHandle) == 0
			|| entry == buildSettings.Scenes.end())
		{
			return fail("Player build failed: choose an enabled Entry Scene in Build Settings.");
		}
		const AssetHandle entryScene = buildSettings.EntrySceneHandle;

		ScriptProjectCompiler playerCompiler;
        if (!playerCompiler.Configure(m_Layer.m_CurrentProject, {}, {}, m_BuildState.DevelopmentBuild
            ? ScriptBuildProfile::Development : ScriptBuildProfile::Production))
            return fail("Player build failed: could not configure script build profile.");
        // Building must not replace the Editor's development metadata runtime.
        struct RestoreRuntime {
            std::shared_ptr<Scripting::IScriptRuntime> Previous = Scripting::ScriptEngine::Get().GetRuntime();
            ~RestoreRuntime() { Scripting::ScriptEngine::Get().SetRuntime(std::move(Previous)); }
        } restoreRuntime;
        // A Player build is always based on a newly compiled and validated Release
		// candidate. Never fall back to the previous last-good artifact here.
		ScriptBuildResult scriptBuild = playerCompiler.CompileNow();
		if (!scriptBuild.Succeeded || scriptBuild.SourceChangedDuringBuild ||
			scriptBuild.BuildID.empty() || scriptBuild.AssemblyPath.empty())
		{
			return fail(scriptBuild.SourceChangedDuringBuild
				? "Player build failed: C# sources changed during the Release compile. Build again."
				: "Player build failed: the fresh Release C# candidate did not validate. See Console diagnostics.");
		}
		if (!playerCompiler.RefreshSourceState() ||
			!playerCompiler.IsCurrentSourceBuilt() ||
			playerCompiler.GetCurrentSourceHash() != scriptBuild.SourceHash ||
			playerCompiler.GetLastGoodBuildID() != scriptBuild.BuildID ||
			AbsoluteLexicalPath(playerCompiler.GetLastGoodAssemblyPath()) !=
				AbsoluteLexicalPath(scriptBuild.AssemblyPath))
		{
			return fail("Player build failed: the fresh C# candidate no longer matches the current sources.");
		}

		const std::filesystem::path managedDirectory =
			playerCompiler.GetManagedRuntimeDirectory();
		if (managedDirectory.empty())
			return fail("Player build failed: TomCat.ScriptHost Release outputs are unavailable.");
		std::string runtimeError;
		auto runtime = Scripting::CreateManagedScriptRuntime(managedDirectory,
			scriptBuild.AssemblyPath, scriptBuild.PdbPath, {}, &runtimeError);
		if (!runtime || !runtime->IsReady())
		{
			if (runtimeError.empty())
				runtimeError = "managed host initialization failed";
			return fail("Player build failed: the fresh C# candidate could not be hosted: " +
				runtimeError);
		}
		std::string scriptManifestJson;
		if (!runtime->ReadProjectMetadata(scriptManifestJson))
			return fail("Player build failed: the fresh C# manifest could not be read.");
		std::string metadataError;
		if (!m_Layer.m_ScriptMetadata.ParseAndReplace(scriptManifestJson, metadataError))
			return fail("Player build failed: the fresh C# manifest is invalid: " + metadataError);

		Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		if (!m_Layer.m_EditorScene)
			return fail("Player build failed: there is no current scene to reconcile and save.");
		if (m_Layer.ReconcileManagedScriptFields(m_Layer.m_EditorScene))
			m_Layer.CommitImmediateSceneTransaction("Reconcile C# Fields");
		m_Layer.SaveScene();
		if (m_Layer.m_EditorScenePath.empty() || m_Layer.IsSceneDirty())
			return fail("Player build failed: save the current scene before building.");

		AssetManager& assetManager = AssetManager::Get();
		if (!assetManager.Refresh())
			return fail("Player build failed: the Asset Registry could not be refreshed.");
		const AssetMetadata* entrySceneMetadata =
			assetManager.GetRegistry().GetMetadata(entryScene);
		if (!entrySceneMetadata || entrySceneMetadata->IsMissing ||
			entrySceneMetadata->Type != AssetType::Scene)
			return fail("Player build failed: the enabled Entry Scene is missing or is not a Scene asset.");

		std::vector<uint8_t> assemblyBytes;
		std::string fileError;
		if (!ReadBinaryFileForBuild(scriptBuild.AssemblyPath, assemblyBytes, fileError))
			return fail("Player build failed: " + fileError);

		PlayerBuildRequest request;
		request.ProjectInstance = m_Layer.m_CurrentProject;
		request.EntryScene = entryScene;
		request.TemplateDirectory = PlayerBuilder::FindDefaultTemplateDirectory();
		request.ManagedAssembly = std::move(assemblyBytes);
		request.ScriptManifestJson = scriptManifestJson;
		request.ScriptBuildID = scriptBuild.BuildID;
		request.ValidateBeforePublish = [&playerCompiler, scriptBuild]()
		{
			return playerCompiler.RefreshSourceState() &&
				playerCompiler.IsCurrentSourceBuilt() &&
				playerCompiler.GetCurrentSourceHash() == scriptBuild.SourceHash &&
				playerCompiler.GetLastGoodBuildID() == scriptBuild.BuildID &&
				AbsoluteLexicalPath(playerCompiler.GetLastGoodAssemblyPath()) ==
					AbsoluteLexicalPath(scriptBuild.AssemblyPath);
		};
		PlayerBuildResult playerBuild = PlayerBuilder::Build(std::move(request));
		if (!playerBuild.Succeeded)
			return fail("Player build failed: " + playerBuild.Message);

		m_BuildState.PlayerBuildSucceeded = true;
		m_BuildState.PlayerBuildStatus = playerBuild.Message;
		m_Layer.m_ConsolePanel.Push(ConsoleMessageSeverity::Info,
			m_BuildState.PlayerBuildStatus, "Player Build");
		TC_Core_Info("{0}", m_BuildState.PlayerBuildStatus);

		if (runAfterBuild && !PlayerBuilder::Launch(playerBuild.PlayerExecutable,
			playerBuild.OutputDirectory, fileError))
		{
			return fail("Player build succeeded, but Build And Run failed: " + fileError);
		}
		return true;
	}


}
