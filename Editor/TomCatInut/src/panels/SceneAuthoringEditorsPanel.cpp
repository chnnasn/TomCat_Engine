#include "SceneAuthoringEditorsPanel.h"
#include "SceneHierarchyDetail.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <vector>

#include "TomCat/Scene/Components.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "../EditorDragDrop.h"
#include "../EditorVisuals.h"

namespace TomCat {
	using namespace HierarchyDetail;

	bool SceneAuthoringEditorsPanel::OpenAuthoringAsset(AssetHandle handle, AssetType type)
	{
		if (static_cast<uint64_t>(handle) == 0)
			return false;
		const std::filesystem::path path = AssetManager::Get().ResolvePath(handle);
		if (path.empty())
			return false;

		auto rejectDirtyReplacement = [](AssetHandle current, bool loaded,
			bool dirty, std::string& message)
		{
			(void)current;
			if (loaded && dirty)
			{
				message = "Save or revert the current asset before opening another one.";
				return true;
			}
			return false;
		};
		std::string document;
		std::string error;
		if (type == AssetType::AnimationClip)
		{
			auto& editor = m_AnimationClipAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			AnimationClipAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& AnimationClipAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftSprite = AssetHandle(0);
			}
			else editor.Message = error;
			m_AnimationOpenRequested = true;
			return true;
		}
		if (type == AssetType::AnimatorController)
		{
			auto& editor = m_AnimatorControllerAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			AnimatorControllerAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& AnimatorControllerAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftClip = AssetHandle(0);
			}
			else editor.Message = error;
			m_AnimatorGraphOpenRequested = true;
			return true;
		}
		if (type == AssetType::TilePalette)
		{
			auto& editor = m_TilePaletteAssetEditor;
			if (rejectDirtyReplacement(editor.Handle, editor.Loaded, editor.Dirty,
				editor.Message)) return false;
			editor.Handle = handle;
			editor.Path = path;
			editor.Message.clear();
			TilePaletteAsset decoded;
			editor.Loaded = ReadAuthoringAssetDocument(path, document, error)
				&& TilePaletteAssetCodec::Decode(document, decoded, error);
			if (editor.Loaded)
			{
				editor.Asset = std::move(decoded);
				editor.Dirty = false;
				editor.DraftSprite = AssetHandle(0);
			}
			else editor.Message = error;
			m_TilePaletteOpenRequested = true;
			return true;
		}
		return false;
	}

	bool SceneAuthoringEditorsPanel::SaveAnimationClipAsset()
	{
		auto& editor = m_AnimationClipAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::AnimationClip || currentPath.empty())
		{
			editor.Message = "The Animation Clip no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !AnimationClipAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Animation Clip is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	bool SceneAuthoringEditorsPanel::SaveAnimatorControllerAsset()
	{
		auto& editor = m_AnimatorControllerAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::AnimatorController || currentPath.empty())
		{
			editor.Message = "The Animator Controller no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !AnimatorControllerAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Animator Controller is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	bool SceneAuthoringEditorsPanel::SaveTilePaletteAsset()
	{
		auto& editor = m_TilePaletteAssetEditor;
		std::string document;
		std::string error;
		const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
			.GetMetadata(editor.Handle);
		const std::filesystem::path currentPath =
			AssetManager::Get().ResolvePath(editor.Handle);
		if (!metadata || metadata->IsMissing
			|| metadata->Type != AssetType::TilePalette || currentPath.empty())
		{
			editor.Message = "The Tile Palette no longer resolves to its source asset.";
			return false;
		}
		editor.Path = currentPath;
		if (!editor.Loaded || !TilePaletteAssetCodec::Encode(editor.Asset,
			document, error) || !FileSystem::WriteFileAtomically(editor.Path,
			document, error))
		{
			editor.Message = error.empty() ? "Tile Palette is not loaded." : error;
			return false;
		}
		const AssetHandle imported = AssetManager::Get().ImportAsset(editor.Path);
		if (static_cast<uint64_t>(imported) == 0)
		{
			editor.Message = "The file was saved but could not be imported.";
			return false;
		}
		editor.Handle = imported;
		editor.Dirty = false;
		if (m_ActiveTilePaletteHandle == imported)
			m_ActiveTilePalette = editor.Asset;
		editor.Message = AssetManager::Get().Refresh()
			? "Saved and refreshed." : "Saved; the registry refresh reported errors.";
		return true;
	}

	void SceneAuthoringEditorsPanel::DrawAnimationClipAssetEditor()
	{
		auto& editor = m_AnimationClipAssetEditor;
		ImGui::Text("Animation Clip: %s%s", PathToUTF8(editor.Path.filename()).c_str(),
			editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveAnimationClipAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::AnimationClip);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		ImGui::Separator();
		char name[128]{};
		std::snprintf(name, sizeof(name), "%s", editor.Asset.Clip.Name.c_str());
		if (ImGui::InputText("Name", name, sizeof(name)))
		{
			editor.Asset.Clip.Name = name;
			editor.Dirty = true;
		}
		if (ImGui::Checkbox("Loop", &editor.Asset.Clip.Loop)) editor.Dirty = true;
		if (ImGui::DragFloat("Sample Rate", &editor.Asset.SampleRate, 0.25f,
			0.01f, 10000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
        if (!editor.Asset.Clip.Frames.empty())
        {
            editor.SelectedFrame = std::min(editor.SelectedFrame, editor.Asset.Clip.Frames.size()-1);
            if (ImGui::Button(editor.PreviewPlaying ? "Pause preview" : "Play preview")) editor.PreviewPlaying = !editor.PreviewPlaying;
            ImGui::SameLine();
            int selected = static_cast<int>(editor.SelectedFrame);
            ImGui::SetNextItemWidth(220);
            if (ImGui::SliderInt("Frame", &selected, 0, static_cast<int>(editor.Asset.Clip.Frames.size())-1))
            { editor.SelectedFrame = selected; editor.PreviewPlaying = false; editor.PreviewTime = 0; }
            if (editor.PreviewPlaying)
            {
                editor.PreviewTime += ImGui::GetIO().DeltaTime;
                size_t advances = 0;
                while (editor.PreviewTime >= editor.Asset.Clip.Frames[editor.SelectedFrame].DurationSeconds && advances++ < editor.Asset.Clip.Frames.size())
                {
                    editor.PreviewTime -= std::max(0.001f, editor.Asset.Clip.Frames[editor.SelectedFrame].DurationSeconds);
                    if (editor.SelectedFrame+1 < editor.Asset.Clip.Frames.size()) ++editor.SelectedFrame;
                    else if(editor.Asset.Clip.Loop) editor.SelectedFrame=0;
                    else { editor.PreviewPlaying=false; editor.PreviewTime=0; break; }
                }
            }
            DrawSpritePaletteTile(editor.Asset.Clip.Frames[editor.SelectedFrame].SpriteHandle, "Preview", false, 120);
            ImGui::BeginChild("ClipFrameStrip", ImVec2(0, 105), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (size_t index=0; index<editor.Asset.Clip.Frames.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                if(index) ImGui::SameLine();
                if (DrawSpritePaletteTile(editor.Asset.Clip.Frames[index].SpriteHandle, std::to_string(index).c_str(), index==editor.SelectedFrame, 64))
                { editor.SelectedFrame=index; editor.PreviewPlaying=false; }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        DrawAnimatorSectionLabel("Selected frame");
		std::optional<size_t> remove;
		for (size_t index = 0; index < editor.Asset.Clip.Frames.size(); ++index)
		{
            if (index != editor.SelectedFrame) continue;
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("Frame %zu", index);
			AssetHandle sprite = editor.Asset.Clip.Frames[index].SpriteHandle;
			if (DrawAnimatorSpriteField("ExternalClipFrame", sprite))
			{
				editor.Asset.Clip.Frames[index].SpriteHandle = sprite;
				editor.Dirty = true;
			}
			if (ImGui::DragFloat("Duration", &editor.Asset.Clip.Frames[index].DurationSeconds,
				0.005f, 0.001f, 3600.0f, "%.3f s", ImGuiSliderFlags_AlwaysClamp))
				editor.Dirty = true;
			if (ImGui::SmallButton("Remove")) remove = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (remove)
		{
			editor.Asset.Clip.Frames.erase(editor.Asset.Clip.Frames.begin()
				+ static_cast<std::ptrdiff_t>(*remove));
			editor.Dirty = true;
		}
		ImGui::TextUnformatted("New Frame Sprite");
		DrawAnimatorSpriteField("ExternalClipNewFrame", editor.DraftSprite);
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftSprite) == 0);
		if (ImGui::Button("Add Frame"))
		{
			editor.Asset.Clip.Frames.push_back({ editor.DraftSprite,
				1.0f / std::max(editor.Asset.SampleRate, 0.01f) });
			editor.DraftSprite = AssetHandle(0);
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
	}

	void SceneAuthoringEditorsPanel::DrawAnimatorControllerAssetEditor()
	{
		auto& editor = m_AnimatorControllerAssetEditor;
		auto& asset = editor.Asset;
		ImGui::Text("Animator Controller: %s%s",
			PathToUTF8(editor.Path.filename()).c_str(), editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveAnimatorControllerAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::AnimatorController);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		DrawAnimatorSectionLabel("Parameters");
		std::optional<size_t> removeParameter;
		for (size_t index = 0; index < asset.Parameters.size(); ++index)
		{
			AnimatorParameter& parameter = asset.Parameters[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::TextUnformatted(parameter.Name.c_str());
			ImGui::SameLine();
			const char* types[] = { "Bool", "Int", "Float", "Trigger" };
			int type = static_cast<int>(parameter.Type);
			const bool used = std::any_of(asset.Transitions.begin(), asset.Transitions.end(),
				[&](const AnimatorTransition& transition)
				{
					return std::any_of(transition.Conditions.begin(),
						transition.Conditions.end(), [&](const AnimatorCondition& condition)
						{ return condition.Parameter == parameter.Name; });
				});
			ImGui::BeginDisabled(used);
			if (ImGui::Combo("Type", &type, types, 4))
			{
				parameter.Type = static_cast<AnimatorParameterType>(type);
				editor.Dirty = true;
			}
			ImGui::EndDisabled();
			switch (parameter.Type)
			{
				case AnimatorParameterType::Bool:
				case AnimatorParameterType::Trigger:
					if (ImGui::Checkbox("Default", &parameter.BoolValue)) editor.Dirty = true;
					break;
				case AnimatorParameterType::Int:
					if (ImGui::InputInt("Default", &parameter.IntValue)) editor.Dirty = true;
					break;
				case AnimatorParameterType::Float:
					if (ImGui::DragFloat("Default", &parameter.FloatValue, 0.05f)) editor.Dirty = true;
					break;
			}
			if (ImGui::SmallButton("Remove Parameter")) removeParameter = index;
			ImGui::PopID();
		}
		if (removeParameter)
		{
			const std::string removed = asset.Parameters[*removeParameter].Name;
			asset.Parameters.erase(asset.Parameters.begin()
				+ static_cast<std::ptrdiff_t>(*removeParameter));
			for (AnimatorTransition& transition : asset.Transitions)
				std::erase_if(transition.Conditions,
					[&](const AnimatorCondition& condition)
					{ return condition.Parameter == removed; });
			editor.Dirty = true;
		}
		if (ImGui::Button("Add Parameter"))
		{
			AnimatorParameter parameter;
			parameter.Name = SpriteAnimatorAuthoring::MakeUniqueName(asset.Parameters,
				std::string("Parameter"), [](const AnimatorParameter& value)
				{ return value.Name; });
			asset.Parameters.push_back(std::move(parameter));
			editor.Dirty = true;
		}

		DrawAnimatorSectionLabel("States");
		if (!asset.States.empty())
		{
			const char* initial = asset.InitialState.c_str();
			if (ImGui::BeginCombo("Initial State", initial))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == asset.InitialState))
					{
						asset.InitialState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
		}
		std::optional<size_t> removeState;
		for (size_t index = 0; index < asset.States.size(); ++index)
		{
			auto& state = asset.States[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::Text("%s", state.Name.c_str());
			if (DrawTypedAuthoringAssetField("StateClip", AssetType::AnimationClip,
				state.ClipHandle)) editor.Dirty = true;
			if (ImGui::DragFloat("Speed", &state.Speed, 0.05f, 0.001f, 100.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
			if (ImGui::SmallButton("Open Clip"))
				OpenAuthoringAsset(state.ClipHandle, AssetType::AnimationClip);
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove State")) removeState = index;
			ImGui::PopID();
		}
		if (removeState)
		{
			const std::string removed = asset.States[*removeState].Name;
			asset.States.erase(asset.States.begin()
				+ static_cast<std::ptrdiff_t>(*removeState));
			std::erase_if(asset.Transitions, [&](const AnimatorTransition& transition)
			{
				return transition.ToState == removed
					|| (!transition.AnyState && transition.FromState == removed);
			});
			asset.InitialState = asset.States.empty() ? std::string{}
				: asset.States.front().Name;
			editor.Dirty = true;
		}
		ImGui::TextUnformatted("New State Animation Clip");
		DrawTypedAuthoringAssetField("NewStateClip", AssetType::AnimationClip,
			editor.DraftClip);
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftClip) == 0);
		if (ImGui::Button("Add State"))
		{
			AnimatorControllerAsset::State state;
			state.Name = SpriteAnimatorAuthoring::MakeUniqueName(asset.States,
				std::string("State"), [](const AnimatorControllerAsset::State& value)
				{ return value.Name; });
			state.ClipHandle = editor.DraftClip;
			asset.States.push_back(std::move(state));
			if (asset.InitialState.empty()) asset.InitialState = asset.States.back().Name;
			editor.DraftClip = AssetHandle(0);
			editor.Dirty = true;
		}
		ImGui::EndDisabled();

		DrawAnimatorSectionLabel("Transitions");
		std::optional<size_t> removeTransition;
		for (size_t index = 0; index < asset.Transitions.size(); ++index)
		{
			AnimatorTransition& transition = asset.Transitions[index];
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::Checkbox("Any State", &transition.AnyState))
			{
				if (!transition.AnyState && transition.FromState.empty()
					&& !asset.States.empty()) transition.FromState = asset.States.front().Name;
				editor.Dirty = true;
			}
			if (!transition.AnyState && ImGui::BeginCombo("From", transition.FromState.c_str()))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == transition.FromState))
					{
						transition.FromState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("To", transition.ToState.c_str()))
			{
				for (const auto& state : asset.States)
					if (ImGui::Selectable(state.Name.c_str(), state.Name == transition.ToState))
					{
						transition.ToState = state.Name;
						editor.Dirty = true;
					}
				ImGui::EndCombo();
			}
			if (ImGui::DragFloat("Exit Time", &transition.ExitTime, 0.01f, -1.0f,
				1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
			ImGui::TextDisabled("Conditions: %zu", transition.Conditions.size());
			std::optional<size_t> removeCondition;
			for (size_t conditionIndex = 0;
				conditionIndex < transition.Conditions.size(); ++conditionIndex)
			{
				AnimatorCondition& condition = transition.Conditions[conditionIndex];
				ImGui::PushID(static_cast<int>(conditionIndex));
				if (ImGui::BeginCombo("Parameter", condition.Parameter.c_str()))
				{
					for (const AnimatorParameter& parameter : asset.Parameters)
					{
						if (ImGui::Selectable(parameter.Name.c_str(),
							condition.Parameter == parameter.Name))
						{
							condition.Parameter = parameter.Name;
							condition.Mode = (parameter.Type == AnimatorParameterType::Bool
								|| parameter.Type == AnimatorParameterType::Trigger)
								? AnimatorConditionMode::If
								: parameter.Type == AnimatorParameterType::Float
									? AnimatorConditionMode::Greater
									: AnimatorConditionMode::Equals;
							editor.Dirty = true;
						}
					}
					ImGui::EndCombo();
				}
				const auto parameter = std::find_if(asset.Parameters.begin(),
					asset.Parameters.end(), [&](const AnimatorParameter& value)
					{ return value.Name == condition.Parameter; });
				if (parameter != asset.Parameters.end())
				{
					struct ModeOption { AnimatorConditionMode Mode; const char* Label; };
					std::vector<ModeOption> modes;
					if (parameter->Type == AnimatorParameterType::Bool
						|| parameter->Type == AnimatorParameterType::Trigger)
						modes = { { AnimatorConditionMode::If, "If" },
							{ AnimatorConditionMode::IfNot, "If Not" } };
					else if (parameter->Type == AnimatorParameterType::Float)
						modes = { { AnimatorConditionMode::Greater, "Greater" },
							{ AnimatorConditionMode::Less, "Less" } };
					else
						modes = { { AnimatorConditionMode::Greater, "Greater" },
							{ AnimatorConditionMode::Less, "Less" },
							{ AnimatorConditionMode::Equals, "Equals" },
							{ AnimatorConditionMode::NotEqual, "Not Equal" } };
					const auto currentMode = std::find_if(modes.begin(), modes.end(),
						[&](const ModeOption& option)
						{ return option.Mode == condition.Mode; });
					const char* modeLabel = currentMode == modes.end()
						? "Choose" : currentMode->Label;
					if (ImGui::BeginCombo("Mode", modeLabel))
					{
						for (const ModeOption& option : modes)
							if (ImGui::Selectable(option.Label,
								condition.Mode == option.Mode))
							{
								condition.Mode = option.Mode;
								editor.Dirty = true;
							}
						ImGui::EndCombo();
					}
					if (parameter->Type == AnimatorParameterType::Float)
					{
						if (ImGui::DragFloat("Threshold", &condition.Threshold, 0.05f))
							editor.Dirty = true;
					}
					else if (parameter->Type == AnimatorParameterType::Int)
					{
						int threshold = static_cast<int>(condition.Threshold);
						if (ImGui::InputInt("Threshold", &threshold))
						{
							condition.Threshold = static_cast<float>(threshold);
							editor.Dirty = true;
						}
					}
				}
				if (ImGui::SmallButton("Delete Condition"))
					removeCondition = conditionIndex;
				ImGui::PopID();
			}
			if (removeCondition)
			{
				transition.Conditions.erase(transition.Conditions.begin()
					+ static_cast<std::ptrdiff_t>(*removeCondition));
				editor.Dirty = true;
			}
			ImGui::BeginDisabled(asset.Parameters.empty());
			if (ImGui::SmallButton("Add Condition"))
			{
				AnimatorCondition condition;
				condition.Parameter = asset.Parameters.front().Name;
				condition.Mode = (asset.Parameters.front().Type == AnimatorParameterType::Bool
					|| asset.Parameters.front().Type == AnimatorParameterType::Trigger)
					? AnimatorConditionMode::If
					: asset.Parameters.front().Type == AnimatorParameterType::Float
						? AnimatorConditionMode::Greater
						: AnimatorConditionMode::Equals;
				transition.Conditions.push_back(std::move(condition));
				editor.Dirty = true;
			}
			ImGui::EndDisabled();
			if (ImGui::SmallButton("Remove Transition")) removeTransition = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeTransition)
		{
			asset.Transitions.erase(asset.Transitions.begin()
				+ static_cast<std::ptrdiff_t>(*removeTransition));
			editor.Dirty = true;
		}
		ImGui::BeginDisabled(asset.States.empty());
		if (ImGui::Button("Add Transition"))
		{
			AnimatorTransition transition;
			transition.FromState = asset.States.front().Name;
			transition.ToState = asset.States.front().Name;
			transition.ExitTime = 1.0f;
			asset.Transitions.push_back(std::move(transition));
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
	}

	void SceneAuthoringEditorsPanel::DrawTilePaletteAssetEditor()
	{
		auto& editor = m_TilePaletteAssetEditor;
		auto& asset = editor.Asset;
		ImGui::Text("Tile Palette: %s%s", PathToUTF8(editor.Path.filename()).c_str(),
			editor.Dirty ? " *" : "");
		if (!editor.Loaded)
		{
			ImGui::TextWrapped("Could not load asset: %s", editor.Message.c_str());
			if (ImGui::Button("Close Asset")) editor = {};
			return;
		}
		ImGui::TextDisabled(editor.Dirty ? "Unsaved changes - Save to write this asset" : "All changes saved");
        if (ImGui::Button("Save")) SaveTilePaletteAsset();
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			editor.Dirty = false;
			OpenAuthoringAsset(editor.Handle, AssetType::TilePalette);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(editor.Dirty);
		const bool closeAsset = ImGui::Button("Close Asset") && !editor.Dirty;
		ImGui::EndDisabled();
		if (closeAsset)
		{
			editor = {};
			return;
		}
		ImGui::SameLine();
		if (ImGui::Button("Use for Painting"))
		{
			m_ActiveTilePaletteHandle = editor.Handle;
			m_ActiveTilePalette = editor.Asset;
			if (m_Shared.SelectionContext && m_Shared.SelectionContext.HasComponent<Tilemap2D>()
				&& !editor.Asset.Tiles.empty())
			{
				m_TilePaletteStates[static_cast<uint64_t>(m_Shared.SelectionContext.GetUUID())]
					.SpriteHandle = editor.Asset.Tiles.front().SpriteHandle;
			}
			editor.Message = "Active painting palette selected.";
		}
		if (!editor.Message.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", editor.Message.c_str());
		}
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(asset.CellSize), 0.05f,
			0.001f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(asset.CellGap), 0.05f,
			-1000000.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) editor.Dirty = true;
		DrawAnimatorSectionLabel("Palette Tiles");
		std::optional<size_t> remove;
		for (size_t index = 0; index < asset.Tiles.size(); ++index)
		{
			TilePaletteEntry& tile = asset.Tiles[index];
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::InputInt2("Coordinate", &tile.Coordinate.x)) editor.Dirty = true;
			if (DrawAnimatorSpriteField("PaletteEntrySprite", tile.SpriteHandle))
				editor.Dirty = true;
			if (ImGui::SmallButton("Remove Tile")) remove = index;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (remove)
		{
			asset.Tiles.erase(asset.Tiles.begin() + static_cast<std::ptrdiff_t>(*remove));
			editor.Dirty = true;
		}
		ImGui::InputInt2("New Coordinate", &editor.DraftCoordinate.x);
		ImGui::TextUnformatted("New Tile Sprite");
		DrawAnimatorSpriteField("PaletteNewSprite", editor.DraftSprite);
		const bool duplicate = std::any_of(asset.Tiles.begin(), asset.Tiles.end(),
			[&](const TilePaletteEntry& tile)
			{ return tile.Coordinate == editor.DraftCoordinate; });
		ImGui::BeginDisabled(static_cast<uint64_t>(editor.DraftSprite) == 0 || duplicate);
		if (ImGui::Button("Add Tile"))
		{
			asset.Tiles.push_back({ editor.DraftCoordinate, editor.DraftSprite });
			editor.DraftSprite = AssetHandle(0);
			editor.DraftCoordinate.x++;
			editor.Dirty = true;
		}
		ImGui::EndDisabled();
		if (duplicate) ImGui::TextDisabled("That palette coordinate is already occupied.");
	}

	void SceneAuthoringEditorsPanel::OnAnimatorGraphImGuiRender(bool* open)
	{
		m_AnimatorGraphFocused = false;
		if (open && !*open)
			return;

		PrepareEditorToolWindow(ImVec2(1040,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Animator", open);
		m_AnimatorGraphDocked = ImGui::IsWindowDocked();
		m_AnimatorGraphFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		bool drewAnimator = false;
		if (visible)
		{
			if (static_cast<uint64_t>(m_AnimatorControllerAssetEditor.Handle) != 0)
			{
				DrawAnimatorControllerAssetEditor();
				drewAnimator = true;
			}
			else if (!m_Shared.SelectionContext)
			{
				ImGui::TextDisabled("Select an entity with a Sprite Animator component.");
			}
			else if (!m_Shared.SelectionContext.HasComponent<SpriteAnimator>())
			{
				ImGui::Text("%s", m_Shared.SelectionContext.GetName().c_str());
				ImGui::Separator();
				ImGui::TextDisabled("The selected entity does not have a Sprite Animator component.");
			}
			else
			{
				Entity entity = m_Shared.SelectionContext;
				SpriteAnimator& animator = entity.GetComponent<SpriteAnimator>();
				if (static_cast<uint64_t>(animator.ControllerHandle) != 0)
				{
					ImGui::Text("Selected: %s", entity.GetName().c_str());
					ImGui::Separator();
					ImGui::TextWrapped("This component uses an external Animator Controller. "
						"Its embedded snapshot is ignored at Play start and is read-only here.");
					if (ImGui::Button("Open Controller"))
						OpenAuthoringAsset(animator.ControllerHandle,
							AssetType::AnimatorController);
					drewAnimator = true;
				}
				else
				{
				ImGui::Text("Selected: %s", entity.GetName().c_str());
				ImGui::Separator();
				std::function<void()> pendingMutation;
				const float sidebarWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.24f,
					190.0f, 300.0f);
				if (ImGui::BeginChild("AnimatorSidebar", ImVec2(sidebarWidth, 0.0f), true))
				{
					if (ImGui::BeginTabBar("AnimatorSidebarTabs"))
					{
						if (ImGui::BeginTabItem("Layers"))
						{
							ImGui::Selectable("Base Layer", true);
							ImGui::TextDisabled("States: %zu", animator.States.size());
							ImGui::EndTabItem();
						}
						if (ImGui::BeginTabItem("Parameters"))
						{
							if (ImGui::SmallButton("+##AnimatorParameter"))
							{
								AnimatorParameter parameter;
								parameter.Name = SpriteAnimatorAuthoring::MakeUniqueName(
									animator.Parameters, std::string("Parameter"),
									[](const AnimatorParameter& value) { return value.Name; });
								animator.Parameters.push_back(std::move(parameter));
								m_Shared.MarkModified(true);
							}
							ImGui::Separator();
							for (AnimatorParameter& parameter : animator.Parameters)
							{
								ImGui::PushID(&parameter);
								ImGui::TextUnformatted(parameter.Name.c_str());
								ImGui::SameLine(ImGui::GetContentRegionAvail().x - 36.0f);
								switch (parameter.Type)
								{
									case AnimatorParameterType::Bool:
									case AnimatorParameterType::Trigger:
										if (ImGui::Checkbox("##Value", &parameter.BoolValue)) m_Shared.MarkModified();
										break;
									case AnimatorParameterType::Int:
										if (ImGui::InputInt("##Value", &parameter.IntValue)) m_Shared.MarkModified();
										break;
									case AnimatorParameterType::Float:
										if (ImGui::DragFloat("##Value", &parameter.FloatValue, 0.05f)) m_Shared.MarkModified();
										break;
								}
								ImGui::PopID();
							}
							if (animator.Parameters.empty())
								ImGui::TextDisabled("List is Empty");
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
				ImGui::EndChild();
				ImGui::SameLine();
				if (ImGui::BeginChild("AnimatorStateMachine", ImVec2(0.0f, 0.0f), false))
					DrawSpriteAnimatorGraph(animator, entity, pendingMutation, true);
				ImGui::EndChild();
				if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
					&& m_AnimatorRenameEntity != entity.GetUUID())
					DismissAnimatorRenamePopup(true);
				else
					DrawAnimatorRenamePopup(animator, entity, true);
				ApplyAnimatorPendingMutation(entity, pendingMutation);
				drewAnimator = true;
				}
			}
		}
		if (!drewAnimator && m_AnimatorRenamePopupGraphOwner)
			DismissAnimatorRenamePopup(true);
		ImGui::End();
		m_Shared.FinishModificationGesture();
	}

	void SceneAuthoringEditorsPanel::OnAnimationImGuiRender(bool* open)
	{
		using namespace SpriteAnimatorAuthoring;
		m_AnimationFocused = false;
		if (open && !*open)
			return;

		auto restorePreview = [this](uint64_t entityID, AnimationTimelineState& state)
		{
			if (!m_Shared.Context)
				return;
			Entity entity = m_Shared.Context->FindEntityByUUID(UUID(entityID));
			if (entity && entity.HasComponent<SpriteRenderer>())
			{
				auto& renderer = entity.GetComponent<SpriteRenderer>();
				renderer.RuntimeSpriteOverrideActive = false;
				renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				renderer.Sprite = static_cast<uint64_t>(renderer.SpriteHandle) != 0
					? AssetManager::Get().LoadTexture(renderer.SpriteHandle) : Ref<Texture2D>{};
			}
			state.Playing = false;
		};

		PrepareEditorToolWindow(ImVec2(1040,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Animation", open);
		m_AnimationDocked = ImGui::IsWindowDocked();
		m_AnimationFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			if (open && !*open)
				for (auto& [entityID, state] : m_AnimationTimelineStates)
					restorePreview(entityID, state);
			return;
		}
		if (static_cast<uint64_t>(m_AnimationClipAssetEditor.Handle) != 0)
		{
			DrawAnimationClipAssetEditor();
			ImGui::End();
			return;
		}

		if (!m_Shared.SelectionContext || !m_Shared.SelectionContext.HasComponent<SpriteAnimator>())
		{
			for (auto& [entityID, state] : m_AnimationTimelineStates)
				restorePreview(entityID, state);
			ImGui::TextDisabled("Select an entity with a Sprite Animator component.");
			ImGui::End();
			return;
		}

		Entity entity = m_Shared.SelectionContext;
		SpriteAnimator& animator = entity.GetComponent<SpriteAnimator>();
		if (static_cast<uint64_t>(animator.ControllerHandle) != 0)
		{
			for (auto& [entityID, state] : m_AnimationTimelineStates)
				restorePreview(entityID, state);
			ImGui::TextWrapped("This component uses an external Animator Controller. "
				"Open an Animation Clip asset from the Project panel, or open the "
				"Controller and choose a state's clip.");
			if (ImGui::Button("Open Controller"))
				OpenAuthoringAsset(animator.ControllerHandle,
					AssetType::AnimatorController);
			ImGui::End();
			return;
		}
		const uint64_t entityID = static_cast<uint64_t>(entity.GetUUID());
		for (auto& [otherID, otherState] : m_AnimationTimelineStates)
			if (otherID != entityID)
				restorePreview(otherID, otherState);
		AnimationTimelineState& timeline = m_AnimationTimelineStates[entityID];

		ImGui::Text("%s", entity.GetName().c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(210.0f);
		const char* clipPreview = timeline.SelectedClip.empty()
			? "Select Clip" : timeline.SelectedClip.c_str();
		if (ImGui::BeginCombo("##AnimationClip", clipPreview))
		{
			for (const SpriteAnimationClip& clip : animator.Clips)
			{
				if (ImGui::Selectable(clip.Name.c_str(), timeline.SelectedClip == clip.Name))
				{
					timeline.SelectedClip = clip.Name;
					timeline.SelectedFrame = 0;
					timeline.Time = 0.0;
					timeline.Playing = false;
				}
			}
			ImGui::EndCombo();
		}
		if (animator.Clips.empty())
		{
			ImGui::SameLine();
			if (ImGui::Button("Create Clip"))
			{
				SpriteAnimationClip clip;
				clip.Name = "Default";
				clip.Frames.push_back({ entity.HasComponent<SpriteRenderer>()
					? entity.GetComponent<SpriteRenderer>().SpriteHandle
					: AssetHandle(0), 1.0f / 12.0f });
				animator.Clips.push_back(std::move(clip));
				timeline.SelectedClip = animator.Clips.front().Name;
				m_Shared.MarkModified(true);
			}
		}
		if (timeline.SelectedClip.empty() && !animator.Clips.empty())
			timeline.SelectedClip = animator.Clips.front().Name;
		const std::optional<size_t> clipIndex = FindClipIndex(animator,
			timeline.SelectedClip);

		ImGui::Separator();
		if (ImGui::Checkbox("Preview", &timeline.Preview))
		{
			if (!timeline.Preview)
				restorePreview(entityID, timeline);
		}
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 0.2f, 0.18f, 1.0f));
		timeline.Recording = false;
		ImGui::BeginDisabled();
		ImGui::Checkbox("Record", &timeline.Recording);
		ImGui::EndDisabled();
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Property recording support pending");
		ImGui::SameLine();
		ImGui::BeginDisabled(!clipIndex || animator.Clips[*clipIndex].Frames.empty());
		if (ImGui::Button("|<"))
		{
			timeline.SelectedFrame = 0;
			timeline.Time = 0.0;
		}
		ImGui::SameLine();
		if (ImGui::Button("<"))
		{
			timeline.SelectedFrame = timeline.SelectedFrame > 0
				? timeline.SelectedFrame - 1 : 0;
			timeline.Time = 0.0;
			for (size_t i = 0; clipIndex && i < timeline.SelectedFrame; ++i)
				timeline.Time += animator.Clips[*clipIndex].Frames[i].DurationSeconds;
		}
		ImGui::SameLine();
		if (ImGui::Button(timeline.Playing ? "||" : ">"))
		{
			timeline.Playing = !timeline.Playing;
			if (timeline.Playing)
				timeline.Preview = true;
		}
		ImGui::SameLine();
		if (ImGui::Button(">##NextFrame"))
		{
			const size_t count = animator.Clips[*clipIndex].Frames.size();
			timeline.SelectedFrame = std::min(timeline.SelectedFrame + 1, count - 1);
			timeline.Time = 0.0;
			for (size_t i = 0; i < timeline.SelectedFrame; ++i)
				timeline.Time += animator.Clips[*clipIndex].Frames[i].DurationSeconds;
		}
		ImGui::SameLine();
		if (ImGui::Button(">|"))
		{
			timeline.SelectedFrame = animator.Clips[*clipIndex].Frames.size() - 1;
			timeline.Time = std::nextafter(ClipDuration(animator.Clips[*clipIndex]), 0.0);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(86.0f);
		if (ImGui::DragFloat("FPS", &timeline.FrameRate, 0.25f, 1.0f, 240.0f, "%.1f"))
			timeline.FrameRate = std::clamp(timeline.FrameRate, 1.0f, 240.0f);

		if (clipIndex)
		{
			SpriteAnimationClip& clip = animator.Clips[*clipIndex];
			const double duration = ClipDuration(clip);
			if (timeline.Playing && duration > 0.0)
			{
				timeline.Time += static_cast<double>(ImGui::GetIO().DeltaTime);
				if (clip.Loop)
					timeline.Time = std::fmod(timeline.Time, duration);
				else if (timeline.Time >= duration)
				{
					timeline.Time = std::nextafter(duration, 0.0);
					timeline.Playing = false;
				}
				timeline.SelectedFrame = FrameAtTime(clip, timeline.Time);
			}
			double scrubTime = timeline.Time;
			const double minimumTime = 0.0;
			ImGui::SetNextItemWidth(-1.0f);
			if (duration > 0.0 && ImGui::SliderScalar("##AnimationTime",
				ImGuiDataType_Double, &scrubTime, &minimumTime,
				&duration, "%.3f s"))
			{
				timeline.Time = std::clamp(scrubTime, 0.0,
					std::nextafter(duration, 0.0));
				timeline.SelectedFrame = FrameAtTime(clip, timeline.Time);
				timeline.Playing = false;
			}

			if (timeline.Preview && entity.HasComponent<SpriteRenderer>())
			{
				auto& renderer = entity.GetComponent<SpriteRenderer>();
				if (clip.Frames.empty())
				{
					renderer.RuntimeSpriteOverrideActive = false;
					renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
				}
				else
				{
					timeline.SelectedFrame = std::min(timeline.SelectedFrame,
						clip.Frames.size() - 1);
					const AssetHandle frameSprite =
						clip.Frames[timeline.SelectedFrame].SpriteHandle;
					renderer.RuntimeSpriteOverrideActive = true;
					renderer.RuntimeSpriteOverrideHandle = frameSprite;
					renderer.Sprite = AssetManager::Get().LoadTexture(frameSprite);
				}
			}

			ImGui::Separator();
			ImGui::TextDisabled("DOPESHEET  •  Drop Sprite assets to append frames");
			std::optional<size_t> removeFrame;
			std::optional<std::pair<size_t, size_t>> moveFrame;
			if (ImGui::BeginChild("AnimationDopesheet", ImVec2(0.0f, 210.0f), true,
				ImGuiWindowFlags_HorizontalScrollbar))
			{
				for (size_t frameIndex = 0; frameIndex < clip.Frames.size(); ++frameIndex)
				{
					ImGui::PushID(static_cast<int>(frameIndex));
					const float width = std::clamp(clip.Frames[frameIndex].DurationSeconds
						* timeline.FrameRate * 34.0f, 54.0f, 220.0f);
					const std::string label = std::to_string(frameIndex) + "\n"
						+ AnimatorSpriteLabel(clip.Frames[frameIndex].SpriteHandle);
					if (ImGui::Selectable(label.c_str(), timeline.SelectedFrame == frameIndex,
						0, ImVec2(width, 92.0f)))
					{
						timeline.SelectedFrame = frameIndex;
						timeline.Time = 0.0;
						for (size_t i = 0; i < frameIndex; ++i)
							timeline.Time += clip.Frames[i].DurationSeconds;
						timeline.Playing = false;
					}
					if (ImGui::BeginDragDropSource())
					{
						ImGui::SetDragDropPayload("TC_ANIMATION_FRAME", &frameIndex,
							sizeof(frameIndex));
						ImGui::Text("Move Frame %zu", frameIndex);
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							"TC_ANIMATION_FRAME"))
							if (payload->DataSize == sizeof(size_t))
								moveFrame = { *static_cast<const size_t*>(payload->Data), frameIndex };
						ImGui::EndDragDropTarget();
					}
					if (frameIndex + 1 < clip.Frames.size()) ImGui::SameLine();
					ImGui::PopID();
				}
				const ImVec2 dropMin = ImGui::GetWindowPos();
				const ImVec2 dropMax(dropMin.x + ImGui::GetWindowSize().x,
					dropMin.y + ImGui::GetWindowSize().y);
				if (ImGui::BeginDragDropTargetCustom(ImRect(dropMin, dropMax),
					ImGui::GetID("##AnimationDropTarget")))
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
						AssetDragDropPayloadID))
					{
						if (payload->DataSize == sizeof(uint64_t))
						{
							const AssetHandle sprite(*static_cast<const uint64_t*>(payload->Data));
							const AssetMetadata* metadata = nullptr;
							const AssetSubAsset* subAsset = nullptr;
							if (ResolveSelectableSprite(sprite, metadata, subAsset))
							{
								std::string error;
								if (InsertFrame(clip, clip.Frames.size(),
									{ sprite, 1.0f / timeline.FrameRate }, error))
								{
									timeline.SelectedFrame = clip.Frames.size() - 1;
									m_Shared.MarkModified(true);
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
			ImGui::EndChild();
			if (moveFrame)
			{
				std::string error;
				if (MoveFrame(clip, moveFrame->first, moveFrame->second, error))
				{
					timeline.SelectedFrame = moveFrame->second;
					m_Shared.MarkModified(true);
				}
			}

			if (!clip.Frames.empty())
			{
				timeline.SelectedFrame = std::min(timeline.SelectedFrame,
					clip.Frames.size() - 1);
				SpriteAnimationFrame& frame = clip.Frames[timeline.SelectedFrame];
				AssetHandle editedSprite = frame.SpriteHandle;
				if (DrawAnimatorSpriteField("AnimationFrameSprite", editedSprite)
					&& editedSprite != frame.SpriteHandle)
				{
					frame.SpriteHandle = editedSprite;
					m_Shared.MarkModified(true);
				}
				float durationSeconds = frame.DurationSeconds;
				if (ImGui::DragFloat("Frame Duration", &durationSeconds, 0.005f,
					0.001f, 3600.0f, "%.3f s"))
				{
					std::string error;
					if (SetFrameDuration(clip, timeline.SelectedFrame,
						durationSeconds, error)) m_Shared.MarkModified();
				}
				if (ImGui::Button("Delete Frame"))
					removeFrame = timeline.SelectedFrame;
			}
			AssetHandle newFrameSprite = AssetHandle(0);
			ImGui::SetNextItemWidth(240.0f);
			if (DrawAnimatorSpriteField("AnimationNewFrameSprite", newFrameSprite)
				&& static_cast<uint64_t>(newFrameSprite) != 0)
			{
				std::string error;
				if (InsertFrame(clip, clip.Frames.size(),
					{ newFrameSprite, 1.0f / timeline.FrameRate }, error))
				{
					timeline.SelectedFrame = clip.Frames.size() - 1;
					m_Shared.MarkModified(true);
				}
			}
			if (removeFrame)
			{
				std::string error;
				if (RemoveFrame(clip, *removeFrame, error))
				{
					timeline.SelectedFrame = clip.Frames.empty() ? 0
						: std::min(timeline.SelectedFrame, clip.Frames.size() - 1);
					m_Shared.MarkModified(true);
				}
			}
		}
		ImGui::End();
		if (open && !*open)
			for (auto& [previewEntityID, previewState] : m_AnimationTimelineStates)
				restorePreview(previewEntityID, previewState);
		m_Shared.FinishModificationGesture();
	}

	void SceneAuthoringEditorsPanel::OnTilePaletteImGuiRender(bool* open)
	{
		m_TilePaletteFocused = false;
		if (open && !*open)
			return;
		PrepareEditorToolWindow(ImVec2(860,620),ImVec2(420,300));
        const bool visible = BeginEditorWindow("Tile Palette", open);
		m_TilePaletteDocked = ImGui::IsWindowDocked();
		m_TilePaletteFocused = visible
			&& ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (!visible)
		{
			ImGui::End();
			return;
		}
		if (static_cast<uint64_t>(m_TilePaletteAssetEditor.Handle) != 0)
		{
			DrawTilePaletteAssetEditor();
			ImGui::End();
			return;
		}
		if (!m_Shared.SelectionContext || !m_Shared.SelectionContext.HasComponent<Tilemap2D>())
		{
			ImGui::TextDisabled("Select a Tilemap 2D entity to paint.");
			ImGui::End();
			return;
		}

		Entity entity = m_Shared.SelectionContext;
		Tilemap2D& tilemap = entity.GetComponent<Tilemap2D>();
		TilePaletteState& palette = m_TilePaletteStates[
			static_cast<uint64_t>(entity.GetUUID())];
		static constexpr const char* toolLabels[] = {
			"Select", "Move", "Paint", "Box", "Eyedropper", "Erase", "Fill"
		};
		for (int tool = 0; tool < 7; ++tool)
		{
			if (tool > 0 && ImGui::GetItemRectMax().x + 90.0f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
			if (ImGui::Selectable(toolLabels[tool],
				static_cast<int>(palette.Tool) == tool, 0, ImVec2(82.0f, 0.0f)))
				palette.Tool = static_cast<TilePaletteTool>(tool);
		}
		ImGui::Separator();
		ImGui::Text("Active Tilemap: %s", entity.GetName().c_str());
		ImGui::InputInt2("Cell", &palette.Coordinate.x);
		if (palette.Tool == TilePaletteTool::Box)
			ImGui::InputInt2("Box End", &palette.BoxEnd.x);
		if (palette.Tool == TilePaletteTool::Move)
			ImGui::InputInt2("Move To", &palette.MoveDestination.x);
		DrawColorField("Tint", glm::value_ptr(palette.Tint));

		std::vector<std::pair<AssetHandle, std::string>> sprites;
		if (static_cast<uint64_t>(m_ActiveTilePaletteHandle) != 0)
		{
			const AssetMetadata* paletteMetadata = AssetManager::Get().GetRegistry()
				.GetMetadata(m_ActiveTilePaletteHandle);
			ImGui::TextDisabled("Palette: %s", paletteMetadata
				? PathToUTF8(paletteMetadata->FilePath.filename()).c_str() : "Missing");
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear Palette"))
			{
				m_ActiveTilePaletteHandle = AssetHandle(0);
				m_ActiveTilePalette = {};
			}
			for (const TilePaletteEntry& tile : m_ActiveTilePalette.Tiles)
			{
				const std::string label = AnimatorSpriteLabel(tile.SpriteHandle)
					+ " [" + std::to_string(tile.Coordinate.x) + ", "
					+ std::to_string(tile.Coordinate.y) + "]";
				if (std::none_of(sprites.begin(), sprites.end(),
					[&](const auto& item) { return item.first == tile.SpriteHandle; }))
					sprites.emplace_back(tile.SpriteHandle, label);
			}
		}
		else
		{
			for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				sprites.emplace_back(builtIn.Handle, std::string(builtIn.Name));
			for (const auto& [handle, metadata] :
				AssetManager::Get().GetRegistry().GetAssets())
			{
				if (!IsSpriteAsset(metadata) || metadata.IsMissing)
					continue;
				sprites.emplace_back(handle, PathToUTF8(metadata.FilePath.filename()));
				for (const AssetSubAsset& child : metadata.SubAssets)
					if (child.Type == AssetType::Texture2D)
						sprites.emplace_back(child.Handle, child.Name);
			}
		}
		std::sort(sprites.begin(), sprites.end(), [](const auto& left, const auto& right)
		{
			return LowerASCII(left.second) < LowerASCII(right.second);
		});
		ImGui::TextDisabled("PALETTE SPRITES");
		if (ImGui::BeginChild("TilePaletteSprites", ImVec2(0.0f, 260.0f), true))
		{
			const float tileWidth = 72.0f;
			const int columns = std::max(1, static_cast<int>(
				ImGui::GetContentRegionAvail().x / (tileWidth + ImGui::GetStyle().ItemSpacing.x)));
			int column = 0;
			for (const auto& [handle, label] : sprites)
			{
				const std::string spriteID = "TilePaletteSprite##"
					+ std::to_string(static_cast<uint64_t>(handle));
				ImGui::PushID(spriteID.c_str());
				if (DrawSpritePaletteTile(handle, label.c_str(),
					palette.SpriteHandle == handle, tileWidth))
					palette.SpriteHandle = handle;
				ImGui::PopID();
				if (++column % columns != 0) ImGui::SameLine();
			}
		}
		ImGui::EndChild();

		const TilemapCell* selectedCell = Tilemap2DRuntime::FindCell(tilemap,
			palette.Coordinate);
		const int64_t boxMinX = std::min<int64_t>(palette.Coordinate.x,
			palette.BoxEnd.x);
		const int64_t boxMaxX = std::max<int64_t>(palette.Coordinate.x,
			palette.BoxEnd.x);
		const int64_t boxMinY = std::min<int64_t>(palette.Coordinate.y,
			palette.BoxEnd.y);
		const int64_t boxMaxY = std::max<int64_t>(palette.Coordinate.y,
			palette.BoxEnd.y);
		const uint64_t boxWidth = static_cast<uint64_t>(boxMaxX - boxMinX + 1);
		const uint64_t boxHeight = static_cast<uint64_t>(boxMaxY - boxMinY + 1);
		const bool boxAreaValid = boxWidth <= 65536 && boxHeight <= 65536
			&& boxWidth * boxHeight <= 65536;
		const bool missingPaintSprite = (palette.Tool == TilePaletteTool::Paint
			|| palette.Tool == TilePaletteTool::Box
			|| palette.Tool == TilePaletteTool::Fill)
			&& static_cast<uint64_t>(palette.SpriteHandle) == 0;
		ImGui::BeginDisabled(missingPaintSprite
			|| (palette.Tool == TilePaletteTool::Box && !boxAreaValid));
		if (ImGui::Button("Apply Tool", ImVec2(-1.0f, 0.0f)))
		{
			bool changed = false;
			auto paintCell = [&](glm::ivec2 coordinate)
			{
				TilemapCell cell;
				cell.Coordinate = coordinate;
				cell.SpriteHandle = palette.SpriteHandle;
				cell.Tint = palette.Tint;
				changed |= Tilemap2DRuntime::SetCell(tilemap, std::move(cell));
			};
			switch (palette.Tool)
			{
				case TilePaletteTool::Select:
				case TilePaletteTool::Eyedropper:
					if (selectedCell)
					{
						palette.SpriteHandle = selectedCell->SpriteHandle;
						palette.Tint = selectedCell->Tint;
					}
					break;
				case TilePaletteTool::Move:
					if (selectedCell)
					{
						TilemapCell moved = *selectedCell;
						changed = Tilemap2DRuntime::EraseCell(tilemap,
							palette.Coordinate);
						moved.Coordinate = palette.MoveDestination;
						changed |= Tilemap2DRuntime::SetCell(tilemap, std::move(moved));
					}
					break;
				case TilePaletteTool::Paint:
					paintCell(palette.Coordinate);
					break;
				case TilePaletteTool::Box:
				{
					for (int64_t y = boxMinY; y <= boxMaxY; ++y)
						for (int64_t x = boxMinX; x <= boxMaxX; ++x)
							paintCell({ static_cast<int>(x), static_cast<int>(y) });
					break;
				}
				case TilePaletteTool::Erase:
					changed = Tilemap2DRuntime::EraseCell(tilemap, palette.Coordinate);
					break;
				case TilePaletteTool::Fill:
				{
					const AssetHandle replaced = selectedCell
						? selectedCell->SpriteHandle : AssetHandle(0);
					if (static_cast<uint64_t>(replaced) == 0)
					{
						paintCell(palette.Coordinate);
						break;
					}
					std::vector<glm::ivec2> queue{ palette.Coordinate };
					std::unordered_set<int64_t> visited;
					auto key = [](glm::ivec2 value)
					{
						return static_cast<int64_t>((static_cast<uint64_t>(
							static_cast<uint32_t>(value.x)) << 32)
							^ static_cast<uint32_t>(value.y));
					};
					for (size_t cursor = 0; cursor < queue.size() && queue.size() < 65536; ++cursor)
					{
						const glm::ivec2 coordinate = queue[cursor];
						if (!visited.insert(key(coordinate)).second) continue;
						const TilemapCell* cell = Tilemap2DRuntime::FindCell(tilemap, coordinate);
						if (!cell || cell->SpriteHandle != replaced) continue;
						paintCell(coordinate);
						queue.push_back(coordinate + glm::ivec2(1, 0));
						queue.push_back(coordinate + glm::ivec2(-1, 0));
						queue.push_back(coordinate + glm::ivec2(0, 1));
						queue.push_back(coordinate + glm::ivec2(0, -1));
					}
					break;
				}
			}
			if (changed) m_Shared.MarkModified(true);
		}
		ImGui::EndDisabled();
		if (palette.Tool == TilePaletteTool::Box && !boxAreaValid)
			ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
				"Box is limited to 65,536 cells.");
		ImGui::TextDisabled("%zu occupied cells", tilemap.Cells.size());
		ImGui::End();
		m_Shared.FinishModificationGesture();
	}


	void SceneAuthoringEditorsPanel::DrawSpriteAnimatorGraph(SpriteAnimator& animator,
		Entity entity,
		std::function<void()>& pendingMutation, bool standaloneWindow)
	{
		using namespace SpriteAnimatorAuthoring;
		constexpr size_t noTransition = static_cast<size_t>(-1);
		constexpr float nodeWidth = 160.0f;
		constexpr float nodeHeight = 62.0f;
		const uint64_t entityID = static_cast<uint64_t>(entity.GetUUID());
		AnimatorGraphEditorState& graph = m_AnimatorGraphStates[entityID];
		SynchronizeGraphLayout(animator, graph.Layout);
		auto requestRename = [this, entity, standaloneWindow](AnimatorRenameTarget target,
			size_t index, const std::string& currentName)
		{
			RequestAnimatorRename(target, entity, index, currentName,
				standaloneWindow);
		};

		if (!graph.SelectedState.empty()
			&& !FindStateIndex(animator, graph.SelectedState))
			graph.SelectedState.clear();
		if (!graph.TransitionSource.empty()
			&& !FindStateIndex(animator, graph.TransitionSource))
			graph.TransitionSource.clear();
		if (graph.SelectedTransition >= animator.Transitions.size())
			graph.SelectedTransition = noTransition;

		ImGui::BeginDisabled(animator.Clips.empty());
		if (ImGui::Button("Add State##Graph") && !pendingMutation)
		{
			AnimatorState state;
			state.Name = MakeUniqueName(animator.States, std::string("State"),
				[](const AnimatorState& item) { return item.Name; });
			state.Clip = animator.Clips.front().Name;
			animator.States.push_back(std::move(state));
			graph.SelectedState = animator.States.back().Name;
			graph.SelectedAnyState = false;
			graph.SelectedTransition = noTransition;
			SynchronizeGraphLayout(animator, graph.Layout);
			m_Shared.MarkModified(true);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Reset Layout"))
		{
			ResetGraphLayout(animator, graph.Layout);
			graph.PanX = 0.0f;
			graph.PanY = 0.0f;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("Drag nodes | middle-drag pans | right-click connects");

		if (ImGui::BeginChild("AnimatorGraphCanvas", ImVec2(0.0f, 360.0f), true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImVec2 canvasSize = ImGui::GetContentRegionAvail();
			canvasSize.x = std::max(canvasSize.x, 260.0f);
			canvasSize.y = std::max(canvasSize.y, 120.0f);
			ImGui::InvisibleButton("##AnimatorGraphBackground", canvasSize,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
			const bool canvasHovered = ImGui::IsItemHovered();
			if (ImGui::BeginPopupContextItem("Animator Canvas Menu"))
			{
				ImGui::BeginDisabled(animator.Clips.empty());
				if (ImGui::MenuItem("Create State"))
				{
					AnimatorState state;
					state.Name = MakeUniqueName(animator.States, std::string("State"),
						[](const AnimatorState& item) { return item.Name; });
					state.Clip = animator.Clips.front().Name;
					animator.States.push_back(std::move(state));
					graph.SelectedState = animator.States.back().Name;
					SynchronizeGraphLayout(animator, graph.Layout);
					m_Shared.MarkModified(true);
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(animator.States.empty());
				if (ImGui::MenuItem("Create Any State Transition"))
				{
					graph.TransitionSource.clear();
					graph.TransitionSourceAnyState = true;
				}
				ImGui::EndDisabled();
				ImGui::Separator();
				if (ImGui::MenuItem("Reset Layout"))
				{
					ResetGraphLayout(animator, graph.Layout);
					graph.PanX = 0.0f;
					graph.PanY = 0.0f;
				}
				ImGui::EndPopup();
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				graph.SelectedState.clear();
				graph.SelectedAnyState = false;
				graph.SelectedTransition = noTransition;
			}
			ImGui::SetItemAllowOverlap();
			if (ImGui::IsWindowHovered()
				&& ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
			{
				graph.PanX += ImGui::GetIO().MouseDelta.x;
				graph.PanY += ImGui::GetIO().MouseDelta.y;
			}

			const auto screenPosition = [&](const AnimatorGraphPosition& position)
			{
				return ImVec2(origin.x + graph.PanX + position.X,
					origin.y + graph.PanY + position.Y);
			};
			const auto add = [](const ImVec2& left, const ImVec2& right)
			{
				return ImVec2(left.x + right.x, left.y + right.y);
			};
			const auto subtract = [](const ImVec2& left, const ImVec2& right)
			{
				return ImVec2(left.x - right.x, left.y - right.y);
			};
			const auto multiply = [](const ImVec2& value, float scalar)
			{
				return ImVec2(value.x * scalar, value.y * scalar);
			};
			const auto bezierPoint = [&](const ImVec2& p0, const ImVec2& p1,
				const ImVec2& p2, const ImVec2& p3, float t)
			{
				const float inverse = 1.0f - t;
				return add(add(multiply(p0, inverse * inverse * inverse),
					multiply(p1, 3.0f * inverse * inverse * t)),
					add(multiply(p2, 3.0f * inverse * t * t),
						multiply(p3, t * t * t)));
			};

			drawList->PushClipRect(origin,
				ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y), true);
			const ImU32 gridColor = ImGui::GetColorU32(ImVec4(0.38f, 0.40f, 0.44f, 0.22f));
			constexpr float gridSize = 32.0f;
			float gridX = std::fmod(graph.PanX, gridSize);
			float gridY = std::fmod(graph.PanY, gridSize);
			if (gridX < 0.0f) gridX += gridSize;
			if (gridY < 0.0f) gridY += gridSize;
			for (float x = gridX; x < canvasSize.x; x += gridSize)
				drawList->AddLine(ImVec2(origin.x + x, origin.y),
					ImVec2(origin.x + x, origin.y + canvasSize.y), gridColor);
			for (float y = gridY; y < canvasSize.y; y += gridSize)
				drawList->AddLine(ImVec2(origin.x, origin.y + y),
					ImVec2(origin.x + canvasSize.x, origin.y + y), gridColor);

			const AnimatorGraphPosition anyStatePosition{ 24.0f, 32.0f };
			const auto nodeTopLeft = [&](std::string_view name)
			{
				const auto found = graph.Layout.StatePositions.find(std::string(name));
				return found == graph.Layout.StatePositions.end()
					? screenPosition(AnimatorGraphPosition{})
					: screenPosition(found->second);
			};
			const auto nodeCenter = [&](std::string_view name)
			{
				const ImVec2 topLeft = nodeTopLeft(name);
				return ImVec2(topLeft.x + nodeWidth * 0.5f,
					topLeft.y + nodeHeight * 0.5f);
			};
			const ImVec2 anyCenter = add(screenPosition(anyStatePosition),
				ImVec2(nodeWidth * 0.5f, nodeHeight * 0.5f));

			// Edges are drawn before nodes. Their center handles provide a large,
			// unambiguous selection target even where multiple curves overlap.
			for (size_t transitionIndex = 0;
				transitionIndex < animator.Transitions.size(); ++transitionIndex)
			{
				const AnimatorTransition& transition = animator.Transitions[transitionIndex];
				if (!FindStateIndex(animator, transition.ToState)
					|| (!transition.AnyState
						&& !FindStateIndex(animator, transition.FromState)))
					continue;
				const ImVec2 sourceCenter = transition.AnyState ? anyCenter
					: nodeCenter(transition.FromState);
				const ImVec2 targetCenter = nodeCenter(transition.ToState);
				ImVec2 p0;
				ImVec2 p1;
				ImVec2 p2;
				ImVec2 p3;
				if (!transition.AnyState && transition.FromState == transition.ToState)
				{
					p0 = ImVec2(sourceCenter.x - 34.0f,
						sourceCenter.y - nodeHeight * 0.5f);
					p1 = ImVec2(sourceCenter.x - 70.0f, p0.y - 58.0f);
					p2 = ImVec2(sourceCenter.x + 70.0f, p0.y - 58.0f);
					p3 = ImVec2(sourceCenter.x + 34.0f, p0.y);
				}
				else
				{
					const bool forward = sourceCenter.x <= targetCenter.x;
					p0 = ImVec2(sourceCenter.x + (forward ? nodeWidth : -nodeWidth) * 0.5f,
						sourceCenter.y);
					p3 = ImVec2(targetCenter.x + (forward ? -nodeWidth : nodeWidth) * 0.5f,
						targetCenter.y);
					const float control = std::max(55.0f,
						std::abs(p3.x - p0.x) * 0.45f);
					p1 = ImVec2(p0.x + (forward ? control : -control), p0.y);
					p2 = ImVec2(p3.x + (forward ? -control : control), p3.y);
				}
				const bool selected = graph.SelectedTransition == transitionIndex;
				const ImU32 edgeColor = selected
					? ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 1.0f))
					: ImGui::GetColorU32(ImVec4(0.63f, 0.68f, 0.78f, 0.92f));
				drawList->AddBezierCubic(p0, p1, p2, p3, edgeColor,
					selected ? 3.0f : 2.0f);
				const ImVec2 marker = bezierPoint(p0, p1, p2, p3, 0.5f);
				const ImVec2 arrow = bezierPoint(p0, p1, p2, p3, 0.92f);
				const ImVec2 beforeArrow = bezierPoint(p0, p1, p2, p3, 0.87f);
				const ImVec2 direction = subtract(arrow, beforeArrow);
				const float length = std::sqrt(direction.x * direction.x
					+ direction.y * direction.y);
				if (length > 0.001f)
				{
					const ImVec2 unit(direction.x / length, direction.y / length);
					const ImVec2 side(-unit.y, unit.x);
					drawList->AddTriangleFilled(arrow,
						add(subtract(arrow, multiply(unit, 10.0f)), multiply(side, 5.0f)),
						subtract(subtract(arrow, multiply(unit, 10.0f)), multiply(side, 5.0f)),
						edgeColor);
				}
				drawList->AddCircleFilled(marker, selected ? 6.0f : 5.0f, edgeColor);

				ImGui::PushID(static_cast<int>(transitionIndex));
				ImGui::SetCursorScreenPos(ImVec2(marker.x - 10.0f, marker.y - 10.0f));
				ImGui::InvisibleButton("##TransitionEdge", ImVec2(20.0f, 20.0f),
					ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					graph.SelectedTransition = transitionIndex;
					graph.SelectedState.clear();
					graph.SelectedAnyState = false;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("Transition Edge Menu");
				if (ImGui::BeginPopup("Transition Edge Menu"))
				{
					ImGui::TextUnformatted(transition.AnyState ? "Any State" : transition.FromState.c_str());
					ImGui::SameLine();
					ImGui::Text("-> %s", transition.ToState.c_str());
					if (ImGui::MenuItem("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, entityID, transitionIndex]()
						{
							std::string error;
							if (SpriteAnimatorAuthoring::RemoveTransition(animator,
								transitionIndex, error))
							{
								auto found = m_AnimatorGraphStates.find(entityID);
								if (found != m_AnimatorGraphStates.end())
									found->second.SelectedTransition = noTransition;
								m_Shared.MarkModified(true);
							}
						};
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}

			if (canvasHovered && (!graph.TransitionSource.empty()
				|| graph.TransitionSourceAnyState))
			{
				const ImVec2 source = graph.TransitionSourceAnyState ? anyCenter
					: nodeCenter(graph.TransitionSource);
				drawList->AddLine(source, ImGui::GetMousePos(),
					ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 0.95f)), 2.0f);
			}

			// Any State participates as a transition source but is not runtime data.
			const ImVec2 anyTopLeft = screenPosition(anyStatePosition);
			ImGui::SetCursorScreenPos(anyTopLeft);
			ImGui::InvisibleButton("##AnyStateNode", ImVec2(nodeWidth, nodeHeight),
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
			ImGui::SetItemAllowOverlap();
			const bool anyHovered = ImGui::IsItemHovered();
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				graph.SelectedAnyState = true;
				graph.SelectedState.clear();
				graph.SelectedTransition = noTransition;
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				ImGui::OpenPopup("Any State Menu");
			if (ImGui::BeginPopup("Any State Menu"))
			{
				if (ImGui::MenuItem("Make Transition"))
				{
					graph.TransitionSourceAnyState = true;
					graph.TransitionSource.clear();
				}
				ImGui::EndPopup();
			}
			const ImU32 anyFill = graph.SelectedAnyState
				? ImGui::GetColorU32(ImVec4(0.48f, 0.27f, 0.60f, 1.0f))
				: ImGui::GetColorU32(ImVec4(0.30f, 0.19f, 0.38f, 1.0f));
			drawList->AddRectFilled(anyTopLeft,
				ImVec2(anyTopLeft.x + nodeWidth, anyTopLeft.y + nodeHeight),
				anyFill, 7.0f);
			drawList->AddRect(anyTopLeft,
				ImVec2(anyTopLeft.x + nodeWidth, anyTopLeft.y + nodeHeight),
				anyHovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
					: ImGui::GetColorU32(ImGuiCol_Border), 7.0f, 0, anyHovered ? 2.0f : 1.0f);
			drawList->AddText(ImVec2(anyTopLeft.x + 12.0f, anyTopLeft.y + 12.0f),
				ImGui::GetColorU32(ImGuiCol_Text), "Any State");
			drawList->AddText(ImVec2(anyTopLeft.x + 12.0f, anyTopLeft.y + 34.0f),
				ImGui::GetColorU32(ImGuiCol_TextDisabled), "global source");

			for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
			{
				const AnimatorState& state = animator.States[stateIndex];
				auto position = graph.Layout.StatePositions.find(state.Name);
				if (position == graph.Layout.StatePositions.end())
					continue;
				ImVec2 topLeft = screenPosition(position->second);
				ImGui::PushID(static_cast<int>(stateIndex));
				ImGui::SetCursorScreenPos(topLeft);
				ImGui::InvisibleButton("##StateNode", ImVec2(nodeWidth, nodeHeight),
					ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();
				const bool hovered = ImGui::IsItemHovered();
				if (ImGui::IsItemActive()
					&& ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
				{
					position->second.X += ImGui::GetIO().MouseDelta.x;
					position->second.Y += ImGui::GetIO().MouseDelta.y;
					topLeft = screenPosition(position->second);
					graph.SelectedState = state.Name;
					graph.SelectedAnyState = false;
					graph.SelectedTransition = noTransition;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					if (!graph.TransitionSource.empty()
						|| graph.TransitionSourceAnyState)
					{
						std::optional<size_t> sourceIndex;
						if (!graph.TransitionSourceAnyState)
							sourceIndex = FindStateIndex(animator,
								graph.TransitionSource);
						std::string error;
						size_t addedIndex = noTransition;
						if ((graph.TransitionSourceAnyState || sourceIndex)
							&& AddTransition(animator, sourceIndex, stateIndex,
								&addedIndex, error))
						{
							graph.SelectedTransition = addedIndex;
							m_Shared.MarkModified(true);
						}
						else if (!error.empty())
							TC_Core_Warn("Could not create Animator transition: {0}", error);
						graph.TransitionSource.clear();
						graph.TransitionSourceAnyState = false;
					}
					else
					{
						graph.SelectedState = state.Name;
						graph.SelectedAnyState = false;
						graph.SelectedTransition = noTransition;
					}
				}
				if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					requestRename(AnimatorRenameTarget::State, stateIndex, state.Name);
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
					ImGui::OpenPopup("State Node Menu");
				if (ImGui::BeginPopup("State Node Menu"))
				{
					if (ImGui::MenuItem("Make Transition"))
					{
						graph.TransitionSource = state.Name;
						graph.TransitionSourceAnyState = false;
					}
					if (ImGui::MenuItem("Set as Layer Default State", nullptr,
						animator.InitialState == state.Name))
					{
						animator.InitialState = state.Name;
						m_Shared.MarkModified(true);
					}
					if (ImGui::MenuItem("Rename"))
						requestRename(AnimatorRenameTarget::State, stateIndex, state.Name);
					if (ImGui::MenuItem("Delete") && !pendingMutation)
					{
						const std::string stateName = state.Name;
						pendingMutation = [this, &animator, entityID, stateName]()
						{
							const auto stateIndex = SpriteAnimatorAuthoring::FindStateIndex(
								animator, stateName);
							std::string error;
							if (stateIndex && SpriteAnimatorAuthoring::RemoveState(animator,
								*stateIndex, error))
							{
								auto found = m_AnimatorGraphStates.find(entityID);
								if (found != m_AnimatorGraphStates.end())
								{
									found->second.SelectedState.clear();
									found->second.SelectedTransition = noTransition;
								}
								m_Shared.MarkModified(true);
							}
						};
					}
					ImGui::EndPopup();
				}

				const bool selected = graph.SelectedState == state.Name;
				const bool initial = animator.InitialState == state.Name
					|| (animator.InitialState.empty() && stateIndex == 0);
				const ImU32 fill = initial
					? ImGui::GetColorU32(ImVec4(0.16f, 0.42f, 0.25f, 1.0f))
					: ImGui::GetColorU32(ImVec4(0.16f, 0.25f, 0.36f, 1.0f));
				const ImU32 outline = selected
					? ImGui::GetColorU32(ImVec4(1.0f, 0.72f, 0.18f, 1.0f))
					: hovered ? ImGui::GetColorU32(ImGuiCol_ButtonHovered)
						: ImGui::GetColorU32(ImGuiCol_Border);
				drawList->AddRectFilled(topLeft,
					ImVec2(topLeft.x + nodeWidth, topLeft.y + nodeHeight), fill, 7.0f);
				drawList->AddRect(topLeft,
					ImVec2(topLeft.x + nodeWidth, topLeft.y + nodeHeight), outline,
					7.0f, 0, selected || hovered ? 2.0f : 1.0f);
				drawList->AddText(ImVec2(topLeft.x + 12.0f, topLeft.y + 10.0f),
					ImGui::GetColorU32(ImGuiCol_Text), state.Name.c_str());
				const std::string clipLabel = std::string("Clip: ") + state.Clip;
				drawList->AddText(ImVec2(topLeft.x + 12.0f, topLeft.y + 34.0f),
					ImGui::GetColorU32(ImGuiCol_TextDisabled), clipLabel.c_str());
				if (initial)
					drawList->AddCircleFilled(ImVec2(topLeft.x + nodeWidth - 12.0f,
						topLeft.y + 12.0f), 4.0f,
						ImGui::GetColorU32(ImVec4(0.48f, 1.0f, 0.58f, 1.0f)));
				ImGui::PopID();
			}
			drawList->PopClipRect();
		}
		ImGui::EndChild();

		if (!graph.TransitionSource.empty() || graph.TransitionSourceAnyState)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.18f, 1.0f),
				"Select a target state for %s",
				graph.TransitionSourceAnyState ? "Any State"
					: graph.TransitionSource.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Cancel Link"))
			{
				graph.TransitionSource.clear();
				graph.TransitionSourceAnyState = false;
			}
		}

		if (!graph.SelectedState.empty())
		{
			const std::optional<size_t> selectedIndex = FindStateIndex(animator,
				graph.SelectedState);
			if (selectedIndex)
			{
				AnimatorState& selected = animator.States[*selectedIndex];
				DrawAnimatorSectionLabel(selected.Name.c_str());
				if (ImGui::SmallButton("Rename Selected"))
					requestRename(AnimatorRenameTarget::State, *selectedIndex, selected.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Make Transition"))
				{
					graph.TransitionSource = selected.Name;
					graph.TransitionSourceAnyState = false;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Set Initial"))
				{
					animator.InitialState = selected.Name;
					m_Shared.MarkModified(true);
				}
				if (ImGui::BeginCombo("Selected Clip", selected.Clip.c_str()))
				{
					for (const SpriteAnimationClip& clip : animator.Clips)
						if (ImGui::Selectable(clip.Name.c_str(),
							selected.Clip == clip.Name))
						{
							selected.Clip = clip.Name;
							m_Shared.MarkModified(true);
						}
					ImGui::EndCombo();
				}
			}
		}
		else if (graph.SelectedTransition < animator.Transitions.size())
		{
			const size_t transitionIndex = graph.SelectedTransition;
			AnimatorTransition& transition = animator.Transitions[transitionIndex];
			DrawAnimatorSectionLabel("Selected Transition");
			const char* sourcePreview = transition.AnyState
				? "Any State" : transition.FromState.c_str();
			if (ImGui::BeginCombo("Graph Source", sourcePreview))
			{
				const auto target = FindStateIndex(animator, transition.ToState);
				if (target && ImGui::Selectable("Any State", transition.AnyState))
				{
					std::string error;
					if (SetTransitionEndpoints(animator, transitionIndex, std::nullopt,
						*target, error))
						m_Shared.MarkModified(true);
				}
				for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
				{
					const AnimatorState& state = animator.States[stateIndex];
					if (target && ImGui::Selectable(state.Name.c_str(),
						!transition.AnyState && transition.FromState == state.Name))
					{
						std::string error;
						if (SetTransitionEndpoints(animator, transitionIndex, stateIndex,
							*target, error))
							m_Shared.MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("Graph Target", transition.ToState.c_str()))
			{
				std::optional<size_t> source;
				if (!transition.AnyState)
					source = FindStateIndex(animator, transition.FromState);
				for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
				{
					const AnimatorState& state = animator.States[stateIndex];
					if ((transition.AnyState || source)
						&& ImGui::Selectable(state.Name.c_str(),
							transition.ToState == state.Name))
					{
						std::string error;
						if (SetTransitionEndpoints(animator, transitionIndex, source,
							stateIndex, error))
							m_Shared.MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
			bool hasExitTime = transition.ExitTime >= 0.0f;
			ImGui::BeginDisabled(hasExitTime && transition.Conditions.empty());
			if (ImGui::Checkbox("Graph Has Exit Time", &hasExitTime))
			{
				transition.ExitTime = hasExitTime ? 1.0f : -1.0f;
				m_Shared.MarkModified(true);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove Selected Transition") && !pendingMutation)
			{
				pendingMutation = [this, &animator, entityID, transitionIndex]()
				{
					std::string error;
					if (SpriteAnimatorAuthoring::RemoveTransition(animator,
						transitionIndex, error))
					{
						auto found = m_AnimatorGraphStates.find(entityID);
						if (found != m_AnimatorGraphStates.end())
							found->second.SelectedTransition = noTransition;
						m_Shared.MarkModified(true);
					}
				};
			}
			ImGui::TextDisabled("Conditions and priority remain editable in Transitions below.");
		}
	}

	void SceneAuthoringEditorsPanel::RequestAnimatorRename(AnimatorRenameTarget target,
		Entity entity, size_t index, const std::string& currentName,
		bool graphWindow)
	{
		m_AnimatorRenameTarget = target;
		m_AnimatorRenameEntity = entity.GetUUID();
		m_AnimatorRenameIndex = index;
		std::snprintf(m_AnimatorRenameBuffer.data(),
			m_AnimatorRenameBuffer.size(), "%s", currentName.c_str());
		m_AnimatorRenameError.clear();
		m_AnimatorRenamePopupGraphOwner = graphWindow;
		m_AnimatorRenamePopupRequested = true;
	}

	void SceneAuthoringEditorsPanel::DrawAnimatorRenamePopup(SpriteAnimator& animator,
		Entity entity, bool graphWindow)
	{
		using namespace SpriteAnimatorAuthoring;
		if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
			&& m_AnimatorRenamePopupGraphOwner != graphWindow)
			return;
		const bool requestPopup = m_AnimatorRenamePopupRequested;
		if (requestPopup)
		{
			ImGui::OpenPopup("Rename Animator Item");
			m_AnimatorRenamePopupRequested = false;
		}
		bool renamePopupOpen = true;
		PrepareEditorPopup("Rename Animator Item",520,true);
        if (ImGui::BeginPopupModal("Rename Animator Item", &renamePopupOpen,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();
			const bool submitted = ImGui::InputText("Name",
				m_AnimatorRenameBuffer.data(), m_AnimatorRenameBuffer.size(),
				ImGuiInputTextFlags_EnterReturnsTrue);

			const std::string candidate(m_AnimatorRenameBuffer.data());
			SpriteAnimator probe = animator;
			std::string validationError;
			bool validChange = false;
			switch (m_AnimatorRenameTarget)
			{
				case AnimatorRenameTarget::Clip:
					validChange = RenameClip(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::Parameter:
					validChange = RenameParameter(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::State:
					validChange = RenameState(probe, m_AnimatorRenameIndex,
						candidate, validationError);
					break;
				case AnimatorRenameTarget::None:
					validationError = "Nothing is selected for rename";
					break;
			}
			if (!validChange && validationError.empty())
				validationError = "Name is unchanged";
			m_AnimatorRenameError = validationError;
			if (!m_AnimatorRenameError.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.32f, 1.0f), "%s",
					m_AnimatorRenameError.c_str());

			ImGui::BeginDisabled(!validChange);
			const bool apply = ImGui::Button("Apply") || submitted;
			ImGui::EndDisabled();
			ImGui::SameLine();
			const bool cancel = ImGui::Button("Cancel");
			if (apply && validChange)
			{
				std::string error;
				bool renamed = false;
				switch (m_AnimatorRenameTarget)
				{
					case AnimatorRenameTarget::Clip:
						renamed = RenameClip(animator, m_AnimatorRenameIndex,
							candidate, error);
						break;
					case AnimatorRenameTarget::Parameter:
						renamed = RenameParameter(animator, m_AnimatorRenameIndex,
							candidate, error);
						break;
					case AnimatorRenameTarget::State:
					{
						const std::string oldName = m_AnimatorRenameIndex
							< animator.States.size()
							? animator.States[m_AnimatorRenameIndex].Name : std::string{};
						renamed = RenameState(animator, m_AnimatorRenameIndex,
							candidate, error);
						if (renamed)
						{
							auto graph = m_AnimatorGraphStates.find(
								static_cast<uint64_t>(entity.GetUUID()));
							if (graph != m_AnimatorGraphStates.end())
							{
								RenameGraphState(graph->second.Layout, oldName, candidate);
								if (graph->second.SelectedState == oldName)
									graph->second.SelectedState = candidate;
								if (graph->second.TransitionSource == oldName)
									graph->second.TransitionSource = candidate;
							}
						}
						break;
					}
					case AnimatorRenameTarget::None:
						break;
				}
				if (renamed)
					m_Shared.MarkModified(true);
				else if (!error.empty())
					TC_Core_Warn("Could not rename Animator item: {0}", error);
				m_AnimatorRenameTarget = AnimatorRenameTarget::None;
				m_AnimatorRenameEntity = UUID(0);
				m_AnimatorRenameError.clear();
				ImGui::CloseCurrentPopup();
			}
			else if (cancel || !renamePopupOpen)
			{
				m_AnimatorRenameTarget = AnimatorRenameTarget::None;
				m_AnimatorRenameEntity = UUID(0);
				m_AnimatorRenameError.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else if (m_AnimatorRenameTarget != AnimatorRenameTarget::None
			&& !requestPopup && !ImGui::IsPopupOpen("Rename Animator Item"))
		{
			m_AnimatorRenameTarget = AnimatorRenameTarget::None;
			m_AnimatorRenameEntity = UUID(0);
			m_AnimatorRenameError.clear();
			m_AnimatorRenamePopupRequested = false;
		}
	}

	void SceneAuthoringEditorsPanel::DismissAnimatorRenamePopup(bool graphWindow)
	{
		if (m_AnimatorRenamePopupGraphOwner != graphWindow)
			return;
		m_AnimatorRenamePopupRequested = false;
		PrepareEditorPopup("Rename Animator Item",520,true);
        if (ImGui::BeginPopupModal("Rename Animator Item", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		m_AnimatorRenameEntity = UUID(0);
		m_AnimatorRenameError.clear();
	}

	void SceneAuthoringEditorsPanel::ApplyAnimatorPendingMutation(Entity entity,
		std::function<void()>& pendingMutation)
	{
		if (!pendingMutation)
			return;
		pendingMutation();
		// Deferred list edits may reorder or erase transitions. Clear the graph's
		// index-based edge selection so the next click cannot edit a different
		// transition that moved into the old slot.
		auto graph = m_AnimatorGraphStates.find(
			static_cast<uint64_t>(entity.GetUUID()));
		if (graph != m_AnimatorGraphStates.end())
			graph->second.SelectedTransition = static_cast<size_t>(-1);
	}


	void SceneAuthoringEditorsPanel::OnSceneContextChanging()
	{
		// Runtime sprite overrides armed by timeline preview belong to the
		// outgoing scene; clear them before the facade swaps the context.
		for (const auto& [entityID, timeline] : m_AnimationTimelineStates)
		{
			(void)timeline;
			Entity previewed = m_Shared.Context->FindEntityByUUID(UUID(entityID));
			if (!previewed || !previewed.HasComponent<SpriteRenderer>())
				continue;
			auto& renderer = previewed.GetComponent<SpriteRenderer>();
			renderer.RuntimeSpriteOverrideActive = false;
			renderer.RuntimeSpriteOverrideHandle = AssetHandle(0);
		}
	}

	void SceneAuthoringEditorsPanel::OnSceneContextChanged(bool contextChanged)
	{
		ResetRenamePopupState();
		if (contextChanged)
			ClearSceneState();
	}

	void SceneAuthoringEditorsPanel::ClearSceneState()
	{
		m_AnimatorGraphStates.clear();
		m_AnimationTimelineStates.clear();
		m_TilePaletteStates.clear();
	}

	void SceneAuthoringEditorsPanel::ResetRenamePopupState()
	{
		m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		m_AnimatorRenameEntity = UUID(0);
		m_AnimatorRenameBuffer.fill('\0');
		m_AnimatorRenameError.clear();
		m_AnimatorRenamePopupRequested = false;
	}

	void SceneAuthoringEditorsPanel::ResetRenamePopupSelection()
	{
		m_AnimatorRenameTarget = AnimatorRenameTarget::None;
		m_AnimatorRenameEntity = UUID(0);
		m_AnimatorRenameError.clear();
		m_AnimatorRenamePopupRequested = false;
	}

	void SceneAuthoringEditorsPanel::StopTimelinePreview(UUID entityId)
	{
		const auto timeline = m_AnimationTimelineStates.find(
			static_cast<uint64_t>(entityId));
		if (timeline != m_AnimationTimelineStates.end())
			timeline->second.Playing = false;
	}

	void SceneAuthoringEditorsPanel::DismissEmbeddedRenamePopupIfStale(Entity selection)
	{
		if (!m_AnimatorRenamePopupGraphOwner
			&& (m_AnimatorRenameTarget == AnimatorRenameTarget::None
				|| !selection
				|| !selection.HasComponent<SpriteAnimator>()
				|| m_AnimatorRenameEntity != selection.GetUUID()))
			DismissAnimatorRenamePopup(false);
	}

}
