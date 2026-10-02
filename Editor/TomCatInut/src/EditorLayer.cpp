#include "EditorLayer.h"
#include "EditorStartupWindow.h"
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
#include "TomCat/Module/ModuleSystem.h"
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


namespace TomCat {

	using namespace EditorLayerDetail;

	EditorLayer::EditorLayer(std::filesystem::path startupProjectPath)
		: Layer("EditorLayer"),
		  m_StartupProjectPath(std::move(startupProjectPath)),
		  m_Viewport(m_ViewportState, *this), m_Build(m_BuildState, *this),
		  m_ProjectSettings(*this)
	{
		m_CurrentProject = ProjectManager::Get().GetActiveProject();
		if (m_CurrentProject)
			m_Is2DMode = m_CurrentProject->GetConfig().Template == "2D";
	}

	bool EditorLayer::CaptureSceneArchive(std::string& archive) const
	{
		archive.clear();
		if (!m_EditorScene)
			return false;
		std::string error;
		if (SceneArchiveCodec::Encode(m_EditorScene, archive, error))
			return true;
		TC_Core_Error("Could not capture Scene history: {0}", error);
		return false;
	}

	void EditorLayer::InitializeSceneHistory(bool isSaved)
	{
		m_PrefabFileEdits.clear();
		CancelSceneTransaction();
		std::string archive;
		if (!CaptureSceneArchive(archive))
			return;
		const Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		const uint64_t selectedID =
			selected ? static_cast<uint64_t>(selected.GetUUID()) : 0;
		m_SceneHistory.Reset(std::move(archive), selectedID, isSaved);
		CheckForRecovery();
	}

	void EditorLayer::BeginSceneTransaction(const char* label)
	{
		if (m_SceneState != SceneState::Edit
			|| !m_EditorScene || m_SceneHistory.HasActiveTransaction())
			return;
		const Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		m_SceneHistory.SetCurrentSelection(
			selected ? static_cast<uint64_t>(selected.GetUUID()) : 0);
		m_SceneTransactionChanged = false;
		m_SceneHistory.BeginTransaction(label ? label : "Scene Edit");
	}

	void EditorLayer::UpdateSceneTransaction()
	{
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		if (!m_SceneHistory.HasActiveTransaction())
			BeginSceneTransaction("Scene Edit");
		m_SceneTransactionChanged = m_SceneHistory.HasActiveTransaction();
	}

	void EditorLayer::CommitSceneTransaction()
	{
		if (!m_SceneHistory.HasActiveTransaction())
			return;
		if (m_SceneState != SceneState::Edit || !m_SceneTransactionChanged)
		{
			CancelSceneTransaction();
			return;
		}
		std::string archive;
		if (!CaptureSceneArchive(archive))
		{
			CancelSceneTransaction();
			return;
		}
		const Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		const uint64_t selectedID =
			selected ? static_cast<uint64_t>(selected.GetUUID()) : 0;
		const bool committed =
			m_SceneHistory.CommitTransaction(std::move(archive), selectedID);
		m_SceneTransactionChanged = false;
		if (committed)
			ScheduleCurrentSceneAutosave();
	}

	void EditorLayer::CommitImmediateSceneTransaction(const char* label)
	{
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		BeginSceneTransaction(label);
		UpdateSceneTransaction();
		CommitSceneTransaction();
	}

	void EditorLayer::CancelSceneTransaction()
	{
		m_SceneHistory.CancelTransaction();
		m_SceneTransactionChanged = false;
		m_GizmoTransactionActive = false;
		m_ViewportState.ColliderTransactionActive = false;
	}

	void EditorLayer::OnSceneModified(
		SceneHierarchyPanel::SceneModificationPhase phase)
	{
		if (m_SceneState != SceneState::Edit)
			return;
		switch (phase)
		{
			case SceneHierarchyPanel::SceneModificationPhase::Begin:
				BeginSceneTransaction("Inspector Edit");
				break;
			case SceneHierarchyPanel::SceneModificationPhase::Update:
				UpdateSceneTransaction();
				break;
			case SceneHierarchyPanel::SceneModificationPhase::Commit:
				CommitSceneTransaction();
				break;
			case SceneHierarchyPanel::SceneModificationPhase::Instant:
				CommitImmediateSceneTransaction("Scene Command");
				break;
			case SceneHierarchyPanel::SceneModificationPhase::Cancel:
				CancelSceneTransaction();
				break;
		}
	}

	bool EditorLayer::ApplyHistorySnapshot(
		const SceneHistory::Snapshot& snapshot)
	{
		if (m_SceneState != SceneState::Edit || !snapshot.Archive)
			return false;
		const std::vector<uint8_t> bytes(snapshot.Archive->begin(),
			snapshot.Archive->end());
		Ref<Scene> restored = CreateRef<Scene>();
		const std::filesystem::path diagnosticPath = m_EditorScenePath.empty()
			? std::filesystem::path("<Editor History>")
			: m_EditorScenePath;
		if (!SceneArchiveCodec::Decode(bytes, restored, diagnosticPath, true))
		{
			TC_Core_Error("Scene history snapshot could not be decoded");
			return false;
		}

		// Apply edits span the Scene and a template file. Restore both only if the
		// asset still matches our transaction; concurrent disk edits remain intact.
		const auto currentState = m_SceneHistory.GetCurrentStateId();
		for (const auto& edit : m_PrefabFileEdits)
		{
			const bool undo = currentState == edit.AfterState && snapshot.Id == edit.BeforeState;
			const bool redo = currentState == edit.BeforeState && snapshot.Id == edit.AfterState;
			if (!undo && !redo) continue;
			const auto path = AssetManager::Get().GetRegistry().GetFileSystemPath(edit.Asset);
			std::ifstream input(path, std::ios::binary);
			const std::string actual((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			input.close();
			const auto& expected = undo ? edit.After : edit.Before;
			const auto& replacement = undo ? edit.Before : edit.After;
			if (actual != expected)
			{
				ReportPrefabOperation(false, "Cannot restore Apply history: the template was changed outside this transaction.");
				return false;
			}
			std::string error;
			if (!FileSystem::WriteFileAtomically(path, replacement, error))
			{
				ReportPrefabOperation(false, error);
				return false;
			}
			if (AssetManager::Get().ImportAsset(path) != edit.Asset)
			{
				std::string message = "Could not refresh the template while restoring Apply history.";
				std::string rollbackError;
				if (!FileSystem::WriteFileAtomically(path, expected, rollbackError))
					message += " Template rollback failed: " + rollbackError
						+ ". The template file may no longer match the unchanged Scene.";
				else if (AssetManager::Get().ImportAsset(path) != edit.Asset)
					message += " Original template bytes were restored, but their asset registration could not be refreshed.";
				else
					message += " The original template and asset registration were restored; Scene history was not changed.";
				ReportPrefabOperation(false, message);
				return false;
			}
			break;
		}
		m_EditorScene = restored;
		m_ActiveScene = restored;
		ResizeSceneForGameView(restored);
		m_SceneHierarchyPanel.ResetForSceneReplacement(restored, UUID(snapshot.SelectedEntity));
		ResetSceneInteractionState();
		return true;
	}

	bool EditorLayer::UndoScene()
	{
		if (m_SceneState != SceneState::Edit)
			return false;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		std::string recoveryError;
		const bool restored =
			m_RecoveryService.RestoreHistoryAndScheduleAutosave(
				m_SceneHistory,
				EditorRecoveryService::HistoryDirection::Undo,
			[this](const SceneHistory::Snapshot& snapshot)
			{
				return ApplyHistorySnapshot(snapshot);
			}, m_EditorScenePath, recoveryError);
		if (!recoveryError.empty())
			TC_Core_Warn(
				"Undo completed but recovery autosave was not scheduled: {0}",
				recoveryError);
		return restored;
	}

	bool EditorLayer::RedoScene()
	{
		if (m_SceneState != SceneState::Edit)
			return false;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		std::string recoveryError;
		const bool restored =
			m_RecoveryService.RestoreHistoryAndScheduleAutosave(
				m_SceneHistory,
				EditorRecoveryService::HistoryDirection::Redo,
			[this](const SceneHistory::Snapshot& snapshot)
			{
				return ApplyHistorySnapshot(snapshot);
			}, m_EditorScenePath, recoveryError);
		if (!recoveryError.empty())
			TC_Core_Warn(
				"Redo completed but recovery autosave was not scheduled: {0}",
				recoveryError);
		return restored;
	}

	void EditorLayer::MarkCurrentSceneSaved()
	{
		std::string archive;
		if (!CaptureSceneArchive(archive))
		{
			m_SceneHistory.InvalidateSavedState();
			return;
		}
		const Entity selected = m_SceneHierarchyPanel.GetSelectedEntity();
		const uint64_t selectedID =
			selected ? static_cast<uint64_t>(selected.GetUUID()) : 0;
		if (!m_SceneHistory.RefreshCurrentSnapshot(
			std::move(archive), selectedID))
		{
			m_SceneHistory.InvalidateSavedState();
			return;
		}
		m_SceneHistory.MarkSaved();
		std::string recoveryError;
		if (!m_RecoveryService.RemoveRecovery(
			m_EditorScenePath, recoveryError))
			TC_Core_Warn("Could not clear saved Scene recovery: {0}",
				recoveryError);
	}

	void EditorLayer::ScheduleCurrentSceneAutosave()
	{
		const SceneHistory::Snapshot* snapshot =
			m_SceneHistory.GetCurrentSnapshot();
		if (!snapshot || !snapshot->Archive
			|| m_RecoveryService.GetAutosaveDirectory().empty())
			return;
		std::string error;
		if (!m_RecoveryService.ScheduleAutosave(m_EditorScenePath,
			snapshot->Id, snapshot->SelectedEntity, snapshot->Archive, error))
			TC_Core_Warn("Scene autosave was not scheduled: {0}", error);
	}

	void EditorLayer::CheckForRecovery()
	{
		m_PendingRecovery.reset();
		m_OpenRecoveryModal = false;
		if (m_RecoveryService.GetAutosaveDirectory().empty())
			return;
		std::string error;
		auto recovery =
			m_RecoveryService.FindRecovery(m_EditorScenePath, error);
		if (!error.empty())
		{
			TC_Core_Warn("Could not inspect Scene recovery: {0}", error);
			return;
		}
		if (recovery)
		{
			m_PendingRecovery = std::move(*recovery);
			m_OpenRecoveryModal = true;
		}
	}

	bool EditorLayer::RestorePendingRecovery()
	{
		if (!m_PendingRecovery || !m_PendingRecovery->Archive)
			return false;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		const SceneHistory::Snapshot* current =
			m_SceneHistory.GetCurrentSnapshot();
		if (!current || !current->Archive)
			return false;
		const Entity previousSelection =
			m_SceneHierarchyPanel.GetSelectedEntity();
		m_SceneHistory.SetCurrentSelection(previousSelection
			? static_cast<uint64_t>(previousSelection.GetUUID()) : 0);
		const bool wasDifferent =
			*current->Archive != *m_PendingRecovery->Archive;
		SceneHistory::Snapshot recovered;
		recovered.Archive = m_PendingRecovery->Archive;
		recovered.SelectedEntity = m_PendingRecovery->SelectedEntity;
		recovered.Label = "Recovered Autosave";
		if (!ApplyHistorySnapshot(recovered))
			return false;
		if (wasDifferent)
		{
			if (!m_SceneHistory.BeginTransaction("Recover Autosave"))
				return false;
			const bool committed = m_SceneHistory.CommitTransaction(
				*recovered.Archive, recovered.SelectedEntity);
			m_SceneTransactionChanged = false;
			if (committed)
				ScheduleCurrentSceneAutosave();
		}
		else
		{
			CancelSceneTransaction();
			m_SceneHistory.InvalidateSavedState();
		}
		return true;
	}

	void EditorLayer::LoadSceneToolbarLayout()
	{
		// Restore the in-code fallback before applying the packaged baseline and
		// the selected global/project override. This prevents one layout's values
		// from leaking into another layout that has no settings file yet.
		m_GizmoModeToolbarDocked = true;
		m_GizmoTransformToolbarDocked = false;
		m_GizmoModeToolbarFirst = true;
		m_GizmoModeToolbarOffset = { 16.0f, 10.0f };
		m_GizmoToolbarOffset = { 16.0f, 48.0f };

		auto loadFrom = [&](const std::filesystem::path& iniPath) -> bool
		{
			std::ifstream fin(iniPath);
			if (!fin)
				return false;

			bool inSection = false;
			bool foundSection = false;
			bool hasModeDocked = false;
			bool hasTransformDocked = false;
			bool hasModeFirst = false;
			bool hasModeX = false, hasModeY = false;
			bool hasTransformX = false, hasTransformY = false;
			std::string line;
			while (std::getline(fin, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[SceneToolbars]")
				{
					inSection = true;
					foundSection = true;
					continue;
				}
				if (!inSection)
					continue;
				if (!line.empty() && line.front() == '[')
					break;
				const std::string::size_type equals = line.find('=');
				if (equals == std::string::npos)
					continue;
				const std::string key = line.substr(0, equals);
				const std::string value = line.substr(equals + 1);
				auto readBool = [&value](bool& destination, bool& present)
				{
					if (value == "1" || value == "true" || value == "True")
					{
						destination = true;
						present = true;
					}
					else if (value == "0" || value == "false" || value == "False")
					{
						destination = false;
						present = true;
					}
				};
				auto readFloat = [&value](float& destination, bool& present)
				{
					try
					{
						destination = std::stof(value);
						present = true;
					}
					catch (const std::exception&)
					{
						// Ignore malformed values and retain the in-code default.
					}
				};

				if (key == "ModeToolbarDocked")
					readBool(m_GizmoModeToolbarDocked, hasModeDocked);
				else if (key == "TransformToolbarDocked")
					readBool(m_GizmoTransformToolbarDocked, hasTransformDocked);
				else if (key == "ModeToolbarFirst")
					readBool(m_GizmoModeToolbarFirst, hasModeFirst);
				else if (key == "ModeToolbarOffsetX")
					readFloat(m_GizmoModeToolbarOffset.x, hasModeX);
				else if (key == "ModeToolbarOffsetY")
					readFloat(m_GizmoModeToolbarOffset.y, hasModeY);
				else if (key == "TransformToolbarOffsetX")
					readFloat(m_GizmoToolbarOffset.x, hasTransformX);
				else if (key == "TransformToolbarOffsetY")
					readFloat(m_GizmoToolbarOffset.y, hasTransformY);
			}
			return foundSection;
		};

		// Custom sections are not consumed by ImGui itself. Apply the packaged
		// defaults first, then let the selected global/project section override them.
		loadFrom(GetDefaultEditorLayoutPath());
		loadFrom(GetEditorLayoutPath(m_CurrentProject));
	}

	void EditorLayer::SaveSceneToolbarLayout()
	{
		const std::filesystem::path iniPath = GetEditorLayoutPath(m_CurrentProject);
		if (!EnsureSettingsDirectory(iniPath))
			return;

		std::string ini;
		{
			std::error_code existsError;
			const bool exists = std::filesystem::exists(iniPath, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect Scene toolbar settings '{0}': {1}",
					PathToUTF8(iniPath), existsError.message());
				return;
			}

			std::ifstream fin(iniPath, std::ios::binary);
			if (exists && !fin)
			{
				TC_Core_Error("Could not read Scene toolbar settings '{0}'", PathToUTF8(iniPath));
				return;
			}
			if (fin)
			{
				std::stringstream contents;
				contents << fin.rdbuf();
				if (fin.bad())
				{
					TC_Core_Error("Failed while reading Scene toolbar settings '{0}'", PathToUTF8(iniPath));
					return;
				}
				ini = contents.str();
			}
		}

		std::ostringstream section;
		section << "\n[SceneToolbars]\n"
			<< "ModeToolbarDocked=" << (m_GizmoModeToolbarDocked ? 1 : 0) << "\n"
			<< "TransformToolbarDocked=" << (m_GizmoTransformToolbarDocked ? 1 : 0) << "\n"
			<< "ModeToolbarFirst=" << (m_GizmoModeToolbarFirst ? 1 : 0) << "\n"
			<< std::fixed << std::setprecision(3)
			<< "ModeToolbarOffsetX=" << m_GizmoModeToolbarOffset.x << "\n"
			<< "ModeToolbarOffsetY=" << m_GizmoModeToolbarOffset.y << "\n"
			<< "TransformToolbarOffsetX=" << m_GizmoToolbarOffset.x << "\n"
			<< "TransformToolbarOffsetY=" << m_GizmoToolbarOffset.y << "\n";

		const std::string sectionName = "[SceneToolbars]";
		const std::string::size_type sectionPos = FindIniSectionHeader(ini, sectionName);
		if (sectionPos != std::string::npos)
		{
			const std::string::size_type nextSection = ini.find("\n[", sectionPos + sectionName.size());
			ini.erase(sectionPos, nextSection == std::string::npos ? std::string::npos : nextSection - sectionPos);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += section.str();

		std::string writeError;
		if (!FileSystem::WriteFileAtomically(iniPath, ini, writeError))
			TC_Core_Error("Failed to save Scene toolbar layout '{0}': {1}", PathToUTF8(iniPath), writeError);
	}

	uint32_t EditorLayer::GetEditorPanelVisibilityMask() const
	{
		uint32_t mask = 0;
		auto add = [&mask](bool visible, uint32_t bit)
		{
			if (visible)
				mask |= 1u << bit;
		};
		add(m_ShowScenePanel, 0);
		add(m_ShowGamePanel, 1);
		add(m_ShowHierarchyPanel, 2);
		add(m_ShowInspectorPanel, 3);
		add(m_ShowProjectPanel, 4);
		add(m_ShowConsolePanel, 5);
		add(m_ShowBuildSettingsPanel, 6);
		add(m_ShowProjectSettingsPanel, 7);
		add(m_ShowAnimatorPanel, 8);
		add(m_ShowAnimationPanel, 9);
		add(m_ShowTilePalettePanel, 10);
		add(m_ShowProfilerPanel, 11);
		return mask;
	}

	void EditorLayer::LoadEditorPanelLayout()
	{
		m_ShowScenePanel = true;
		m_ShowGamePanel = true;
		m_ShowAnimatorPanel = false;
		m_ShowAnimationPanel = false;
		m_ShowTilePalettePanel = false;
		m_ShowHierarchyPanel = true;
		m_ShowInspectorPanel = true;
		m_ShowProjectPanel = true;
		m_ShowConsolePanel = false;
		m_ShowProfilerPanel = false;
		m_ShowBuildSettingsPanel = false;
		m_ShowProjectSettingsPanel = false;

		auto loadFrom = [&](const std::filesystem::path& iniPath)
		{
			std::ifstream input(iniPath);
			if (!input)
				return;

			bool inSection = false;
			std::string line;
			while (std::getline(input, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == "[EditorPanels]")
				{
					inSection = true;
					continue;
				}
				if (!inSection)
					continue;
				if (!line.empty() && line.front() == '[')
					break;

				const std::string::size_type equals = line.find('=');
				if (equals == std::string::npos)
					continue;
				const std::string key = line.substr(0, equals);
				const std::string value = line.substr(equals + 1);
				bool parsed = false;
				bool visible = false;
				if (value == "1" || value == "true" || value == "True")
				{
					parsed = true;
					visible = true;
				}
				else if (value == "0" || value == "false" || value == "False")
					parsed = true;
				if (!parsed)
					continue;

				if (key == "Scene") m_ShowScenePanel = visible;
				else if (key == "Game") m_ShowGamePanel = visible;
				else if (key == "Hierarchy") m_ShowHierarchyPanel = visible;
				else if (key == "Inspector") m_ShowInspectorPanel = visible;
				else if (key == "Project") m_ShowProjectPanel = visible;
				else if (key == "Console") m_ShowConsolePanel = visible;
				else if (key == "Profiler") m_ShowProfilerPanel = visible;
				else if (key == "BuildSettings") m_ShowBuildSettingsPanel = visible;
				else if (key == "ProjectSettings") m_ShowProjectSettingsPanel = visible;
				else if (key == "Animator") m_ShowAnimatorPanel = visible;
				else if (key == "Animation") m_ShowAnimationPanel = visible;
				else if (key == "TilePalette") m_ShowTilePalettePanel = visible;
			}
		};

		loadFrom(GetDefaultEditorLayoutPath());
		loadFrom(GetEditorLayoutPath(m_CurrentProject));
		m_LastSavedPanelVisibilityMask = GetEditorPanelVisibilityMask();
		m_PanelVisibilitySnapshotInitialized = true;
	}

	bool EditorLayer::SaveEditorPanelLayout()
	{
		const std::filesystem::path iniPath = GetEditorLayoutPath(m_CurrentProject);
		if (!EnsureSettingsDirectory(iniPath))
			return false;

		std::string ini;
		{
			std::error_code existsError;
			const bool exists = std::filesystem::exists(iniPath, existsError);
			if (existsError)
			{
				TC_Core_Error("Could not inspect Editor panel settings '{0}': {1}",
					PathToUTF8(iniPath), existsError.message());
				return false;
			}
			std::ifstream input(iniPath, std::ios::binary);
			if (exists && !input)
			{
				TC_Core_Error("Could not read Editor panel settings '{0}'", PathToUTF8(iniPath));
				return false;
			}
			if (input)
			{
				std::ostringstream contents;
				contents << input.rdbuf();
				if (input.bad())
				{
					TC_Core_Error("Failed while reading Editor panel settings '{0}'", PathToUTF8(iniPath));
					return false;
				}
				ini = contents.str();
			}
		}

		std::ostringstream section;
		section << "\n[EditorPanels]\n"
			<< "Scene=" << (m_ShowScenePanel ? 1 : 0) << "\n"
			<< "Game=" << (m_ShowGamePanel ? 1 : 0) << "\n"
			<< "Hierarchy=" << (m_ShowHierarchyPanel ? 1 : 0) << "\n"
			<< "Inspector=" << (m_ShowInspectorPanel ? 1 : 0) << "\n"
			<< "Project=" << (m_ShowProjectPanel ? 1 : 0) << "\n"
			<< "Console=" << (m_ShowConsolePanel ? 1 : 0) << "\n"
			<< "Profiler=" << (m_ShowProfilerPanel ? 1 : 0) << "\n"
			<< "BuildSettings=" << (m_ShowBuildSettingsPanel ? 1 : 0) << "\n"
			<< "ProjectSettings=" << (m_ShowProjectSettingsPanel ? 1 : 0) << "\n"
			<< "Animator=" << (m_ShowAnimatorPanel ? 1 : 0) << "\n"
			<< "Animation=" << (m_ShowAnimationPanel ? 1 : 0) << "\n"
			<< "TilePalette=" << (m_ShowTilePalettePanel ? 1 : 0) << "\n";

		const std::string sectionName = "[EditorPanels]";
		const std::string::size_type sectionPos = FindIniSectionHeader(ini, sectionName);
		if (sectionPos != std::string::npos)
		{
			const std::string::size_type nextSection = ini.find("\n[", sectionPos + sectionName.size());
			ini.erase(sectionPos,
				nextSection == std::string::npos ? std::string::npos : nextSection - sectionPos);
		}
		if (!ini.empty() && ini.back() != '\n')
			ini.push_back('\n');
		ini += section.str();

		std::string writeError;
		if (FileSystem::WriteFileAtomically(iniPath, ini, writeError))
			return true;
		TC_Core_Error("Failed to save Editor panel layout '{0}': {1}",
			PathToUTF8(iniPath), writeError);
		return false;
	}

	void EditorLayer::SaveEditorLayoutIfNeeded()
	{
		ImGuiIO& io = ImGui::GetIO();
		// Maximizing a dock tab is a temporary view transaction. Never let its
		// one-node dock tree replace the user's persisted workspace.
		if (m_PanelMaximized
			|| m_PendingPanelMaximizeAction != PanelMaximizeAction::None
			|| m_PendingRestoredTabOrder >= 0)
		{
			io.WantSaveIniSettings = false;
			return;
		}
		const uint32_t visibilityMask = GetEditorPanelVisibilityMask();
		const bool panelVisibilityChanged = !m_PanelVisibilitySnapshotInitialized
			|| visibilityMask != m_LastSavedPanelVisibilityMask;
		if (!io.WantSaveIniSettings && !panelVisibilityChanged)
			return;

		const bool imguiSaved = SaveImGuiSettingsPreservingCustomSections(
			GetEditorLayoutPath(m_CurrentProject));
		const bool panelsSaved = SaveEditorPanelLayout();
		if (!imguiSaved || !panelsSaved)
			return;

		io.WantSaveIniSettings = false;
		m_LastSavedPanelVisibilityMask = visibilityMask;
		m_PanelVisibilitySnapshotInitialized = true;
	}

	bool EditorLayer::ShouldRenderDockPanel(std::string_view windowName) const
	{
		return !m_PanelMaximized || m_MaximizedPanelWindow == windowName;
	}

	void EditorLayer::RestorePanelLayoutBeforePersistence()
	{
		// A requested maximize has not changed the dock tree yet, so cancelling it
		// is enough. An active maximize must restore the in-memory snapshot before
		// any explicit save, project switch, or shutdown path runs.
		m_PendingPanelMaximizeAction = PanelMaximizeAction::None;
		m_PendingMaximizedPanelWindow.clear();
		m_PendingPanelFocusAfterRestore.clear();
		if (!m_PanelMaximized)
			return;

		if (!m_DockLayoutBeforeMaximize.empty())
		{
			ImGui::LoadIniSettingsFromMemory(m_DockLayoutBeforeMaximize.data(),
				m_DockLayoutBeforeMaximize.size());
			// ImGui's serialized DockOrder can lag one frame behind the live tab
			// vector. Reapply the order captured from the actual visible tab bars so
			// a maximize round-trip cannot swap neighboring tabs.
			for (size_t index = 0; index < kMaximizableDockPanels.size(); ++index)
			{
				const int order = m_DockTabOrdersBeforeMaximize[index];
				if (order < 0)
					continue;
				ImGuiWindow* window = ImGui::FindWindowByName(
					kMaximizableDockPanels[index]);
				if (!window)
					continue;
				window->DockOrder = static_cast<short>(order);
				if (ImGuiWindowSettings* windowSettings =
					ImGui::FindWindowSettings(window->ID))
				{
					windowSettings->DockOrder = static_cast<short>(order);
				}
			}
		}
		m_DockTabOrdersBeforeMaximize.fill(-1);
		m_DockLayoutBeforeMaximize.clear();
		m_MaximizedPanelWindow.clear();
		m_PanelMaximized = false;
		ImGui::GetIO().WantSaveIniSettings = false;
	}

	void EditorLayer::ApplyPendingPanelMaximizeTransition(uint32_t dockspaceId,
		const ImVec2& dockspaceSize)
	{
		if (m_PendingPanelMaximizeAction == PanelMaximizeAction::None)
			return;

		if (m_PendingPanelMaximizeAction == PanelMaximizeAction::Restore)
		{
			// Loading the saved dock layout restores the tab group, but ImGui does
			// not preserve which tab was selected while that group was replaced by
			// the maximized node. Focus the panel that is leaving maximized mode so
			// its restored dock tab remains active (for example, Game stays on Game).
			const std::string restoredPanel = m_MaximizedPanelWindow;
			const std::string requestedPanel =
				std::exchange(m_PendingPanelFocusAfterRestore, {});
			int restoredTabOrder = -1;
			for (size_t index = 0; index < kMaximizableDockPanels.size(); ++index)
			{
				if (restoredPanel == kMaximizableDockPanels[index])
				{
					restoredTabOrder = m_DockTabOrdersBeforeMaximize[index];
					break;
				}
			}
			RestorePanelLayoutBeforePersistence();
			if (!restoredPanel.empty())
			{
				m_PendingPanelFocus = requestedPanel.empty()
					? restoredPanel : requestedPanel;
				m_PendingRestoredTabWindow = restoredPanel;
				m_PendingRestoredTabOrder = restoredTabOrder;
			}
			return;
		}

		const std::string target = m_PendingMaximizedPanelWindow;
		m_PendingPanelMaximizeAction = PanelMaximizeAction::None;
		m_PendingMaximizedPanelWindow.clear();
		if (target.empty() || m_PanelMaximized)
			return;

		m_DockTabOrdersBeforeMaximize.fill(-1);
		for (size_t index = 0; index < kMaximizableDockPanels.size(); ++index)
		{
			ImGuiWindow* window = ImGui::FindWindowByName(
				kMaximizableDockPanels[index]);
			if (!window || !window->DockNode || !window->DockNode->TabBar)
				continue;
			if (ImGuiTabItem* tab = ImGui::TabBarFindTabByID(
				window->DockNode->TabBar, window->TabId))
			{
				m_DockTabOrdersBeforeMaximize[index] =
					ImGui::TabBarGetTabOrder(window->DockNode->TabBar, tab);
			}
		}

		// Commit the ordinary layout first. If the process exits unexpectedly while
		// maximized, the next launch still opens the exact pre-maximize workspace.
		const bool imguiSaved = SaveImGuiSettingsPreservingCustomSections(
			GetEditorLayoutPath(m_CurrentProject));
		const bool panelsSaved = SaveEditorPanelLayout();
		if (imguiSaved && panelsSaved)
		{
			m_LastSavedPanelVisibilityMask = GetEditorPanelVisibilityMask();
			m_PanelVisibilitySnapshotInitialized = true;
		}

		size_t settingsSize = 0;
		const char* settings = ImGui::SaveIniSettingsToMemory(&settingsSize);
		if (!settings || settingsSize == 0)
			return;

		m_DockLayoutBeforeMaximize.assign(settings, settingsSize);
		m_MaximizedPanelWindow = target;
		m_PanelMaximized = true;

		const ImGuiDockNodeFlags maximizeFlags = ImGuiDockNodeFlags_DockSpace
			| ImGuiDockNodeFlags_NoSplit | ImGuiDockNodeFlags_NoResize;
		ImGui::DockBuilderRemoveNode(static_cast<ImGuiID>(dockspaceId));
		ImGui::DockBuilderAddNode(static_cast<ImGuiID>(dockspaceId), maximizeFlags);
		ImGui::DockBuilderSetNodeSize(static_cast<ImGuiID>(dockspaceId), dockspaceSize);
		ImGui::DockBuilderDockWindow(m_MaximizedPanelWindow.c_str(),
			static_cast<ImGuiID>(dockspaceId));
		ImGui::DockBuilderFinish(static_cast<ImGuiID>(dockspaceId));
		ImGui::GetIO().WantSaveIniSettings = false;
	}

	void EditorLayer::DetectPanelTabDoubleClick()
	{
		if (m_PendingPanelMaximizeAction != PanelMaximizeAction::None
			|| !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			return;

		for (const char* panelName : kMaximizableDockPanels)
		{
			ImGuiWindow* window = ImGui::FindWindowByName(panelName);
			if (!window || !window->Active || !window->DockNode
				|| !window->DockNode->TabBar)
				continue;

			ImGuiDockNode* root = ImGui::DockNodeGetRootNode(window->DockNode);
			if (!root || root->ID != static_cast<ImGuiID>(m_EditorDockspaceId))
				continue;

			ImGuiTabItem* tab = ImGui::TabBarFindTabByID(
				window->DockNode->TabBar, window->TabId);
			if (!tab || !window->DockTabItemRect.Contains(ImGui::GetIO().MousePos))
				continue;

			if (m_PanelMaximized)
			{
				if (m_MaximizedPanelWindow == panelName)
					m_PendingPanelMaximizeAction = PanelMaximizeAction::Restore;
			}
			else
			{
				m_PendingMaximizedPanelWindow = panelName;
				m_PendingPanelMaximizeAction = PanelMaximizeAction::Maximize;
			}
			return;
		}
	}

    bool* EditorLayer::PanelVisibility(const std::string& name)
    {
        if (name == "Scene###Scene") return &m_ShowScenePanel;
        if (name == "Game") return &m_ShowGamePanel;
        if (name == "Hierarchy") return &m_ShowHierarchyPanel;
        if (name == "Inspector") return &m_ShowInspectorPanel;
        if (name == "Project") return &m_ShowProjectPanel;
        if (name == "Console") return &m_ShowConsolePanel;
        if (name == "Profiler") return &m_ShowProfilerPanel;
        if (name == "Animation") return &m_ShowAnimationPanel;
        if (name == "Animator") return &m_ShowAnimatorPanel;
        if (name == "Tile Palette") return &m_ShowTilePalettePanel;
        return nullptr;
    }

    void EditorLayer::ApplyPendingTabActions()
    {
        if (!m_PendingTabClose.empty())
        {
            if (bool* visible = PanelVisibility(m_PendingTabClose)) *visible = false;
            if (m_PendingTabClose == "Profiler") FrameProfiler::Get().SetRecording(false);
            m_PendingPanelFocus.clear();
            m_PendingRestoredTabWindow.clear();
            m_PendingRestoredTabOrder=-1;
            m_PendingTabClose.clear();
        }
        if (!m_PendingTabAdd.empty())
        {
            // Add to the clicked group, including when restoring from maximized view.
            ImGuiID dockID = m_TabContextDockID;
            auto* targetNode=ImGui::DockBuilderGetNode(dockID);
            if (!targetNode || !targetNode->IsLeafNode())
                if (auto* settings = ImGui::FindWindowSettings(ImHashStr(m_TabContextPanel.c_str())))
                    if (settings->DockId) dockID = settings->DockId;
            if (auto* node = ImGui::DockBuilderGetNode(dockID))
            {
                if (node->IsLeafNode())
                {
                    ImGui::DockBuilderDockWindow(m_PendingTabAdd.c_str(), dockID);
                    ImGui::DockBuilderFinish(m_EditorDockspaceId);
                }
            }
            if (bool* visible = PanelVisibility(m_PendingTabAdd)) *visible = true;
            m_PendingPanelFocus = std::exchange(m_PendingTabAdd, {});
        }
    }

    void EditorLayer::UI_PanelTabContextMenu()
    {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        {
            ImGuiWindow* hovered = ImGui::GetCurrentContext()->HoveredWindow;
            for (const char* name : kMaximizableDockPanels)
            {
                ImGuiWindow* window = ImGui::FindWindowByName(name);
                if (!window || !window->Active || !window->DockNode || !window->DockNode->TabBar) continue;
                ImGuiDockNode* node = window->DockNode;
                if (hovered != node->HostWindow && hovered != window) continue;
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const bool tab = window->DockTabItemRect.Contains(mouse);
                const bool emptyBar = node->SelectedTabId == window->TabId && node->TabBar->BarRect.Contains(mouse);
                if (!tab && !emptyBar) continue;
                // Resolve a tab first when the pointer is over a neighboring inactive tab.
                const char* target = name;
                for (const char* candidate : kMaximizableDockPanels)
                    if (auto* other = ImGui::FindWindowByName(candidate))
                        if (other->Active && other->DockNode == node && other->DockTabItemRect.Contains(mouse)) target = candidate;
                m_TabContextPanel = target;
                m_TabContextDockID = node->ID;
                ImGui::OpenPopup("Panel Tab Menu");
                break;
            }
        }
        PrepareEditorPopup("Panel Tab Menu", 260);
        if (ImGui::BeginPopup("Panel Tab Menu"))
        {
            if (m_TabContextPanel == "Scene###Scene" || m_TabContextPanel == "Game")
            {
                if (ImGui::BeginMenu("Overlay Menu"))
                {
                    if (m_TabContextPanel == "Game") ImGui::MenuItem("Rendering Statistics", nullptr, &m_GameViewStatsVisible);
                    else ImGui::TextDisabled("Use the Scene toolbar to select tools.");
                    ImGui::EndMenu();
                }
            }
            if (ImGui::MenuItem(m_PanelMaximized ? "Restore" : "Maximize"))
            {
                m_PendingMaximizedPanelWindow = m_TabContextPanel;
                m_PendingPanelMaximizeAction = m_PanelMaximized ? PanelMaximizeAction::Restore : PanelMaximizeAction::Maximize;
            }
            if (ImGui::MenuItem("Close Tab"))
            {
                m_PendingTabClose = m_TabContextPanel;
                if (m_PanelMaximized) m_PendingPanelMaximizeAction = PanelMaximizeAction::Restore;
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Add Tab"))
            {
                for (const char* name : kMaximizableDockPanels)
                {
                    const char* label = std::string_view(name) == "Scene###Scene" ? "Scene" : name;
                    const EditorIcon icon=std::string_view(name)=="Scene###Scene" ? EditorIcon::SceneView
                        : std::string_view(name)=="Game" ? EditorIcon::GameView
                        : std::string_view(name)=="Hierarchy" ? EditorIcon::Hierarchy
                        : std::string_view(name)=="Inspector" ? EditorIcon::Inspector
                        : std::string_view(name)=="Project" ? EditorIcon::ProjectBrowser : EditorIcon::Count;
                    if (EditorIconMenuItem(label,icon))
                    {
                        m_PendingTabAdd = name;
                        if (m_PanelMaximized) m_PendingPanelMaximizeAction = PanelMaximizeAction::Restore;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }

	void EditorLayer::OnAttach()
	{
		TC_PROFILE_FUNCTION();
		// ImGui's manual persistence signal is consumed by SaveEditorLayoutIfNeeded.
		// A short debounce keeps layout changes safe without writing every frame.
		ImGui::GetIO().IniSavingRate = 1.0f;

		m_EditorIcons = CreateRef<EditorIconSet>();
        g_EditorVisualIcons=m_EditorIcons;
		if (!m_EditorIcons->Load())
			TC_Core_Warn("One or more editor icons could not be loaded");
		m_SceneHierarchyPanel.SetIcons(m_EditorIcons);
		m_SceneHierarchyPanel.SetProject(m_CurrentProject);
		m_SceneHierarchyPanel.SetScriptMetadataProvider([this](AssetHandle handle)
		{
			return m_ScriptMetadata.Find(handle);
		});
		m_ContentBrowserPanel.SetIcons(m_EditorIcons);
        m_ContentBrowserPanel.SetScriptMetadataProvider([this](AssetHandle handle) { return m_ScriptMetadata.Find(handle); });
        m_ContentBrowserPanel.SetAssetSelectionCallback([this](const std::filesystem::path& path) {
            m_SceneHierarchyPanel.SetAssetSelection(path);
            if (!path.empty()) m_ShowInspectorPanel = true;
        });
        m_SceneHierarchyPanel.SetAssetInspectorRenderer([this](const std::filesystem::path& path) {
            m_ContentBrowserPanel.DrawAssetInspector(path);
        });
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		Scripting::SetScriptDiagnosticSink([this](const Scripting::ScriptDiagnostic& diagnostic)
		{
			ConsoleMessage message;
			message.Source = "C# Runtime";
			message.File = diagnostic.File;
			message.Line = diagnostic.Line;
			message.Column = diagnostic.Column;
			switch (diagnostic.Severity)
			{
				case Scripting::ScriptDiagnosticSeverity::Info:
					message.Severity = ConsoleMessageSeverity::Info;
					break;
				case Scripting::ScriptDiagnosticSeverity::Warning:
					message.Severity = ConsoleMessageSeverity::Warning;
					break;
				case Scripting::ScriptDiagnosticSeverity::Error:
					message.Severity = ConsoleMessageSeverity::Error;
					break;
			}
			const size_t stackStart = diagnostic.Message.find('\n');
			if (stackStart == std::string::npos)
				message.Text = diagnostic.Message;
			else
			{
				message.Text = diagnostic.Message.substr(0, stackStart);
				if (!message.Text.empty() && message.Text.back() == '\r')
					message.Text.pop_back();
				message.StackTrace = diagnostic.Message.substr(stackStart + 1);
			}
			m_ConsolePanel.Push(std::move(message));
		});

		m_ScriptCompiler.SetDiagnosticCallback([this](const ScriptCompilerDiagnostic& diagnostic)
		{
			ConsoleMessage message;
			message.Source = "C# Compiler";
			message.Code = diagnostic.Code;
			message.Text = diagnostic.Message;
			message.File = diagnostic.File;
			message.Line = diagnostic.Line;
			message.Column = diagnostic.Column;
			switch (diagnostic.Level)
			{
				case ScriptCompilerDiagnostic::Severity::Info:
					message.Severity = ConsoleMessageSeverity::Info;
					break;
				case ScriptCompilerDiagnostic::Severity::Warning:
					message.Severity = ConsoleMessageSeverity::Warning;
					break;
				case ScriptCompilerDiagnostic::Severity::Error:
					message.Severity = ConsoleMessageSeverity::Error;
					break;
			}
			m_ConsolePanel.Push(std::move(message));
		});
		if (m_CurrentProject && m_ScriptCompiler.Configure(m_CurrentProject))
		{
			ResetScriptCompileTracking();
			if (m_ScriptCompiler.IsCurrentSourceBuilt())
				PrepareManagedRuntime();
		}

		FramebufferSpecification fbSpec;
		fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
		fbSpec.Width = 1280;
		fbSpec.Height = 720;
		m_Framebuffer = Framebuffer::Create(fbSpec);
		m_GameFramebuffer = Framebuffer::Create(fbSpec);

		// The packaged root file is a read-only baseline. The selected writable
		// layout is global in no-project mode and project-local otherwise.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
        if(!LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject))) m_LayoutRequest=1;

		LoadSceneToolbarLayout();
		LoadEditorPanelLayout();

		m_EditorCamera = EditorCamera(30.0f, 1.778f, 0.1f, 1000.0f);
		m_EditorCamera.Set2DMode(m_Is2DMode);

		m_SceneHierarchyPanel.SetSceneLoadCallback([this](AssetHandle handle) {
			const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
			if (!path.empty())
				OpenScene(path);
		});
		m_ContentBrowserPanel.SetSceneOpenCallback([this](AssetHandle handle) {
			const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
			if (!path.empty())
				OpenScene(path);
		});
		m_ContentBrowserPanel.SetAuthoringAssetOpenCallback(
			[this](AssetHandle handle, AssetType type)
			{
				if (!m_SceneHierarchyPanel.OpenAuthoringAsset(handle, type))
					return;
				switch (type)
				{
					case AssetType::AnimationClip:
						FocusEditorPanel("Animation", m_ShowAnimationPanel);
						break;
					case AssetType::AnimatorController:
						FocusEditorPanel("Animator", m_ShowAnimatorPanel);
						break;
					case AssetType::TilePalette:
						FocusEditorPanel("Tile Palette", m_ShowTilePalettePanel);
						break;
					default:
						break;
				}
			});
		m_SceneHierarchyPanel.SetPrefabCreateCallback([this](Entity entity) {
			return CreatePrefabFromEntity(entity,
				m_ContentBrowserPanel.GetWritableCreationDirectory());
		});
		m_SceneHierarchyPanel.SetPrefabInstantiateCallback(
			[this](AssetHandle handle, Entity parent) {
				std::optional<UUID> parentID;
				if (parent)
					parentID = parent.GetUUID();
				return InstantiatePrefab(handle, parentID, std::nullopt);
			});
		m_SceneHierarchyPanel.SetPrefabActionCallback([this](Entity root, int action, UUID target, UUID component, UUID property) {
			if (m_SceneState != SceneState::Edit || !root || !root.HasComponent<PrefabLink>()) return;
			if (m_SceneHistory.HasActiveTransaction()) CommitSceneTransaction();
			const UUID rootID = root.GetUUID();
			const AssetHandle source = root.GetComponent<PrefabLink>().Source;
			PrefabFileEdit fileEdit;
			fileEdit.BeforeState = m_SceneHistory.GetCurrentStateId();
			fileEdit.Asset = source;
			const auto sourcePath = AssetManager::Get().GetRegistry().GetFileSystemPath(source);
			if (action == 2 || action == 5)
			{
				std::ifstream input(sourcePath, std::ios::binary);
				fileEdit.Before.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			}
			std::string error;
			bool succeeded = false;
			if (action == 4)
			{
				std::vector<std::string> paths;
				if (!PrefabLinkedInstance::GetOverridePaths(m_EditorScene, rootID, paths, error))
					ReportPrefabOperation(false, error);
				else
				{
					ReportPrefabOperation(true, std::to_string(paths.size()) + " Prefab override(s).");
					for (const auto& path : paths) m_ConsolePanel.Push(ConsoleMessageSeverity::Info, path, "Prefab");
					m_ShowConsolePanel = true;
				}
				return;
			}
			if (action == 3)
			{
				root.RemoveComponent<PrefabLink>();
				succeeded = true;
			}
			else
			{
				PrefabArchive latest;
				if (PrefabArchiveCodec::Load(source, latest, error))
				{
					if (action == 2 || action == 5)
					{
						succeeded = action == 5 ? PrefabLinkedInstance::ApplyProperty(m_EditorScene, rootID, target, component, property, error) : PrefabLinkedInstance::Apply(m_EditorScene, rootID, error);
					}
					else if (action == 6) succeeded = PrefabLinkedInstance::RevertProperty(m_EditorScene, rootID, target, component, property, error);
					else succeeded = PrefabLinkedInstance::Update(m_EditorScene, rootID, latest, action == 1, error);
				}
			}
			if (succeeded)
			{
				// UUID was captured before the operation. Never ask an old Entity
				// wrapper for its identity after Update/Apply replaces the registry.
				m_SceneHierarchyPanel.ResetForSceneReplacement(m_EditorScene, rootID);
				ResetSceneInteractionState();
				CommitImmediateSceneTransaction("Prefab Instance");
				if (action == 2 || action == 5)
				{
					fileEdit.AfterState = m_SceneHistory.GetCurrentStateId();
					std::ifstream input(sourcePath, std::ios::binary);
					fileEdit.After.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
					if (fileEdit.BeforeState != fileEdit.AfterState)
						m_PrefabFileEdits.push_back(std::move(fileEdit));
					if (m_PrefabFileEdits.size() > 128) m_PrefabFileEdits.erase(m_PrefabFileEdits.begin());
				}
			}
			ReportPrefabOperation(succeeded, succeeded ? "Prefab operation completed." : error);
		});
		m_ContentBrowserPanel.SetEntityPrefabCreateCallback(
			[this](UUID entityID, const std::filesystem::path& directory) {
				Entity entity = m_ActiveScene
					? m_ActiveScene->FindEntityByUUID(entityID) : Entity{};
				return CreatePrefabFromEntity(entity, directory);
			});
		m_ContentBrowserPanel.SetAssetRenamedCallback([this](const std::filesystem::path& oldPath,
			const std::filesystem::path& newPath) {
			const std::filesystem::path previousEditorScenePath = m_EditorScenePath;
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(oldPath, m_EditorScenePath, relative))
				m_EditorScenePath = newPath / relative;

			if (m_CurrentProject)
			{
				BuildSettings repaired = m_CurrentProject->GetBuildSettings();
				bool changed = false;
				for (BuildSceneSettings& scene : repaired.Scenes)
				{
					const AssetMetadata* metadata =
						AssetManager::Get().GetRegistry().GetMetadata(scene.Handle);
					if (metadata && !metadata->IsMissing && metadata->Type == AssetType::Scene
						&& metadata->FilePath != scene.PathHint)
					{
						scene.PathHint = metadata->FilePath;
						changed = true;
					}
				}
				if (changed && !m_CurrentProject->SetBuildSettings(repaired))
				{
					m_EditorScenePath = previousEditorScenePath;
					m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
					m_BuildState.BuildSettingsSucceeded = false;
					m_BuildState.BuildSettingsStatus =
						"Scene move was rejected because BuildSettings.json could not update its PathHints.";
					TC_Core_Error("{0}", m_BuildState.BuildSettingsStatus);
					return false;
				}
				if (changed)
				{
					m_BuildState.BuildSettingsSucceeded = true;
					m_BuildState.BuildSettingsStatus =
						"Updated build-scene PathHints in ProjectSettings/BuildSettings.json.";
				}
			}
			m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
			return true;
		});
		m_ContentBrowserPanel.SetAssetDeletedCallback([this](const std::filesystem::path& deletedPath) {
			std::filesystem::path relative;
			if (!m_EditorScenePath.empty() && TryGetRelativeWithin(deletedPath, m_EditorScenePath, relative))
			{
				m_EditorScenePath.clear();
				m_ContentBrowserPanel.SetActiveScenePath({});
				m_SceneHistory.InvalidateSavedState();
			}
			if (m_CurrentProject)
			{
				for (const BuildSceneSettings& scene : m_CurrentProject->GetBuildSettings().Scenes)
				{
					const std::filesystem::path hintedPath =
						m_CurrentProject->GetAssetPath() / scene.PathHint;
					if (!scene.PathHint.empty()
						&& TryGetRelativeWithin(deletedPath, hintedPath, relative))
					{
						m_BuildState.BuildSettingsSucceeded = false;
						m_BuildState.BuildSettingsStatus = "Build Scene is now missing: "
							+ PathToUTF8(scene.PathHint)
							+ ". Its stable Handle and last PathHint were preserved.";
						TC_Warn("{0}", m_BuildState.BuildSettingsStatus);
					}
				}
			}
			// Sprite handles deliberately survive deletion. AssetManager resolves
			// them to the shared missing-resource texture until the asset is restored.
		});

		m_SceneHierarchyPanel.SetSpriteCreateCallback([this](AssetHandle handle) {
			if (!m_ActiveScene)
				return;
			const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(handle);
			const AssetMetadata* metadata = builtIn ? nullptr
				: AssetManager::Get().GetRegistry().GetMetadata(handle);
			if (!builtIn && (!metadata || metadata->Type != AssetType::Texture2D))
				return;
			const std::string fileName = builtIn ? std::string(builtIn->Name)
				: PathToUTF8(metadata->FilePath.stem());
			auto sprite = m_ActiveScene->CreateEntity(fileName);
			auto& SpriteR = sprite.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
			SpriteR.SpriteHandle = handle;
			SpriteR.Sprite = AssetManager::Get().LoadTexture(handle);
			if (m_SceneState == SceneState::Edit)
				CommitImmediateSceneTransaction("Create Sprite");
		});
		m_SceneHierarchyPanel.SetScriptOpenCallback([this](AssetHandle handle) {
			m_ContentBrowserPanel.OpenDiagnosticSource(AssetManager::Get().GetRegistry().GetFileSystemPath(handle));
		});
		m_SceneHierarchyPanel.SetAssetRevealCallback([this](AssetHandle handle) {
			if (FindBuiltInSpriteAsset(handle))
			{
				m_ContentBrowserPanel.RevealAsset(GetBuiltInSpriteAssetPath(handle));
				FocusEditorPanel("Project", m_ShowProjectPanel);
				return;
			}
			AssetRegistry& registry = AssetManager::Get().GetRegistry();
			const AssetMetadata* metadata = registry.GetMetadata(handle);
			if (!metadata)
			{
				const AssetSubAsset* subAsset = nullptr;
				metadata = registry.GetSubAssetOwner(handle, &subAsset);
			}
			if (!metadata || metadata->IsMissing)
				return;
			m_ContentBrowserPanel.RevealAsset(
				(registry.GetAssetDirectory() / metadata->FilePath).lexically_normal());
			FocusEditorPanel("Project", m_ShowProjectPanel);
		});
		m_SceneHierarchyPanel.SetSceneModifiedCallback(
			[this](SceneHierarchyPanel::SceneModificationPhase phase) {
			OnSceneModified(phase);
		});
		AssetManager::Get().SetLiveReferenceProvider([this](AssetHandle handle) {
			std::vector<AssetReference> references;
			if (m_EditorScene)
				references = m_EditorScene->FindAssetReferences(handle);
			AssetHandle sceneHandle(0);
			if (!m_EditorScenePath.empty())
			{
				if (const AssetMetadata* metadata =
					AssetManager::Get().GetRegistry().GetMetadata(m_EditorScenePath))
					sceneHandle = metadata->Handle;
			}
			for (AssetReference& reference : references)
			{
				reference.ReferencingAsset = sceneHandle;
				reference.FilePath = m_EditorScenePath.empty()
					? std::filesystem::path("<Unsaved Scene>") : m_EditorScenePath;
			}
			return references;
		});

		if (!m_StartupProjectPath.empty())
		{
			EditorStartupWindow::Get().Update(L"正在打开项目");
			const std::filesystem::path startup =
				std::move(m_StartupProjectPath);
			m_StartupProjectPath.clear();
			if (!OpenProject(startup))
			{
				std::string reason = "Could not open project: " + PathToUTF8(startup);
                const auto messages = m_ConsolePanel.Snapshot();
                for (auto it = messages.rbegin(); it != messages.rend(); ++it)
                    if (it->Severity == ConsoleMessageSeverity::Error) { reason += "\n" + it->Text; break; }
                throw std::runtime_error(reason);
			}
		}
		else if (m_CurrentProject)
		{
			std::string lockError;
			EditorProjectLock startupLock;
			if (startupLock.Acquire(m_CurrentProject->GetProjectPath(),
				lockError) == ProjectLockAcquireResult::Acquired)
			{
				m_ProjectLock = std::move(startupLock);
				std::string recoveryError;
				if (!m_RecoveryService.Configure(
					m_CurrentProject->GetProjectPath(), recoveryError))
					TC_Core_Warn("Editor recovery is unavailable: {0}",
						recoveryError);
				OpenProjectStartScene();
			}
			else
				TC_Core_Error("Project opened without a write lock: {0}",
					lockError);
		}
		else
		{
			std::string recoveryError;
			if (!m_RecoveryService.Configure({}, recoveryError))
				TC_Core_Warn("Editor recovery is unavailable: {0}",
					recoveryError);
			NewScene();
			m_SceneHistory.MarkSaved();
		}
	}

	void EditorLayer::OnDetach()
	{
		Scripting::ScriptEngine::Get().SetInputEnabled(true);
		TC_PROFILE_FUNCTION();
		RestorePanelLayoutBeforePersistence();
		if (IsSceneRunning())
			OnSceneStop();
		CommitSceneTransaction();
		std::string recoveryError;
		if (!m_RecoveryService.Flush(recoveryError))
			TC_Core_Warn("Editor recovery flush failed: {0}", recoveryError);

		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();

		// ImGui-managed state and custom panel sections share either the global
		// no-project layout or the active project's UserSettings/imgui.ini.
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		SaveEditorPanelLayout();
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
		m_SceneHierarchyPanel.SetScriptMetadataProvider({});
		m_ScriptMetadata.Clear();
		Scripting::SetScriptDiagnosticSink({});
		Scripting::ScriptEngine::Get().SetRuntime({});
		m_ScriptCompiler.Reset();
		AssetManager::Get().SetLiveReferenceProvider({});
		AssetManager::Get().Shutdown();
		m_ProjectLock.Release();
	}

	void EditorLayer::ResetScriptCompileTracking()
	{
		m_ScriptSourcePollCountdown = 0.0f;
		m_ScriptCompileDebounceRemaining = 0.65f;
		m_ObservedScriptSourceHash = m_ScriptCompiler.GetCurrentSourceHash();
		m_PlayScriptDirtyNoticeShown = false;
	}

	void EditorLayer::UpdateScriptCompilation(Timestep ts)
	{
		ScriptBuildResult completed;
		if (m_ScriptCompiler.PollCompile(completed))
		{
			m_ObservedScriptSourceHash = m_ScriptCompiler.GetCurrentSourceHash();
			m_ScriptCompileDebounceRemaining = 0.65f;
			if (completed.Succeeded && m_SceneState == SceneState::Edit && PrepareManagedRuntime())
				m_ConsolePanel.Push(ConsoleMessageSeverity::Info,
					"C# assembly reloaded. Inspector metadata is current; the next Play uses build "
					+ completed.BuildID + ".", "Script Runtime");
		}
		if (!m_CurrentProject)
			return;

		const float deltaSeconds = std::clamp(ts.GetSeconds(), 0.0f, 0.25f);
		m_ScriptSourcePollCountdown -= deltaSeconds;
		if (!m_ScriptCompiler.IsCompileInProgress() &&
			m_ScriptSourcePollCountdown <= 0.0f)
		{
			m_ScriptSourcePollCountdown = 0.35f;
			if (m_ScriptCompiler.RefreshSourceState())
			{
				const std::string& sourceHash =
					m_ScriptCompiler.GetCurrentSourceHash();
				if (sourceHash != m_ObservedScriptSourceHash)
				{
					m_ObservedScriptSourceHash = sourceHash;
					m_ScriptCompileDebounceRemaining = 0.65f;
				}
				if (IsSceneRunning() &&
					!m_ScriptCompiler.IsCurrentSourceBuilt() &&
					!m_PlayScriptDirtyNoticeShown)
				{
					m_PlayScriptDirtyNoticeShown = true;
					m_ShowConsolePanel = true;
					m_ConsolePanel.Push(ConsoleMessageSeverity::Warning,
						"C# source changes were detected. Stop Play to apply them; "
						"the running scene will keep its current assembly.",
						"Editor");
				}
			}
		}

		if (m_SceneState == SceneState::Edit &&
			!m_ScriptCompiler.IsCompileInProgress() &&
			m_ScriptCompiler.GetState() == ScriptBuildState::Dirty)
		{
			m_ScriptCompileDebounceRemaining -= deltaSeconds;
			if (m_ScriptCompileDebounceRemaining <= 0.0f)
			{
				m_ScriptCompileDebounceRemaining = 0.65f;
				m_ScriptCompiler.StartCompile();
			}
		}
	}

	void EditorLayer::OnUpdate(Timestep ts)
	{
		TC_PROFILE_FUNCTION();
		if (m_SceneState == SceneState::Edit && !m_SceneHistory.HasActiveTransaction()
			&& m_PrefabImportRevision != AssetManager::Get().GetImportRevision())
		{
			m_PrefabImportRevision = AssetManager::Get().GetImportRevision();
			RefreshLinkedPrefabs();
		}
		Window& applicationWindow = Application::Get().GetWindow();
		const float runtimeUIDPIScale = applicationWindow.GetDPIScale();
		const glm::vec2 screenToFramebufferScale{
			applicationWindow.GetScreenToFramebufferScaleX(),
			applicationWindow.GetScreenToFramebufferScaleY() };
		const float safeGameViewScale = std::max(m_GameViewEffectiveScale, 0.01f);
		const glm::vec2 gameScreenToFramebufferScale{ 1.0f / safeGameViewScale };
		const glm::vec2 runtimeUIOrigin = m_ShowGamePanel
			? m_GameViewportBounds[0] : glm::vec2(-1000000.0f);
		if (IsSceneRunning())
			m_RuntimeSceneManager.SetRuntimeUIViewportMetrics(runtimeUIOrigin,
				runtimeUIDPIScale, gameScreenToFramebufferScale);
		else if (m_ActiveScene)
			m_ActiveScene->SetRuntimeUIViewportMetrics(runtimeUIOrigin,
				runtimeUIDPIScale, gameScreenToFramebufferScale);
		UpdateScriptCompilation(ts);

		// Resize Scene Framebuffer
		const uint32_t sceneWidth = ToFramebufferExtent(m_ViewportSize.x,
			screenToFramebufferScale.x);
		const uint32_t sceneHeight = ToFramebufferExtent(m_ViewportSize.y,
			screenToFramebufferScale.y);
		if (FramebufferSpecification spec = m_Framebuffer->GetSpecification();
			sceneWidth > 0 && sceneHeight > 0 &&
			(spec.Width != sceneWidth || spec.Height != sceneHeight))
		{
			if (m_Framebuffer->Resize(sceneWidth, sceneHeight))
				m_EditorCamera.SetViewportSize(static_cast<float>(sceneWidth), static_cast<float>(sceneHeight));
		}

		// Named Game resolutions stay pixel exact. Scale changes only how the
		// framebuffer is presented inside the editor.
		const GameViewResolutionPreset& gameResolution =
			kGameViewResolutions[static_cast<size_t>(std::clamp(
				m_GameViewResolutionIndex, 0,
				static_cast<int>(kGameViewResolutions.size()) - 1))];
		const uint32_t gameWidth = gameResolution.Width > 0
			? gameResolution.Width
			: ToFramebufferExtent(m_GameViewportSize.x,
				screenToFramebufferScale.x);
		const uint32_t gameHeight = gameResolution.Height > 0
			? gameResolution.Height
			: ToFramebufferExtent(m_GameViewportSize.y,
				screenToFramebufferScale.y);
		if (FramebufferSpecification gameSpec = m_GameFramebuffer->GetSpecification();
			gameWidth > 0 && gameHeight > 0 &&
			(gameSpec.Width != gameWidth || gameSpec.Height != gameHeight))
		{
			if (m_GameFramebuffer->Resize(gameWidth, gameHeight))
			{
				if (IsSceneRunning())
					m_RuntimeSceneManager.SetViewportSize(gameWidth, gameHeight);
				else if (m_ActiveScene)
					m_ActiveScene->OnViewportResize(gameWidth, gameHeight);
			}
		}
		const FramebufferSpecification actualGameSpec =
			m_GameFramebuffer->GetSpecification();
		if (m_ActiveScene && gameWidth > 0 && gameHeight > 0
			&& actualGameSpec.Width > 0 && actualGameSpec.Height > 0
			&& (m_ActiveScene->GetViewportWidth() != actualGameSpec.Width
				|| m_ActiveScene->GetViewportHeight() != actualGameSpec.Height))
		{
			// A restored, replaced, or newly started Scene can inherit an already
			// correctly-sized framebuffer. Keep Camera aspect and Canvas layout in
			// sync even when no framebuffer resize event occurs this frame. Read the
			// actual size after Resize so an allocation failure cannot publish a
			// viewport that the Game framebuffer did not reach.
			if (IsSceneRunning())
				m_RuntimeSceneManager.SetViewportSize(actualGameSpec.Width,
					actualGameSpec.Height);
			else
				m_ActiveScene->OnViewportResize(actualGameSpec.Width,
					actualGameSpec.Height);
		}

		// Render Scene View (Editor Camera).  Unity's Scene canvas is one step
		// lighter than the surrounding #383838 panels (#474747); the grid and
		// selection overlays then provide the additional contrast seen in the
		// reference.
		Renderer2D::ResetStats();
		m_Framebuffer->Bind();
		RenderCommand::SetClearColor({ 71.0f / 255.0f, 71.0f / 255.0f, 71.0f / 255.0f, 1 });
		RenderCommand::Clear();
		m_Framebuffer->ClearAttachment(1, -1);

		// Update
		m_EditorCamera.OnUpdate(ts, m_ViewportCameraDragOwned);

		// Scene窗口始终使用EditorCamera渲染
		m_ActiveScene->OnUpdateEditor(ts, m_EditorCamera);

		// Editor overlays participate in the same ID attachment as ordinary scene
		// geometry. Submit them before sampling so thin Camera/Canvas outlines and
		// the Camera icon can select their owning Entity.
		m_Viewport.RenderSceneCameraOverlay();
		m_Viewport.RenderSceneCanvasOverlay();

		// Mouse picking for Scene viewport
		auto [mx, my] = ImGui::GetMousePos();
		mx -= m_ViewportBounds[0].x;
		my -= m_ViewportBounds[0].y;
		glm::vec2 viewportSize = m_ViewportBounds[1] - m_ViewportBounds[0];
		my = viewportSize.y - my;
		const int mouseX = static_cast<int>(std::floor(
			mx * screenToFramebufferScale.x));
		const int mouseY = static_cast<int>(std::floor(
			my * screenToFramebufferScale.y));

		if (mx >= 0.0f && my >= 0.0f && mx < viewportSize.x
			&& my < viewportSize.y && mouseX >= 0 && mouseY >= 0
			&& mouseX < static_cast<int>(sceneWidth)
			&& mouseY < static_cast<int>(sceneHeight))
		{
			int pixelData = m_Framebuffer->ReadPixel(1, mouseX, mouseY);
			m_HoveredEntity = pixelData == -1 ? Entity() : Entity((entt::entity)pixelData, m_ActiveScene.get());
		}
		else
		{
			m_HoveredEntity = {};
		}
		// Collider editing remains a visual overlay. It intentionally renders after
		// picking so its non-pickable handles cannot erase an Entity ID underneath.
		m_Viewport.RenderSceneColliderOverlays();

		m_Framebuffer->Unbind();

		// Render Game View (Runtime Camera) - Always render runtime camera. Reset
		// here so Game Stats contains no Scene-view draw calls.
		Renderer2D::ResetStats();
		m_GameFramebuffer->Bind();

		RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		RenderCommand::Clear();
		m_GameFramebuffer->ClearAttachment(1, -1);

		// Game窗口使用Runtime渲染，背景色由摄像机的BackgroundColor设置
		bool runtimeAdvanced = false;
		if (m_SceneState == SceneState::Play)
		{
			m_ActiveScene->OnUpdateRuntime(ts);
			runtimeAdvanced = true;
		}
		else if (m_SceneState == SceneState::Pause && m_StepRequested)
		{
			m_ActiveScene->OnRuntimeStep();
			m_StepRequested = false;
			runtimeAdvanced = true;
		}
		else
			m_ActiveScene->OnRenderRuntime();
		m_GameFramebuffer->Unbind();
		if (runtimeAdvanced)
			CommitRuntimeSceneTransition();
	}

	void EditorLayer::UpdateWindowTitle()
	{
		if (m_CurrentProject) EditorStartupWindow::Get().SetProject(UTF8ToPath(m_CurrentProject->GetName()).wstring());
		const std::string projectName = m_CurrentProject && !m_CurrentProject->GetName().empty()
			? m_CurrentProject->GetName() : "TomCat Editor";
		std::string sceneName = "Untitled";
		if (!m_EditorScenePath.empty())
			sceneName = PathToUTF8(m_EditorScenePath.stem());
		else if (m_EditorScene)
		{
			sceneName = m_EditorScene->GetSceneName();
		}
		if (sceneName.empty())
			sceneName = "Untitled";

		const char* rendererName = Renderer::GetAPI() == RendererAPI::API::OpenGL
			? "OpenGL" : "No Renderer";
		std::string title = projectName + " - " + sceneName;
		if (IsSceneDirty())
			title += '*';
		title += " - Windows (64-bit) - TomCat Editor";
		if (m_CurrentProject && !m_CurrentProject->GetEditorVersion().empty())
		{
			title += ' ';
			title += m_CurrentProject->GetEditorVersion();
		}
		title += " <";
		title += rendererName;
		title += '>';

		if (title == m_LastWindowTitle)
			return;
		Application::Get().GetWindow().SetTitle(title);
		m_LastWindowTitle = std::move(title);
	}

	void EditorLayer::FocusEditorPanel(const char* panelName, bool& panelVisible)
	{
		panelVisible = true;
		const std::string_view name = panelName ? panelName : "";
		const bool targetsMaximizedPanel = name == m_MaximizedPanelWindow
			|| (name == "Scene" && m_MaximizedPanelWindow == "Scene###Scene");
		if (m_PanelMaximized && !targetsMaximizedPanel)
		{
			m_PendingPanelFocusAfterRestore = std::string(name);
			m_PendingPanelMaximizeAction = PanelMaximizeAction::Restore;
		}
		else
		{
			m_PendingPanelFocus = std::string(name);
		}
		if (name == "Build Settings") m_EditorPanelCycleIndex = 0;
		else if (name == "Game") m_EditorPanelCycleIndex = 1;
		else if (name == "Hierarchy") m_EditorPanelCycleIndex = 2;
		else if (name == "Inspector") m_EditorPanelCycleIndex = 3;
		else if (name == "Project") m_EditorPanelCycleIndex = 4;
		else if (name == "Scene") m_EditorPanelCycleIndex = 5;
		else if (name == "Console") m_EditorPanelCycleIndex = 6;
		else if (name == "Animator") m_EditorPanelCycleIndex = 7;
		else if (name == "Animation") m_EditorPanelCycleIndex = 8;
		else if (name == "Tile Palette") m_EditorPanelCycleIndex = 9;
	}

	void EditorLayer::CycleEditorPanel(int direction)
	{
		constexpr int panelCount = 10;
		if (direction == 0)
			return;

		auto isPanelOpen = [this](int index)
		{
			switch (index)
			{
				case 0: return m_ShowBuildSettingsPanel;
				case 1: return m_ShowGamePanel;
				case 2: return m_ShowHierarchyPanel;
				case 3: return m_ShowInspectorPanel;
				case 4: return m_ShowProjectPanel;
				case 5: return m_ShowScenePanel;
				case 6: return m_ShowConsolePanel;
				case 7: return m_ShowAnimatorPanel;
				case 8: return m_ShowAnimationPanel;
				case 9: return m_ShowTilePalettePanel;
				default: return false;
			}
		};

		for (int step = 1; step <= panelCount; ++step)
		{
			const int delta = direction > 0 ? step : -step;
			const int candidate = (m_EditorPanelCycleIndex + delta +
				panelCount * 2) % panelCount;
			if (!isPanelOpen(candidate))
				continue;

			m_EditorPanelCycleIndex = candidate;
			switch (candidate)
			{
				case 0:
					FocusEditorPanel("Build Settings", m_ShowBuildSettingsPanel);
					m_FocusBuildSettingsPanel = true;
					break;
				case 1: FocusEditorPanel("Game", m_ShowGamePanel); break;
				case 2: FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel); break;
				case 3: FocusEditorPanel("Inspector", m_ShowInspectorPanel); break;
				case 4: FocusEditorPanel("Project", m_ShowProjectPanel); break;
				case 5: FocusEditorPanel("Scene", m_ShowScenePanel); break;
				case 6: FocusEditorPanel("Console", m_ShowConsolePanel); break;
				case 7: FocusEditorPanel("Animator", m_ShowAnimatorPanel); break;
				case 8: FocusEditorPanel("Animation", m_ShowAnimationPanel); break;
				case 9: FocusEditorPanel("Tile Palette", m_ShowTilePalettePanel); break;
			}
			return;
		}
	}

	void EditorLayer::UI_MainMenuBar()
	{
        const auto& colors = ImGui::GetStyle().Colors;
        const ImVec4 menuText = colors[ImGuiCol_Text];
        const ImVec4 menuTextDisabled = colors[ImGuiCol_TextDisabled];
        const ImVec4 menuSurface = colors[ImGuiCol_PopupBg];
        const ImVec4 menuSelected = colors[ImGuiCol_Header];
        const ImVec4 menuHover = colors[ImGuiCol_HeaderHovered];
        const ImVec4 menuActive = colors[ImGuiCol_HeaderActive];
        const ImVec4 menuLine = colors[ImGuiCol_Border];
		ImGui::PushStyleColor(ImGuiCol_Text, menuText);
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, menuTextDisabled);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, menuSurface);
		ImGui::PushStyleColor(ImGuiCol_Header, menuSelected);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, menuHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, menuActive);
		ImGui::PushStyleColor(ImGuiCol_Separator, menuLine);
		ImGui::PushStyleColor(ImGuiCol_Border, menuLine);
		ImGui::PushStyleColor(ImGuiCol_CheckMark, menuText);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, ImGui::GetStyle().PopupRounding);
		ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Open Project..."))
					OpenProject();
				if (ImGui::MenuItem("Save Project"))
					SaveProject();
				if (ImGui::MenuItem("Build Settings..."))
				{
					m_ShowBuildSettingsPanel = true;
					m_FocusBuildSettingsPanel = true;
					m_EditorPanelCycleIndex = 0;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("New Scene", "Ctrl+N"))
					NewScene();
				if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
					OpenScene();
				if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
					SaveScene();
				if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
					SaveSceneAs();
				ImGui::Separator();
				if (ImGui::MenuItem("Exit"))
					RequestExit();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				const bool editMode = m_SceneState == SceneState::Edit;
				if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
					editMode && m_SceneHistory.CanUndo()))
					UndoScene();
				if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
					editMode && m_SceneHistory.CanRedo()))
					RedoScene();
				ImGui::Separator();
				if (ImGui::MenuItem("Project Settings..."))
					OpenProjectSettingsPanel();
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Assets", m_CurrentProject != nullptr))
			{
				m_ContentBrowserPanel.DrawAssetsMenu();
				ImGui::Separator();
				if (ImGui::MenuItem("Compile C# Scripts", nullptr, false,
					!IsSceneRunning() && !m_ScriptCompiler.IsCompileInProgress()))
				{
					m_ShowConsolePanel = true;
					m_PendingPanelFocus = "Console";
					m_ScriptCompileDebounceRemaining = 0.65f;
					m_ScriptCompiler.StartCompile(true);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("GameObject", m_ActiveScene != nullptr))
			{
				if (m_SceneHierarchyPanel.DrawGameObjectMenu())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Component"))
			{
				const bool canAddComponent = m_SceneState == SceneState::Edit
					&& m_SceneHierarchyPanel.CanAddComponentToSelection();
				if (ImGui::MenuItem("Add Component...", nullptr, false,
					canAddComponent))
				{
					m_SceneHierarchyPanel.RequestAddComponentPopup();
					FocusEditorPanel("Inspector", m_ShowInspectorPanel);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::BeginMenu("Animation"))
				{
					if (ImGui::MenuItem("Animation"))
						FocusEditorPanel("Animation", m_ShowAnimationPanel);
					if (ImGui::MenuItem("Animator"))
						FocusEditorPanel("Animator", m_ShowAnimatorPanel);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("2D"))
				{
					const bool canOpenAtlas = m_ContentBrowserPanel.CanOpenSpriteAtlasTools();
					if (ImGui::MenuItem("Sprite Atlas Tools", nullptr, false, canOpenAtlas))
					{
						m_ContentBrowserPanel.OpenSpriteAtlasToolsForSelection();
						FocusEditorPanel("Project", m_ShowProjectPanel);
					}
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)
						&& !canOpenAtlas)
						ImGui::SetTooltip("Select a texture in Project first.");
					if (ImGui::MenuItem("Tile Palette"))
						FocusEditorPanel("Tile Palette", m_ShowTilePalettePanel);
					ImGui::EndMenu();
				}
				ImGui::Separator();
				if (ImGui::BeginMenu("Panels"))
				{
					const bool hasFloatingPanel = m_ShowBuildSettingsPanel || m_ShowProjectSettingsPanel ||
						(m_ShowAnimationPanel && !m_SceneHierarchyPanel.IsAnimationDocked()) ||
						(m_ShowAnimatorPanel && !m_SceneHierarchyPanel.IsAnimatorGraphDocked()) ||
						(m_ShowTilePalettePanel && !m_SceneHierarchyPanel.IsTilePaletteDocked()) ||
						(m_ShowScenePanel && !m_ScenePanelDocked) ||
						(m_ShowGamePanel && !m_GamePanelDocked) ||
						(m_ShowHierarchyPanel && !m_SceneHierarchyPanel.IsHierarchyDocked()) ||
						(m_ShowInspectorPanel && !m_SceneHierarchyPanel.IsInspectorDocked()) ||
						(m_ShowProjectPanel && !m_ContentBrowserPanel.IsDocked()) ||
						(m_ShowConsolePanel && !m_ConsolePanel.IsDocked());
					if (ImGui::MenuItem("Close all floating panels...", nullptr, false,
						hasFloatingPanel))
					{
						m_ShowBuildSettingsPanel = false;
						m_ShowProjectSettingsPanel = false;
						if (!m_SceneHierarchyPanel.IsAnimationDocked()) m_ShowAnimationPanel = false;
						if (!m_SceneHierarchyPanel.IsAnimatorGraphDocked()) m_ShowAnimatorPanel = false;
						if (!m_SceneHierarchyPanel.IsTilePaletteDocked()) m_ShowTilePalettePanel = false;
						if (!m_ScenePanelDocked) m_ShowScenePanel = false;
						if (!m_GamePanelDocked) m_ShowGamePanel = false;
						if (!m_SceneHierarchyPanel.IsHierarchyDocked()) m_ShowHierarchyPanel = false;
						if (!m_SceneHierarchyPanel.IsInspectorDocked()) m_ShowInspectorPanel = false;
						if (!m_ContentBrowserPanel.IsDocked()) m_ShowProjectPanel = false;
						if (!m_ConsolePanel.IsDocked()) m_ShowConsolePanel = false;
					}
					ImGui::Separator();
					if (ImGui::MenuItem("1 Animation"))
						FocusEditorPanel("Animation", m_ShowAnimationPanel);
					if (ImGui::MenuItem("2 Animator"))
						FocusEditorPanel("Animator", m_ShowAnimatorPanel);
					if (ImGui::MenuItem("3 Tile Palette"))
						FocusEditorPanel("Tile Palette", m_ShowTilePalettePanel);
					if (ImGui::MenuItem("4 Build Settings"))
					{
						m_ShowBuildSettingsPanel = true;
						m_FocusBuildSettingsPanel = true;
						m_EditorPanelCycleIndex = 0;
					}
					if (ImGui::MenuItem("5 Console"))
						FocusEditorPanel("Console", m_ShowConsolePanel);
					if (ImGui::MenuItem("Profiler"))
						FocusEditorPanel("Profiler", m_ShowProfilerPanel);
					if (ImGui::MenuItem("6 Game"))
						FocusEditorPanel("Game", m_ShowGamePanel);
					if (ImGui::MenuItem("7 Hierarchy"))
						FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
					if (ImGui::MenuItem("8 Inspector"))
						FocusEditorPanel("Inspector", m_ShowInspectorPanel);
					if (ImGui::MenuItem("9 Project"))
						FocusEditorPanel("Project", m_ShowProjectPanel);
					if (ImGui::MenuItem("10 Scene"))
						FocusEditorPanel("Scene", m_ShowScenePanel);
					ImGui::EndMenu();
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Next Window", "Ctrl+Tab"))
					CycleEditorPanel(1);
				if (ImGui::MenuItem("Previous Window", "Ctrl+Shift+Tab"))
					CycleEditorPanel(-1);
				ImGui::Separator();
                ImGui::MenuItem("Asset Inspector",nullptr,&m_ShowAssetInspector);
                ImGui::MenuItem("Runtime Scenes", nullptr, &m_ShowRuntimeScenes);
                ImGui::MenuItem("Editor Preferences", nullptr, &m_ShowEditorPreferences);
                if (ImGui::BeginMenu("Layouts"))
				{
					if (ImGui::MenuItem("Default / Reset Layout")) m_LayoutRequest = 1;
                    if (ImGui::MenuItem("Animation")) m_LayoutRequest = 2;
                    if (ImGui::MenuItem("Debugging")) m_LayoutRequest = 3;
					ImGui::EndMenu();
				}
				ImGui::Separator();

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Modules"))
			{
				const auto loadedModules =
					ModuleSystem::Get().GetLoadedModules();
				if (loadedModules.empty())
					ImGui::MenuItem("No modules loaded", nullptr, false, false);
				for (const auto& module : loadedModules)
					ImGui::MenuItem(module.DisplayName.c_str(), nullptr, false, false);
				const auto commands = ModuleSystem::Get().GetEditorCommands();
				if (!commands.empty())
					ImGui::Separator();
				for (const auto& command : commands)
				{
					if (ImGui::MenuItem(command.Label.c_str()))
					{
						try
						{
							command.Callback();
						}
						catch (const std::exception& error)
						{
							m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
								std::string("Module command failed: ") + error.what(),
								command.ModuleName);
							m_ShowConsolePanel = true;
						}
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("TomCat Editor", nullptr, false, false);
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(9);
	}

	void EditorLayer::OnImGuiRender()
	{
		TC_PROFILE_FUNCTION();
		UpdateWindowTitle();

		static bool dockspaceOpen = true;
		static bool opt_fullscreen_persistant = true;
		bool opt_fullscreen = opt_fullscreen_persistant;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (opt_fullscreen)
		{
			ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->Pos);
			ImGui::SetNextWindowSize(viewport->Size);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
			window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}

		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyleColorVec4(ImGuiCol_TitleBg));
		BeginEditorWindow("DockSpace Demo", &dockspaceOpen, window_flags);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		if (opt_fullscreen)
			ImGui::PopStyleVar(2);

		UI_MainMenuBar();

		const float toolbarHeight = std::round(ImGui::GetFontSize()*1.75f);
		// Unity keeps the global playbar one step darker than docked panels.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_TitleBg]);
		ImGui::BeginChild("ToolbarRegion", ImVec2(0, toolbarHeight), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		UI_Toolbar();
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::Separator();

		ImGui::BeginChild("DockSpaceRegion", ImVec2(0, 0), false,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGuiIO& io = ImGui::GetIO();
		ImGuiStyle& style = ImGui::GetStyle();
		const float previousMinimumWidth = style.WindowMinSize.x;
		const ImVec2 dockspaceSize = ImGui::GetContentRegionAvail();
		const float dockspaceWidth = dockspaceSize.x;
		// Docked panels share ImGui's splitter minimum. Keep it compact in a
		// small editor window and let it grow to a comfortable desktop width.
		style.WindowMinSize.x = std::clamp(
			dockspaceWidth * kDockedPanelMinimumWidthRatio,
			kDockedPanelCompactMinimumWidth,
			kDockedPanelExpandedMinimumWidth);

		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            m_EditorDockspaceId = dockspace_id;
            if (m_LayoutRequest || !ImGui::DockBuilderGetNode(dockspace_id))
            {
                const int layout = m_LayoutRequest;
                m_PanelMaximized = false;
                ImGui::DockBuilderRemoveNode(dockspace_id);
                ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspace_id, dockspaceSize);
                ImGuiID center = dockspace_id, left, right, bottom;
                const float inspectorWidth=std::clamp(dockspaceSize.x*0.27f,340.0f,480.0f);
                const float hierarchyWidth=std::clamp(dockspaceSize.x*0.18f,240.0f,340.0f);
                ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,std::min(0.36f,inspectorWidth/dockspaceSize.x),&right,&center);
                ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,layout==3?0.46f:0.28f,&bottom,&center);
                ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,std::min(0.34f,hierarchyWidth/std::max(1.0f,dockspaceSize.x-inspectorWidth)),&left,&center);
                ImGui::DockBuilderDockWindow("Hierarchy", left);
                ImGui::DockBuilderDockWindow("Inspector", right);
                ImGui::DockBuilderDockWindow("Scene###Scene", center);
                ImGui::DockBuilderDockWindow("Game", center);
                ImGui::DockBuilderDockWindow("Animator", center);
                for (const char* name : { "Project", "Console", "Animation", "Tile Palette", "Profiler" })
                    ImGui::DockBuilderDockWindow(name, bottom);
                ImGui::DockBuilderFinish(dockspace_id);
                m_ShowScenePanel = m_ShowGamePanel = m_ShowHierarchyPanel = m_ShowInspectorPanel = m_ShowProjectPanel = true;
                m_ShowAnimationPanel = m_ShowAnimatorPanel = layout == 2;
                m_ShowConsolePanel = m_ShowProfilerPanel = layout == 3;
                m_LayoutRequest = 0;
                m_PendingPanelFocus = layout==2 ? "Animator" : "Scene###Scene";
            }
			ApplyPendingPanelMaximizeTransition(dockspace_id, dockspaceSize);
            ApplyPendingTabActions();
			ImGuiDockNodeFlags activeDockspaceFlags = dockspace_flags;
			if (m_PanelMaximized)
				activeDockspaceFlags |= ImGuiDockNodeFlags_NoSplit
					| ImGuiDockNodeFlags_NoResize;
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),
				activeDockspaceFlags);
		}

		style.WindowMinSize.x = previousMinimumWidth;

		ImGui::EndChild(); 

		// Process asset selection and retire preview textures before the Inspector draws.
		if (ShouldRenderDockPanel("Project"))
		{
			m_ContentBrowserPanel.OnImGuiRender(&m_ShowProjectPanel);
			if (m_ContentBrowserPanel.IsFocused())
				m_EditorPanelCycleIndex = 4;
		}

		m_SceneHierarchyPanel.SetColliderEditingAllowed(m_SceneState == SceneState::Edit);
		m_SceneHierarchyPanel.SetPrefabCreationAllowed(m_SceneState == SceneState::Edit);
		m_SceneHierarchyPanel.FlushPendingCommands();
		if (!m_PanelMaximized
			|| ShouldRenderDockPanel("Hierarchy")
			|| ShouldRenderDockPanel("Inspector"))
		{
			bool hierarchyOpen = m_ShowHierarchyPanel
				&& ShouldRenderDockPanel("Hierarchy");
			bool inspectorOpen = m_ShowInspectorPanel
				&& ShouldRenderDockPanel("Inspector");
			m_SceneHierarchyPanel.OnImGuiRender(&hierarchyOpen, &inspectorOpen,
				IsSceneDirty());
			if (const UUID requested = m_SceneHierarchyPanel.ConsumeFrameEntityRequest();
				static_cast<uint64_t>(requested) != 0 && m_ActiveScene)
			{
				m_Viewport.FrameSceneEntity(m_ActiveScene->FindEntityByUUID(requested));
			}
			if (!m_PanelMaximized)
			{
				m_ShowHierarchyPanel = hierarchyOpen;
				m_ShowInspectorPanel = inspectorOpen;
			}
			if (m_SceneHierarchyPanel.IsHierarchyFocused())
				m_EditorPanelCycleIndex = 2;
			else if (m_SceneHierarchyPanel.IsInspectorFocused())
				m_EditorPanelCycleIndex = 3;
		}
		if (m_SceneHierarchyPanel.ConsumeAnimatorGraphOpenRequest())
			FocusEditorPanel("Animator", m_ShowAnimatorPanel);
		if (m_SceneHierarchyPanel.ConsumeAnimationOpenRequest())
			FocusEditorPanel("Animation", m_ShowAnimationPanel);
		if (m_SceneHierarchyPanel.ConsumeTilePaletteOpenRequest())
			FocusEditorPanel("Tile Palette", m_ShowTilePalettePanel);
		if (m_ShowAnimationPanel && ShouldRenderDockPanel("Animation"))
		{
			m_SceneHierarchyPanel.OnAnimationImGuiRender(&m_ShowAnimationPanel);
			if (m_SceneHierarchyPanel.IsAnimationFocused())
				m_EditorPanelCycleIndex = 8;
		}
		if (m_ShowAnimatorPanel && ShouldRenderDockPanel("Animator"))
		{
			m_SceneHierarchyPanel.OnAnimatorGraphImGuiRender(&m_ShowAnimatorPanel);
			if (m_SceneHierarchyPanel.IsAnimatorGraphFocused())
				m_EditorPanelCycleIndex = 7;
		}
		if (m_ShowTilePalettePanel && ShouldRenderDockPanel("Tile Palette"))
		{
			m_SceneHierarchyPanel.OnTilePaletteImGuiRender(&m_ShowTilePalettePanel);
			if (m_SceneHierarchyPanel.IsTilePaletteFocused())
				m_EditorPanelCycleIndex = 9;
		}
		if (ShouldRenderDockPanel("Console"))
		{
			m_ConsolePanel.OnImGuiRender(&m_ShowConsolePanel);
			if (m_ConsolePanel.IsFocused())
				m_EditorPanelCycleIndex = 6;
		}

        m_ContentBrowserPanel.OnAssetInspectorRender(&m_ShowAssetInspector);
        m_ConsolePanel.SetErrorPauseCallback([this] { if (m_SceneState == SceneState::Play) OnScenePause(); });
        m_ConsolePanel.SetOpenSourceCallback([this](const std::filesystem::path& path, uint32_t line, uint32_t column) { m_ContentBrowserPanel.OpenDiagnosticSource(path, line, column); });
        if (ShouldRenderDockPanel("Profiler")) m_ProfilerPanel.OnImGuiRender(&m_ShowProfilerPanel);
        if (m_ShowRuntimeScenes)
        {
            PrepareEditorToolWindow(ImVec2(720,520));
            if(BeginEditorWindow("Runtime Scenes",&m_ShowRuntimeScenes))
            {
                if(!IsSceneRunning()) ImGui::TextWrapped("Enter Play mode to inspect loaded scenes, control asynchronous loading and mark persistent roots.");
                else
                {
                    const char* states[]={"Idle","Reading","Ready","Completed","Failed","Cancelled"};
                    const auto state=m_RuntimeSceneManager.GetLoadState();
                    ImGui::Text("Load state: %s",states[static_cast<unsigned>(state)]);
                    ImGui::ProgressBar(m_RuntimeSceneManager.GetLoadProgress(),ImVec2(-1,0));
                    bool allow=m_RuntimeSceneManager.GetAllowSceneActivation();
                    if(ImGui::Checkbox("Allow scene activation",&allow)) m_RuntimeSceneManager.SetAllowSceneActivation(allow);
                    if(m_RuntimeSceneManager.HasPendingTransition() && ImGui::Button("Cancel pending load")) m_RuntimeSceneManager.CancelPendingLoad();
                    ImGui::Separator();
                    ImGui::TextUnformatted("Loaded scenes");
                    for(AssetHandle handle:m_RuntimeSceneManager.GetLoadedSceneHandles())
                    {
                        ImGui::PushID(std::to_string(static_cast<uint64_t>(handle)).c_str());
                        const auto* metadata=AssetManager::Get().GetRegistry().GetMetadata(handle);
                        ImGui::TextWrapped("%s%s",metadata?PathToUTF8(metadata->FilePath).c_str():"Unknown scene",handle==m_RuntimeSceneManager.GetActiveSceneHandle()?" (Active)":"");
                        if(ImGui::SmallButton("Set active")) m_RuntimeSceneManager.SetActiveScene(handle);
                        ImGui::SameLine();
                        if(ImGui::SmallButton("Unload")) m_RuntimeSceneManager.RequestUnloadScene(handle);
                        ImGui::PopID();
                    }
                    if(ImGui::TreeNode("Load a build scene"))
                    {
                        for(AssetHandle handle:m_RuntimeSceneManager.GetBuildSceneHandles())
                        {
                            ImGui::PushID(std::to_string(static_cast<uint64_t>(handle)).c_str());
                            const auto* metadata=AssetManager::Get().GetRegistry().GetMetadata(handle);
                            ImGui::TextWrapped("%s",metadata?PathToUTF8(metadata->FilePath).c_str():"Unknown scene");
                            ImGui::BeginDisabled(m_RuntimeSceneManager.HasPendingTransition());
                            if(ImGui::SmallButton("Load single")) m_RuntimeSceneManager.RequestLoadSceneAsync(handle,SceneLoadMode::Single);
                            ImGui::SameLine();
                            if(ImGui::SmallButton("Load additive")) m_RuntimeSceneManager.RequestLoadSceneAsync(handle,SceneLoadMode::Additive);
                            ImGui::EndDisabled(); ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                    Entity selected=m_SceneHierarchyPanel.GetSelectedEntity();
                    if(selected)
                    {
                        bool persistent=m_RuntimeSceneManager.IsEntityPersistent(selected);
                        if(ImGui::Checkbox("Selected root persists across scenes",&persistent)) m_RuntimeSceneManager.SetEntityPersistent(selected,persistent);
                    }
                    if(!m_RuntimeSceneManager.GetLastError().empty()) ImGui::TextWrapped("%s",m_RuntimeSceneManager.GetLastError().c_str());
                }
            }
            ImGui::End();
        }
        if(m_ShowEditorPreferences)
        {
            PrepareEditorToolWindow(ImVec2(620,420));
            if(BeginEditorWindow("Editor Preferences",&m_ShowEditorPreferences))
            {
                ImGui::TextUnformatted("Interface"); ImGui::Separator();
                float scale=m_EditorUIScale;
                if(ImGui::SliderFloat("UI scale",&scale,0.8f,1.6f,"%.2fx"))
                { ImGui::GetStyle().ScaleAllSizes(scale/m_EditorUIScale); ImGui::GetIO().FontGlobalScale=scale; m_EditorUIScale=scale; }
                ImGui::TextWrapped("Interface scale applies to this editor session. Layout presets are available under Window > Layouts.");
                if(ImGui::Button("Reset layout")) m_LayoutRequest=1;
                ImGui::TextUnformatted("Navigation"); ImGui::Separator();
                ImGui::TextWrapped("Ctrl+Tab / Ctrl+Shift+Tab: cycle panels. Ctrl-click: toggle selection. Shift-click: select a range. Right-click an axis value: reset that axis. Drag assets onto compatible reference fields.");
            }
            ImGui::End();
        }


		if (m_ShowScenePanel && ShouldRenderDockPanel("Scene###Scene"))
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		// Use the same native menu-bar slot as Hierarchy.  ImGui's dock tab and
		// menu-bar layout then share one geometry source, eliminating the hand-
		// positioned gap that appeared with the custom Scene strip.
		// Scene is a fixed viewport. Overlay items may extend ImGui's content
		// bounds, but must never scroll or shrink the rendered camera area.
		ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
		const bool sceneVisible = BeginEditorWindow("Scene###Scene", &m_ShowScenePanel,
			ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar
				| ImGuiWindowFlags_NoScrollWithMouse);
		m_ScenePanelDocked = ImGui::IsWindowDocked();
		if (!sceneVisible)
		{
			m_ViewportFocused = false;
			m_HoveredEntity = {};
		}

		auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		auto viewportOffset = ImGui::GetWindowPos();
		m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };
		// The content origin is the bottom edge of the native MenuBar.  Derive the
		// dock row from that edge so the custom Scene toolbar occupies the actual
		// menu-bar slot (and remains vertically centered at every font/DPI scale).
		m_GizmoModeDockHeight = ImGui::GetFrameHeight();
		m_GizmoModeDockY = m_ViewportBounds[0].y - m_GizmoModeDockHeight;

		m_ViewportFocused = ImGui::IsWindowFocused();
		if (m_ViewportFocused)
			m_EditorPanelCycleIndex = 5;

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint64_t sceneTextureID = m_Framebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(sceneTextureID), ImVec2{ m_ViewportSize.x, m_ViewportSize.y },
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		m_ViewportCanvasHovered = sceneVisible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

		// The framebuffer image must remain the current ImGui item while registering
		// its drop target. Toolbar items submitted later must never steal the target.
		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID, flags))
			{
				if (payload->DataSize == sizeof(uint64_t) && m_ActiveScene)
				{
					const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
					const BuiltInSpriteAsset* builtIn = FindBuiltInSpriteAsset(handle);
					const AssetMetadata* metadata = builtIn ? nullptr
						: AssetManager::Get().GetRegistry().GetMetadata(handle);
					if (builtIn || (metadata && !metadata->IsMissing
						&& metadata->Type == AssetType::Texture2D))
					{
						const std::string name = builtIn ? std::string(builtIn->Name)
							: PathToUTF8(metadata->FilePath.stem());
						Entity sprite = m_ActiveScene->CreateEntity(name);
						auto& renderer = sprite.AddComponent<SpriteRenderer>(glm::vec4{ 1.0f });
						renderer.SpriteHandle = handle;
						renderer.Sprite = AssetManager::Get().LoadTexture(handle);
						if (m_SceneState == SceneState::Edit)
							CommitImmediateSceneTransaction("Create Sprite");
					}
					else if (metadata && !metadata->IsMissing
						&& metadata->Type == AssetType::Prefab)
					{
						const ImVec2 mouse = ImGui::GetMousePos();
						glm::vec2 worldPosition{};
						if (m_Viewport.ScreenToWorldOnPlane({ mouse.x, mouse.y }, 0.0f,
							worldPosition))
						{
							InstantiatePrefab(handle, std::nullopt,
								glm::vec3(worldPosition, 0.0f));
						}
						else
						{
							ReportPrefabOperation(false,
								"Could not project the Prefab drop point into the Scene view.");
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// Draw the mode bar in the foreground layer for both docked and floating
		// states.  The docked position is still computed from the Scene row, while
		// the foreground draw list keeps it above that row during drag operations.
		// Foreground primitives are global.  Do not submit ordinary overlays while
		// the Scene tab is hidden; an active drag is the one exception below so the
		// toolbar remains visible while crossing the tab bar.
		// Keep rendering an active drag even when docking temporarily marks the
		// Scene tab hidden (for example while the cursor crosses the Scene/Game
		// tab bar).  Otherwise the drag state has no frame in which to paint and
		// the anchor toolbar appears to disappear.
		if (sceneVisible || m_GizmoModeToolbarDragging || m_GizmoTransformToolbarDragging)
		{
			// BeginMenuBar switches ImGui to the same full-width clip/layout region
			// used by Hierarchy.  Submit the custom controls after the framebuffer
			// image so their foreground draw order remains above the Scene content.
			if (sceneVisible && ImGui::BeginMenuBar())
			{
				// MenuBarBg is intentionally the darker foundation.  Scene's toolbar
				// row is an explicit lighter overlay, painted before the controls so
				// an empty dock still shows the complete top-bar surface.
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImVec2(m_ViewportBounds[0].x, m_GizmoModeDockY),
					ImVec2(m_ViewportBounds[1].x, m_GizmoModeDockY + m_GizmoModeDockHeight),
					ImGui::GetColorU32(ImGuiCol_Tab));
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
				UI_SceneToolbarDockPreview();
				ImGui::EndMenuBar();
			}
			else
			{
				// Keep the overlay alive if the native menu-bar slot is temporarily
				// unavailable (for example while the tab is being hidden during a drag).
				UI_SceneGizmoModeToolbarOverlay();
				UI_SceneGizmoToolbar();
				UI_SceneToolbarDockPreview();
			}
		}
		// Gizmos
		Entity selectedEntity = m_SceneHierarchyPanel.GetSelectedEntity();
		bool usesRectTransformHandles = false;
		if (sceneVisible)
			usesRectTransformHandles = m_Viewport.UI_RectTransformHandles();
		else
			m_Viewport.ResetRectTransformEditState();

		bool submittedWorldGizmo = false;
		if (sceneVisible && selectedEntity && m_ActiveScene
			&& m_ActiveScene->IsVisibleInEditorHierarchy(selectedEntity)
			&& m_GizmoType != -1 && !m_SceneHierarchyPanel.IsEditingCollider()
			&& !usesRectTransformHandles
			&& (!IsSceneOrientationGizmoPointerInside() || m_GizmoDragActive))
		{
			ImGuizmo::AllowAxisFlip(false);
			ImGuizmo::SetOrthographic(m_EditorCamera.IsOrthographic());
			ImGuizmo::SetDrawlist();

			ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
				m_ViewportBounds[1].x - m_ViewportBounds[0].x,
				m_ViewportBounds[1].y - m_ViewportBounds[0].y);

			glm::mat4 cameraProjection(1.0f);
			glm::mat4 cameraView(1.0f);
			m_EditorCamera.GetRightHandedToolMatrices(cameraView,
				cameraProjection);

			if (selectedEntity.HasComponent<Transform>())
			{
				auto& tc = selectedEntity.GetComponent<Transform>();
				const glm::mat4 originalTransform = tc.GetTransform();
				glm::mat4 transform = originalTransform;
				glm::vec3 minimum, maximum;
				const bool standardSingle = m_ActiveScene->GetChildrenUUIDs(selectedEntity).empty()
					&& !selectedEntity.HasComponent<Tilemap2D>()
					&& !selectedEntity.HasComponent<ParticleSystem2D>();
				if ((standardSingle || m_GizmoPivotMode == GizmoPivotMode::Center)
					&& m_Viewport.GetEntityBounds(selectedEntity, minimum, maximum))
					transform[3] = glm::vec4((minimum + maximum) * 0.5f, 1.0f);
				const glm::mat4 initialGizmoTransform = transform;

				bool snap = Input::IsKeyPressed(Key::LeftControl);
				float snapValue = 0.5f;
				if (m_GizmoType == ImGuizmo::OPERATION::ROTATE)
					snapValue = 45.0f;

				float snapValues[3] = { snapValue, snapValue, snapValue };

				ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
					(ImGuizmo::OPERATION)m_GizmoType,
					m_GizmoSpaceMode == GizmoSpaceMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
					glm::value_ptr(transform),
					nullptr, snap ? snapValues : nullptr);
				submittedWorldGizmo = true;
				m_GizmoDragActive = ImGuizmo::IsUsing();
				m_GizmoHandleHovered = ImGuizmo::IsOver(
					static_cast<ImGuizmo::OPERATION>(m_GizmoType));

				if (m_GizmoDragActive)
				{
					if (m_SceneState == SceneState::Edit
						&& !m_GizmoTransactionActive)
					{
						BeginSceneTransaction("Gizmo Drag");
						m_GizmoTransactionActive =
							m_SceneHistory.HasActiveTransaction();
					}
					// Apply the delta around the displayed center, preserving the authored origin.
					const glm::mat4 editedTransform = transform * glm::inverse(initialGizmoTransform) * originalTransform;
					if (m_ActiveScene->SetWorldTransform(selectedEntity, editedTransform)
						&& m_GizmoTransactionActive)
						UpdateSceneTransaction();
				}
			}
		}
		if (!submittedWorldGizmo)
		{
			m_GizmoDragActive = false;
			m_GizmoHandleHovered = false;
		}
		if (m_GizmoTransactionActive && !m_GizmoDragActive)
		{
			m_GizmoTransactionActive = false;
			CommitSceneTransaction();
		}

		if (sceneVisible)
			m_Viewport.UI_ColliderEditHandles();
		else
			m_Viewport.ResetColliderEditState();

		// Draw the orientation control last so entity and collider gizmos cannot
		// cover it or receive a click intended for one of its axis cones.
		if (sceneVisible)
			UI_SceneOrientationGizmo();
		else
		{
			m_SceneOrientationGizmoHovered = false;
			m_SceneOrientationGizmoBounds[0] = {};
			m_SceneOrientationGizmoBounds[1] = {};
			m_SceneOrientationPressedTarget = -2;
		}

		// ImGui receives wheel input for both the main window and detached Scene
		// windows. Route it after this frame's hover/overlay state is established.
		const float sceneWheel = ImGui::GetIO().MouseWheel;
		if (m_ViewportCanvasHovered && !m_SceneOrientationGizmoHovered
			&& std::isfinite(sceneWheel) && sceneWheel != 0.0f)
		{
			MouseScrolledEvent scroll(0.0f, sceneWheel);
			m_EditorCamera.OnEvent(scroll);
		}

		ImGui::End();
		ImGui::PopStyleVar();
		}
		else
		{
			if (m_GizmoTransactionActive)
			{
				m_GizmoTransactionActive = false;
				CommitSceneTransaction();
			}
			m_GizmoDragActive = false;
			m_GizmoHandleHovered = false;
			m_Viewport.ResetRectTransformEditState();
			m_ViewportFocused = false;
			m_ViewportCanvasHovered = false;
			m_ViewportCameraDragOwned = false;
			m_SceneOrientationGizmoHovered = false;
			m_SceneOrientationGizmoBounds[0] = {};
			m_SceneOrientationGizmoBounds[1] = {};
			m_SceneOrientationPressedTarget = -2;
			m_HoveredEntity = {};
		}

		if (m_ShowGamePanel && ShouldRenderDockPanel("Game"))
		{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });

		BeginEditorWindow("Game", &m_ShowGamePanel,
			ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollWithMouse);
		m_GamePanelDocked = ImGui::IsWindowDocked();
		const bool gameViewportFocused = ImGui::IsWindowFocused(
			ImGuiFocusedFlags_RootAndChildWindows);
		const bool gameViewportHovered = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_RootAndChildWindows
				| ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		if (gameViewportFocused)
			m_EditorPanelCycleIndex = 1;
		if (gameViewportFocused && gameViewportHovered)
		{
			const float wheel = ImGui::GetIO().MouseWheel;
			if (std::isfinite(wheel) && std::abs(wheel) > 0.0001f)
				m_GameViewScale = std::clamp(m_GameViewScale
					+ wheel * kGameViewScaleWheelStep,
					kGameViewScaleMinimum, kGameViewScaleMaximum);
		}

		ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.075f, 0.075f, 0.075f, 1.0f));
		if (ImGui::BeginMenuBar())
		{
			const ImGuiStyle& gameStyle = ImGui::GetStyle();
			const float toolbarHeight = ImGui::GetFrameHeight();
			const float toolbarWidth = ImGui::GetContentRegionAvail().x;
			const ImVec2 statsLabelSize = ImGui::CalcTextSize("Stats");
			const float statsButtonWidth = std::max(69.0f,
				statsLabelSize.x + gameStyle.FramePadding.x * 2.0f);
			const float gameButtonWidth = std::max(116.0f,
				ImGui::CalcTextSize("Game").x + toolbarHeight
				+ gameStyle.FramePadding.x * 2.0f);
			const int resolutionIndex = std::clamp(m_GameViewResolutionIndex, 0,
				static_cast<int>(kGameViewResolutions.size()) - 1);
			const char* resolutionLabel =
				kGameViewResolutions[static_cast<size_t>(resolutionIndex)].Label;
			float resolutionButtonWidth = std::max(260.0f,
				ImGui::CalcTextSize(resolutionLabel).x + toolbarHeight
				+ gameStyle.FramePadding.x * 2.0f);
			constexpr float minimumScaleWidth = 180.0f;
			constexpr float toolbarSpacing = 3.0f;
			if (gameButtonWidth + resolutionButtonWidth + minimumScaleWidth
				+ statsButtonWidth + toolbarSpacing > toolbarWidth)
			{
				resolutionButtonWidth = std::max(150.0f, toolbarWidth
					- gameButtonWidth - minimumScaleWidth - statsButtonWidth
					- toolbarSpacing);
			}
			const float scaleButtonWidth = std::max(minimumScaleWidth, toolbarWidth
				- gameButtonWidth - resolutionButtonWidth - statsButtonWidth
				- toolbarSpacing);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.0f, 0.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
			ImGui::PushStyleColor(ImGuiCol_FrameBg, gameStyle.Colors[ImGuiCol_Tab]);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, gameStyle.Colors[ImGuiCol_ButtonHovered]);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, gameStyle.Colors[ImGuiCol_ButtonActive]);

			ImGui::SetNextItemWidth(gameButtonWidth);
			if (ImGui::BeginCombo("##GameViewMode", "Game"))
			{
				ImGui::Selectable("Game", true, ImGuiSelectableFlags_Disabled);
				ImGui::Separator();
				if (ImGui::MenuItem("Reset Scale", "1x"))
					m_GameViewScale = 1.0f;
				ImGui::EndCombo();
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Game view display options");

			ImGui::SetNextItemWidth(resolutionButtonWidth);
			if (ImGui::BeginCombo("##GameViewResolution",
				resolutionLabel))
			{
				for (int index = 0;
					index < static_cast<int>(kGameViewResolutions.size()); ++index)
				{
					const bool selected = index == m_GameViewResolutionIndex;
					if (ImGui::Selectable(kGameViewResolutions[static_cast<size_t>(index)].Label,
						selected))
					{
						m_GameViewResolutionIndex = index;
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			DrawGameViewScaleSlider(m_GameViewScale, scaleButtonWidth);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Game preview scale: 0.8x to 8.8x");

			const ImVec4 statsSurface = m_GameViewStatsVisible
				? gameStyle.Colors[ImGuiCol_ButtonActive]
				: gameStyle.Colors[ImGuiCol_Tab];
			const ImVec4 statsHovered = gameStyle.Colors[ImGuiCol_ButtonHovered];
			const ImVec4 statsActive = gameStyle.Colors[ImGuiCol_ButtonActive];
			ImGui::PushStyleColor(ImGuiCol_Button, statsSurface);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, statsHovered);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, statsActive);
			if (ImGui::Button("Stats##GameStatsButton",
				ImVec2(statsButtonWidth, toolbarHeight)))
				m_GameViewStatsVisible = !m_GameViewStatsVisible;
			ImGui::PopStyleColor(3);
			ImGui::PopStyleColor(3);
			ImGui::PopStyleVar(2);
			ImGui::EndMenuBar();
		}
		ImGui::PopStyleColor();

		ImVec2 gameViewportPanelSize = ImGui::GetContentRegionAvail();
		m_GameViewportSize = { gameViewportPanelSize.x, gameViewportPanelSize.y };

		const FramebufferSpecification& gameSpec = m_GameFramebuffer->GetSpecification();
		const ImVec2 renderSize(static_cast<float>(gameSpec.Width),
			static_cast<float>(gameSpec.Height));
		// A docked Game panel uses its complete fitted image as the 1x baseline.
		// Maximizing the Game panel switches 1x to physical pixel scale so changing
		// resolution visibly changes preview size, with scrollbars for overflow.
		const float screenToFramebufferScale = std::max(0.01f,
			Application::Get().GetWindow().GetScreenToFramebufferScaleX());
		const float requestedScale = std::clamp(m_GameViewScale,
			kGameViewScaleMinimum, kGameViewScaleMaximum);
		const bool gamePanelMaximized = m_PanelMaximized
			&& m_MaximizedPanelWindow == "Game";
		const float fitScale = renderSize.x > 0.0f && renderSize.y > 0.0f
			? std::min(gameViewportPanelSize.x / renderSize.x,
				gameViewportPanelSize.y / renderSize.y)
			: 1.0f;
		const float imageScale = gamePanelMaximized
			? requestedScale / screenToFramebufferScale
			: std::max(fitScale, 0.0001f) * requestedScale;
		m_GameViewEffectiveScale = imageScale;
		const ImVec2 imageSize(renderSize.x * imageScale, renderSize.y * imageScale);
		const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
		const ImVec2 imageOrigin(contentOrigin.x
			+ std::max(0.0f, (gameViewportPanelSize.x - imageSize.x) * 0.5f),
			contentOrigin.y + std::max(0.0f,
				(gameViewportPanelSize.y - imageSize.y) * 0.5f));
		ImGui::SetCursorScreenPos(imageOrigin);

		// 始终显示GameFramebuffer（Runtime摄像机渲染内容）
		uint64_t gameTextureID = m_GameFramebuffer->GetColorAttachmentRendererID();
		ImGui::Image(reinterpret_cast<void*>(gameTextureID), imageSize,
			ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
		const ImVec2 gameImageMinimum = ImGui::GetItemRectMin();
		const ImVec2 gameImageMaximum = ImGui::GetItemRectMax();
		m_GameViewportBounds[0] = { gameImageMinimum.x, gameImageMinimum.y };
		m_GameViewportBounds[1] = { gameImageMaximum.x, gameImageMaximum.y };
		UI_GameNoCameraOverlay();

		if (m_GameViewStatsVisible)
		{
			const Renderer2D::Statistics stats = Renderer2D::GetStats();
            // Anchor to the panel's visible work rectangle, not its letterboxed image.
            // Window scrolling affects contentOrigin, but must not move this overlay.
            const ImGuiWindow* gameWindow = ImGui::GetCurrentWindow();
            const ImVec2 padding = ImGui::GetStyle().WindowPadding;
            const float margin = ImGui::GetFontSize() * 0.4f;
            const ImRect bounds = gameWindow->InnerRect;
            const float statsWidth = std::min(ImGui::CalcTextSize("FPS: 9999.9 (999.99 ms)").x + padding.x * 2,
                std::max(1.0f, bounds.GetWidth() - margin * 2));
            const float statsHeight = std::min(ImGui::GetTextLineHeightWithSpacing() * 8 + padding.y * 2 + 8,
                std::max(1.0f, bounds.GetHeight() - margin * 2));
            const ImVec2 savedCursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(bounds.Max.x - statsWidth - margin, bounds.Min.y + margin));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 0.90f));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f);
			ImGui::BeginChild("##GameStatsOverlay", ImVec2(statsWidth, statsHeight),
				true, ImGuiWindowFlags_NoScrollWithMouse);
			ImGui::TextUnformatted("Rendering Statistics");
			ImGui::Separator();
			const float frameRate = ImGui::GetIO().Framerate;
			ImGui::Text("FPS: %.1f (%.2f ms)", frameRate,
				frameRate > 0.0f ? 1000.0f / frameRate : 0.0f);
			ImGui::Text("Draw Calls: %u", stats.DrawCalls);
			ImGui::Text("Quads: %u", stats.QuadCount);
			ImGui::Text("Circles: %u", stats.CircleCount);
			ImGui::Text("Lines: %u", stats.LineCount);
			ImGui::Text("Vertices: %u", stats.GetTotalVertexCount());
			ImGui::Text("Indices: %u", stats.GetTotalIndexCount());
			ImGui::EndChild();
            ImGui::SetCursorScreenPos(savedCursor);
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		ImGui::End();
		ImGui::PopStyleVar();
		}

		DetectPanelTabDoubleClick();
        UI_PanelTabContextMenu();
		if (!m_PanelMaximized)
		{
			m_Build.UI_BuildSettings();
			m_ProjectSettings.UI_ProjectSettings();
		}
		UI_UnsavedChangesModal();
		UI_RecoveryModal();
		ImGuiLayer* imguiLayer = Application::Get().GetImGuiLayer();
		imguiLayer->BlockMouseEvents(
			!m_ViewportCanvasHovered && !m_ViewportCameraDragOwned);
		// EditorLayer resolves keyboard shortcuts after ImGui has established the
		// focused widget and popup state for this frame.
		imguiLayer->BlockKeyboardEvents(false);
		if (!m_PendingPanelFocus.empty())
		{
			ImGui::SetWindowFocus(m_PendingPanelFocus.c_str());
			m_PendingPanelFocus.clear();
		}
		if (m_PendingRestoredTabOrder >= 0
			&& !m_PendingRestoredTabWindow.empty())
		{
			ImGuiWindow* restoredWindow = ImGui::FindWindowByName(
				m_PendingRestoredTabWindow.c_str());
			if (restoredWindow && restoredWindow->DockNode
				&& restoredWindow->DockNode->TabBar)
			{
				ImGuiTabBar* tabBar = restoredWindow->DockNode->TabBar;
				if (ImGuiTabItem* tab = ImGui::TabBarFindTabByID(
					tabBar, restoredWindow->TabId))
				{
					const int currentOrder = ImGui::TabBarGetTabOrder(tabBar, tab);
					const int orderOffset = m_PendingRestoredTabOrder - currentOrder;
					if (orderOffset != 0 && tabBar->ReorderRequestTabId == 0)
						ImGui::TabBarQueueReorder(tabBar, tab, orderOffset);
					else if (orderOffset == 0)
					{
						m_PendingRestoredTabWindow.clear();
						m_PendingRestoredTabOrder = -1;
					}
				}
			}
		}
		ImGui::End();
		SaveEditorLayoutIfNeeded();
	}

	void EditorLayer::UI_GameNoCameraOverlay()
	{
		// Screen Space Canvas renders directly into Game view and does not require
		// a Camera. Do not cover that preview with the no-camera message.
		if (!m_ActiveScene || m_ActiveScene->HasGameViewRenderSource())
			return;

		ImVec2 imageMin = ImGui::GetItemRectMin();
		ImVec2 imageMax = ImGui::GetItemRectMax();
		ImVec2 imageCenter((imageMin.x + imageMax.x) * 0.5f, (imageMin.y + imageMax.y) * 0.5f);
		const float panelWidth = std::min(520.0f, std::max(300.0f, imageMax.x - imageMin.x - 40.0f));
		const float panelHeight = 112.0f;
		ImVec2 panelMin(imageCenter.x - panelWidth * 0.5f, imageCenter.y - panelHeight * 0.5f);
		ImVec2 panelMax(imageCenter.x + panelWidth * 0.5f, imageCenter.y + panelHeight * 0.5f);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		// Unity's empty Game view uses a soft rounded notification card; keep its
		// larger radius while moving the fill into the reference gray ramp.
		draw->AddRectFilled(panelMin, panelMax, IM_COL32(98, 98, 98, 235), 18.0f);
		draw->AddRect(panelMin, panelMax, IM_COL32(140, 140, 140, 255), 18.0f, 0, 1.0f);

		const char* messageText = "No cameras rendering";
		ImVec2 messageSize = ImGui::CalcTextSize(messageText);
		ImVec2 messagePos(imageCenter.x - messageSize.x * 0.5f, imageCenter.y - messageSize.y * 0.5f);
		draw->AddText(messagePos, IM_COL32(243, 243, 243, 255), messageText);
	}


	void EditorLayer::OpenProjectSettingsPanel()
	{
		if (!m_ShowProjectSettingsPanel || m_ProjectSettings.m_ProjectSettingsDraftProject != m_CurrentProject)
			m_ProjectSettings.LoadProjectSettingsDraft();
		m_ShowProjectSettingsPanel = true;
		m_FocusProjectSettingsPanel = true;
	}


	void EditorLayer::UI_SceneToolbarDragHandle(const char* id, glm::vec2& offset, bool& docked, bool& dragging,
		const ImVec2& handleMin, const ImVec2& handleMax, float tearX, bool canDock)
	{
#include "panels/UI_SceneToolbarDragHandle.inl"
	}
	void EditorLayer::UI_SceneGizmoToolbar()
	{
#include "panels/UI_SceneGizmoToolbar.inl"
	}
	void EditorLayer::UI_SceneGizmoModeToolbarOverlay()
	{
#include "panels/UI_SceneGizmoModeToolbarOverlay.inl"
	}
	void EditorLayer::UI_SceneToolbarDockPreview()
	{
#include "panels/UI_SceneToolbarDockPreview.inl"
	}

	void EditorLayer::UI_SceneOrientationGizmo()
	{
#include "panels/UI_SceneOrientationGizmo.inl"
	}

	bool EditorLayer::IsSceneOrientationGizmoPointerInside() const
	{
		if (m_Is2DMode)
			return false;
		if (m_SceneOrientationGizmoBounds[1].x
				<= m_SceneOrientationGizmoBounds[0].x
			|| m_SceneOrientationGizmoBounds[1].y
				<= m_SceneOrientationGizmoBounds[0].y)
			return false;
		const glm::vec2 mouse{ Input::GetMouseX(), Input::GetMouseY() };
		return mouse.x >= m_SceneOrientationGizmoBounds[0].x
			&& mouse.y >= m_SceneOrientationGizmoBounds[0].y
			&& mouse.x <= m_SceneOrientationGizmoBounds[1].x
			&& mouse.y <= m_SceneOrientationGizmoBounds[1].y;
	}
	void EditorLayer::UI_Toolbar()
	{
		DrawEditorPlayToolbar(m_EditorIcons, m_EditorScene != nullptr, IsSceneRunning(),
			m_SceneState == SceneState::Pause, [this] { OnScenePlay(); },
			[this] { OnSceneStop(); }, [this] { OnScenePause(); }, [this] { OnSceneStep(); });
	}

	bool EditorLayer::IsSceneRunning() const
	{
		return m_SceneState != SceneState::Edit;
	}

	bool EditorLayer::PrepareManagedRuntime()
	{
		if (IsSceneRunning())
			return false;
		m_ScriptMetadata.Clear();
		Scripting::ScriptEngine::Get().SetRuntime({});
		auto fail = [this](std::string message)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error, std::move(message),
				"Script Runtime");
			return false;
		};

		const std::filesystem::path managedDirectory =
			m_ScriptCompiler.GetManagedRuntimeDirectory();
		if (managedDirectory.empty())
			return fail("TomCat.ScriptHost outputs could not be located.");
		const std::filesystem::path assembly =
			m_ScriptCompiler.GetLastGoodAssemblyPath();
		std::filesystem::path pdb = assembly;
		pdb.replace_extension(".pdb");
		std::error_code pdbError;
		if (!std::filesystem::is_regular_file(pdb, pdbError) || pdbError)
			pdb.clear();

		std::string runtimeError;
		auto runtime = Scripting::CreateManagedScriptRuntime(managedDirectory,
			assembly, pdb, {}, &runtimeError);
		if (!runtime)
		{
			if (runtimeError.empty())
				runtimeError = "managed host initialization failed";
			return fail("The managed runtime could not be initialized: " + runtimeError);
		}

		std::string manifestJson;
		if (!runtime->ReadProjectMetadata(manifestJson))
			return fail("The generated C# script manifest could not be read.");
		std::string metadataError;
		if (!m_ScriptMetadata.ParseAndReplace(manifestJson, metadataError))
			return fail("The generated C# script manifest is invalid: " + metadataError);
		if (ReconcileManagedScriptFields(m_EditorScene))
			CommitImmediateSceneTransaction("Reconcile C# Fields");

		Scripting::ScriptEngine::Get().SetRuntime(std::move(runtime));
		return true;
	}

	bool EditorLayer::ReconcileManagedScriptFields(const Ref<Scene>& scene)
	{
		if (!scene)
			return false;
		bool changed = false;
		for (UUID entityID : scene->m_EntityOrder)
		{
			Entity entity = scene->FindEntityByUUID(entityID);
			if (!entity || !entity.HasComponent<CSharpScripts>())
				continue;
			for (CSharpScriptEntry& entry :
				entity.GetComponent<CSharpScripts>().Scripts)
			{
				const std::optional<EditorScriptMetadata> metadata =
					m_ScriptMetadata.Find(entry.ScriptAsset);
				if (!metadata)
					continue;
				if (!metadata->TypeName.empty()
					&& entry.LastKnownClassName != metadata->TypeName)
				{
					entry.LastKnownClassName = metadata->TypeName;
					changed = true;
				}
				changed = ReconcileScriptEntryFields(entry, *metadata) || changed;
			}
		}
		return changed;
	}

	void EditorLayer::OnScenePlay()
	{
		if (m_SceneState != SceneState::Edit || !m_EditorScene)
			return;
		m_ConsolePanel.OnPlayStarted();
        m_ProfilerPanel.OnPlayStarted();
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		auto blockPlay = [this](std::string message)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				std::move(message), "Scene Manager");
		};
		if (!m_CurrentProject)
		{
			blockPlay("Play requires an open project with BuildSettings.json.");
			return;
		}
		if (!AssetManager::Get().Refresh())
		{
			blockPlay("Play was blocked because the Asset Registry could not be refreshed.");
			return;
		}
		const AssetHandle currentSceneHandle = GetSavedEditorSceneHandle();
		if (static_cast<uint64_t>(currentSceneHandle) == 0)
		{
			blockPlay("Play requires the current Scene to be saved as a live Scene asset inside Assets.");
			return;
		}
		const BuildSettings& buildSettings = m_CurrentProject->GetBuildSettings();
		const bool currentSceneEnabled = std::any_of(buildSettings.Scenes.begin(),
			buildSettings.Scenes.end(), [currentSceneHandle](const BuildSceneSettings& scene)
			{
				return scene.Enabled && scene.Handle == currentSceneHandle;
			});
		if (!currentSceneEnabled)
		{
			blockPlay("Play was blocked because the current saved Scene is not enabled in Build Settings.");
			return;
		}
		const auto entry = std::find_if(buildSettings.Scenes.begin(),
			buildSettings.Scenes.end(), [&](const BuildSceneSettings& scene)
			{
				return scene.Enabled && scene.Handle == buildSettings.EntrySceneHandle;
			});
		const AssetMetadata* entryMetadata = entry != buildSettings.Scenes.end()
			? AssetManager::Get().GetRegistry().GetMetadata(buildSettings.EntrySceneHandle)
			: nullptr;
		if (!entryMetadata || entryMetadata->IsMissing
			|| entryMetadata->Type != AssetType::Scene)
		{
			blockPlay("Play requires an enabled, live Entry Scene in Build Settings.");
			return;
		}

		{
			ScriptBuildResult completed;
			(void)m_ScriptCompiler.PollCompile(completed);
			if (m_ScriptCompiler.IsCompileInProgress())
			{
				m_ShowConsolePanel = true;
				m_PendingPanelFocus = "Console";
				m_ConsolePanel.Push(ConsoleMessageSeverity::Warning,
					"Play is waiting for the background C# compilation to finish.",
					"Editor");
				return;
			}
			if (!m_ScriptCompiler.RefreshSourceState() ||
				!m_ScriptCompiler.IsCurrentSourceBuilt())
			{
				m_ShowConsolePanel = true;
				m_PendingPanelFocus = "Console";
				m_ScriptCompiler.StartCompile(true);
				m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
					"Play was blocked because the current C# sources have no validated "
					"build. Compilation is running in the background.",
					"Editor");
				return;
			}
		}
		if (!PrepareManagedRuntime())
			return;
		// Publish the project's per-game data directories so gameplay save
		// APIs resolve identically in Play mode and in the packaged Player.
		{
			PlayerSettings playerSettings = m_CurrentProject->GetPlayerSettings();
			std::string settingsError;
			if (!NormalizeAndValidatePlayerSettings(playerSettings, settingsError))
			{
				blockPlay("Play was blocked because PlayerSettings are invalid: "
					+ settingsError);
				return;
			}
			const auto gameDataPaths = ApplicationPaths::GetGameDataPaths(
				playerSettings.CompanyName, playerSettings.ProductName,
				playerSettings.SaveDirectory, playerSettings.LogDirectory,
				playerSettings.CrashDirectory);
			std::string dataError;
			if (!gameDataPaths)
				dataError = "LocalAppData or the game data paths are invalid";
			else
			{
				const std::array<std::filesystem::path, 4> directories = {
					gameDataPaths->Root, gameDataPaths->Saves,
					gameDataPaths->Logs, gameDataPaths->Crashes };
				for (const std::filesystem::path& directory : directories)
				{
					std::error_code directoryError;
					std::filesystem::create_directories(directory,
						directoryError);
					if (directoryError || !std::filesystem::is_directory(
						directory, directoryError) || directoryError)
					{
						dataError = "could not create game data directory: "
							+ directoryError.message();
						break;
					}
				}
			}
			if (!dataError.empty())
			{
				blockPlay("Play was blocked because game data initialization "
					"failed: " + dataError);
				return;
			}
			ApplicationPaths::SetRuntimeGameDataPaths(*gameDataPaths);
		}
		if (!m_RuntimeSceneManager.ConfigureBuildSettings(buildSettings))
		{
			blockPlay("Play could not configure Build Settings: "
				+ m_RuntimeSceneManager.GetLastError());
			return;
		}
		if (!m_RuntimeSceneManager.ActivateRuntime())
		{
			blockPlay("Play could not activate SceneManager: "
				+ m_RuntimeSceneManager.GetLastError());
			return;
		}
		Window& applicationWindow = Application::Get().GetWindow();
		const glm::vec2 screenToFramebufferScale{
			applicationWindow.GetScreenToFramebufferScaleX(),
			applicationWindow.GetScreenToFramebufferScaleY() };
		const FramebufferSpecification& gameFramebufferSpec =
			m_GameFramebuffer->GetSpecification();
		m_RuntimeSceneManager.SetViewportSize(gameFramebufferSpec.Width,
			gameFramebufferSpec.Height);
		m_RuntimeSceneManager.SetRuntimeUIViewportMetrics(
			m_ShowGamePanel ? m_GameViewportBounds[0] : glm::vec2(-1000000.0f),
			applicationWindow.GetDPIScale(), screenToFramebufferScale);
		Ref<Scene> preparedScene = Scene::Copy(m_EditorScene);
		if (!preparedScene || !m_RuntimeSceneManager.StartPreparedScene(
			preparedScene, currentSceneHandle))
		{
			const std::string detail = preparedScene
				? m_RuntimeSceneManager.GetLastError() : "the Editor Scene could not be copied";
			m_RuntimeSceneManager.Stop();
			blockPlay("Play could not start the current build Scene: " + detail);
			return;
		}
		m_ActiveScene = m_RuntimeSceneManager.GetActiveScene();
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		m_SceneState = SceneState::Play;
		m_PlayScriptDirtyNoticeShown = false;
		m_StepRequested = false;
		m_SceneHierarchyPanel.SetColliderEditingAllowed(false);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		ResetSceneInteractionState();

		// 切换到Game窗口焦点
		ImGui::SetWindowFocus("Game");

	}

	void EditorLayer::OnScenePause()
	{
		if (m_SceneState == SceneState::Play)
			m_SceneState = SceneState::Pause;
		else if (m_SceneState == SceneState::Pause)
			m_SceneState = SceneState::Play;
		m_StepRequested = false;
	}

	void EditorLayer::OnSceneStep()
	{
		if (m_SceneState == SceneState::Pause)
			m_StepRequested = true;
	}

	void EditorLayer::OnSceneStop()
	{
		if (!IsSceneRunning())
			return;

		m_RuntimeSceneManager.Stop();
		m_ActiveScene = m_EditorScene;
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		m_SceneState = SceneState::Edit;
		m_PlayScriptDirtyNoticeShown = false;
		m_ScriptSourcePollCountdown = 0.0f;
		m_ScriptCompileDebounceRemaining = 0.35f;
		m_StepRequested = false;
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
		m_SceneHierarchyPanel.SetColliderEditingAllowed(true);
		ResetSceneInteractionState();

		// 切换到Scene窗口焦点
		ImGui::SetWindowFocus("Scene");
	}

	void EditorLayer::OnBeforeInputCapture()
	{
		ImGuiWindow* game = ImGui::FindWindowByName("Game");
		ImGuiWindow* focused = GImGui->NavWindow;
		bool enabled = IsSceneRunning() && m_ShowGamePanel && game && game->WasActive
			&& focused && focused->RootWindow == game->RootWindow
			&& !ImGui::GetIO().WantTextInput && GImGui->OpenPopupStack.empty();
		// Native clicks arrive before ImGui changes NavWindow. Revoke gameplay
		// immediately for a click outside the Game image, including toolbar clicks.
		const auto& frame = Input::GetFrameSnapshot();
		if (std::any_of(frame.MouseButtonsPressed.begin(), frame.MouseButtonsPressed.end(),
			[](bool pressed) { return pressed; }))
		{
			const auto [x, y] = Input::GetMousePosition();
			enabled = enabled && x >= m_GameViewportBounds[0].x && y >= m_GameViewportBounds[0].y
				&& x < m_GameViewportBounds[1].x && y < m_GameViewportBounds[1].y;
		}
		Scripting::ScriptEngine::Get().SetInputEnabled(enabled);
	}

	void EditorLayer::OnEvent(Event& e)
	{
		// Scene wheel zoom is consumed once in OnImGuiRender, including input
		// from detached windows that never reaches the native editor event path.
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(TC_Bind_Event_Fn(EditorLayer::OnWindowClose));
		dispatcher.Dispatch<KeyPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnKeyPressed));
		dispatcher.Dispatch<MouseButtonPressedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonPressed));
		dispatcher.Dispatch<MouseButtonReleasedEvent>(TC_Bind_Event_Fn(EditorLayer::OnMouseButtonReleased));
		// ImGui lets keyboard events reach this layer so editor shortcuts can be
		// resolved first. Preserve its capture contract for any lower layer after
		// that routing decision, including key releases and typed characters.
		if (!e.m_Handled && e.IsInCategory(EventCategoryKeyboard)
			&& ImGui::GetIO().WantCaptureKeyboard)
			e.m_Handled = true;
	}

	bool EditorLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		const bool popupOpen = ImGui::IsPopupOpen(nullptr,
			ImGuiPopupFlags_AnyPopupId);
		if (e.GetKeyCode() == Key::Escape && e.GetRepeatCount() == 0
			&& !ImGui::GetIO().WantTextInput && !popupOpen
			&& !e.IsControlDown() && !e.IsShiftDown()
			&& !e.IsAltDown() && !e.IsSuperDown()
			&& m_SceneHierarchyPanel.IsEditingCollider())
		{
			m_SceneHierarchyPanel.ClearColliderEditMode();
			m_Viewport.ResetColliderEditState();
			return true;
		}

		const Entity selectedEntity =
			m_SceneHierarchyPanel.GetSelectedEntity();
		EditorShortcutContext context;
		context.KeyCode = e.GetKeyCode();
		context.RepeatCount = e.GetRepeatCount();
		context.Modifiers = e.GetModifiers();
		context.WantsTextInput = ImGui::GetIO().WantTextInput;
		context.PopupOpen = popupOpen;
		context.SceneFocused = m_ViewportFocused && m_ShowScenePanel
			&& ShouldRenderDockPanel("Scene###Scene");
		context.EntityContextFocused = m_ViewportFocused
			|| m_SceneHierarchyPanel.IsHierarchyFocused()
			|| m_SceneHierarchyPanel.IsInspectorFocused();
		context.HasSelection = static_cast<bool>(selectedEntity);
		context.EditingScene = m_SceneState == SceneState::Edit;
		context.TransformDragActive = m_GizmoDragActive
			|| m_ViewportState.UIRectDragActive
			|| m_ViewportState.ActiveColliderHandle != ColliderEditHandle::None;

		const EditorShortcutAction action = ResolveEditorShortcut(context);
		switch (action)
		{
			case EditorShortcutAction::ToggleScene2D:
				m_Is2DMode = !m_Is2DMode;
				m_EditorCamera.Set2DMode(m_Is2DMode);
				return true;
			case EditorShortcutAction::NewScene:
				NewScene();
				return true;
			case EditorShortcutAction::OpenScene:
				OpenScene();
				return true;
			case EditorShortcutAction::SaveScene:
				SaveScene();
				return true;
			case EditorShortcutAction::SaveSceneAs:
				SaveSceneAs();
				return true;
			case EditorShortcutAction::Undo:
				return UndoScene();
			case EditorShortcutAction::Redo:
				return RedoScene();
			case EditorShortcutAction::NextWindow:
				CycleEditorPanel(1);
				return true;
			case EditorShortcutAction::PreviousWindow:
				CycleEditorPanel(-1);
				return true;
			case EditorShortcutAction::CutSelection:
			case EditorShortcutAction::CopySelection:
			case EditorShortcutAction::PasteSelection:
			case EditorShortcutAction::DuplicateSelection:
			case EditorShortcutAction::RenameSelection:
			case EditorShortcutAction::DeleteSelection:
			{
				int keyCode = 0;
				bool control = false;
				switch (action)
				{
					case EditorShortcutAction::CutSelection:
						keyCode = Key::X; control = true; break;
					case EditorShortcutAction::CopySelection:
						keyCode = Key::C; control = true; break;
					case EditorShortcutAction::PasteSelection:
						keyCode = Key::V; control = true; break;
					case EditorShortcutAction::DuplicateSelection:
						keyCode = Key::D; control = true; break;
					case EditorShortcutAction::RenameSelection:
						keyCode = Key::F2; break;
					case EditorShortcutAction::DeleteSelection:
						keyCode = Key::Delete; break;
					default:
						break;
				}
				const bool handled =
					m_SceneHierarchyPanel.HandleShortcut(keyCode, control);
				if (handled && m_SceneHierarchyPanel.HasPendingRenameFocus())
					FocusEditorPanel("Hierarchy", m_ShowHierarchyPanel);
				return handled;
			}
			case EditorShortcutAction::ToolNone:
				m_GizmoType = -1;
				return true;
			case EditorShortcutAction::ToolTranslate:
				m_GizmoType = ImGuizmo::OPERATION::TRANSLATE;
				return true;
			case EditorShortcutAction::ToolRotate:
				m_GizmoType = ImGuizmo::OPERATION::ROTATE;
				return true;
			case EditorShortcutAction::ToolScale:
				m_GizmoType = ImGuizmo::OPERATION::SCALE;
				return true;
			case EditorShortcutAction::FrameSelection:
				m_Viewport.FrameSceneEntity(selectedEntity);
				return true;
			case EditorShortcutAction::None:
			default:
				return false;
		}
	}

	bool EditorLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		const int button = e.GetMouseButton();
		const bool altDown = e.IsAltDown();
		const bool cameraButton = button == Mouse::ButtonMiddle || button == Mouse::ButtonRight ||
			(button == Mouse::ButtonLeft && altDown);
		// Native mouse events arrive before this frame's ImGui pass. The cached
		// screen bounds stop all Scene navigation/selection from clicking through
		// the orientation control while its ImGui button handles the same input.
		if (IsSceneOrientationGizmoPointerInside())
			return true;
		// A camera drag starts from the Scene canvas itself.  Requiring the Scene
		// window to already own keyboard focus makes the first middle/right drag a
		// no-op after selecting an entity from Hierarchy or Inspector.
		if (cameraButton && m_ViewportCanvasHovered)
		{
			m_ViewportCameraDragOwned = true;
			return true;
		}

		if (e.GetMouseButton() == Mouse::ButtonLeft)
		{
			if (m_ViewportState.UIRectHandleHovered || m_ViewportState.UIRectDragActive)
				return true;
			if (m_ViewportState.ColliderHandleHovered || m_ViewportState.ActiveColliderHandle != ColliderEditHandle::None)
				return true;
			if (m_ViewportCanvasHovered && !m_GizmoHandleHovered
				&& !m_GizmoDragActive && !altDown)
			{
				m_SceneHierarchyPanel.SetSelectedEntity(m_HoveredEntity);
				return true;
			}
		}
		return false;
	}

	bool EditorLayer::OnMouseButtonReleased(MouseButtonReleasedEvent& e)
	{
		const int button = e.GetMouseButton();
		if (button == Mouse::ButtonLeft && m_ViewportState.ActiveColliderHandle != ColliderEditHandle::None)
		{
			if (m_ViewportState.ColliderTransactionActive)
			{
				m_ViewportState.ColliderTransactionActive = false;
				CommitSceneTransaction();
			}
			m_ViewportState.ActiveColliderHandle = ColliderEditHandle::None;
			return true;
		}
		if (m_ViewportCameraDragOwned &&
			(button == Mouse::ButtonLeft || button == Mouse::ButtonMiddle || button == Mouse::ButtonRight))
		{
			m_ViewportCameraDragOwned = false;
			return true;
		}
		return false;
	}

	bool EditorLayer::OnWindowClose(WindowCloseEvent&)
	{
		RequestExit();
		return true;
	}


	void EditorLayer::NewScene()
	{
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		if (IsSceneDirty() && !m_BypassUnsavedCheck)
		{
			RequestDestructiveAction([this]() {
				NewScene();
				return true;
			});
			return;
		}
		if (IsSceneRunning())
			OnSceneStop();
		m_EditorScene = CreateRef<Scene>();
		m_EditorScene->SetSceneName("Untitled");
		m_ActiveScene = m_EditorScene;
		AddDefaultMainCamera();
		ResizeSceneForGameView(m_ActiveScene);
		m_SceneHierarchyPanel.SetContext(m_ActiveScene);
		m_SceneHierarchyPanel.SetSelectedEntity({});

		m_EditorScenePath = std::filesystem::path();
		m_ContentBrowserPanel.SetActiveScenePath({});
		ResetSceneInteractionState();
		InitializeSceneHistory(false);
	}

	void EditorLayer::AddDefaultMainCamera()
	{
		if (!m_ActiveScene)
			return;

		Entity mainCamera = m_ActiveScene->CreateEntity("MainCamera");
		auto& camera = mainCamera.AddComponent<C_Camera>();
		if (m_CurrentProject && m_CurrentProject->GetConfig().Template == "2D")
			camera._Camera.SetOrthographic(10.0f, 0.0f, 1000.0f);
		else
			camera._Camera.SetPerspective(glm::radians(45.0f), 0.01f, 1000.0f);
	}

	bool EditorLayer::OpenProjectStartScene()
	{
		if (!m_CurrentProject)
			return false;
		const AssetHandle entrySceneHandle =
			m_CurrentProject->GetBuildSettings().EntrySceneHandle;
		if (static_cast<uint64_t>(entrySceneHandle) == 0)
		{
			TC_Core_Error("Project has no Entry Scene in BuildSettings.json");
			NewScene();
			return false;
		}

		std::filesystem::path entryScene;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(entrySceneHandle);
		if (metadata && metadata->Type == AssetType::Scene && !metadata->IsMissing)
			entryScene = AssetManager::Get().ResolvePath(entrySceneHandle);
		else
			TC_Core_Error("Project Entry Scene handle is missing or is not a Scene: {0}",
				static_cast<uint64_t>(entrySceneHandle));
		std::error_code error;
		if (!entryScene.empty() && std::filesystem::is_regular_file(entryScene, error))
		{
			if (OpenScene(entryScene))
				return true;
			TC_Core_Error("Failed to load project Entry Scene: {0}", PathToUTF8(entryScene));
		}
		else
		{
			TC_Warn("Project Entry Scene is missing: {0}", entryScene.empty()
				? std::to_string(static_cast<uint64_t>(entrySceneHandle))
				: PathToUTF8(entryScene));
		}

		// A project switch must never leave the previous project's scene or path
		// active when the new Entry Scene is missing or malformed.
		NewScene();
		return false;
	}

	bool EditorLayer::OpenScene()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		return !filepath.empty() && OpenScene(filepath);
	}

	bool EditorLayer::OpenScene(const std::filesystem::path& path)
	{
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		std::string extension = PathToUTF8(path.extension());
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension != ".tomcat")
		{
			TC_Warn("Could not load {0} - not a scene file", PathToUTF8(path.filename()));
			return false;
		}
		if (IsSceneDirty() && !m_BypassUnsavedCheck)
		{
			RequestDestructiveAction([this, path]() { return OpenScene(path); });
			return false;
		}
		if (m_CurrentProject)
		{
			const AssetHandle handle = AssetManager::Get().ImportAsset(path);
			const AssetMetadata* metadata = AssetManager::Get().GetRegistry().GetMetadata(handle);
			if (static_cast<uint64_t>(handle) == 0 || !metadata || metadata->IsMissing ||
				metadata->Type != AssetType::Scene)
			{
				TC_Warn("Project scenes must be registered .tomcat assets inside Assets: {0}",
					PathToUTF8(path));
				return false;
			}
		}

		Ref<Scene> newScene = CreateRef<Scene>();
		SceneSerializer serializer(newScene);
		if (!serializer.Deserialize(path))
			return false;

		if (IsSceneRunning())
			OnSceneStop();
		newScene->SetSceneName(PathToUTF8(path.stem()));
		m_EditorScene = newScene;
		ResizeSceneForGameView(m_EditorScene);
		m_SceneHierarchyPanel.SetContext(m_EditorScene);

		m_ActiveScene = m_EditorScene;
		m_EditorScenePath = AbsoluteLexicalPath(path);
		m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
		ResetSceneInteractionState();
		InitializeSceneHistory(true);
		RefreshLinkedPrefabs();
		return true;
	}

	void EditorLayer::SaveScene()
	{
		if (!m_EditorScene)
			return;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		if (!m_EditorScenePath.empty())
		{
			if (SerializeScene(m_EditorScene, m_EditorScenePath))
				MarkCurrentSceneSaved();
		}
		else
			SaveSceneAs();
	}	

	void EditorLayer::SaveSceneAs()
	{
		if (!m_EditorScene)
			return;
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		const std::filesystem::path previousRecoverySource =
			m_EditorScenePath;
		std::filesystem::path filepath = FileDialogs::SaveFile("TomCat Scene (*.tomcat)\0*.tomcat\0");
		if (!filepath.empty())
		{
			std::filesystem::path path = std::move(filepath);
			std::string extension = PathToUTF8(path.extension());
			std::transform(extension.begin(), extension.end(), extension.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (extension != ".tomcat")
				path.replace_extension(".tomcat");
			if (SerializeScene(m_EditorScene, path))
			{
				m_EditorScenePath = AbsoluteLexicalPath(path);
				m_ContentBrowserPanel.SetActiveScenePath(m_EditorScenePath);
				if (previousRecoverySource != m_EditorScenePath)
				{
					std::string recoveryError;
					if (!m_RecoveryService.RemoveRecovery(
						previousRecoverySource, recoveryError))
						TC_Core_Warn(
							"Could not clear previous Scene recovery: {0}",
							recoveryError);
				}
				MarkCurrentSceneSaved();
			}
		}
	}

	bool EditorLayer::SerializeScene(const Ref<Scene>& scene, const std::filesystem::path& path)
	{
		if (!scene || path.empty())
			return false;
		const std::string previousName = scene->GetSceneName();
		scene->SetSceneName(PathToUTF8(path.stem()));
		SceneSerializer serializer(scene);
		if (!serializer.Serialize(path))
		{
			scene->SetSceneName(previousName);
			return false;
		}
		return true;
	}

	AssetHandle EditorLayer::GetSavedEditorSceneHandle() const
	{
		if (!m_CurrentProject || m_EditorScenePath.empty())
			return AssetHandle(0);
		const AssetRegistry& registry = AssetManager::Get().GetRegistry();
		const AssetMetadata* metadata = registry.GetMetadata(m_EditorScenePath);
		if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Scene)
			return AssetHandle(0);
		const AssetMetadata* current = registry.GetMetadata(metadata->Handle);
		return current && !current->IsMissing && current->Type == AssetType::Scene
			&& current->FilePath == metadata->FilePath
			? metadata->Handle : AssetHandle(0);
	}

	void EditorLayer::RefreshLinkedPrefabs()
	{
		if (!m_EditorScene || m_SceneState != SceneState::Edit) return;
		if (m_SceneHistory.HasActiveTransaction()) CommitSceneTransaction();
		const Entity selection = m_SceneHierarchyPanel.GetSelectedEntity();
		const UUID selectedID = selection ? selection.GetUUID() : UUID(0);
		// Save the pre-refresh selection in the old snapshot, before a template
		// removal can delete it. Undo then restores both the entity and selection.
		BeginSceneTransaction("Update linked Prefabs");
		bool changed = false;
		std::string error;
		if (!PrefabLinkedInstance::RefreshAll(m_EditorScene, changed, error))
		{
			CancelSceneTransaction();
			ReportPrefabOperation(false, "Prefab refresh retained the original Scene: " + error);
			return;
		}
		if (!changed)
		{
			CancelSceneTransaction();
			return;
		}
		m_SceneHierarchyPanel.ResetForSceneReplacement(m_EditorScene, selectedID);
		UpdateSceneTransaction();
		ResetSceneInteractionState();
		CommitSceneTransaction();
	}

	void EditorLayer::ReportPrefabOperation(bool succeeded, std::string message)
	{
		if (!succeeded)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
		}
		m_ConsolePanel.Push(succeeded ? ConsoleMessageSeverity::Info
			: ConsoleMessageSeverity::Error, message, "Prefab");
		if (succeeded)
			TC_Core_Info("{0}", message);
		else
			TC_Core_Error("{0}", message);
	}

	bool EditorLayer::CreatePrefabFromEntity(Entity entity,
		const std::filesystem::path& destinationDirectory)
	{
		if (m_SceneState != SceneState::Edit)
		{
			ReportPrefabOperation(false,
				"Creating a Prefab asset is only available in Edit mode.");
			return false;
		}
		if (!m_CurrentProject || !m_EditorScene || !entity)
		{
			ReportPrefabOperation(false,
				"Create Prefab requires a selected Entity in an open project.");
			return false;
		}

		Entity source = m_EditorScene->FindEntityByUUID(entity.GetUUID());
		AssetRegistry& registry = AssetManager::Get().GetRegistry();
		std::error_code directoryError;
		const std::filesystem::path directory =
			AbsoluteLexicalPath(destinationDirectory);
		if (!source || directory.empty()
			|| !std::filesystem::is_directory(directory, directoryError)
			|| directoryError)
		{
			ReportPrefabOperation(false,
				"Browse a writable folder inside Assets before creating a Prefab.");
			return false;
		}

		const std::filesystem::path prefabPath =
			MakeUniquePrefabPath(directory, source.GetName());
		if (prefabPath.empty() || !registry.IsManagedPath(prefabPath, true))
		{
			ReportPrefabOperation(false,
				"Could not reserve a unique .tcprefab name in the selected Assets folder.");
			return false;
		}

		AssetHandle savedHandle(0);
		if (!PrefabArchiveCodec::SaveSubtree(m_EditorScene, source,
			prefabPath, &savedHandle)
			|| static_cast<uint64_t>(savedHandle) == 0)
		{
			ReportPrefabOperation(false,
				"Could not create Prefab '" + PathToUTF8(prefabPath.filename())
				+ "'. See the preceding validation diagnostic.");
			return false;
		}

		m_ContentBrowserPanel.RevealAsset(prefabPath);
		ReportPrefabOperation(true, "Created Prefab '"
			+ PathToUTF8(prefabPath.lexically_relative(
				m_CurrentProject->GetAssetPath())) + "' from Entity '"
			+ source.GetName() + "'.");
		return true;
	}

	Entity EditorLayer::InstantiatePrefab(AssetHandle handle,
		std::optional<UUID> parent, std::optional<glm::vec3> rootWorldPosition)
	{
		if (!m_ActiveScene)
		{
			ReportPrefabOperation(false,
				"Cannot instantiate a Prefab without an active Scene.");
			return {};
		}
		const AssetMetadata* metadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!metadata || metadata->IsMissing || metadata->Type != AssetType::Prefab)
		{
			ReportPrefabOperation(false,
				"The dropped asset is missing or is not a Prefab.");
			return {};
		}
		if (IsSceneRunning() && !m_ActiveScene->IsRuntimeRunning())
		{
			ReportPrefabOperation(false,
				"Runtime Prefab creation requires a running Scene safe point.");
			return {};
		}

		PrefabArchive archive;
		std::string error;
		if (!PrefabArchiveCodec::Load(handle, archive, error))
		{
			ReportPrefabOperation(false, "Could not load Prefab '"
				+ PathToUTF8(metadata->FilePath) + "': " + error);
			return {};
		}

		PrefabInstantiateOptions options;
		options.Parent = parent;
		options.RootWorldPosition = rootWorldPosition;
		options.ResolveAssets = true;
		PrefabInstantiationResult result;
		if (!PrefabArchiveCodec::Instantiate(archive, *m_ActiveScene,
			options, result, error) || !result.Root)
		{
			ReportPrefabOperation(false, "Could not instantiate Prefab '"
				+ PathToUTF8(metadata->FilePath) + "': " + error);
			return {};
		}

		if (m_SceneState == SceneState::Edit
			&& !PrefabLinkedInstance::Attach(m_ActiveScene, handle, archive, result, error))
		{
			m_ActiveScene->DestroyEntity(result.Root);
			ReportPrefabOperation(false, "Could not link Prefab instance: " + error);
			return {};
		}
		m_SceneHierarchyPanel.SetSelectedEntity(result.Root);
		if (m_SceneState == SceneState::Edit)
			CommitImmediateSceneTransaction("Instantiate Prefab");
		ReportPrefabOperation(true, "Instantiated Prefab '"
			+ PathToUTF8(metadata->FilePath) + "' ("
			+ std::to_string(result.Entities.size())
			+ (result.Entities.size() == 1 ? " Entity" : " Entities") + ").");
		return result.Root;
	}

	void EditorLayer::ResizeSceneForGameView(const Ref<Scene>& scene)
	{
		Window& applicationWindow = Application::Get().GetWindow();
		const GameViewResolutionPreset& resolution =
			kGameViewResolutions[static_cast<size_t>(std::clamp(
				m_GameViewResolutionIndex, 0,
				static_cast<int>(kGameViewResolutions.size()) - 1))];
		const uint32_t width = resolution.Width > 0
			? resolution.Width
			: ToFramebufferExtent(m_GameViewportSize.x,
				applicationWindow.GetScreenToFramebufferScaleX());
		const uint32_t height = resolution.Height > 0
			? resolution.Height
			: ToFramebufferExtent(m_GameViewportSize.y,
				applicationWindow.GetScreenToFramebufferScaleY());
		if (scene && width > 0 && height > 0)
			scene->OnViewportResize(width, height);
	}

	void EditorLayer::CommitRuntimeSceneTransition()
	{
		if (!IsSceneRunning())
			return;
		const AssetHandle previousSceneHandle = m_RuntimeSceneManager.GetActiveSceneHandle();
		const size_t previousSceneCount = m_RuntimeSceneManager.GetLoadedSceneHandles().size();
		const bool hadPendingOperation = m_RuntimeSceneManager.HasPendingTransition();
		const bool committed = m_RuntimeSceneManager.CommitPendingTransition();
		Ref<Scene> runtimeScene = m_RuntimeSceneManager.GetActiveScene();
		if (!runtimeScene)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				"Runtime scene transition failed: " + m_RuntimeSceneManager.GetLastError(),
				"Scene Manager");
			OnSceneStop();
			return;
		}
		if (runtimeScene != m_ActiveScene
			|| previousSceneHandle != m_RuntimeSceneManager.GetActiveSceneHandle()
			|| previousSceneCount != m_RuntimeSceneManager.GetLoadedSceneHandles().size()
			|| (committed && hadPendingOperation && !m_RuntimeSceneManager.HasPendingTransition()))
		{
			m_ActiveScene = std::move(runtimeScene);
			ResizeSceneForGameView(m_ActiveScene);
			m_SceneHierarchyPanel.SetContext(m_ActiveScene, false, true);
			const std::filesystem::path activeScenePath =
				AssetManager::Get().ResolvePath(
					m_RuntimeSceneManager.GetActiveSceneHandle());
			m_ContentBrowserPanel.SetActiveScenePath(activeScenePath);
			ResetSceneInteractionState();
		}
		if (!committed)
		{
			m_ShowConsolePanel = true;
			m_PendingPanelFocus = "Console";
			m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
				"Runtime scene transition failed: " + m_RuntimeSceneManager.GetLastError(),
				"Scene Manager");
		}
	}

	void EditorLayer::ResetSceneInteractionState()
	{
		if (m_GizmoTransactionActive)
		{
			m_GizmoTransactionActive = false;
			CommitSceneTransaction();
		}
		m_HoveredEntity = {};
		m_ViewportCameraDragOwned = false;
		m_GizmoDragActive = false;
		m_GizmoHandleHovered = false;
		m_Viewport.ResetRectTransformEditState();
		m_Viewport.ResetColliderEditState();
	}

	void EditorLayer::RequestDestructiveAction(std::function<bool()> action)
	{
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		if (!IsSceneDirty() || m_BypassUnsavedCheck)
		{
			action();
			return;
		}
		m_PendingUnsavedAction = std::move(action);
		m_OpenUnsavedChangesModal = true;
	}

	void EditorLayer::UI_UnsavedChangesModal()
	{
		if (m_OpenUnsavedChangesModal)
		{
			ImGui::OpenPopup("Unsaved Scene Changes");
			m_OpenUnsavedChangesModal = false;
		}

		std::function<bool()> actionToRun;
		PrepareEditorPopup("Unsaved Scene Changes",560,true);
        if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("The current scene has unsaved changes.");
			ImGui::TextUnformatted("Save before continuing?");
			ImGui::Separator();
			if (ImGui::Button("Save"))
			{
				SaveScene();
				if (!IsSceneDirty())
				{
					actionToRun = std::move(m_PendingUnsavedAction);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
			{
				actionToRun = std::move(m_PendingUnsavedAction);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_PendingUnsavedAction = {};
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		if (actionToRun)
		{
			m_BypassUnsavedCheck = true;
			actionToRun();
			m_BypassUnsavedCheck = false;
		}
	}

	void EditorLayer::UI_RecoveryModal()
	{
		if (m_OpenRecoveryModal && m_PendingRecovery)
		{
			ImGui::OpenPopup("Recover Scene");
			m_OpenRecoveryModal = false;
		}
		PrepareEditorPopup("Recover Scene",640,true);
        if (!ImGui::BeginPopupModal("Recover Scene", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::TextUnformatted(
			"A newer autosave differs from the scene on disk.");
		ImGui::TextUnformatted(
			"Restore loads it into memory as unsaved changes.");
		ImGui::TextUnformatted("The original scene file will not be overwritten.");
		ImGui::Separator();
		if (ImGui::Button("Restore"))
		{
			if (RestorePendingRecovery())
			{
				m_PendingRecovery.reset();
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard Recovery"))
		{
			std::string error;
			if (m_PendingRecovery
				&& !m_RecoveryService.RemoveRecovery(
					m_PendingRecovery->SourceScenePath, error))
				TC_Core_Warn("Could not discard Scene recovery: {0}", error);
			m_PendingRecovery.reset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Not Now"))
		{
			m_PendingRecovery.reset();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void EditorLayer::RequestExit()
	{
		RequestDestructiveAction([]() {
			Application::Get().Close();
			return true;
		});
	}

	bool EditorLayer::OpenProject()
	{
		const std::filesystem::path filepath = FileDialogs::OpenFile("TomCat Project (*.tcproj)\0*.tcproj\0");
		if (filepath.empty())
			return false;

		const std::filesystem::path projectPath = filepath;
		if (IsSceneDirty() && !m_BypassUnsavedCheck)
		{
			RequestDestructiveAction([this, projectPath]() { return OpenProject(projectPath); });
			return false;
		}
		return OpenProject(projectPath);
	}

	bool EditorLayer::OpenProject(const std::filesystem::path& path)
	{
		if (m_SceneHistory.HasActiveTransaction())
			CommitSceneTransaction();
		if (IsSceneDirty() && !m_BypassUnsavedCheck)
		{
			RequestDestructiveAction([this, path]() { return OpenProject(path); });
			return false;
		}

		const std::filesystem::path normalizedPath = AbsoluteLexicalPath(path);
		const bool alreadyOwns = m_ProjectLock.OwnsProject(normalizedPath);
		EditorProjectLock candidateLock;
		if (!alreadyOwns)
		{
			{
				std::string lockError;
				const ProjectLockAcquireResult lockResult =
					candidateLock.Acquire(normalizedPath, lockError);
				if (lockResult != ProjectLockAcquireResult::Acquired)
				{
					const std::string message =
						lockResult == ProjectLockAcquireResult::LiveOwner
						? "Project open rejected: another Editor instance owns "
							"the write lock. " + lockError
						: "Project open rejected: a safe write lock could not be "
							"created. " + lockError;
					m_ShowConsolePanel = true;
					m_PendingPanelFocus = "Console";
					m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
						message, "Project Lock");
					TC_Core_Error("{0}", message);
					return false;
				}
			}
		}

		// Persist the current layout before ProjectManager changes the active
		// project. No-project mode writes to the global LocalAppData layout.
		RestorePanelLayoutBeforePersistence();
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		SaveEditorPanelLayout();
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		auto project = ProjectManager::Get().LoadProject(normalizedPath);
		if (!project)
			return false;
		if (!alreadyOwns)
			m_ProjectLock = std::move(candidateLock);

		if (IsSceneRunning())
			OnSceneStop();
		m_CurrentProject = project;
		{
			std::string moduleError;
			if (!ModuleSystem::Get().LoadProjectModules(
				project->GetProjectDirectory(), moduleError))
			{
				m_ConsolePanel.Push(ConsoleMessageSeverity::Error,
					"Module discovery failed: " + moduleError, "Modules");
				m_ShowConsolePanel = true;
			}
		}
		m_BuildState.BuildSettingsStatus.clear();
		m_BuildState.BuildSettingsSucceeded = false;
		m_BuildState.PlayerBuildStatus.clear();
		m_BuildState.PlayerBuildSucceeded = false;
		m_SceneHierarchyPanel.SetProject(m_CurrentProject);
		if (m_ShowProjectSettingsPanel)
			m_ProjectSettings.LoadProjectSettingsDraft();
		m_Is2DMode = project->GetConfig().Template == "2D";
		m_EditorCamera.Set2DMode(m_Is2DMode);

		// Reapply the packaged baseline before the new project's override so UI
		// state never carries over from the project that was just closed.
		LoadImGuiSettings(GetDefaultEditorLayoutPath());
        if(!LoadImGuiSettings(GetEditorLayoutPath(m_CurrentProject))) m_LayoutRequest=1;
		LoadSceneToolbarLayout();
		LoadEditorPanelLayout();

		m_ContentBrowserPanel.SetProject(m_CurrentProject);
		std::string recoveryError;
		if (!m_RecoveryService.Configure(
			m_CurrentProject->GetProjectPath(), recoveryError))
			TC_Core_Warn("Editor recovery is unavailable: {0}",
				recoveryError);
		m_ScriptMetadata.Clear();
		Scripting::ScriptEngine::Get().SetRuntime({});
		if (m_ScriptCompiler.Configure(m_CurrentProject))
		{
			ResetScriptCompileTracking();
			if (m_ScriptCompiler.IsCurrentSourceBuilt())
				PrepareManagedRuntime();
		}
		OpenProjectStartScene();
		return true;
	}

	void EditorLayer::SaveProject()
	{
		RestorePanelLayoutBeforePersistence();
		if (m_CurrentProject)
			m_ContentBrowserPanel.Serialize();
		SaveImGuiSettingsPreservingCustomSections(GetEditorLayoutPath(m_CurrentProject));
		SaveEditorPanelLayout();
		m_ContentBrowserPanel.SaveLayoutSetting();
		SaveSceneToolbarLayout();
	}


}
