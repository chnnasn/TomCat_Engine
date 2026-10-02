#include "TomCat/Core/Log.h"
#include "TomCat/Debug/Instrumentor.h"
#include "SceneInspectorPanel.h"
#include "SceneHierarchyDetail.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <yaml-cpp/yaml.h>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Advanced2D.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Project/Project.h"
#include "TomCat/Renderer/Font.h"
#include "TomCat/Utils/PathUtils.h"
#include "TomCat/Utils/FileSystemUtils.h"
#include "TomCat/Scene/Serialization/PrefabLink.h"
#include "../EditorDragDrop.h"
#include "../EditorPropertyTransaction.h"
#include "../EditorVisuals.h"

namespace TomCat {
	using namespace HierarchyDetail;

	void SceneInspectorPanel::DrawInspectorWindow(bool* inspectorOpen)
	{
        TC_PROFILE_SCOPE("Panel Inspector");
		m_InspectorFocused = false;
		if (!inspectorOpen || *inspectorOpen)
		{
			const bool inspectorVisible = BeginEditorWindow("Inspector", inspectorOpen);
			m_InspectorDocked = ImGui::IsWindowDocked();
			m_InspectorFocused = inspectorVisible &&
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (inspectorVisible)
            {
                auto* inspectorWindow = ImGui::GetCurrentWindow();
                auto* node = inspectorWindow->DockNode;
                bool clicked = false;
                const auto drawLock = [&]() {
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    const float size = ImGui::GetFontSize() * 0.8f;
                    const ImVec2 start((a.x+b.x-size)*0.5f,(a.y+b.y-size)*0.5f);
                    DrawEditorGlyph(ImGui::GetWindowDrawList(),m_Shared.Icons,EditorIcon::Lock,start,
                        ImVec2(start.x+size,start.y+size),ImGui::GetColorU32(m_InspectorLocked ? ImGuiCol_CheckMark : ImGuiCol_Text));
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",m_InspectorLocked ? "Unlock Inspector" : "Lock Inspector");
                };
                if (node && ImGui::DockNodeBeginAmendTabBar(node))
                {
                    // This ImGui fork packs trailing tabs after labels; explicitly align this title action.
                    if (auto* lockTab=ImGui::TabBarFindTabByID(node->TabBar,ImGui::GetID("   ##InspectorLockTab")))
                        lockTab->Offset=std::max(0.0f,node->Pos.x+node->Size.x-node->TabBar->BarRect.Min.x-lockTab->Width);
                    clicked = ImGui::TabItemButton("   ##InspectorLockTab",ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip);
                    drawLock();
                    ImGui::DockNodeEndAmendTabBar();
                }
                else if (!node)
                {
                    // A floating Inspector uses the same title-bar alignment.
                    const float size = inspectorWindow->TitleBarHeight();
                    const ImRect rect(ImVec2(inspectorWindow->Pos.x+inspectorWindow->Size.x-size*2,inspectorWindow->Pos.y),
                        ImVec2(inspectorWindow->Pos.x+inspectorWindow->Size.x-size,inspectorWindow->Pos.y+size));
                    const ImRect clip = inspectorWindow->ClipRect;
                    ImGui::PushClipRect(rect.Min,rect.Max,false);
                    bool hovered=false, held=false;
                    const ImGuiID id = inspectorWindow->GetID("InspectorTitleLock");
                    ImGui::ItemAdd(rect,id);
                    clicked = ImGui::ButtonBehavior(rect,id,&hovered,&held);
                    drawLock();
                    ImGui::PopClipRect();
                    inspectorWindow->ClipRect=clip;
                }
                if (clicked)
                {
                    m_InspectorLocked = !m_InspectorLocked;
                    m_LockedInspectorAssetPath = m_InspectorLocked ? m_InspectorAssetPath : std::filesystem::path{};
                    m_InspectedEntity = m_InspectorLocked && m_Shared.SelectionContext ? m_Shared.SelectionContext.GetUUID() : UUID(0);
                }
            }
            const auto& inspectedAsset = m_InspectorLocked ? m_LockedInspectorAssetPath : m_InspectorAssetPath;
            if (inspectorVisible && !inspectedAsset.empty() && m_AssetInspectorRenderer)
                m_AssetInspectorRenderer(inspectedAsset);
            else if (inspectorVisible && m_Shared.Context && (m_Shared.SelectionContext || (m_InspectorLocked && m_Shared.Context->FindEntityByUUID(m_InspectedEntity))))
			{
                Entity inspected = m_InspectorLocked ? m_Shared.Context->FindEntityByUUID(m_InspectedEntity) : m_Shared.SelectionContext;
                if (!inspected) { m_InspectorLocked = false; inspected = m_Shared.SelectionContext; }
                if (!m_InspectorLocked && m_Shared.MultiSelection.size()>1) DrawMultiSelectionInspector();
                else DrawComponents(inspected);
				const ImVec2 windowPosition = ImGui::GetWindowPos();
				const ImVec2 contentMinimum = ImGui::GetWindowContentRegionMin();
				const ImVec2 contentMaximum = ImGui::GetWindowContentRegionMax();
				const ImRect dropRect(
					ImVec2(windowPosition.x + contentMinimum.x,
						windowPosition.y + contentMinimum.y),
					ImVec2(windowPosition.x + contentMaximum.x,
						windowPosition.y + contentMaximum.y));
				if (ImGui::BeginDragDropTargetCustom(dropRect,
					ImGui::GetID("##InspectorCSharpScriptDrop")))
				{
					AcceptCSharpScriptDrop(inspected);
					ImGui::EndDragDropTarget();
				}
			}
			if (inspectorVisible)
				m_Authors.DismissEmbeddedRenamePopupIfStale(m_Shared.SelectionContext);
			ImGui::End();
		}}

	bool SceneInspectorPanel::AttachCSharpScript(Entity entity, AssetHandle handle)
	{
		if (!entity || !m_Shared.Context || !m_Shared.ColliderEditingAllowed || !m_Shared.ScriptEditingEnabled ||
			static_cast<uint64_t>(handle) == 0)
			return false;
		const AssetMetadata* assetMetadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!assetMetadata || assetMetadata->IsMissing ||
			assetMetadata->Type != AssetType::CSharpScript)
			return false;

		std::optional<EditorScriptMetadata> scriptMetadata;
		if (m_Shared.ScriptMetadata)
			scriptMetadata = m_Shared.ScriptMetadata(handle);

		auto& component = entity.HasComponent<CSharpScripts>()
			? entity.GetComponent<CSharpScripts>()
			: entity.AddComponent<CSharpScripts>();
		if (scriptMetadata && scriptMetadata->DisallowMultiple)
		{
			const bool alreadyAttached = std::any_of(component.Scripts.begin(),
				component.Scripts.end(), [handle](const CSharpScriptEntry& entry)
				{
					return entry.ScriptAsset == handle;
				});
			if (alreadyAttached)
			{
				TC_Warn("Script '{0}' disallows multiple attachments on one entity",
					PathToUTF8(assetMetadata->FilePath));
				return false;
			}
		}

		CSharpScriptEntry entry;
		auto attachmentIDIsAvailable = [this](uint64_t candidate)
		{
			if (candidate == 0)
				return false;
			const auto view = m_Shared.Context->m_Registry.view<CSharpScripts>();
			for (const entt::entity entityHandle : view)
			{
				const auto& existingScripts =
					view.get<CSharpScripts>(entityHandle).Scripts;
				if (std::any_of(existingScripts.begin(), existingScripts.end(),
					[candidate](const CSharpScriptEntry& existing)
					{
						return static_cast<uint64_t>(existing.AttachmentID) ==
							candidate;
					}))
					return false;
			}
			return true;
		};
		bool hasUniqueAttachmentID = false;
		for (uint32_t attempt = 0; attempt < 64; ++attempt)
		{
			const uint64_t rawID = static_cast<uint64_t>(entry.AttachmentID);
			hasUniqueAttachmentID = attachmentIDIsAvailable(rawID);
			if (hasUniqueAttachmentID)
				break;
			entry.AttachmentID = UUID();
		}
		if (!hasUniqueAttachmentID)
		{
			TC_Core_Error("Could not allocate a unique C# script attachment ID");
			return false;
		}

		entry.ScriptAsset = handle;
		entry.Enabled = true;
		entry.LastKnownClassName = scriptMetadata && !scriptMetadata->TypeName.empty()
			? scriptMetadata->TypeName
			: PathToUTF8(AssetManager::Get().ResolvePath(handle).stem());
		if (scriptMetadata)
		{
			entry.Fields.reserve(scriptMetadata->Fields.size());
			for (const EditorScriptFieldMetadata& fieldMetadata : scriptMetadata->Fields)
			{
				ScriptFieldValue value = fieldMetadata.DefaultValue &&
					IsScriptFieldValueCompatible(fieldMetadata.Type,
						*fieldMetadata.DefaultValue)
					? *fieldMetadata.DefaultValue
					: DefaultScriptFieldValue(fieldMetadata.Type);
				entry.Fields.emplace_back(fieldMetadata.FieldID, fieldMetadata.Name,
					fieldMetadata.Type, std::move(value));
			}
		}
		component.Scripts.push_back(std::move(entry));
		m_Shared.MarkModified();
		return true;
	}

	bool SceneInspectorPanel::AcceptCSharpScriptDrop(Entity entity)
	{
		if (!entity || !m_Shared.ColliderEditingAllowed || !m_Shared.ScriptEditingEnabled)
			return false;
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			AssetDragDropPayloadID, ImGuiDragDropFlags_AcceptBeforeDelivery);
		if (!payload || payload->DataSize != sizeof(uint64_t))
			return false;
		const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
		const AssetMetadata* metadata =
			AssetManager::Get().GetRegistry().GetMetadata(handle);
		if (!metadata || metadata->Type != AssetType::CSharpScript)
			return false;
		if (payload->IsDelivery())
			AttachCSharpScript(entity, handle);
		return true;
	}


	void SceneInspectorPanel::DrawSpriteAnimatorInspector(SpriteAnimator& animator,
		Entity entity)
	{
		using namespace SpriteAnimatorAuthoring;

		if (m_Authors.m_AnimatorRenameTarget != SceneAuthoringEditorsPanel::AnimatorRenameTarget::None
			&& m_Authors.m_AnimatorRenameEntity != entity.GetUUID())
		{
			m_Authors.m_AnimatorRenameTarget = SceneAuthoringEditorsPanel::AnimatorRenameTarget::None;
			m_Authors.m_AnimatorRenameEntity = UUID(0);
			m_Authors.m_AnimatorRenameError.clear();
		}

		std::function<void()> pendingMutation;
		auto requestRename = [this, entity](SceneAuthoringEditorsPanel::AnimatorRenameTarget target, size_t index,
			const std::string& currentName)
		{
			m_Authors.RequestAnimatorRename(target, entity, index, currentName, false);
		};

		const ComponentDescriptor* animatorDescriptor =
			ComponentRegistry::Get().Find(UUID(ComponentIds::SpriteAnimator));
		const PropertyDescriptor* controllerProperty = nullptr;
		if (animatorDescriptor)
		{
			const auto found = std::find_if(animatorDescriptor->Properties.begin(),
				animatorDescriptor->Properties.end(), [](const PropertyDescriptor& property)
				{
					return property.PropertyId
						== UUID(ComponentIds::SpriteAnimatorProperties::Controller);
				});
			if (found != animatorDescriptor->Properties.end())
				controllerProperty = &*found;
		}
		if (controllerProperty && controllerProperty->AssetReference)
		{
			AssetHandle controller = animator.ControllerHandle;
			if (DrawRegisteredAssetReference(*controllerProperty, controller,
				&m_Shared.AssetReveal)
				&& controller != animator.ControllerHandle)
			{
				std::string error;
				if (controllerProperty->Set(entity,
					static_cast<uint64_t>(controller), error))
					m_Shared.MarkModified(true);
				else
					TC_Core_Warn("Could not assign Animator Controller: {0}", error);
			}
		}
		const bool usesExternalController =
			static_cast<uint64_t>(animator.ControllerHandle) != 0;
		if (!usesExternalController)
			ImGui::TextDisabled("Embedded controller: graph and clips are stored with this entity.");
		else
			ImGui::TextDisabled("External controller and Animation Clips are loaded when Play starts.");
		if (usesExternalController)
		{
			if (ImGui::Button("Open Controller"))
				m_Authors.OpenAuthoringAsset(animator.ControllerHandle,
					AssetType::AnimatorController);
		}
		else
		{
			if (ImGui::Button("Open Animation")) m_Authors.m_AnimationOpenRequested = true;
			ImGui::SameLine();
			if (ImGui::Button("Open Animator")) m_Authors.m_AnimatorGraphOpenRequested = true;
		}
		if (ImGui::Checkbox("Play On Start", &animator.PlayOnStart))
			m_Shared.MarkModified();
		float animatorSpeed = animator.Speed;
		if (ImGui::DragFloat("Speed", &animatorSpeed, 0.05f, 0.0f, 100.0f,
			"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(animatorSpeed))
		{
			animatorSpeed = std::clamp(animatorSpeed, 0.0f, 100.0f);
			if (animatorSpeed != animator.Speed)
			{
				animator.Speed = animatorSpeed;
				m_Shared.MarkModified();
			}
		}
		if (usesExternalController)
		{
			ImGui::TextWrapped("Controller parameters, states, transitions, and clips "
				"are edited in the external asset. The embedded snapshot is read-only.");
			return;
		}

		const char* initialClip = animator.InitialClip.empty()
			? "First clip" : animator.InitialClip.c_str();
		if (ImGui::BeginCombo("Initial Clip", initialClip))
		{
			if (ImGui::Selectable("First clip", animator.InitialClip.empty()))
			{
				animator.InitialClip.clear();
				m_Shared.MarkModified(true);
			}
			for (const SpriteAnimationClip& clip : animator.Clips)
			{
				if (ImGui::Selectable(clip.Name.c_str(), animator.InitialClip == clip.Name))
				{
					animator.InitialClip = clip.Name;
					m_Shared.MarkModified(true);
				}
			}
			ImGui::EndCombo();
		}

		if (!animator.States.empty())
		{
			const char* initialState = animator.InitialState.empty()
				? "First state" : animator.InitialState.c_str();
			if (ImGui::BeginCombo("Initial State", initialState))
			{
				if (ImGui::Selectable("First state", animator.InitialState.empty()))
				{
					animator.InitialState.clear();
					m_Shared.MarkModified(true);
				}
				for (const AnimatorState& state : animator.States)
				{
					if (ImGui::Selectable(state.Name.c_str(),
						animator.InitialState == state.Name))
					{
						animator.InitialState = state.Name;
						m_Shared.MarkModified(true);
					}
				}
				ImGui::EndCombo();
			}
		}

		ImGui::TextDisabled("%zu clips, %zu parameters, %zu states, %zu transitions",
			animator.Clips.size(), animator.Parameters.size(), animator.States.size(),
			animator.Transitions.size());
		if (ImGui::TreeNodeEx("Animator Graph", ImGuiTreeNodeFlags_DefaultOpen))
		{
			m_Authors.DrawSpriteAnimatorGraph(animator, entity, pendingMutation);
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Clips", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (size_t clipIndex = 0; clipIndex < animator.Clips.size(); ++clipIndex)
			{
				SpriteAnimationClip& clip = animator.Clips[clipIndex];
				ImGui::PushID(static_cast<int>(clipIndex));
				const bool clipOpen = ImGui::TreeNodeEx("##Clip", 0, "%zu. %s",
					clipIndex + 1, clip.Name.c_str());
				if (clipOpen)
				{
					if (ImGui::Checkbox("Loop", &clip.Loop))
						m_Shared.MarkModified();
					if (ImGui::Button("Rename"))
						requestRename(SceneAuthoringEditorsPanel::AnimatorRenameTarget::Clip, clipIndex, clip.Name);
					ImGui::SameLine();
					ImGui::BeginDisabled(clipIndex == 0);
					if (ImGui::SmallButton("Up") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::swap(animator.Clips[clipIndex - 1],
								animator.Clips[clipIndex]);
							m_Shared.MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(clipIndex + 1 >= animator.Clips.size());
					if (ImGui::SmallButton("Down") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::swap(animator.Clips[clipIndex],
								animator.Clips[clipIndex + 1]);
							m_Shared.MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(animator.Clips.size() == 1);
					if (ImGui::SmallButton("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, clipIndex]()
						{
							std::string error;
							if (RemoveClip(animator, clipIndex, error))
								m_Shared.MarkModified(true);
							else if (!error.empty())
								TC_Core_Warn("Could not remove Animator clip: {0}", error);
						};
					}
					ImGui::EndDisabled();

					DrawAnimatorSectionLabel("Frames");
					for (size_t frameIndex = 0; frameIndex < clip.Frames.size(); ++frameIndex)
					{
						SpriteAnimationFrame& frame = clip.Frames[frameIndex];
						ImGui::PushID(static_cast<int>(frameIndex));
						ImGui::Text("Frame %zu", frameIndex + 1);
						ImGui::SameLine();
						ImGui::BeginDisabled(frameIndex == 0);
						if (ImGui::SmallButton("Up") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								std::swap(frames[frameIndex - 1], frames[frameIndex]);
								m_Shared.MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(frameIndex + 1 >= clip.Frames.size());
						if (ImGui::SmallButton("Down") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								std::swap(frames[frameIndex], frames[frameIndex + 1]);
								m_Shared.MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(clip.Frames.size() == 1);
						if (ImGui::SmallButton("Remove") && !pendingMutation)
						{
							pendingMutation = [this, &animator, clipIndex, frameIndex]()
							{
								auto& frames = animator.Clips[clipIndex].Frames;
								frames.erase(frames.begin()
									+ static_cast<std::ptrdiff_t>(frameIndex));
								m_Shared.MarkModified(true);
							};
						}
						ImGui::EndDisabled();

						ImGui::TextDisabled("Sprite");
						if (DrawAnimatorSpriteField("Sprite", frame.SpriteHandle))
							m_Shared.MarkModified(true);
						float duration = frame.DurationSeconds;
						if (ImGui::DragFloat("Duration (s)", &duration, 0.005f,
							0.001f, 60.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)
							&& std::isfinite(duration))
						{
							duration = std::clamp(duration, 0.001f, 60.0f);
							if (duration != frame.DurationSeconds)
							{
								frame.DurationSeconds = duration;
								m_Shared.MarkModified();
							}
						}
						ImGui::Separator();
						ImGui::PopID();
					}
					if (ImGui::Button("Add Frame") && !pendingMutation)
					{
						AssetHandle sprite = AssetHandle(0);
						if (!clip.Frames.empty())
							sprite = clip.Frames.back().SpriteHandle;
						else if (entity.HasComponent<SpriteRenderer>())
							sprite = entity.GetComponent<SpriteRenderer>().SpriteHandle;
						pendingMutation = [this, &animator, clipIndex, sprite]()
						{
							animator.Clips[clipIndex].Frames.push_back(
								{ sprite, 1.0f / 12.0f });
							m_Shared.MarkModified(true);
						};
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (ImGui::Button("Add Clip") && !pendingMutation)
			{
				AssetHandle sprite = AssetHandle(0);
				if (entity.HasComponent<SpriteRenderer>())
					sprite = entity.GetComponent<SpriteRenderer>().SpriteHandle;
				pendingMutation = [this, &animator, sprite]()
				{
					SpriteAnimationClip clip;
					clip.Name = MakeUniqueName(animator.Clips, std::string("Clip"),
						[](const SpriteAnimationClip& item) { return item.Name; });
					clip.Frames.push_back({ sprite, 1.0f / 12.0f });
					animator.Clips.push_back(std::move(clip));
					m_Shared.MarkModified(true);
				};
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			static constexpr const char* typeNames[] =
				{ "Bool", "Int", "Float", "Trigger" };
			for (size_t parameterIndex = 0;
				parameterIndex < animator.Parameters.size(); ++parameterIndex)
			{
				AnimatorParameter& parameter = animator.Parameters[parameterIndex];
				ImGui::PushID(static_cast<int>(parameterIndex));
				DrawAnimatorSectionLabel(parameter.Name.c_str());
				if (ImGui::SmallButton("Rename"))
					requestRename(SceneAuthoringEditorsPanel::AnimatorRenameTarget::Parameter, parameterIndex,
						parameter.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove") && !pendingMutation)
				{
					pendingMutation = [this, &animator, parameterIndex]()
					{
						std::string error;
						if (RemoveParameter(animator, parameterIndex, error))
							m_Shared.MarkModified(true);
						else if (!error.empty())
							TC_Core_Warn("Could not remove Animator parameter: {0}", error);
					};
				}
				int type = std::clamp(static_cast<int>(parameter.Type), 0, 3);
				if (ImGui::BeginCombo("Type", typeNames[type]))
				{
					for (int candidate = 0; candidate < 4; ++candidate)
					{
						if (ImGui::Selectable(typeNames[candidate], candidate == type)
							&& SetParameterType(animator, parameterIndex,
								static_cast<AnimatorParameterType>(candidate)))
							m_Shared.MarkModified(true);
					}
					ImGui::EndCombo();
				}
				switch (parameter.Type)
				{
					case AnimatorParameterType::Bool:
						if (ImGui::Checkbox("Default", &parameter.BoolValue))
							m_Shared.MarkModified();
						break;
					case AnimatorParameterType::Int:
						if (ImGui::InputInt("Default", &parameter.IntValue))
							m_Shared.MarkModified();
						break;
					case AnimatorParameterType::Float:
					{
						float value = parameter.FloatValue;
						if (ImGui::DragFloat("Default", &value, 0.05f)
							&& std::isfinite(value) && value != parameter.FloatValue)
						{
							parameter.FloatValue = value;
							m_Shared.MarkModified();
						}
						break;
					}
					case AnimatorParameterType::Trigger:
						ImGui::TextDisabled("Triggers always start reset.");
						break;
				}
				ImGui::PopID();
			}
			if (ImGui::Button("Add Parameter") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorParameter parameter;
					parameter.Name = MakeUniqueName(animator.Parameters,
						std::string("Parameter"),
						[](const AnimatorParameter& item) { return item.Name; });
					animator.Parameters.push_back(std::move(parameter));
					m_Shared.MarkModified(true);
				};
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (size_t stateIndex = 0; stateIndex < animator.States.size(); ++stateIndex)
			{
				AnimatorState& state = animator.States[stateIndex];
				ImGui::PushID(static_cast<int>(stateIndex));
				DrawAnimatorSectionLabel(state.Name.c_str());
				if (ImGui::SmallButton("Rename"))
					requestRename(SceneAuthoringEditorsPanel::AnimatorRenameTarget::State, stateIndex, state.Name);
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove") && !pendingMutation)
				{
					pendingMutation = [this, &animator, stateIndex]()
					{
						std::string error;
						if (RemoveState(animator, stateIndex, error))
							m_Shared.MarkModified(true);
						else if (!error.empty())
							TC_Core_Warn("Could not remove Animator state: {0}", error);
					};
				}
				if (ImGui::BeginCombo("Clip", state.Clip.c_str()))
				{
					for (const SpriteAnimationClip& clip : animator.Clips)
					{
						if (ImGui::Selectable(clip.Name.c_str(), state.Clip == clip.Name))
						{
							state.Clip = clip.Name;
							m_Shared.MarkModified(true);
						}
					}
					ImGui::EndCombo();
				}
				float stateSpeed = state.Speed;
				if (ImGui::DragFloat("Speed", &stateSpeed, 0.05f, 0.01f, 100.0f,
					"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(stateSpeed))
				{
					stateSpeed = std::clamp(stateSpeed, 0.01f, 100.0f);
					if (stateSpeed != state.Speed)
					{
						state.Speed = stateSpeed;
						m_Shared.MarkModified();
					}
				}
				ImGui::PopID();
			}
			ImGui::BeginDisabled(animator.Clips.empty());
			if (ImGui::Button("Add State") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorState state;
					state.Name = MakeUniqueName(animator.States, std::string("State"),
						[](const AnimatorState& item) { return item.Name; });
					state.Clip = animator.Clips.front().Name;
					animator.States.push_back(std::move(state));
					m_Shared.MarkModified(true);
				};
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::TextDisabled("Evaluated top to bottom; first matching transition wins.");
			for (size_t transitionIndex = 0;
				transitionIndex < animator.Transitions.size(); ++transitionIndex)
			{
				AnimatorTransition& transition = animator.Transitions[transitionIndex];
				ImGui::PushID(static_cast<int>(transitionIndex));
				const char* source = transition.AnyState
					? "AnyState" : transition.FromState.c_str();
				const bool transitionOpen = ImGui::TreeNodeEx("##Transition", 0,
					"%zu. %s -> %s", transitionIndex + 1, source,
					transition.ToState.c_str());
				if (transitionOpen)
				{
					ImGui::BeginDisabled(transitionIndex == 0);
					if (ImGui::SmallButton("Up") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							std::swap(animator.Transitions[transitionIndex - 1],
								animator.Transitions[transitionIndex]);
							m_Shared.MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(transitionIndex + 1 >= animator.Transitions.size());
					if (ImGui::SmallButton("Down") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							std::swap(animator.Transitions[transitionIndex],
								animator.Transitions[transitionIndex + 1]);
							m_Shared.MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							animator.Transitions.erase(animator.Transitions.begin()
								+ static_cast<std::ptrdiff_t>(transitionIndex));
							m_Shared.MarkModified(true);
						};
					}

					bool anyState = transition.AnyState;
					if (ImGui::Checkbox("Any State", &anyState))
					{
						transition.AnyState = anyState;
						transition.FromState = anyState || animator.States.empty()
							? std::string{} : animator.States.front().Name;
						m_Shared.MarkModified(true);
					}
					if (!transition.AnyState)
					{
						if (ImGui::BeginCombo("From", transition.FromState.c_str()))
						{
							for (const AnimatorState& state : animator.States)
							{
								if (ImGui::Selectable(state.Name.c_str(),
									transition.FromState == state.Name))
								{
									transition.FromState = state.Name;
									m_Shared.MarkModified(true);
								}
							}
							ImGui::EndCombo();
						}
					}
					if (ImGui::BeginCombo("To", transition.ToState.c_str()))
					{
						for (const AnimatorState& state : animator.States)
						{
							if (ImGui::Selectable(state.Name.c_str(),
								transition.ToState == state.Name))
							{
								transition.ToState = state.Name;
								m_Shared.MarkModified(true);
							}
						}
						ImGui::EndCombo();
					}

					bool hasExitTime = transition.ExitTime >= 0.0f;
					ImGui::BeginDisabled(hasExitTime && transition.Conditions.empty());
					if (ImGui::Checkbox("Has Exit Time", &hasExitTime))
					{
						transition.ExitTime = hasExitTime ? 1.0f : -1.0f;
						m_Shared.MarkModified(true);
					}
					ImGui::EndDisabled();
					if (transition.ExitTime >= 0.0f)
					{
						float exitTime = transition.ExitTime;
						if (ImGui::SliderFloat("Exit Time", &exitTime, 0.0f, 1.0f,
							"%.3f", ImGuiSliderFlags_AlwaysClamp)
							&& std::isfinite(exitTime) && exitTime != transition.ExitTime)
						{
							transition.ExitTime = std::clamp(exitTime, 0.0f, 1.0f);
							m_Shared.MarkModified();
						}
					}

					DrawAnimatorSectionLabel("Conditions");
					for (size_t conditionIndex = 0;
						conditionIndex < transition.Conditions.size(); ++conditionIndex)
					{
						AnimatorCondition& condition = transition.Conditions[conditionIndex];
						ImGui::PushID(static_cast<int>(conditionIndex));
						const AnimatorParameter* selectedParameter = nullptr;
						for (const AnimatorParameter& parameter : animator.Parameters)
							if (parameter.Name == condition.Parameter)
								selectedParameter = &parameter;
						const char* parameterPreview = selectedParameter
							? selectedParameter->Name.c_str() : "<Missing>";
						if (ImGui::BeginCombo("Parameter", parameterPreview))
						{
							for (const AnimatorParameter& parameter : animator.Parameters)
							{
								if (ImGui::Selectable(parameter.Name.c_str(),
									condition.Parameter == parameter.Name))
								{
									condition.Parameter = parameter.Name;
									condition.Mode = DefaultConditionMode(parameter.Type);
									condition.Threshold = 0.0f;
									m_Shared.MarkModified(true);
								}
							}
							ImGui::EndCombo();
						}
						selectedParameter = nullptr;
						for (const AnimatorParameter& parameter : animator.Parameters)
							if (parameter.Name == condition.Parameter)
								selectedParameter = &parameter;
						if (selectedParameter)
						{
							if (IsBooleanParameter(selectedParameter->Type))
							{
								static constexpr AnimatorConditionMode modes[] =
									{ AnimatorConditionMode::If, AnimatorConditionMode::IfNot };
								static constexpr const char* names[] = { "If", "If Not" };
								const int current = condition.Mode == AnimatorConditionMode::IfNot
									? 1 : 0;
								if (ImGui::BeginCombo("Mode", names[current]))
								{
									for (int modeIndex = 0; modeIndex < 2; ++modeIndex)
										if (ImGui::Selectable(names[modeIndex], current == modeIndex))
										{
											condition.Mode = modes[modeIndex];
											m_Shared.MarkModified(true);
										}
									ImGui::EndCombo();
								}
							}
							else
							{
								static constexpr AnimatorConditionMode modes[] = {
									AnimatorConditionMode::Greater, AnimatorConditionMode::Less,
									AnimatorConditionMode::Equals, AnimatorConditionMode::NotEqual };
								static constexpr const char* names[] =
									{ "Greater", "Less", "Equals", "Not Equal" };
								int current = std::clamp(static_cast<int>(condition.Mode)
									- static_cast<int>(AnimatorConditionMode::Greater), 0, 3);
								if (ImGui::BeginCombo("Mode", names[current]))
								{
									for (int modeIndex = 0; modeIndex < 4; ++modeIndex)
										if (ImGui::Selectable(names[modeIndex], current == modeIndex))
										{
											condition.Mode = modes[modeIndex];
											m_Shared.MarkModified(true);
										}
									ImGui::EndCombo();
								}
								if (selectedParameter->Type == AnimatorParameterType::Int)
								{
									int threshold = static_cast<int>(condition.Threshold);
									if (ImGui::InputInt("Threshold", &threshold))
									{
										condition.Threshold = static_cast<float>(threshold);
										m_Shared.MarkModified();
									}
								}
								else
								{
									float threshold = condition.Threshold;
									if (ImGui::DragFloat("Threshold", &threshold, 0.05f)
										&& std::isfinite(threshold)
										&& threshold != condition.Threshold)
									{
										condition.Threshold = threshold;
										m_Shared.MarkModified();
									}
								}
							}
						}
						ImGui::BeginDisabled(transition.ExitTime < 0.0f
							&& transition.Conditions.size() == 1);
						if (ImGui::SmallButton("Remove Condition") && !pendingMutation)
						{
							pendingMutation = [this, &animator, transitionIndex,
								conditionIndex]()
							{
								auto& conditions = animator.Transitions[transitionIndex].Conditions;
								conditions.erase(conditions.begin()
									+ static_cast<std::ptrdiff_t>(conditionIndex));
								m_Shared.MarkModified(true);
							};
						}
						ImGui::EndDisabled();
						ImGui::Separator();
						ImGui::PopID();
					}
					ImGui::BeginDisabled(animator.Parameters.empty());
					if (ImGui::Button("Add Condition") && !pendingMutation)
					{
						pendingMutation = [this, &animator, transitionIndex]()
						{
							const AnimatorParameter& parameter = animator.Parameters.front();
							animator.Transitions[transitionIndex].Conditions.push_back(
								{ parameter.Name, DefaultConditionMode(parameter.Type), 0.0f });
							m_Shared.MarkModified(true);
						};
					}
					ImGui::EndDisabled();
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			ImGui::BeginDisabled(animator.States.empty());
			if (ImGui::Button("Add Transition") && !pendingMutation)
			{
				pendingMutation = [this, &animator]()
				{
					AnimatorTransition transition;
					transition.FromState = animator.States.front().Name;
					transition.ToState = animator.States.front().Name;
					transition.ExitTime = 1.0f;
					animator.Transitions.push_back(std::move(transition));
					m_Shared.MarkModified(true);
				};
			}
			ImGui::EndDisabled();
			ImGui::TreePop();
		}

		m_Authors.DrawAnimatorRenamePopup(animator, entity, false);

		m_Authors.ApplyAnimatorPendingMutation(entity, pendingMutation);
	}

	void SceneInspectorPanel::DrawGrid2DInspector(Grid2D& grid)
	{
		bool changed = false;
		glm::vec2 cellSize = grid.CellSize;
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(cellSize), 0.05f,
			0.0001f, 0.0f, "%.3f"))
		{
			cellSize.x = std::max(0.0001f, std::isfinite(cellSize.x)
				? cellSize.x : grid.CellSize.x);
			cellSize.y = std::max(0.0001f, std::isfinite(cellSize.y)
				? cellSize.y : grid.CellSize.y);
			grid.CellSize = cellSize;
			changed = true;
		}
		glm::vec2 cellGap = grid.CellGap;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(cellGap), 0.01f)
			&& std::isfinite(cellGap.x) && std::isfinite(cellGap.y))
		{
			grid.CellGap = cellGap;
			changed = true;
		}
		const char* layouts[] = {
			"Rectangle", "Isometric", "Isometric Z As Y", "Hexagon"
		};
		int layoutIndex = std::clamp(static_cast<int>(grid.Layout), 0, 3);
		if (ImGui::Combo("Cell Layout", &layoutIndex, layouts,
			static_cast<int>(std::size(layouts))))
		{
			grid.Layout = static_cast<GridCellLayout2D>(layoutIndex);
			changed = true;
		}
		const char* swizzles[] = { "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX" };
		int swizzle = std::clamp(static_cast<int>(grid.Swizzle), 0, 5);
		if (ImGui::Combo("Cell Swizzle", &swizzle, swizzles,
			static_cast<int>(std::size(swizzles))))
		{
			grid.Swizzle = static_cast<GridCellSwizzle2D>(swizzle);
			changed = true;
		}
		if (changed)
			m_Shared.MarkModified();
	}

	void SceneInspectorPanel::DrawTilemap2DInspector(Tilemap2D& tilemap,
		Entity entity)
	{
		if (ImGui::Button("Open Tile Palette", ImVec2(-1.0f, 0.0f)))
			m_Authors.m_TilePaletteOpenRequested = true;
		bool gridChanged = false;
		glm::vec2 cellSize = tilemap.CellSize;
		if (ImGui::DragFloat2("Cell Size", glm::value_ptr(cellSize), 0.05f,
			0.0001f, 0.0f, "%.3f"))
		{
			if (!std::isfinite(cellSize.x) || !std::isfinite(cellSize.y))
				cellSize = tilemap.CellSize;
			cellSize.x = std::max(cellSize.x, 0.0001f);
			cellSize.y = std::max(cellSize.y, 0.0001f);
			if (cellSize != tilemap.CellSize)
			{
				tilemap.CellSize = cellSize;
				gridChanged = true;
			}
		}
		glm::vec2 cellGap = tilemap.CellGap;
		if (ImGui::DragFloat2("Cell Gap", glm::value_ptr(cellGap), 0.01f))
		{
			if (std::isfinite(cellGap.x) && std::isfinite(cellGap.y)
				&& cellGap != tilemap.CellGap)
			{
				tilemap.CellGap = cellGap;
				gridChanged = true;
			}
		}
		if (entity.HasComponent<TilemapRenderer2D>())
			ImGui::TextDisabled("Sorting is configured by Tilemap Renderer 2D.");
		else
		{
			gridChanged |= ImGui::InputInt("Sorting Layer", &tilemap.SortingLayer);
			gridChanged |= ImGui::InputInt("Order In Layer", &tilemap.OrderInLayer);
		}
		if (gridChanged)
			m_Shared.MarkModified();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("PAINT BRUSH");
		TilemapBrushState& brush = m_TilemapBrushStates[
			static_cast<uint64_t>(entity.GetUUID())];
		ImGui::InputInt2("Coordinate", &brush.Coordinate.x);
		ImGui::TextUnformatted("Palette Sprite");
		ImGui::SetNextItemWidth(-1.0f);
		DrawAnimatorSpriteField("TilemapBrushSprite", brush.SpriteHandle);
		DrawColorField("Tint", glm::value_ptr(brush.Tint));
		ImGui::Checkbox("Flip X", &brush.FlipX);
		ImGui::SameLine();
		ImGui::Checkbox("Flip Y", &brush.FlipY);
		const char* rotations[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
		int brushRotation = std::clamp(brush.RotationQuarterTurns, 0, 3);
		if (ImGui::Combo("Rotation", &brushRotation, rotations, 4))
			brush.RotationQuarterTurns = brushRotation;

		const TilemapCell* occupied = Tilemap2DRuntime::FindCell(tilemap,
			brush.Coordinate);
		ImGui::BeginDisabled(static_cast<uint64_t>(brush.SpriteHandle) == 0);
		if (ImGui::Button(occupied ? "Update Cell" : "Paint Cell"))
		{
			TilemapCell cell;
			cell.Coordinate = brush.Coordinate;
			cell.SpriteHandle = brush.SpriteHandle;
			cell.Tint = brush.Tint;
			cell.FlipX = brush.FlipX;
			cell.FlipY = brush.FlipY;
			cell.RotationQuarterTurns = brush.RotationQuarterTurns;
			if (Tilemap2DRuntime::SetCell(tilemap, std::move(cell)))
				m_Shared.MarkModified(true);
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!occupied);
		if (ImGui::Button("Erase"))
		{
			if (Tilemap2DRuntime::EraseCell(tilemap, brush.Coordinate))
				m_Shared.MarkModified(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Pick Cell") && occupied)
		{
			brush.SpriteHandle = occupied->SpriteHandle;
			brush.Tint = occupied->Tint;
			brush.FlipX = occupied->FlipX;
			brush.FlipY = occupied->FlipY;
			brush.RotationQuarterTurns = occupied->RotationQuarterTurns;
		}
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextDisabled("CELLS (%zu)", tilemap.Cells.size());
		std::optional<TilemapCell> editedCell;
		std::optional<glm::ivec2> removedCoordinate;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH
			| ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg
			| ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
		if (ImGui::BeginTable("##TilemapCells", 7, tableFlags,
			ImVec2(0.0f, std::min(260.0f,
				74.0f + static_cast<float>(tilemap.Cells.size()) * 27.0f))))
		{
			ImGui::TableSetupColumn("Cell", ImGuiTableColumnFlags_WidthFixed, 62.0f);
			ImGui::TableSetupColumn("Sprite", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableSetupColumn("Tint", ImGuiTableColumnFlags_WidthFixed, 54.0f);
			ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 28.0f);
			ImGui::TableSetupColumn("Rot", ImGuiTableColumnFlags_WidthFixed, 66.0f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 26.0f);
			ImGui::TableSetupScrollFreeze(1, 1);
			ImGui::TableHeadersRow();
			for (size_t cellIndex = 0; cellIndex < tilemap.Cells.size(); ++cellIndex)
			{
				const TilemapCell& source = tilemap.Cells[cellIndex];
				TilemapCell candidate = source;
				bool changed = false;
				ImGui::PushID(static_cast<int>(cellIndex));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%d, %d", source.Coordinate.x, source.Coordinate.y);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= DrawAnimatorSpriteField("CellSprite", candidate.SpriteHandle);
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= DrawColorField("##CellTint", glm::value_ptr(candidate.Tint),
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
				ImGui::TableSetColumnIndex(3);
				changed |= ImGui::Checkbox("##FlipX", &candidate.FlipX);
				ImGui::TableSetColumnIndex(4);
				changed |= ImGui::Checkbox("##FlipY", &candidate.FlipY);
				ImGui::TableSetColumnIndex(5);
				int rotation = std::clamp(candidate.RotationQuarterTurns, 0, 3);
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::Combo("##Rotation", &rotation, rotations, 4))
				{
					candidate.RotationQuarterTurns = rotation;
					changed = true;
				}
				ImGui::TableSetColumnIndex(6);
				if (ImGui::SmallButton("x"))
					removedCoordinate = source.Coordinate;
				if (changed)
					editedCell = std::move(candidate);
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		if (editedCell && Tilemap2DRuntime::SetCell(tilemap, std::move(*editedCell)))
			m_Shared.MarkModified(true);
		if (removedCoordinate
			&& Tilemap2DRuntime::EraseCell(tilemap, *removedCoordinate))
			m_Shared.MarkModified(true);
		ImGui::BeginDisabled(tilemap.Cells.empty());
		if (ImGui::Button("Clear All Cells", ImVec2(-1.0f, 0.0f)))
		{
			tilemap.Cells.clear();
			m_Shared.MarkModified(true);
		}
		ImGui::EndDisabled();
	}

	void SceneInspectorPanel::DrawTilemapRenderer2DInspector(
		TilemapRenderer2D& renderer)
	{
		bool changed = false;
		const char* sortOrders[] = {
			"Bottom Left", "Bottom Right", "Top Left", "Top Right"
		};
		int sortOrder = std::clamp(static_cast<int>(renderer.SortOrder), 0, 3);
		if (ImGui::Combo("Sort Order", &sortOrder, sortOrders,
			static_cast<int>(std::size(sortOrders))))
		{
			renderer.SortOrder = static_cast<TilemapSortOrder2D>(sortOrder);
			changed = true;
		}
		changed |= ImGui::InputInt("Sorting Layer", &renderer.SortingLayer);
		changed |= ImGui::InputInt("Order In Layer", &renderer.OrderInLayer);

		ImGui::BeginDisabled();
		int mode = static_cast<int>(renderer.Mode);
		const char* modes[] = { "Chunk", "Individual" };
		ImGui::Combo("Mode", &mode, modes, static_cast<int>(std::size(modes)));
		int culling = static_cast<int>(renderer.DetectChunkCulling);
		const char* cullingModes[] = { "Auto", "Manual" };
		ImGui::Combo("Detect Chunk Culling", &culling, cullingModes,
			static_cast<int>(std::size(cullingModes)));
		const std::string material = static_cast<uint64_t>(renderer.MaterialHandle) == 0
			? "None" : "Material #" + std::to_string(
				static_cast<uint64_t>(renderer.MaterialHandle));
		ImGui::Button((material + "###TilemapMaterial").c_str(), ImVec2(-1.0f, 0.0f));
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("renderer backend support pending");
		ImGui::TextDisabled("Mode, chunk culling, and Material: renderer backend support pending");
		if (changed)
			m_Shared.MarkModified();
	}

	void SceneInspectorPanel::DrawParticleSystem2DInspector(
		ParticleSystem2D& system)
	{
		ImGui::TextDisabled("PREVIEW");
		if (ImGui::Button(system.RuntimePlaying ? "Restart" : "Play"))
			ParticleSystem2DRuntime::Play(system, true);
		ImGui::SameLine();
		ImGui::BeginDisabled(!system.RuntimePlaying);
		if (ImGui::Button("Stop"))
			ParticleSystem2DRuntime::Stop(system, false);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(system.RuntimeParticles.empty());
		if (ImGui::Button("Clear"))
			ParticleSystem2DRuntime::Stop(system, true);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu / %d", system.RuntimeParticles.size(),
			std::max(system.MaxParticles, 0));

		bool changed = false;
		changed |= ImGui::Checkbox("Play On Start", &system.PlayOnStart);
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Loop", &system.Loop);
		float duration = system.Duration;
		if (ImGui::DragFloat("Duration", &duration, 0.05f, 0.0f, 0.0f, "%.2f s"))
		{
			if (std::isfinite(duration))
			{
				system.Duration = std::max(duration, 0.0f);
				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("EMISSION");
		float emissionRate = system.EmissionRate;
		if (ImGui::DragFloat("Rate", &emissionRate, 0.1f, 0.0f, 0.0f, "%.1f / s"))
		{
			if (std::isfinite(emissionRate))
			{
				system.EmissionRate = std::max(emissionRate, 0.0f);
				changed = true;
			}
		}
		int maxParticles = system.MaxParticles;
		if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 0, 100000))
		{
			system.MaxParticles = std::clamp(maxParticles, 0, 100000);
			if (system.RuntimeParticles.size()
				> static_cast<size_t>(system.MaxParticles))
				system.RuntimeParticles.resize(static_cast<size_t>(system.MaxParticles));
			changed = true;
		}
		float lifetime = system.StartLifetime;
		if (ImGui::DragFloat("Lifetime", &lifetime, 0.02f, 0.0001f, 0.0f, "%.2f s"))
		{
			if (std::isfinite(lifetime))
			{
				system.StartLifetime = std::max(lifetime, 0.0001f);
				changed = true;
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("SHAPE AND MOTION");
		float speed = system.StartSpeed;
		if (ImGui::DragFloat("Speed", &speed, 0.02f, 0.0f, 0.0f))
		{
			if (std::isfinite(speed))
			{
				system.StartSpeed = std::max(speed, 0.0f);
				changed = true;
			}
		}
		glm::vec2 direction = system.Direction;
		if (ImGui::DragFloat2("Direction", glm::value_ptr(direction), 0.02f)
			&& std::isfinite(direction.x) && std::isfinite(direction.y))
		{
			system.Direction = direction;
			changed = true;
		}
		float spread = system.SpreadDegrees;
		if (ImGui::SliderFloat("Spread", &spread, 0.0f, 360.0f, "%.0f deg"))
		{
			system.SpreadDegrees = spread;
			changed = true;
		}
		float gravity = system.GravityScale;
		if (ImGui::DragFloat("Gravity Scale", &gravity, 0.02f)
			&& std::isfinite(gravity))
		{
			system.GravityScale = gravity;
			changed = true;
		}

		ImGui::Separator();
		ImGui::TextDisabled("APPEARANCE");
		float startSize = system.StartSize;
		if (ImGui::DragFloat("Start Size", &startSize, 0.01f, 0.0f, 0.0f)
			&& std::isfinite(startSize))
		{
			system.StartSize = std::max(startSize, 0.0f);
			changed = true;
		}
		float endSize = system.EndSize;
		if (ImGui::DragFloat("End Size", &endSize, 0.01f, 0.0f, 0.0f)
			&& std::isfinite(endSize))
		{
			system.EndSize = std::max(endSize, 0.0f);
			changed = true;
		}
		changed |= DrawColorField("Start Color", glm::value_ptr(system.StartColor));
		changed |= DrawColorField("End Color", glm::value_ptr(system.EndColor));
		ImGui::TextUnformatted("Sprite");
		ImGui::SetNextItemWidth(-1.0f);
		changed |= DrawAnimatorSpriteField("ParticleSprite", system.SpriteHandle);
		changed |= ImGui::InputInt("Sorting Layer", &system.SortingLayer);
		changed |= ImGui::InputInt("Order In Layer", &system.OrderInLayer);
		uint32_t seed = system.Seed;
		if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &seed))
		{
			system.Seed = seed;
			changed = true;
		}
		if (changed)
		{
			ParticleSystem2DRuntime::Reset(system);
			m_Shared.MarkModified();
		}
	}

	void SceneInspectorPanel::DrawLight2DInspector(Light2D& light)
	{
		bool changed = false;
		const char* lightTypes[] = { "Global", "Point" };
		int lightType = static_cast<int>(light.Type);
		if (ImGui::Combo("Type", &lightType, lightTypes, 2))
		{
			light.Type = static_cast<Light2DType>(lightType);
			changed = true;
		}
		changed |= DrawColorField("Color", glm::value_ptr(light.Color));
		float intensity = light.Intensity;
		if (ImGui::DragFloat("Intensity", &intensity, 0.02f, 0.0f, 0.0f)
			&& std::isfinite(intensity))
		{
			light.Intensity = std::max(intensity, 0.0f);
			changed = true;
		}
		ImGui::BeginDisabled(light.Type == Light2DType::Global);
		float radius = light.Radius;
		if (ImGui::DragFloat("Radius", &radius, 0.05f, 0.0001f, 0.0f)
			&& std::isfinite(radius))
		{
			light.Radius = std::max(radius, 0.0001f);
			changed = true;
		}
		float falloff = light.Falloff;
		if (ImGui::DragFloat("Falloff", &falloff, 0.02f, 0.0001f, 0.0f)
			&& std::isfinite(falloff))
		{
			light.Falloff = std::max(falloff, 0.0001f);
			changed = true;
		}
		ImGui::EndDisabled();
		if (light.Type == Light2DType::Global)
			ImGui::TextDisabled("Radius and falloff apply to Point lights.");
		if (changed)
			m_Shared.MarkModified();
	}

	void SceneInspectorPanel::DrawUIButtonInspector(UIButton& button, Entity entity)
	{
		bool changed = ImGui::Checkbox("Interactable", &button.Interactable);
		int transition = 0;
		const char* transitions[] = { "Color Tint" };
		ImGui::BeginDisabled();
		ImGui::Combo("Transition", &transition, transitions,
			static_cast<int>(std::size(transitions)));
		const std::string targetGraphic = entity.HasComponent<UIImage>()
			? entity.GetName() + " (Image)" : "None (Image)";
		std::array<char, 256> targetGraphicBuffer{};
		std::copy_n(targetGraphic.data(),
			std::min(targetGraphic.size(), targetGraphicBuffer.size() - 1),
			targetGraphicBuffer.data());
		ImGui::InputText("Target Graphic", targetGraphicBuffer.data(),
			targetGraphicBuffer.size(), ImGuiInputTextFlags_ReadOnly);
		ImGui::EndDisabled();
		changed |= DrawColorField("Normal Color", glm::value_ptr(button.NormalColor));
		changed |= DrawColorField("Highlighted Color", glm::value_ptr(button.HoverColor));
		changed |= DrawColorField("Pressed Color", glm::value_ptr(button.PressedColor));
		changed |= DrawColorField("Selected Color", glm::value_ptr(button.SelectedColor));
		changed |= DrawColorField("Disabled Color", glm::value_ptr(button.DisabledColor));
		changed |= ImGui::SliderFloat("Color Multiplier", &button.ColorMultiplier,
			0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        if (changed) m_Shared.MarkModified();
        if(ImGui::TreeNode("Preview states"))
        {
            const char* names[]={"Normal","Hover","Pressed","Selected","Disabled"};
            const glm::vec4 colors[]={button.NormalColor,button.HoverColor,button.PressedColor,button.SelectedColor,button.DisabledColor};
            for(int i=0;i<5;++i)
            {
                const auto color=colors[i]*button.ColorMultiplier;
                ImGui::ColorButton(names[i],ImVec4(color.x,color.y,color.z,colors[i].a),0,ImVec2(48,24));
                ImGui::SameLine(); ImGui::TextUnformatted(names[i]);
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::TextUnformatted("On Click ()");
		std::optional<size_t> removeIndex;
		for (size_t index = 0; index < button.OnClick.size(); ++index)
		{
			auto& listener = button.OnClick[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::BeginGroup();
			if (ImGui::Checkbox("##Enabled", &listener.Enabled))
				m_Shared.MarkModified();
			ImGui::SameLine();

			Entity target;
			if (static_cast<uint64_t>(listener.TargetEntity) != 0 && m_Shared.Context)
				target = m_Shared.Context->FindEntityByUUID(listener.TargetEntity);
			const std::string targetLabel = target
				? target.GetName() + " (Entity)"
				: (static_cast<uint64_t>(listener.TargetEntity) == 0
					? "None (Entity)" : "Missing Entity");
			ImGui::SetNextItemWidth(-32.0f);
			if (ImGui::Button(targetLabel.c_str(), ImVec2(-32.0f, 0.0f)))
				ImGui::OpenPopup("OnClickTargetPicker");
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID))
				{
					Entity dropped = GetDraggedSceneEntity(payload, m_Shared.Context);
					if (dropped)
					{
						listener.TargetEntity = dropped.GetUUID();
						listener.TargetAttachmentID = UUID(0);
						listener.ScriptAsset = AssetHandle(0);
						listener.MethodName.clear();
						m_Shared.MarkModified();
					}
				}
				ImGui::EndDragDropTarget();
			}
			PrepareEditorPopup("OnClickTargetPicker",440);
        if (ImGui::BeginPopup("OnClickTargetPicker"))
			{
				if (ImGui::MenuItem("This Entity"))
				{
					listener.TargetEntity = entity.GetUUID();
					listener.TargetAttachmentID = UUID(0);
					listener.ScriptAsset = AssetHandle(0);
					listener.MethodName.clear();
					m_Shared.MarkModified();
				}
				ImGui::TextDisabled("Or drag an entity from Hierarchy");
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				listener.TargetEntity = UUID(0);
				listener.TargetAttachmentID = UUID(0);
				listener.ScriptAsset = AssetHandle(0);
				listener.MethodName.clear();
				m_Shared.MarkModified();
				target = {};
			}

			std::string selectedMethod = "No Function";
			if (!listener.MethodName.empty())
			{
				selectedMethod = "Missing: " + listener.MethodName;
				if (target && target.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						target.GetComponent<CSharpScripts>().Scripts)
					{
						if (script.AttachmentID != listener.TargetAttachmentID)
							continue;
						std::string className = script.LastKnownClassName;
						if (m_Shared.ScriptMetadata)
						{
							const auto metadata = m_Shared.ScriptMetadata(script.ScriptAsset);
							if (metadata && !metadata->TypeName.empty())
								className = metadata->TypeName;
						}
						selectedMethod = (className.empty() ? "Script" : className)
							+ "." + listener.MethodName;
						break;
					}
				}
			}

			ImGui::BeginDisabled(!target);
			if (ImGui::BeginCombo("##OnClickMethod", selectedMethod.c_str()))
			{
				if (ImGui::Selectable("No Function", listener.MethodName.empty()))
				{
					listener.TargetAttachmentID = UUID(0);
					listener.ScriptAsset = AssetHandle(0);
					listener.MethodName.clear();
					m_Shared.MarkModified();
				}
				if (target && target.HasComponent<CSharpScripts>())
				{
					for (const CSharpScriptEntry& script :
						target.GetComponent<CSharpScripts>().Scripts)
					{
						const auto metadata = m_Shared.ScriptMetadata
							? m_Shared.ScriptMetadata(script.ScriptAsset)
							: std::optional<EditorScriptMetadata>{};
						if (!metadata || metadata->EventMethods.empty())
							continue;
						const std::string className = metadata->TypeName.empty()
							? script.LastKnownClassName : metadata->TypeName;
						for (const std::string& method : metadata->EventMethods)
						{
							const std::string label = (className.empty() ? "Script" : className)
								+ "/" + method;
							const bool selected = listener.TargetAttachmentID
								== script.AttachmentID && listener.MethodName == method;
							if (ImGui::Selectable(label.c_str(), selected))
							{
								listener.TargetAttachmentID = script.AttachmentID;
								listener.ScriptAsset = script.ScriptAsset;
								listener.MethodName = method;
								m_Shared.MarkModified();
							}
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
				removeIndex = index;
			ImGui::EndGroup();
			ImGui::PopID();
		}
		if (removeIndex)
		{
			button.OnClick.erase(button.OnClick.begin()
				+ static_cast<std::ptrdiff_t>(*removeIndex));
			m_Shared.MarkModified();
		}
		if (ImGui::Button("+", ImVec2(-1.0f, 0.0f)))
		{
			button.OnClick.emplace_back();
			m_Shared.MarkModified();
		}
	}

	void SceneInspectorPanel::DrawCSharpScripts(Entity entity)
	{
        TC_PROFILE_SCOPE("Inspector script fields");
        const uint64_t revision = m_Shared.ScriptMetadataRevision ? m_Shared.ScriptMetadataRevision()
            : static_cast<uint64_t>(ImGui::GetTime() * 2);
        if (revision != m_MetadataRevision || m_MetadataScene != m_Shared.Context.get()) {
            m_MetadataRevision = revision; m_MetadataScene = m_Shared.Context.get();
            m_MetadataCache.clear(); m_ReconciledFields.clear();
        }

		if (!entity || !entity.HasComponent<CSharpScripts>())
			return;

		auto& scripts = entity.GetComponent<CSharpScripts>().Scripts;
		std::optional<size_t> removeIndex;
		std::optional<std::pair<size_t, size_t>> move;
		std::optional<CSharpScriptEntry> addition;
		for (size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex)
		{
			auto& entry = scripts[scriptIndex];
			const uint64_t attachmentID = static_cast<uint64_t>(entry.AttachmentID);
			ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(attachmentID)));

			const AssetMetadata* assetMetadata =
				AssetManager::Get().GetRegistry().GetMetadata(entry.ScriptAsset);
			const bool missing = static_cast<uint64_t>(entry.ScriptAsset) == 0 ||
				!assetMetadata || assetMetadata->IsMissing ||
				assetMetadata->Type != AssetType::CSharpScript;
			auto [cached, inserted] = m_MetadataCache.try_emplace(static_cast<uint64_t>(entry.ScriptAsset));
            if (inserted && !missing && m_Shared.ScriptMetadata) cached->second = m_Shared.ScriptMetadata(entry.ScriptAsset);
            const auto& metadata = cached->second;

			if (metadata && !metadata->TypeName.empty() &&
				entry.LastKnownClassName != metadata->TypeName)
			{
				entry.LastKnownClassName = metadata->TypeName;
				m_Shared.MarkModified();
			}
			if (metadata && m_Shared.ColliderEditingAllowed &&
                (!m_ReconciledFields.contains(attachmentID) || m_ReconciledFields[attachmentID] != entry.Fields.size())) {
                if (ReconcileScriptEntryFields(entry, *metadata)) m_Shared.MarkModified();
                m_ReconciledFields[attachmentID] = entry.Fields.size();
            }
			std::string className = metadata && !metadata->TypeName.empty()
				? metadata->TypeName : entry.LastKnownClassName;
			if (className.empty() && assetMetadata)
				className = PathToUTF8(assetMetadata->FilePath.stem());
			if (className.empty())
				className = "Unknown Script";
			const std::string header = ScriptDisplayName(className) + (missing ? " (Missing Script)" : " (Script)");
			if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f,0.34f,0.34f,1.0f));
			const bool open = DrawComponentHeader(header, m_Shared.Icons, EditorIcon::Script,
				&entry.Enabled, m_Shared.ColliderEditingAllowed, [this] {m_Shared.MarkModified();}, "ScriptSettings");
			if (missing) ImGui::PopStyleColor();
			if (ImGui::BeginPopup("ScriptSettings"))
			{
				const bool editable = m_Shared.ColliderEditingAllowed;
				if (ImGui::MenuItem("Reset", nullptr, false, editable && metadata.has_value())) {
					entry.Fields.clear();
					for (const auto& field : metadata->Fields)
						entry.Fields.emplace_back(field.FieldID, field.Name, field.Type, ScriptMetadataDefaultValue(field));
					entry.Enabled = true; m_Shared.MarkModified(true);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Remove Component", nullptr, false, editable)) removeIndex = scriptIndex;
				if (ImGui::MenuItem("Move Up", nullptr, false, editable && scriptIndex>0)) move = {scriptIndex,scriptIndex-1};
				if (ImGui::MenuItem("Move Down", nullptr, false, editable && scriptIndex+1<scripts.size())) move = {scriptIndex,scriptIndex+1};
				if (ImGui::MenuItem("Copy Component")) m_ScriptClipboard = entry;
				const bool sameScript = m_ScriptClipboard && m_ScriptClipboard->ScriptAsset == entry.ScriptAsset;
				if (ImGui::MenuItem("Paste Component As New", nullptr, false, editable && sameScript && metadata && !metadata->DisallowMultiple)) {
					addition = *m_ScriptClipboard; addition->AttachmentID = UUID();
					ReconcileScriptEntryFields(*addition, *metadata);
				}
				if (ImGui::MenuItem("Paste Component Values", nullptr, false, editable && sameScript && metadata)) {
					entry.Fields = m_ScriptClipboard->Fields; entry.Enabled = m_ScriptClipboard->Enabled;
					ReconcileScriptEntryFields(entry, *metadata); m_Shared.MarkModified(true);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Find References In Scene", nullptr, false, !missing)) {
					m_Shared.MultiSelection.clear();
					for (UUID id : m_Shared.Context->GetEntityOrder()) {
						auto candidate = m_Shared.Context->FindEntityByUUID(id);
						if (candidate.HasComponent<CSharpScripts>() && std::any_of(candidate.GetComponent<CSharpScripts>().Scripts.begin(),
							candidate.GetComponent<CSharpScripts>().Scripts.end(), [&](const auto& script){return script.ScriptAsset==entry.ScriptAsset;}))
							m_Shared.MultiSelection.push_back(id);
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Properties...", nullptr, false, !missing && bool(m_Shared.AssetReveal))) m_Shared.AssetReveal(entry.ScriptAsset);
				if (ImGui::MenuItem("Edit Script", nullptr, false, !missing && bool(m_Shared.ScriptOpen))) m_Shared.ScriptOpen(entry.ScriptAsset);
				ImGui::EndPopup();
			}

			if (open)
			{
				if (ImGui::BeginTable("ScriptReference", 2, ImGuiTableFlags_SizingStretchProp)) {
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
					ImGui::TableNextRow(); ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Script");
					ImGui::TableNextColumn();
					const ImVec2 fieldMin = ImGui::GetCursorScreenPos();
					const float height = ImGui::GetFrameHeight();
					const float width = std::max(height * 2.0f, ImGui::GetContentRegionAvail().x);
					const ImVec2 fieldMax(fieldMin.x + width, fieldMin.y + height);
					ImDrawList* draw = ImGui::GetWindowDrawList();
					draw->AddRectFilled(fieldMin, fieldMax, ImGui::GetColorU32(ImGuiCol_FrameBg), ImGui::GetStyle().FrameRounding);
					draw->AddRect(fieldMin, fieldMax, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
					ImGui::InvisibleButton("##ScriptReference", ImVec2(width - height, height));
					if (ImGui::IsItemHovered()) {
						if (assetMetadata) ImGui::SetTooltip("%s\nDouble-click to edit. Use the circle to reveal the script asset.", PathToUTF8(assetMetadata->FilePath).c_str());
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !missing && m_Shared.ScriptOpen) m_Shared.ScriptOpen(entry.ScriptAsset);
					}
					const float iconSize = ImGui::GetFontSize();
					const float inset = (height - iconSize) * 0.5f;
					DrawIcon(m_Shared.Icons, EditorIcon::Script,
						ImVec2(fieldMin.x + 4, fieldMin.y + inset), ImVec2(fieldMin.x + 4 + iconSize, fieldMin.y + inset + iconSize));
					const std::string reference = (missing ? "Missing: " : "") + className;
					draw->PushClipRect(fieldMin, ImVec2(fieldMax.x - height, fieldMax.y), true);
					draw->AddText(ImVec2(fieldMin.x + iconSize + 8, fieldMin.y + inset), ImGui::GetColorU32(ImGuiCol_TextDisabled), reference.c_str());
					draw->PopClipRect();
					ImGui::SameLine(0, 0);
					ImGui::BeginDisabled(missing || !m_Shared.AssetReveal);
					if (ImGui::InvisibleButton("##RevealScript", ImVec2(height, height))) m_Shared.AssetReveal(entry.ScriptAsset);
					const ImVec2 center(fieldMax.x - height * 0.5f, fieldMin.y + height * 0.5f);
					const ImU32 pickerColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
					draw->AddCircle(center, height * 0.22f, pickerColor, 16, 1.5f);
					draw->AddCircleFilled(center, height * 0.08f, pickerColor);
					ImGui::EndDisabled();
					ImGui::EndTable();
				}
				ImGui::BeginDisabled(!m_Shared.ColliderEditingAllowed);
				if (missing)
					ImGui::TextWrapped("The script asset is missing or no longer resolves to a C# script. Stored values are preserved.");
				else if (!metadata)
					ImGui::TextDisabled("Managed metadata is unavailable; stored fields are shown as orphans.");

				std::vector<bool> consumed(entry.Fields.size(), false);
				if (metadata)
				{
					std::string lastHeader;
					for (const EditorScriptFieldMetadata& fieldMetadata : metadata->Fields)
					{
						size_t matchedIndex = entry.Fields.size();
						for (size_t fieldIndex = 0; fieldIndex < entry.Fields.size(); ++fieldIndex)
						{
							if (fieldIndex < consumed.size() && consumed[fieldIndex])
								continue;
							const ScriptField& stored = entry.Fields[fieldIndex];
							const bool idMatches = !fieldMetadata.FieldID.empty() &&
								stored.FieldID == fieldMetadata.FieldID;
							const bool nameMatches = stored.Name == fieldMetadata.Name ||
								std::find(fieldMetadata.FormerNames.begin(),
									fieldMetadata.FormerNames.end(), stored.Name) !=
									fieldMetadata.FormerNames.end();
							if ((idMatches || nameMatches) &&
								IsScriptFieldValueCompatible(fieldMetadata.Type, stored.Value))
							{
								matchedIndex = fieldIndex;
								break;
							}
						}

						if (matchedIndex != entry.Fields.size())
							consumed[matchedIndex] = true;
						if (fieldMetadata.Hidden)
							continue;
						if (!fieldMetadata.Header.empty() && fieldMetadata.Header != lastHeader)
						{
							ImGui::Spacing();
							ImGui::Separator();
							ImGui::TextDisabled("%s", fieldMetadata.Header.c_str());
							lastHeader = fieldMetadata.Header;
						}

						if (matchedIndex != entry.Fields.size())
						{
							if (DrawScriptFieldValue(entry.Fields[matchedIndex],
								&fieldMetadata, false, &m_Shared))
								m_Shared.MarkModified();
						}
						else
						{
							ScriptField pending(fieldMetadata.FieldID, fieldMetadata.Name,
								fieldMetadata.Type,
								ScriptMetadataDefaultValue(fieldMetadata));
							if (DrawScriptFieldValue(pending, &fieldMetadata, false, &m_Shared))
							{
								entry.Fields.push_back(std::move(pending));
								consumed.push_back(true);
								m_Shared.MarkModified();
							}
						}
					}
				}

				bool drewOrphanHeader = false;
				for (size_t fieldIndex = 0; fieldIndex < entry.Fields.size(); ++fieldIndex)
				{
					if (fieldIndex < consumed.size() && consumed[fieldIndex])
						continue;
					if (!drewOrphanHeader)
					{
						ImGui::Spacing();
						ImGui::Separator();
						ImGui::TextDisabled("Orphaned serialized fields");
						drewOrphanHeader = true;
					}
					if (DrawScriptFieldValue(entry.Fields[fieldIndex], nullptr, true, &m_Shared))
						m_Shared.MarkModified();
				}
				ImGui::EndDisabled();
			}
			ImGui::PopID();
		}

		if (move) { std::swap(scripts[move->first], scripts[move->second]); m_Shared.MarkModified(true); }
		if (addition) { scripts.push_back(std::move(*addition)); m_Shared.MarkModified(true); }
		if (removeIndex && *removeIndex < scripts.size())
		{
			scripts.erase(scripts.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
			m_Shared.MarkModified();
			if (scripts.empty())
				entity.RemoveComponent<CSharpScripts>();
		}
	}


    void SceneInspectorPanel::DrawMultiSelectionInspector()
    {
        std::vector<Entity> entities;
        for(UUID id:m_Shared.MultiSelection) if(Entity e=m_Shared.Context->FindEntityByUUID(id)) entities.push_back(e);
        if(entities.empty()) return;
        ImGui::Text("%zu objects selected",entities.size());
        ImGui::TextWrapped("Ctrl-click to change selection. Editing a common property replaces that value on every selected object. Mixed values are marked with a dash.");
        for(const auto& descriptor:ComponentRegistry::Get().GetDescriptors())
        {
            if(!descriptor.InspectorVisible || descriptor.Properties.empty()) continue;
            bool common=true;
            for(Entity e:entities) if(!descriptor.Has(e)) { common=false; break; }
            if(common) DrawRegisteredComponent(descriptor,entities.front(),[this](){m_Shared.MarkModified();},m_Shared.ColliderEditingAllowed,&entities);
        }
    }

	void SceneInspectorPanel::DrawComponents(Entity entity)
{
	// 检查实体是否有效
	if (!entity)
		return;

	ImGui::PushID(reinterpret_cast<void*>(
		static_cast<uintptr_t>(static_cast<uint64_t>(entity.GetUUID()))));
	if (entity.HasComponent<Tag>())
	{
		auto& tagComponent = entity.GetComponent<Tag>();
		auto& tag = tagComponent._Tag;
		if (m_Shared.NameEditEntity != entity)
		{
			m_Shared.NameEditEntity = entity;
			std::snprintf(m_Shared.NameEditBuffer, sizeof(m_Shared.NameEditBuffer), "%s", tag.c_str());
		}

	    if (entity.HasComponent<PrefabLink>())
    {
        const auto& link = entity.GetComponent<PrefabLink>();
        const auto* metadata = AssetManager::Get().GetRegistry().GetMetadata(link.Source);
        ImGui::Separator();
        ImGui::TextWrapped("Prefab: %s", metadata ? PathToUTF8(metadata->FilePath).c_str() : "Missing source");
        if (metadata && ImGui::SmallButton("Select source") && m_Shared.AssetReveal) m_Shared.AssetReveal(link.Source);
        if (ImGui::TreeNode("Overrides"))
        {
            static UUID cachedRoot{0}; static Scene* cachedScene=nullptr; static double refreshed=0;
            static std::vector<std::string> paths;
            static std::vector<PrefabPropertyOverride> properties;
            static std::string error;
            if(cachedRoot!=entity.GetUUID() || cachedScene!=m_Shared.Context.get() || (m_Shared.PrefabOverridesDirty && ImGui::GetTime()-refreshed>0.5))
            {
                cachedRoot=entity.GetUUID(); cachedScene=m_Shared.Context.get(); refreshed=ImGui::GetTime(); m_Shared.PrefabOverridesDirty=false;
                PrefabLinkedInstance::GetOverridePaths(m_Shared.Context,entity.GetUUID(),paths,error);
                if(error.empty()) PrefabLinkedInstance::GetPropertyOverrides(m_Shared.Context,entity.GetUUID(),properties,error);
            }
            if(ImGui::SmallButton("Refresh differences")) m_Shared.PrefabOverridesDirty=true;
            if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());
            if(paths.empty()) ImGui::TextDisabled("No overrides. This instance matches its source.");
            else
            {
                ImGui::Text("%zu changed paths",paths.size());
                auto text=[](const PropertyValue& value) {
                    return std::visit([](const auto& item)->std::string {
                        using T=std::decay_t<decltype(item)>;
                        if constexpr(std::is_same_v<T,std::string>) return item;
                        else if constexpr(std::is_same_v<T,bool>) return item?"true":"false";
                        else if constexpr(std::is_arithmetic_v<T>) return std::to_string(item);
                        else { std::string result; for(int i=0;i<item.length();++i) { if(i) result+=", "; result+=std::to_string(item[i]); } return result; }
                    },value);
                };
                ImGui::BeginChild("PropertyOverrides",ImVec2(0,230),true);
                for(size_t i=0;i<properties.size();++i)
                {
                    const auto& item=properties[i]; ImGui::PushID(static_cast<int>(i));
                    ImGui::TextWrapped("%s",item.Path.c_str());
                    ImGui::TextWrapped("Source: %s",text(item.Before).c_str());
                    ImGui::TextWrapped("Instance: %s",text(item.After).c_str());
                    ImGui::BeginDisabled(!m_Shared.PrefabCreationAllowed || !m_Shared.PrefabAction);
                    auto queue=[&](int action) { m_Shared.PendingPrefabRoot=entity.GetUUID(); m_Shared.PendingPrefabAction=action; m_Shared.PendingPrefabEntity=item.EntityID; m_Shared.PendingPrefabComponent=item.ComponentID; m_Shared.PendingPrefabProperty=item.PropertyID; };
                    if(ImGui::SmallButton("Apply property")) queue(5);
                    ImGui::SameLine(); if(ImGui::SmallButton("Revert property")) queue(6);
                    ImGui::EndDisabled(); ImGui::Separator(); ImGui::PopID();
                }
                if(ImGui::TreeNode("All changes (including structure)")) { for(const auto& path:paths) ImGui::TextWrapped("%s",path.c_str()); ImGui::TreePop(); }
                ImGui::EndChild();
            }
            ImGui::BeginDisabled(!m_Shared.PrefabCreationAllowed || !m_Shared.PrefabAction);
            if (ImGui::Button("Update")) { m_Shared.PendingPrefabRoot = entity.GetUUID(); m_Shared.PendingPrefabAction = 0; }
            ImGui::SameLine();
            if (ImGui::Button("Apply All")) ImGui::OpenPopup("ApplyPrefabOverrides");
            ImGui::SameLine();
            if (ImGui::Button("Revert All")) ImGui::OpenPopup("RevertPrefabOverrides");
            PrepareEditorPopup("ApplyPrefabOverrides",480);
        if (ImGui::BeginPopup("ApplyPrefabOverrides"))
            {
                ImGui::TextWrapped("Write all overrides to the source prefab. Other linked instances can receive these changes.");
                if (ImGui::Button("Apply to source")) { m_Shared.PendingPrefabRoot = entity.GetUUID(); m_Shared.PendingPrefabAction = 2; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            PrepareEditorPopup("RevertPrefabOverrides",480);
        if (ImGui::BeginPopup("RevertPrefabOverrides"))
            {
                ImGui::TextWrapped("Replace this instance's overrides with its source values.");
                if (ImGui::Button("Revert instance")) { m_Shared.PendingPrefabRoot = entity.GetUUID(); m_Shared.PendingPrefabAction = 1; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }
	if (entity.HasComponent<EntityMetadata>())
		{
			if (DrawEntityIconSelector(m_Shared.Icons, entity, m_Shared.ColliderEditingAllowed))
				m_Shared.MarkModified();
			ImGui::SameLine(0.0f, 4.0f);
		}
		if (DrawCompactCheckbox("##ActiveSelf", &tagComponent.ActiveSelf))
			m_Shared.MarkModified();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Active Self controls gameplay scripts, rendering, audio, and physics for this Entity and its descendants.");
		ImGui::SameLine(0.0f, 5.0f);
		ImGui::SetNextItemWidth(-1.0f);
		const bool committed = ImGui::InputText("##Name", m_Shared.NameEditBuffer, sizeof(m_Shared.NameEditBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue);
		if (committed || ImGui::IsItemDeactivatedAfterEdit())
		{
			if (m_Shared.Context->RenameEntity(entity, m_Shared.NameEditBuffer))
				m_Shared.MarkModified();
			std::snprintf(m_Shared.NameEditBuffer, sizeof(m_Shared.NameEditBuffer), "%s", tag.c_str());
		}

	}

	if (entity.HasComponent<EntityMetadata>())
	{
		auto& metadata = entity.GetComponent<EntityMetadata>();
		const ProjectSettings* projectSettings = m_Shared.Project ? &m_Shared.Project->GetSettings() : nullptr;
		const bool metadataEditable = m_Shared.ColliderEditingAllowed && projectSettings != nullptr;
		ImGui::PushID("EntityMetadata");

		bool tagDefined = false;
		if (projectSettings)
		{
			const auto& projectTags = projectSettings->TagsAndLayers.Tags;
			tagDefined = std::find(projectTags.begin(), projectTags.end(), metadata.GameplayTag)
				!= projectTags.end();
		}
		std::string tagPreview = metadata.GameplayTag.empty() ? "<Empty>" : metadata.GameplayTag;
		if (projectSettings && !tagDefined)
			tagPreview += " (Undefined)";

		const uint8_t currentLayer = metadata.Layer;
		bool layerDefined = false;
		std::string layerPreview = "Layer " + std::to_string(static_cast<unsigned int>(currentLayer));
		if (projectSettings && currentLayer < Physics2DLayerCount)
		{
			const std::string& layerName = projectSettings->TagsAndLayers.LayerNames[currentLayer];
			if (!layerName.empty())
			{
				layerDefined = true;
				layerPreview = layerName;
			}
		}
		if (projectSettings && !layerDefined)
			layerPreview += " (Undefined)";

		const bool stackMetadataRows = ImGui::GetContentRegionAvail().x < 330.0f;
		const int metadataColumnCount = stackMetadataRows ? 2 : 4;
		const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchProp
			| ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX;
		if (ImGui::BeginTable("##TagLayer", metadataColumnCount, tableFlags))
		{
			const float labelWidth = ImGui::CalcTextSize("Layer").x
				+ ImGui::GetStyle().ItemInnerSpacing.x;
			ImGui::TableSetupColumn("TagLabel", ImGuiTableColumnFlags_WidthFixed, labelWidth);
			ImGui::TableSetupColumn("TagValue", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			if (!stackMetadataRows)
			{
				ImGui::TableSetupColumn("LayerLabel", ImGuiTableColumnFlags_WidthFixed, labelWidth);
				ImGui::TableSetupColumn("LayerValue", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			}
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Tag");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::BeginDisabled(!metadataEditable);
			if (ImGui::BeginCombo("##GameplayTag", tagPreview.c_str()))
			{
				if (!tagDefined)
					ImGui::Selectable(tagPreview.c_str(), true, ImGuiSelectableFlags_Disabled);
				if (projectSettings)
				{
					for (const std::string& projectTag : projectSettings->TagsAndLayers.Tags)
					{
						const bool selected = metadata.GameplayTag == projectTag;
						if (ImGui::Selectable(projectTag.c_str(), selected) && !selected)
						{
							metadata.GameplayTag = projectTag;
							m_Shared.MarkModified();
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			const bool tagHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();

			if (stackMetadataRows)
				ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(stackMetadataRows ? 0 : 2);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Layer");
			ImGui::TableSetColumnIndex(stackMetadataRows ? 1 : 3);
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::BeginDisabled(!metadataEditable);
			if (ImGui::BeginCombo("##EntityLayer", layerPreview.c_str()))
			{
				if (!layerDefined)
					ImGui::Selectable(layerPreview.c_str(), true, ImGuiSelectableFlags_Disabled);
				if (projectSettings)
				{
					for (uint8_t layer = 0; layer < Physics2DLayerCount; ++layer)
					{
						const std::string& layerName = projectSettings->TagsAndLayers.LayerNames[layer];
						if (layerName.empty())
							continue;
						const bool selected = metadata.Layer == layer;
						if (ImGui::Selectable(layerName.c_str(), selected) && !selected)
						{
							metadata.Layer = layer;
							m_Shared.MarkModified();
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			const bool layerHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
			ImGui::EndDisabled();

			if (!metadataEditable && (tagHovered || layerHovered))
			{
				ImGui::SetTooltip("%s", m_Shared.ColliderEditingAllowed
					? "Open a project to choose a Tag or Layer."
					: "Tag and Layer are read-only while the scene is running.");
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
	else
	{
		ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
			"Entity metadata is missing.");
	}
	ImGui::PopID();


		const auto onModified = [this]() { m_Shared.MarkModified(); };
		// Rich editor adapters stay in the Editor, while descriptor enumeration is
		// the sole dispatch source. Third-party descriptors need no adapter: their
		// property metadata is rendered by DrawRegisteredComponent below.
		std::unordered_map<uint64_t, std::function<void()>> richInspectors;
		richInspectors.emplace(ComponentIds::Transform, [&]()
		{
		DrawComponent<Transform>("Transform", entity, m_Shared.Icons, EditorIcon::Move,
			[this, entity](auto& component)
		{
			Entity parent = m_Shared.Context ? m_Shared.Context->GetParent(entity) : Entity{};
			const bool hasParent = (bool)parent;
			glm::vec3 translation = hasParent ? component._LocalTranslation : component._Translation;
			glm::vec3 rotation = glm::degrees(hasParent ? component._LocalRotation : component._Rotation);
			glm::vec3 scale = hasParent ? component._LocalScale : component._Scale;

			bool changed = DrawVec3Control(hasParent ? "Local Translation" : "Translation", translation);
			changed |= DrawVec3Control(hasParent ? "Local Rotation" : "Rotation", rotation);
			changed |= DrawVec3Control(hasParent ? "Local Scale" : "Scale", scale, 1.0f);
			if (changed && m_Shared.Context)
			{
				const glm::mat4 transform = Math::ComposeTransform(translation, glm::radians(rotation), scale);
				if (hasParent)
				{
					if (m_Shared.Context->SetLocalTransform(entity, transform))
						m_Shared.MarkModified();
				}
				else if (m_Shared.Context->SetWorldTransform(entity, transform))
				{
					m_Shared.MarkModified();
				}
			}
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Camera, [&]()
		{
		DrawComponent<C_Camera>("Camera", entity, m_Shared.Icons, EditorIcon::Camera,
			[this](auto& component)
		{
			auto& camera = component._Camera;
			const float columnWidth = 100.0f;

			DrawProperty("Projection", columnWidth);
			const char* projectionTypes[] = { "Perspective", "Orthographic" };
			int projection = (int)camera.GetProjectionType();
			if (ImGui::BeginCombo("##Projection", projectionTypes[projection]))
			{
				for (int i = 0; i < 2; ++i)
				{
					const bool selected = projection == i;
					if (ImGui::Selectable(projectionTypes[i], selected))
					{
						if (projection != i && camera.SetProjectionType((SceneCamera::ProjectionType)i))
							m_Shared.MarkModified();
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Columns(1);

			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				DrawProperty("Vertical FOV", columnWidth);
				float value = glm::degrees(camera.GetPerspectiveVerticalFOV());
				if (ImGui::DragFloat("##VerticalFOV", &value) && camera.SetPerspectiveVerticalFOV(glm::radians(value))) m_Shared.MarkModified();
				ImGui::Columns(1);
				DrawProperty("Near", columnWidth);
				value = camera.GetPerspectiveNearClip();
				if (ImGui::DragFloat("##PerspectiveNear", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetPerspectiveNearClip(value)) m_Shared.MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetPerspectiveFarClip();
				if (ImGui::DragFloat("##PerspectiveFar", &value, 0.1f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetPerspectiveFarClip(value)) m_Shared.MarkModified();
				ImGui::Columns(1);
			}
			else
			{
				DrawProperty("Size", columnWidth);
				float value = camera.GetOrthographicSize();
				if (ImGui::DragFloat("##OrthoSize", &value) && camera.SetOrthographicSize(value)) m_Shared.MarkModified();
				ImGui::Columns(1);
				DrawProperty("Near", columnWidth);
				value = camera.GetOrthographicNearClip();
				if (ImGui::DragFloat("##OrthoNear", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetOrthographicNearClip(value)) m_Shared.MarkModified();
				ImGui::Columns(1);
				DrawProperty("Far", columnWidth);
				value = camera.GetOrthographicFarClip();
				if (ImGui::DragFloat("##OrthoFar", &value, 0.01f,
					0.0f, 0.0f, "%.3f")
					&& camera.SetOrthographicFarClip(value)) m_Shared.MarkModified();
				ImGui::Columns(1);
			}

			DrawProperty("Fixed Aspect Ratio", columnWidth);
			if (ImGui::Checkbox("##FixedAspectRatio", &component.FixedAspectRatio)) m_Shared.MarkModified();
			ImGui::Columns(1);
			DrawProperty("Background Color", columnWidth);
			if (DrawColorField("##BackgroundColor", glm::value_ptr(component.BackgroundColor))) m_Shared.MarkModified();
			ImGui::Columns(1);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::SpriteRenderer, [&]()
		{
		DrawComponent<SpriteRenderer>("Sprite Renderer", entity, m_Shared.Icons, EditorIcon::Sprite,
			[this, entity](auto& component)
		{
			const float columnWidth = 100.0f;
			DrawProperty("Color", columnWidth);
			if (DrawColorField("##Color", glm::value_ptr(component._Color))) m_Shared.MarkModified();
			ImGui::Columns(1);

			DrawProperty("Sprite", columnWidth);
			std::string spriteName = "None";
			EditorIcon spriteFieldIcon = EditorIcon::Sprite;
			Ref<Texture2D> spritePreview;
			const uint64_t rawSpriteHandle = static_cast<uint64_t>(component.SpriteHandle);
			if (rawSpriteHandle != 0)
			{
				if (const BuiltInSpriteAsset* builtIn =
					FindBuiltInSpriteAsset(component.SpriteHandle))
				{
					spriteName = std::string(builtIn->Name);
					component.Sprite = AssetManager::Get().LoadTexture(component.SpriteHandle);
					spritePreview = component.Sprite;
				}
				else
				{
					AssetRegistry& registry = AssetManager::Get().GetRegistry();
					const AssetMetadata* metadata = registry.GetMetadata(component.SpriteHandle);
					const AssetSubAsset* subSprite = nullptr;
					if (!metadata)
						metadata = registry.GetSubAssetOwner(component.SpriteHandle, &subSprite);
					if (metadata && IsSpriteAsset(*metadata))
					{
						spriteName = subSprite ? subSprite->Name
							: PathToUTF8(metadata->FilePath.stem());
						if (metadata->IsMissing)
						{
							spriteName += " (Missing)";
							spriteFieldIcon = EditorIcon::Missing;
						}
						else
						{
							component.Sprite = AssetManager::Get().LoadTexture(component.SpriteHandle);
							spritePreview = component.Sprite;
						}
					}
					else if (metadata)
					{
						spriteName = PathToUTF8(metadata->FilePath.stem()) + " (Not a Sprite)";
						spriteFieldIcon = EditorIcon::Missing;
					}
					else
					{
						spriteName = "Missing #" + std::to_string(rawSpriteHandle);
						spriteFieldIcon = EditorIcon::Missing;
					}
				}
			}

			const float pickerButtonWidth = ImGui::GetFrameHeight();
			const float fieldWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x -
				pickerButtonWidth - ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Button("##SpriteAssetField", ImVec2(fieldWidth, 0.0f));
			const bool revealSprite = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			if (ImGui::IsItemHovered() && rawSpriteHandle != 0)
				ImGui::SetTooltip("Show in Project");
			const ImVec2 spriteButtonMin = ImGui::GetItemRectMin();
			const ImVec2 spriteButtonMax = ImGui::GetItemRectMax();
			const float spriteIconSize = std::min(16.0f,
				std::max(1.0f, spriteButtonMax.y - spriteButtonMin.y - 4.0f));
			const ImVec2 spriteIconMin(spriteButtonMin.x + 6.0f,
				spriteButtonMin.y + (spriteButtonMax.y - spriteButtonMin.y - spriteIconSize) * 0.5f);
			const ImVec2 spriteIconMax(spriteIconMin.x + spriteIconSize, spriteIconMin.y + spriteIconSize);
			if (spritePreview)
				ImGui::GetWindowDrawList()->AddImage(ToImGuiTextureID(spritePreview),
					spriteIconMin, spriteIconMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
			else
				DrawIcon(m_Shared.Icons, spriteFieldIcon, spriteIconMin, spriteIconMax);
			const ImRect spriteTextClip(
				ImVec2(spriteIconMax.x + 5.0f, spriteButtonMin.y),
				ImVec2(spriteButtonMax.x - 4.0f, spriteButtonMax.y));
			ImGui::RenderTextClipped(spriteTextClip.Min, spriteTextClip.Max,
				spriteName.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.5f), &spriteTextClip);
			if (revealSprite && rawSpriteHandle != 0 && m_Shared.AssetReveal)
				m_Shared.AssetReveal(component.SpriteHandle);

			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = nullptr;
						const AssetSubAsset* subSprite = nullptr;
						if (ResolveSelectableSprite(handle, metadata, subSprite))
						{
							component.SpriteHandle = handle;
							component.Sprite = AssetManager::Get().LoadTexture(handle);
							m_Shared.MarkModified();
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("SpriteAssetContext"))
			{
				if (ImGui::MenuItem("Clear", nullptr, false, rawSpriteHandle != 0))
				{
					component.SpriteHandle = AssetHandle(0);
					component.Sprite.reset();
					m_Shared.MarkModified();
				}
				ImGui::EndPopup();
			}

			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			ImGui::Button("##OpenSpritePicker", ImVec2(pickerButtonWidth, 0.0f));
			const bool openSpritePicker = ImGui::IsItemClicked();
			const ImVec2 pickerMin = ImGui::GetItemRectMin();
			const ImVec2 pickerMax = ImGui::GetItemRectMax();
			const ImVec2 pickerCenter((pickerMin.x + pickerMax.x) * 0.5f,
				(pickerMin.y + pickerMax.y) * 0.5f);
			const ImU32 pickerGlyph = ImGui::GetColorU32(ImGuiCol_Text);
			const float pickerRadius = std::max(3.0f,
				std::min(pickerMax.x - pickerMin.x, pickerMax.y - pickerMin.y) * 0.28f);
			ImGui::GetWindowDrawList()->AddCircle(pickerCenter, pickerRadius, pickerGlyph, 16, 1.5f);
			ImGui::GetWindowDrawList()->AddCircleFilled(pickerCenter,
				std::max(1.0f, pickerRadius * 0.28f), pickerGlyph, 12);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Select Sprite");
			if (openSpritePicker)
			{
				m_Shared.SpriteSearch.fill('\0');
				m_Shared.SpritePickerOpen = true;
				m_Shared.SpritePickerEntity = entity.GetUUID();
				ImGui::OpenPopup("Select Sprite##SpritePicker");
			}
			if (m_Shared.SpritePickerOpen && m_Shared.SpritePickerEntity != entity.GetUUID())
				m_Shared.SpritePickerOpen = false;

			PrepareEditorToolWindow(ImVec2(760,520),ImVec2(420,300));
			if (ImGui::BeginPopupModal("Select Sprite##SpritePicker", &m_Shared.SpritePickerOpen,
				ImGuiWindowFlags_NoCollapse))
			{
				if (ImGui::IsWindowAppearing())
					ImGui::SetKeyboardFocusHere();
				ImGui::SetNextItemWidth(-1.0f);
				EditorSearchField("##SpriteSearch", "Search Sprites",
					m_Shared.SpriteSearch.data(), m_Shared.SpriteSearch.size());

				std::vector<const AssetMetadata*> sprites;
				for (const auto& [handle, metadata] :
					AssetManager::Get().GetRegistry().GetAssets())
				{
					if (IsSpriteAsset(metadata) && !metadata.IsMissing)
						sprites.push_back(&metadata);
				}
				std::sort(sprites.begin(), sprites.end(), [](const AssetMetadata* left,
					const AssetMetadata* right)
				{
					return LowerASCII(PathToUTF8(left->FilePath)) <
						LowerASCII(PathToUTF8(right->FilePath));
				});

				size_t spriteCount = 0;
				const std::string spriteQuery = LowerASCII(m_Shared.SpriteSearch.data());
				for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				{
					if (spriteQuery.empty()
						|| LowerASCII(std::string(builtIn.Name)).find(spriteQuery)
							!= std::string::npos)
						++spriteCount;
				}
				for (const AssetMetadata* metadata : sprites)
				{
					if (MatchesSpriteSearch(*metadata, m_Shared.SpriteSearch.data()))
						++spriteCount;
					for (const AssetSubAsset& child : metadata->SubAssets)
						if (child.Type == AssetType::Texture2D
							&& (spriteQuery.empty() || LowerASCII(child.Name).find(spriteQuery)
								!= std::string::npos))
							++spriteCount;
				}
				ImGui::TextDisabled("%zu Sprite%s", spriteCount, spriteCount == 1 ? "" : "s");
				ImGui::Separator();
				ImGui::BeginChild("SpriteGrid", ImVec2(0.0f, 0.0f), false);
				constexpr float cellWidth = 96.0f;
				constexpr float cellHeight = 112.0f;
				const int columns = std::max(1, static_cast<int>(
					ImGui::GetContentRegionAvail().x / cellWidth));
				ImGui::Columns(columns, "SpritePickerColumns", false);

				auto drawSpriteTile = [&](AssetHandle handle, const char* label,
					const std::filesystem::path* relativePath)
				{
					const std::string id = "##Sprite_" +
						std::to_string(static_cast<uint64_t>(handle));
					const float tileWidth = std::max(64.0f,
						ImGui::GetColumnWidth() - ImGui::GetStyle().ItemSpacing.x);
					const ImVec2 tileSize(tileWidth, cellHeight);
					const ImVec2 tileMin = ImGui::GetCursorScreenPos();
					ImGui::InvisibleButton(id.c_str(), tileSize);
					const bool hovered = ImGui::IsItemHovered();
					const bool clicked = ImGui::IsItemClicked();
					const bool selected = component.SpriteHandle == handle;
					const ImVec2 tileMax(tileMin.x + tileSize.x, tileMin.y + tileSize.y);
					const bool visible = ImGui::IsRectVisible(tileMin, tileMax);
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					const ImU32 background = ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive :
						hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
					if (visible)
						drawList->AddRectFilled(tileMin, tileMax, background, 2.0f);

					constexpr float previewPadding = 8.0f;
					const float previewExtent = std::max(1.0f,
						std::min(tileSize.x - previewPadding * 2.0f, 76.0f));
					const ImVec2 previewMin(tileMin.x + (tileSize.x - previewExtent) * 0.5f,
						tileMin.y + previewPadding);
					const ImVec2 previewMax(previewMin.x + previewExtent,
						previewMin.y + previewExtent);
					if (visible && static_cast<uint64_t>(handle) != 0)
					{
						const Ref<Texture2D> texture = AssetManager::Get().LoadTexture(handle);
						if (texture)
						{
							ImVec2 uvMin(0.0f, 1.0f);
							ImVec2 uvMax(1.0f, 0.0f);
							ResolvedSpriteAsset resolved;
							SpriteRenderGeometry geometry;
							if (AssetManager::Get().ResolveSpriteAsset(handle, resolved)
								&& resolved.IsSubAsset
								&& BuildSpriteRenderGeometry(resolved.Data,
									texture->GetWidth(), texture->GetHeight(), geometry))
							{
								uvMin = ImVec2(geometry.UMin, geometry.VMax);
								uvMax = ImVec2(geometry.UMax, geometry.VMin);
							}
							drawList->AddImage(ToImGuiTextureID(texture), previewMin,
								previewMax, uvMin, uvMax);
						}
					}
					else if (visible)
						DrawIcon(m_Shared.Icons, EditorIcon::Sprite, previewMin, previewMax,
							ImGui::GetColorU32(ImGuiCol_TextDisabled));

					const ImRect labelClip(
						ImVec2(tileMin.x + 4.0f, previewMax.y + 5.0f),
						ImVec2(tileMin.x + tileSize.x - 4.0f, tileMin.y + tileSize.y - 4.0f));
					if (visible)
						ImGui::RenderTextClipped(labelClip.Min, labelClip.Max, label, nullptr,
							nullptr, ImVec2(0.5f, 0.0f), &labelClip);
					if (hovered && relativePath)
						ImGui::SetTooltip("%s", PathToUTF8(*relativePath).c_str());
					if (clicked)
					{
						if (component.SpriteHandle != handle)
						{
							component.SpriteHandle = handle;
							component.Sprite = static_cast<uint64_t>(handle) != 0
								? AssetManager::Get().LoadTexture(handle) : Ref<Texture2D>{};
							m_Shared.MarkModified();
						}
						m_Shared.SpritePickerOpen = false;
						m_Shared.SpritePickerEntity = UUID(0);
						ImGui::CloseCurrentPopup();
					}
					ImGui::NextColumn();
				};

				if (m_Shared.SpriteSearch[0] == '\0')
					drawSpriteTile(AssetHandle(0), "None", nullptr);
				for (const BuiltInSpriteAsset& builtIn : GetBuiltInSpriteAssets())
				{
					if (!spriteQuery.empty()
						&& LowerASCII(std::string(builtIn.Name)).find(spriteQuery)
							== std::string::npos)
						continue;
					const std::filesystem::path packagePath =
						GetBuiltInSpriteAssetPath(builtIn.Handle);
					const std::string label(builtIn.Name);
					drawSpriteTile(builtIn.Handle, label.c_str(), &packagePath);
				}
				for (const AssetMetadata* metadata : sprites)
				{
					if (MatchesSpriteSearch(*metadata, m_Shared.SpriteSearch.data()))
					{
						const std::string label = PathToUTF8(metadata->FilePath.stem());
						drawSpriteTile(metadata->Handle, label.c_str(), &metadata->FilePath);
					}
					for (const AssetSubAsset& child : metadata->SubAssets)
					{
						if (child.Type != AssetType::Texture2D
							|| (!spriteQuery.empty() && LowerASCII(child.Name).find(spriteQuery)
								== std::string::npos))
							continue;
						drawSpriteTile(child.Handle, child.Name.c_str(), &metadata->FilePath);
					}
				}
				ImGui::Columns(1);
				ImGui::EndChild();
				ImGui::EndPopup();
			}
			if (!m_Shared.SpritePickerOpen)
				m_Shared.SpritePickerEntity = UUID(0);
			ImGui::Columns(1);

			DrawProperty("Tiling Factor", columnWidth);
			float tilingFactor = component.TilingFactor;
			if (ImGui::DragFloat("##TilingFactor", &tilingFactor, 0.1f, 0.0f, 100.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp))
			{
				if (!std::isfinite(tilingFactor))
					tilingFactor = component.TilingFactor;
				tilingFactor = std::max(0.0f, tilingFactor);
				if (tilingFactor != component.TilingFactor)
				{
					component.TilingFactor = tilingFactor;
					m_Shared.MarkModified();
				}
			}
			ImGui::Columns(1);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::SpriteAnimator, [&]()
		{
		DrawComponent<SpriteAnimator>("Sprite Animator", entity, m_Shared.Icons,
			EditorIcon::Sprite, [this, entity](auto& component)
		{
			DrawSpriteAnimatorInspector(component, entity);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::RectTransform, [&]()
		{
		DrawComponent<RectTransform>("Rect Transform", entity, m_Shared.Icons,
			EditorIcon::Move, [this, entity](RectTransform& component)
		{
			glm::vec2 anchoredPosition = component.AnchoredPosition;
			glm::vec2 sizeDelta = component.SizeDelta;
			glm::vec2 anchorMin = component.AnchorMin;
			glm::vec2 anchorMax = component.AnchorMax;
			glm::vec2 pivot = component.Pivot;
			bool clipChildren = component.ClipChildren;

            bool presetEdited = false;
            if (ImGui::Button("Anchor presets")) ImGui::OpenPopup("AnchorPresets");
            PrepareEditorPopup("AnchorPresets",450);
        if (ImGui::BeginPopup("AnchorPresets"))
            {
                ImGui::TextDisabled("Anchor alignment / stretch");
                ImGui::PushTextWrapPos(0.0f); ImGui::TextDisabled("Shift: also set pivot. Alt: also reset position and size."); ImGui::PopTextWrapPos();
                const char* names[] = { "Left", "Center", "Right", "Stretch" };
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                    {
                        ImGui::PushID(y*4+x);
                        if (x) ImGui::SameLine();
                        const char* vertical[] = { "Bottom", "Middle", "Top", "Stretch" };
                        std::string title = std::string(names[x]) + " / " + vertical[y];
                        const ImVec2 origin=ImGui::GetCursorScreenPos();
                        const bool clicked=ImGui::InvisibleButton("Preset",ImVec2(70,50));
                        auto* draw=ImGui::GetWindowDrawList();
                        draw->AddRectFilled(origin,ImVec2(origin.x+70,origin.y+50),ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_HeaderHovered:ImGuiCol_FrameBg),2);
                        const ImVec2 min(origin.x+16,origin.y+8),max(origin.x+54,origin.y+42);
                        draw->AddRect(min,max,IM_COL32(145,145,145,255));
                        const float ax=min.x+x*0.5f*(max.x-min.x),ay=max.y-y*0.5f*(max.y-min.y);
                        if(x==3) draw->AddLine(ImVec2(min.x,origin.y+25),ImVec2(max.x,origin.y+25),IM_COL32(80,170,240,255),2);
                        if(y==3) draw->AddLine(ImVec2(origin.x+35,min.y),ImVec2(origin.x+35,max.y),IM_COL32(80,170,240,255),2);
                        if(x!=3 && y!=3) draw->AddCircleFilled(ImVec2(ax,ay),3,IM_COL32(80,170,240,255));
                        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",title.c_str());
                        if (clicked)
                        {
                            anchorMin = { x == 3 ? 0.0f : x*0.5f, y == 3 ? 0.0f : y*0.5f };
                            anchorMax = { x == 3 ? 1.0f : x*0.5f, y == 3 ? 1.0f : y*0.5f };
                            if (ImGui::GetIO().KeyShift) pivot = (anchorMin + anchorMax)*0.5f;
                            if (ImGui::GetIO().KeyAlt) { anchoredPosition = {}; if(x==3) sizeDelta.x=0; if(y==3) sizeDelta.y=0; }
                            presetEdited = true;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                ImGui::EndPopup();
            }
            auto vectorField = [](const char* label, glm::vec2& value, float speed, bool normalized) {
                ImGui::PushID(label);
                DrawProperty(label);
                const bool edited = ImGui::DragFloat2("##Value", glm::value_ptr(value), speed,
                    0.0f, normalized ? 1.0f : 0.0f, "%.3f",
                    normalized ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None);
                ImGui::Columns(1);
                ImGui::PopID();
                return edited;
            };
            bool rectEdited = vectorField("Position", anchoredPosition, 0.1f, false);
            rectEdited |= vectorField("Size Delta", sizeDelta, 0.1f, false);
            ImGui::Spacing();
            ImGui::TextDisabled("ANCHORS");
            rectEdited |= vectorField("Min", anchorMin, 0.005f, true);
            rectEdited |= vectorField("Max", anchorMax, 0.005f, true);
            rectEdited |= vectorField("Pivot", pivot, 0.005f, true);
            DrawProperty("Clip Children");
            rectEdited |= ImGui::Checkbox("##ClipChildren", &clipChildren);
            ImGui::Columns(1);
			if (rectEdited || presetEdited)
			{
				const bool finite = std::isfinite(anchoredPosition.x)
					&& std::isfinite(anchoredPosition.y)
					&& std::isfinite(sizeDelta.x) && std::isfinite(sizeDelta.y)
					&& std::isfinite(anchorMin.x) && std::isfinite(anchorMin.y)
					&& std::isfinite(anchorMax.x) && std::isfinite(anchorMax.y)
					&& std::isfinite(pivot.x) && std::isfinite(pivot.y);
				if (finite)
				{
					anchorMin = glm::clamp(anchorMin, glm::vec2(0.0f),
						glm::vec2(1.0f));
					anchorMax = glm::clamp(anchorMax, anchorMin,
						glm::vec2(1.0f));
					pivot = glm::clamp(pivot, glm::vec2(0.0f), glm::vec2(1.0f));
					component.AnchoredPosition = anchoredPosition;
					component.SizeDelta = sizeDelta;
					component.AnchorMin = anchorMin;
					component.AnchorMax = anchorMax;
					component.Pivot = pivot;
					component.ClipChildren = clipChildren;
					m_Shared.MarkModified();
				}
			}

			// RectTransform replaces the ordinary Transform header in the Inspector,
			// while rotation and scale keep using the entity's real Transform data.
			if (!entity.HasComponent<Transform>() || !m_Shared.Context)
				return;
			auto& transformComponent = entity.GetComponent<Transform>();
			const bool hasParent = static_cast<bool>(m_Shared.Context->GetParent(entity));
			const glm::vec3 translation = hasParent
				? transformComponent._LocalTranslation : transformComponent._Translation;
			glm::vec3 rotation = hasParent
				? transformComponent._LocalRotation : transformComponent._Rotation;
			glm::vec3 scale = hasParent
				? transformComponent._LocalScale : transformComponent._Scale;
			float rotationZ = glm::degrees(rotation.z);
			glm::vec2 scaleXY(scale.x, scale.y);
			DrawProperty("Rotation Z");
            bool transformEdited = ImGui::DragFloat("##RotationZ", &rotationZ, 0.1f,
				0.0f, 0.0f, "%.2f deg");
            ImGui::Columns(1);
            transformEdited |= vectorField("Scale", scaleXY, 0.01f, false);
			if (transformEdited && std::isfinite(rotationZ)
				&& std::isfinite(scaleXY.x) && std::isfinite(scaleXY.y))
			{
				rotation.z = glm::radians(rotationZ);
				scale.x = scaleXY.x;
				scale.y = scaleXY.y;
				const glm::mat4 matrix = Math::ComposeTransform(translation,
					rotation, scale);
				const bool committed = hasParent
					? m_Shared.Context->SetLocalTransform(entity, matrix)
					: m_Shared.Context->SetWorldTransform(entity, matrix);
				if (committed)
					m_Shared.MarkModified();
			}
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Grid2D, [&]()
		{
		DrawComponent<Grid2D>("Grid 2D", entity, m_Shared.Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawGrid2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Tilemap2D, [&]()
		{
		DrawComponent<Tilemap2D>("Tilemap 2D", entity, m_Shared.Icons,
			EditorIcon::Sprite, [this, entity](auto& component)
		{
			DrawTilemap2DInspector(component, entity);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::TilemapRenderer2D, [&]()
		{
		DrawComponent<TilemapRenderer2D>("Tilemap Renderer 2D", entity, m_Shared.Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawTilemapRenderer2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::ParticleSystem2D, [&]()
		{
		DrawComponent<ParticleSystem2D>("Particle System 2D", entity, m_Shared.Icons,
			EditorIcon::Sprite, [this](auto& component)
		{
			DrawParticleSystem2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::Light2D, [&]()
		{
		DrawComponent<Light2D>("Light 2D", entity, m_Shared.Icons,
			EditorIcon::Count, [this](auto& component)
		{
			DrawLight2DInspector(component);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::LineRenderer, [&]()
		{
		DrawComponent<LineRenderer>("Line Renderer", entity, m_Shared.Icons, EditorIcon::Count,
			[this](auto& component)
		{
			const float columnWidth = 100.0f;

			DrawProperty("Color", columnWidth);
			if (DrawColorField("##Color", glm::value_ptr(component._Color)))
				m_Shared.MarkModified();
			ImGui::Columns(1);

			DrawProperty("Start", columnWidth);
			glm::vec3 start = component.Start;
			if (ImGui::DragFloat3("##Start", glm::value_ptr(start), 0.1f) &&
				std::isfinite(start.x) && std::isfinite(start.y) && std::isfinite(start.z) &&
				start != component.Start)
			{
				component.Start = start;
				m_Shared.MarkModified();
			}
			ImGui::Columns(1);

			DrawProperty("End", columnWidth);
			glm::vec3 end = component.End;
			if (ImGui::DragFloat3("##End", glm::value_ptr(end), 0.1f) &&
				std::isfinite(end.x) && std::isfinite(end.y) && std::isfinite(end.z) &&
				end != component.End)
			{
				component.End = end;
				m_Shared.MarkModified();
			}
			ImGui::Columns(1);

			DrawProperty("Width", columnWidth);
			float width = component.Width;
			if (ImGui::DragFloat("##Width", &width, 0.1f, 0.0f, 0.0f, "%.3f"))
			{
				if (!std::isfinite(width))
					width = component.Width;
				width = std::max(0.0001f, width);
				if (width != component.Width)
				{
					component.Width = width;
					m_Shared.MarkModified();
				}
			}
			ImGui::Columns(1);
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::AudioSource, [&]()
		{
		DrawComponent<AudioSource>("Audio Source", entity, m_Shared.Icons, EditorIcon::Audio,
			[this](auto& component)
		{
            const auto* clipMetadata = AssetManager::Get().GetRegistry().GetMetadata(component.Clip);
            const std::string clipLabel = clipMetadata ? PathToUTF8(clipMetadata->FilePath.filename()) : "None (Audio)";
            if (ImGui::BeginCombo("Audio Clip", clipLabel.c_str()))
            {
                if (ImGui::Selectable("None")) { component.Clip = AssetHandle(0); m_Shared.MarkModified(); }
                for (const auto& [handle, metadata] : AssetManager::Get().GetRegistry().GetAssets())
                    if (!metadata.IsMissing && metadata.Type == AssetType::Audio)
                    {
                        ImGui::PushID(std::to_string(static_cast<uint64_t>(handle)).c_str());
                        if (ImGui::Selectable(PathToUTF8(metadata.FilePath).c_str())) { component.Clip = handle; m_Shared.MarkModified(); }
                        ImGui::PopID();
                    }
                ImGui::EndCombo();
            }
            uint64_t clip = static_cast<uint64_t>(component.Clip);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					AssetDragDropPayloadID))
				{
					if (payload->DataSize == sizeof(uint64_t))
					{
						const AssetHandle handle(*static_cast<const uint64_t*>(payload->Data));
						const AssetMetadata* metadata = AssetManager::Get().GetRegistry()
							.GetMetadata(handle);
						if (metadata && !metadata->IsMissing
							&& metadata->Type == AssetType::Audio)
						{
							component.Clip = handle;
							m_Shared.MarkModified();
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::Checkbox("Play On Start", &component.PlayOnStart)) m_Shared.MarkModified();
			if (ImGui::Checkbox("Loop", &component.Loop)) m_Shared.MarkModified();
			if (ImGui::Checkbox("Streaming", &component.Streaming)) m_Shared.MarkModified();
			float volume = component.Volume;
			if (ImGui::DragFloat("Volume", &volume, 0.01f, 0.0f, 4.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(volume))
			{
				component.Volume = std::clamp(volume, 0.0f, 4.0f);
				m_Shared.MarkModified();
			}
			float pitch = component.Pitch;
			if (ImGui::DragFloat("Pitch", &pitch, 0.01f, 0.25f, 4.0f,
				"%.2f", ImGuiSliderFlags_AlwaysClamp) && std::isfinite(pitch))
			{
				component.Pitch = std::clamp(pitch, 0.25f, 4.0f);
				m_Shared.MarkModified();
			}
			const char* groups[] = { "Master", "Music", "SFX" };
			int group = component.MixerGroup;
			if (ImGui::Combo("Mixer Group", &group, groups, 3))
			{
				component.MixerGroup = static_cast<uint8_t>(group);
				m_Shared.MarkModified();
			}
			float blend = component.SpatialBlend;
			if (ImGui::SliderFloat("Spatial Blend", &blend, 0.0f, 1.0f, "%.2f")
				&& std::isfinite(blend))
			{
				component.SpatialBlend = std::clamp(blend, 0.0f, 1.0f);
				m_Shared.MarkModified();
			}
			float minimum = component.MinDistance;
			if (ImGui::DragFloat("Min Distance", &minimum, 0.05f, 0.0f,
				component.MaxDistance - 0.001f, "%.2f",
				ImGuiSliderFlags_AlwaysClamp) && std::isfinite(minimum))
			{
				component.MinDistance = std::max(0.0f,
					std::min(minimum, component.MaxDistance - 0.001f));
				m_Shared.MarkModified();
			}
			float maximum = component.MaxDistance;
			if (ImGui::DragFloat("Max Distance", &maximum, 0.05f,
				component.MinDistance + 0.001f, 100000.0f, "%.2f",
				ImGuiSliderFlags_AlwaysClamp) && std::isfinite(maximum))
			{
				component.MaxDistance = std::max(maximum,
					component.MinDistance + 0.001f);
				m_Shared.MarkModified();
			}
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::AudioListener, [&]()
		{
		DrawComponent<AudioListener>("Audio Listener", entity, m_Shared.Icons,
			EditorIcon::Count, [this](auto& component)
		{
			if (ImGui::Checkbox("Primary", &component.Primary))
				m_Shared.MarkModified();
		}, onModified);
		});

		richInspectors.emplace(ComponentIds::UIImage, [&]()
		{
			DrawComponent<UIImage>("Image", entity, m_Shared.Icons, EditorIcon::Sprite,
				[this](UIImage& component)
				{
					if (const PropertyDescriptor* imageProperty = FindRegisteredProperty(
						ComponentIds::UIImage, "Image"))
					{
						AssetHandle image = component.Image;
						if (DrawRegisteredAssetReference(*imageProperty, image))
						{
							component.Image = image;
							m_Shared.MarkModified();
						}
					}
					if (DrawColorField("Color", glm::value_ptr(component.Color)))
						m_Shared.MarkModified();
					if (ImGui::Checkbox("Raycast Target", &component.RaycastTarget))
						m_Shared.MarkModified();
					if (ImGui::Checkbox("Preserve Aspect", &component.PreserveAspect))
						m_Shared.MarkModified();
				}, onModified, m_Shared.ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::UIText, [&]()
		{
			DrawComponent<UIText>("Text", entity, m_Shared.Icons, EditorIcon::Font,
				[this](UIText& component)
				{
					if (DrawBoundedMultilineText("Text", component.Text,
						ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.5f)))
					{
						m_Shared.MarkModified();
					}

					ImGui::Spacing();
					ImGui::TextDisabled("CHARACTER");
					auto drawFont = [this](const char* stableName,
						AssetHandle& handle)
					{
						const PropertyDescriptor* property = FindRegisteredProperty(
							ComponentIds::UIText, stableName);
						if (!property)
							return;
						ImGui::PushID(stableName);
						AssetHandle candidate = handle;
						if (DrawRegisteredAssetReference(*property, candidate))
						{
							handle = candidate;
							m_Shared.MarkModified();
						}
						ImGui::PopID();
					};
					drawFont("Font", component.Font);
					drawFont("FallbackFont", component.FallbackFont);
					drawFont("EmojiFont", component.EmojiFont);
					float fontSize = component.FontSize;
					if (ImGui::DragFloat("Font Size", &fontSize, 0.25f, 1.0f,
						10000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp)
						&& std::isfinite(fontSize))
					{
						component.FontSize = std::clamp(fontSize, 1.0f, 10000.0f);
						m_Shared.MarkModified();
					}
					float lineSpacing = component.LineSpacing;
					if (ImGui::DragFloat("Line Spacing", &lineSpacing, 0.01f,
						0.1f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)
						&& std::isfinite(lineSpacing))
					{
						component.LineSpacing = std::clamp(lineSpacing, 0.1f, 10.0f);
						m_Shared.MarkModified();
					}

					ImGui::Spacing();
					ImGui::TextDisabled("PARAGRAPH");
					const char* alignments[] = { "Left", "Center", "Right" };
					int alignment = static_cast<int>(component.Alignment);
					if (ImGui::Combo("Alignment", &alignment, alignments,
						static_cast<int>(std::size(alignments))))
					{
						component.Alignment = static_cast<TextAlignment>(alignment);
						m_Shared.MarkModified();
					}
					if (ImGui::Checkbox("Wrap", &component.Wrap))
						m_Shared.MarkModified();
					if (DrawColorField("Color", glm::value_ptr(component.Color)))
						m_Shared.MarkModified();
					if (ImGui::Checkbox("Raycast Target", &component.RaycastTarget))
						m_Shared.MarkModified();
				}, onModified, m_Shared.ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::Rigidbody2D, [&]()
		{
		DrawComponent<Rigidbody2D>("Rigidbody 2D", entity, m_Shared.Icons, EditorIcon::Rigidbody2D,
			[this](auto& component)
		{
			const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
			int bodyType = (int)component.Type;
			if (ImGui::BeginCombo("Body Type", bodyTypes[bodyType]))
			{
				for (int i = 0; i < 3; ++i)
				{
					const bool selected = bodyType == i;
					if (ImGui::Selectable(bodyTypes[i], selected)) { component.Type = (Rigidbody2D::BodyType)i; m_Shared.MarkModified(); }
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Checkbox("Fixed Rotation", &component.FixedRotation)) m_Shared.MarkModified();
			}, onModified, m_Shared.ColliderEditingAllowed);
		});
		richInspectors.emplace(ComponentIds::UIButton, [&]()
		{
			DrawComponent<UIButton>("Button", entity, m_Shared.Icons,
				EditorIcon::Count, [&](UIButton& button)
				{
					DrawUIButtonInspector(button, entity);
				}, onModified, m_Shared.ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::BoxCollider2D, [&]()
		{
		DrawComponent<BoxCollider2D>("Box Collider 2D", entity, m_Shared.Icons, EditorIcon::BoxCollider2D,
			[this, entity](auto& component)
		{
			const bool editingCollider = m_Shared.GetColliderEdit() == ColliderEditMode::Box;
			ImGui::BeginDisabled(!m_Shared.ColliderEditingAllowed || !m_Shared.ColliderGizmosEnabled);
			if (editingCollider)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
			if (ImGui::Button("Edit Collider", ImVec2(-1.0f, 0.0f)))
			{
				if (editingCollider)
					m_Shared.ClearColliderEdit();
				else
				{
					m_Shared.ColliderEdit = ColliderEditMode::Box;
					m_Shared.ColliderEditEntity = entity.GetUUID();
				}
			}
			if (editingCollider)
				ImGui::PopStyleColor();
			ImGui::EndDisabled();

			glm::vec2 offset = component.Offset;
			glm::vec2 size = component.Size;
			bool changed = false;
			changed |= DrawCollisionFilterProperties(component.IsTrigger,
				component.CollisionLayer, component.CollisionMask);
			bool geometryEdited = ImGui::DragFloat2("Offset", glm::value_ptr(offset));
			geometryEdited |= ImGui::DragFloat2("Size", glm::value_ptr(size));
			if (geometryEdited)
			{
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
					offset = component.Offset;
				if (!std::isfinite(size.x) || !std::isfinite(size.y))
					size = component.Size;
				constexpr float minimumSize = 0.0001f;
				size.x = std::max(minimumSize, size.x);
				size.y = std::max(minimumSize, size.y);
				if (offset.x != component.Offset.x || offset.y != component.Offset.y
					|| size.x != component.Size.x || size.y != component.Size.y)
				{
					component.Offset = offset;
					component.Size = size;
					changed = true;
				}
			}

			changed |= DrawColliderMaterialProperties(component.Density,
				component.Friction, component.Restitution);
			float restitutionThreshold = component.RestitutionThreshold;
			if (ImGui::DragFloat("Restitution Threshold", &restitutionThreshold, 0.01f, 0.0f, 0.0f))
			{
				if (!std::isfinite(restitutionThreshold))
					restitutionThreshold = component.RestitutionThreshold;
				restitutionThreshold = std::max(0.0f, restitutionThreshold);
				if (restitutionThreshold != component.RestitutionThreshold)
				{
					component.RestitutionThreshold = restitutionThreshold;
					changed = true;
				}
			}
			if (changed)
				m_Shared.MarkModified();
		}, onModified, m_Shared.ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::CircleCollider2D, [&]()
		{
		DrawComponent<CircleCollider2D>("Circle Collider 2D", entity, m_Shared.Icons, EditorIcon::BoxCollider2D,
			[this, entity](auto& component)
		{
			const bool editingCollider = m_Shared.GetColliderEdit() == ColliderEditMode::Circle;
			ImGui::BeginDisabled(!m_Shared.ColliderEditingAllowed || !m_Shared.ColliderGizmosEnabled);
			if (editingCollider)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
			if (ImGui::Button("Edit Collider", ImVec2(-1.0f, 0.0f)))
			{
				if (editingCollider)
					m_Shared.ClearColliderEdit();
				else
				{
					m_Shared.ColliderEdit = ColliderEditMode::Circle;
					m_Shared.ColliderEditEntity = entity.GetUUID();
				}
			}
			if (editingCollider)
				ImGui::PopStyleColor();
			ImGui::EndDisabled();

			glm::vec2 offset = component.Offset;
			float radius = component.Radius;
			bool changed = false;
			changed |= DrawCollisionFilterProperties(component.IsTrigger,
				component.CollisionLayer, component.CollisionMask);
			bool geometryEdited = ImGui::DragFloat2("Offset", glm::value_ptr(offset));
			geometryEdited |= ImGui::DragFloat("Radius", &radius, 0.01f, 0.0001f, 0.0f);
			if (geometryEdited)
			{
				if (!std::isfinite(offset.x) || !std::isfinite(offset.y))
					offset = component.Offset;
				if (!std::isfinite(radius))
					radius = component.Radius;
				radius = std::max(0.0001f, radius);
				if (offset.x != component.Offset.x || offset.y != component.Offset.y
					|| radius != component.Radius)
				{
					component.Offset = offset;
					component.Radius = radius;
					changed = true;
				}
			}

			ImGui::TextDisabled("Non-uniform scale uses max(abs(X), abs(Y)).");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Box2D circles cannot become ellipses. The largest absolute world X/Y scale keeps the fixture circular.");
			changed |= DrawColliderMaterialProperties(component.Density,
				component.Friction, component.Restitution);
			if (changed)
				m_Shared.MarkModified();
		}, onModified, m_Shared.ColliderEditingAllowed);
		});

		richInspectors.emplace(ComponentIds::DistanceJoint2D, [&]()
		{
		DrawComponent<DistanceJoint2D>("Distance Joint 2D", entity, m_Shared.Icons, EditorIcon::Rigidbody2D,
			[this, entity](auto& component)
		{
			bool changed = false;
			if (!entity.HasComponent<Rigidbody2D>())
				ImGui::TextDisabled("Owner Body: implicit static body");
			uint64_t connectedEntity = static_cast<uint64_t>(component.ConnectedEntity);
			if (ImGui::InputScalar("Connected Entity UUID", ImGuiDataType_U64, &connectedEntity))
			{
				component.ConnectedEntity = UUID(connectedEntity);
				changed = true;
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					SceneEntityDragDropPayloadID))
				{
					Entity connected = GetDraggedSceneEntity(payload, m_Shared.Context);
					if (connected && connected != entity &&
						component.ConnectedEntity != connected.GetUUID())
					{
						component.ConnectedEntity = connected.GetUUID();
						connectedEntity = static_cast<uint64_t>(component.ConnectedEntity);
						changed = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Enter an entity UUID or drag an entity here from Hierarchy. Use 0 for no connection.");

			Entity connected;
			if (m_Shared.Context && connectedEntity != 0)
				connected = m_Shared.Context->FindEntityByUUID(UUID(connectedEntity));
			if (connectedEntity == 0)
				ImGui::TextDisabled("Connected Body: None");
			else if (!connected)
				ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f),
					"Connected Body: Missing entity");
			else if (connected == entity)
				ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
					"Connected Body cannot reference itself");
			else if (!connected.HasComponent<Rigidbody2D>())
				ImGui::TextDisabled("Connected Body: %s (implicit static)",
					connected.GetName().c_str());
			else
				ImGui::TextDisabled("Connected Body: %s", connected.GetName().c_str());

			glm::vec2 anchor = component.Anchor;
			if (ImGui::DragFloat2("Anchor", glm::value_ptr(anchor), 0.01f))
			{
				if (std::isfinite(anchor.x) && std::isfinite(anchor.y) && anchor != component.Anchor)
				{
					component.Anchor = anchor;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Unscaled local anchor on this physics body.");

			glm::vec2 connectedAnchor = component.ConnectedAnchor;
			if (ImGui::DragFloat2("Connected Anchor", glm::value_ptr(connectedAnchor), 0.01f))
			{
				if (std::isfinite(connectedAnchor.x) && std::isfinite(connectedAnchor.y)
					&& connectedAnchor != component.ConnectedAnchor)
				{
					component.ConnectedAnchor = connectedAnchor;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Unscaled local anchor on the connected physics body.");

			float distance = component.Distance;
			if (ImGui::DragFloat("Distance", &distance, 0.01f, 0.0f, 0.0f, "%.3f"))
			{
				if (!std::isfinite(distance))
					distance = component.Distance;
				distance = std::max(0.0001f, distance);
				if (distance != component.Distance)
				{
					component.Distance = distance;
					changed = true;
				}
			}

			float frequency = component.Frequency;
			if (ImGui::DragFloat("Frequency", &frequency, 0.01f, 0.0f, 0.0f, "%.3f Hz"))
			{
				if (!std::isfinite(frequency))
					frequency = component.Frequency;
				frequency = std::max(0.0f, frequency);
				if (frequency != component.Frequency)
				{
					component.Frequency = frequency;
					changed = true;
				}
			}

			float damping = component.Damping;
			if (ImGui::DragFloat("Damping", &damping, 0.01f, 0.0f, 1.0f,
				"%.3f", ImGuiSliderFlags_AlwaysClamp))
			{
				if (!std::isfinite(damping))
					damping = component.Damping;
				damping = std::clamp(damping, 0.0f, 1.0f);
				if (damping != component.Damping)
				{
					component.Damping = damping;
					changed = true;
				}
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
				ImGui::SetTooltip("Box2D damping ratio in the range 0 to 1.");

			if (ImGui::Checkbox("Collide Connected", &component.CollideConnected))
				changed = true;
			if (changed)
				m_Shared.MarkModified();
		}, onModified, m_Shared.ColliderEditingAllowed);
		});

        richInspectors.emplace(ComponentIds::UILocalization, [&]()
        {
            DrawComponent<UILocalization>("UI Localization", entity, m_Shared.Icons, EditorIcon::Font,
                [&](UILocalization& component)
                {
                    auto editString = [&](const char* label, std::string& value) {
                        std::array<char, 4096> buffer{};
                        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
                        ImGui::BeginDisabled(value.size() >= buffer.size());
                        if (ImGui::InputText(label, buffer.data(), buffer.size())) { value=buffer.data(); m_Shared.MarkModified(); }
                        ImGui::EndDisabled();
                    };
                    editString("Locale", component.Locale);
                    editString("Fallback locale", component.FallbackLocale);
                    try
                    {
                        YAML::Node table = YAML::Load(component.Table);
                        if (!table.IsMap()) throw std::runtime_error("Translation table must be a language-to-key map.");
                        std::vector<std::string> locales; std::set<std::string> keys;
                        for(const auto& locale : table)
                        {
                            if (!locale.second.IsMap()) throw std::runtime_error("Each language must contain a key/value map.");
                            locales.push_back(locale.first.as<std::string>());
                            for(const auto& value : locale.second) { keys.insert(value.first.as<std::string>()); if(!value.second.IsScalar()) throw std::runtime_error("Translations must be text values."); }
                        }
                        bool changed=false;
                        if(ImGui::Button("Add current locale") && !component.Locale.empty() && !table[component.Locale])
                        { table[component.Locale]=YAML::Node(YAML::NodeType::Map); changed=true; }
                        ImGui::TextWrapped("Missing keys use the fallback locale. An explicitly empty translation displays empty text. Right-click a cell to restore fallback.");
                        static char newKey[128]{};
                        ImGui::InputTextWithHint("##TranslationKey", "New translation key", newKey, sizeof(newKey));
                        ImGui::SameLine();
                        if(ImGui::Button("Add key") && newKey[0] && !component.Locale.empty() && !keys.contains(newKey))
                        { table[component.Locale][newKey]=""; changed=true; newKey[0]=0; }
                        if(!locales.empty() && ImGui::BeginTable("Translations", static_cast<int>(std::min<size_t>(locales.size(), 30))+1,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit))
                        {
                            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 130);
                            for(size_t i=0;i<std::min<size_t>(locales.size(),30);++i) ImGui::TableSetupColumn(locales[i].c_str(), ImGuiTableColumnFlags_WidthFixed,180);
                            ImGui::TableHeadersRow();
                            for(const auto& key : keys)
                            {
                                ImGui::PushID(key.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(key.c_str());
                                for(size_t i=0;i<std::min<size_t>(locales.size(),30);++i)
                                {
                                    ImGui::TableNextColumn(); ImGui::PushID(locales[i].c_str());
                                    const YAML::Node values=table[locales[i]];
                                    std::string value=values[key] ? values[key].as<std::string>() : "";
                                    std::array<char,4096> buffer{}; std::snprintf(buffer.data(),buffer.size(),"%s",value.c_str());
                                    ImGui::SetNextItemWidth(-1); ImGui::BeginDisabled(value.size()>=buffer.size());
                                    if(ImGui::InputTextWithHint("##Translation", values[key] ? "Empty" : "Missing (fallback)",buffer.data(),buffer.size())) { table[locales[i]][key]=buffer.data(); changed=true; }
                                    if (ImGui::BeginPopupContextItem("TranslationActions"))
                                    {
                                        if (ImGui::MenuItem("Use fallback (remove translation)", nullptr, false, static_cast<bool>(values[key])))
                                        { table[locales[i]].remove(key); changed=true; }
                                        ImGui::EndPopup();
                                    }
                                    ImGui::EndDisabled(); ImGui::PopID();
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if(locales.size()>30) ImGui::TextWrapped("Showing first 30 locales. All locales remain in the source table.");
                        if(changed) { YAML::Emitter output; output<<table; component.Table=output.c_str(); m_Shared.MarkModified(); }
                    }
                    catch(const std::exception& error) { ImGui::TextWrapped("Table error: %s",error.what()); }
                    if(ImGui::TreeNode("Source table / recovery"))
                    { if(DrawBoundedMultilineText("##LocalizationSource",component.Table,ImVec2(-1,120))) m_Shared.MarkModified(); ImGui::TreePop(); }
                }, onModified, m_Shared.ColliderEditingAllowed);
        });
        richInspectors.emplace(ComponentIds::UITheme, [&]()
        {
            DrawComponent<UITheme>("UI Theme", entity, m_Shared.Icons, EditorIcon::Material, [&](UITheme& theme) {
                bool changed=DrawColorField("Text",glm::value_ptr(theme.TextColor));
                changed|=DrawColorField("Image",glm::value_ptr(theme.ImageColor));
                changed|=DrawColorField("Accent",glm::value_ptr(theme.AccentColor));
                changed|=ImGui::DragFloat("Font scale",&theme.FontScale,0.01f,0.1f,10.0f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                const auto* descriptor=ComponentRegistry::Get().Find(UUID(ComponentIds::UITheme));
                if(descriptor) for(const auto& property:descriptor->Properties) if(property.StableName=="Font") changed|=DrawRegisteredAssetReference(property,theme.Font);
                ImGui::TextDisabled("Theme preview (inherited by descendants)");
                ImGui::ColorButton("Image tint",ImVec4(theme.ImageColor.x,theme.ImageColor.y,theme.ImageColor.z,theme.ImageColor.w),0,ImVec2(80,24));
                ImGui::SameLine(); ImGui::ColorButton("Accent tint",ImVec4(theme.AccentColor.x,theme.AccentColor.y,theme.AccentColor.z,theme.AccentColor.w),0,ImVec2(80,24));
                ImGui::TextColored(ImVec4(theme.TextColor.x,theme.TextColor.y,theme.TextColor.z,theme.TextColor.w),"Sample text");
                if(changed) m_Shared.MarkModified();
            },onModified,m_Shared.ColliderEditingAllowed);
        });
		richInspectors.emplace(ComponentIds::CSharpScripts, [&]()
		{
			ImGui::BeginDisabled(!m_Shared.ScriptEditingEnabled);
			DrawCSharpScripts(entity);
			ImGui::EndDisabled();
		});
        for (const ComponentDescriptor& descriptor :
            ComponentRegistry::Get().GetDescriptors())
        {
            if (!descriptor.InspectorVisible || !descriptor.Has(entity))
				continue;
			const uint64_t componentType = static_cast<uint64_t>(descriptor.TypeId);
			// RectTransform is the UI-facing Transform. Its rich inspector below also
			// edits the shared Transform rotation/scale, so drawing both headers would
			// expose two conflicting transform surfaces.
			if (componentType == ComponentIds::Transform
				&& entity.HasComponent<RectTransform>())
				continue;
			const auto rich = richInspectors.find(componentType);
			if (rich != richInspectors.end())
				rich->second();
			else if (descriptor.UseGenericInspector)
				DrawRegisteredComponent(descriptor, entity, onModified,
					m_Shared.ColliderEditingAllowed);
		}
		if (entity.HasComponent<OpaqueComponents>())
		{
			for (const OpaqueComponentRecord& missing :
				entity.GetComponent<OpaqueComponents>().Records)
			{
				ImGui::PushID(static_cast<int>(static_cast<uint64_t>(missing.TypeId)));
				ImGui::Separator();
				if (ImGui::TreeNodeEx("##MissingComponent",
					ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth,
					"Missing Component: %s", missing.StableName.c_str()))
				{
					ImGui::Text("Type UUID: %llu",
						static_cast<unsigned long long>(missing.TypeId));
					ImGui::Text("Schema: %u", missing.SchemaVersion);
					ImGui::TextDisabled("Payload is preserved until the component provider is available.");
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
		ImGui::Spacing();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::BeginDisabled(!m_Shared.ColliderEditingAllowed);
		const bool addComponentPressed = ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f));
        const auto addMin=ImGui::GetItemRectMin(),addMax=ImGui::GetItemRectMax();
        const float addSize=ImGui::GetFontSize()*0.85f;
        const ImVec2 addStart(addMin.x+ImGui::GetStyle().FramePadding.x,addMin.y+(addMax.y-addMin.y-addSize)*0.5f);
        DrawEditorGlyph(ImGui::GetWindowDrawList(),m_Shared.Icons,EditorIcon::Add,addStart,
            ImVec2(addStart.x+addSize,addStart.y+addSize),ImGui::GetColorU32(ImVec4(1,1,1,1)));
		ImGui::EndDisabled();
		if (addComponentPressed || m_AddComponentPopupRequested)
		{
			m_AddComponentSearch.fill('\0');
			m_AddComponentSearchFocusRequested = true;
			m_AddComponentPopupRequested = false;
			ImGui::OpenPopup("AddComponent");
		}

		PrepareEditorPopup("AddComponent",420);
        if (ImGui::BeginPopup("AddComponent"))
		{
			ImGui::BeginDisabled(!m_Shared.ColliderEditingAllowed);
			if (m_AddComponentSearchFocusRequested)
			{
				ImGui::SetKeyboardFocusHere();
				m_AddComponentSearchFocusRequested = false;
			}
			ImGui::SetNextItemWidth(-1.0f);
			EditorSearchField("##AddComponentSearch", "Search components...",
				m_AddComponentSearch.data(), m_AddComponentSearch.size());
			ImGui::Separator();

			const std::string query = LowerASCII(m_AddComponentSearch.data());
			constexpr std::array<const char*, 8> categoryOrder = {
				"Rendering", "Animation", "Tilemap", "Physics 2D",
				"Audio", "UI", "Scripting", "Gameplay"
			};
			bool drewResult = false;
			auto drawDescriptor = [&](const ComponentDescriptor& descriptor)
			{
				ImGui::PushID(descriptor.StableName.c_str());
				const bool selected = ImGui::MenuItem(descriptor.DisplayName.c_str());
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
					ImGui::SetTooltip("%s", descriptor.StableName.c_str());
				ImGui::PopID();
				if (selected)
				{
					std::string error;
					const uint64_t componentType =
						static_cast<uint64_t>(descriptor.TypeId);
					if (componentType == ComponentIds::SpriteAnimator
						&& !entity.HasComponent<SpriteRenderer>())
						entity.AddComponent<SpriteRenderer>();
					if (componentType == ComponentIds::Tilemap2D
						&& !entity.HasComponent<TilemapRenderer2D>())
						entity.AddComponent<TilemapRenderer2D>();
					if ((componentType == ComponentIds::UIImage
						|| componentType == ComponentIds::UIText
						|| componentType == ComponentIds::UIButton
						|| componentType == ComponentIds::UISlider
						|| componentType == ComponentIds::UIScrollView
						|| componentType == ComponentIds::UIInputField
						|| componentType == ComponentIds::UILocalizedText)
						&& !entity.HasComponent<RectTransform>())
						entity.AddComponent<RectTransform>();
					if ((componentType == ComponentIds::UIInputField || componentType == ComponentIds::UILocalizedText)
						&& !entity.HasComponent<UIText>()) entity.AddComponent<UIText>();
					if (componentType == ComponentIds::UIButton
						&& !entity.HasComponent<UIImage>())
						entity.AddComponent<UIImage>();
					if (componentType == ComponentIds::Canvas
						&& !entity.HasComponent<UIEventSystem>())
						entity.AddComponent<UIEventSystem>();
					if (descriptor.Add(entity, error))
						m_Shared.MarkModified();
					else
						TC_Core_Warn("Could not add {0}: {1}", descriptor.StableName, error);
					ImGui::CloseCurrentPopup();
				}
			};

			auto descriptorAvailable = [&](const ComponentDescriptor& descriptor,
				const char* category)
			{
				return descriptor.InspectorVisible && descriptor.AddableInInspector
					&& !descriptor.Has(entity)
					&& std::string_view(GetAddComponentCategory(descriptor)) == category
					&& MatchesAddComponentSearch(descriptor, query);
			};

			if (query.empty())
			{
				for (const char* category : categoryOrder)
				{
					bool hasItems = std::string_view(category) == "Scripting";
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
						hasItems = hasItems || descriptorAvailable(descriptor, category);
					if (!hasItems)
						continue;
					drewResult = true;
					if (!ImGui::BeginMenu(category))
						continue;
					if (std::string_view(category) == "Scripting")
						ImGui::MenuItem("C# Script (drag asset into Inspector)",
							nullptr, false, false);
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
					{
						if (descriptorAvailable(descriptor, category))
							drawDescriptor(descriptor);
					}
					ImGui::EndMenu();
				}
			}
			else
			{
				for (const char* category : categoryOrder)
				{
					bool categoryDrawn = false;
					const std::string scriptSearch = LowerASCII(
						std::string("C# Script Scripting"));
					if (std::string_view(category) == "Scripting"
						&& scriptSearch.find(query) != std::string::npos)
					{
						ImGui::TextDisabled("Scripting");
						ImGui::MenuItem("C# Script (drag asset into Inspector)",
							nullptr, false, false);
						categoryDrawn = true;
					}
					for (const ComponentDescriptor& descriptor :
						ComponentRegistry::Get().GetDescriptors())
					{
						if (!descriptorAvailable(descriptor, category))
							continue;
						if (!categoryDrawn)
						{
							ImGui::TextDisabled("%s", category);
							categoryDrawn = true;
						}
						drawDescriptor(descriptor);
					}
					if (categoryDrawn)
					{
						drewResult = true;
						ImGui::Spacing();
					}
				}
			}
			if (!drewResult)
				ImGui::TextDisabled("No matching components");
			ImGui::EndDisabled();
			ImGui::EndPopup();
		}

	}


	void SceneInspectorPanel::OnSceneContextChanged()
	{
		m_InspectorAssetPath.clear();
		m_LockedInspectorAssetPath.clear();
		m_InspectorLocked = false;
		m_InspectedEntity = UUID(0);
		m_TilemapBrushStates.clear();
	}

	void SceneInspectorPanel::ClearSceneState()
	{
		m_TilemapBrushStates.clear();
	}

}
