#pragma once

#include "Components.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace TomCat::SpriteAnimatorAuthoring {

	inline bool IsValidName(std::string_view name, std::string& error)
	{
		if (name.empty())
		{
			error = "Name cannot be empty";
			return false;
		}
		if (name.size() > 127)
		{
			error = "Name cannot exceed 127 UTF-8 bytes";
			return false;
		}
		const auto first = static_cast<unsigned char>(name.front());
		const auto last = static_cast<unsigned char>(name.back());
		if (std::isspace(first) || std::isspace(last))
		{
			error = "Name cannot start or end with whitespace";
			return false;
		}
		for (const unsigned char character : name)
		{
			if (character < 0x20 || character == 0x7f)
			{
				error = "Name cannot contain control characters";
				return false;
			}
		}
		error.clear();
		return true;
	}

	template<typename Collection, typename GetName>
	inline std::string MakeUniqueName(const Collection& items, std::string base,
		GetName&& getName)
	{
		std::unordered_set<std::string> names;
		for (const auto& item : items)
			names.emplace(getName(item));
		if (names.find(base) == names.end())
			return base;
		for (uint32_t suffix = 2; suffix != 0; ++suffix)
		{
			std::string candidate = base + " " + std::to_string(suffix);
			if (names.find(candidate) == names.end())
				return candidate;
		}
		return base;
	}

	inline bool IsBooleanParameter(AnimatorParameterType type)
	{
		return type == AnimatorParameterType::Bool
			|| type == AnimatorParameterType::Trigger;
	}

	inline AnimatorConditionMode DefaultConditionMode(AnimatorParameterType type)
	{
		return IsBooleanParameter(type)
			? AnimatorConditionMode::If : AnimatorConditionMode::Greater;
	}

	inline bool RenameClip(SpriteAnimator& animator, size_t index,
		std::string newName, std::string& error)
	{
		if (index >= animator.Clips.size())
		{
			error = "Clip no longer exists";
			return false;
		}
		if (!IsValidName(newName, error))
			return false;
		for (size_t other = 0; other < animator.Clips.size(); ++other)
		{
			if (other != index && animator.Clips[other].Name == newName)
			{
				error = "Clip names must be unique";
				return false;
			}
		}

		const std::string oldName = animator.Clips[index].Name;
		if (oldName == newName)
		{
			error.clear();
			return false;
		}
		animator.Clips[index].Name = std::move(newName);
		const std::string& replacement = animator.Clips[index].Name;
		if (animator.InitialClip == oldName)
			animator.InitialClip = replacement;
		for (AnimatorState& state : animator.States)
			if (state.Clip == oldName)
				state.Clip = replacement;
		error.clear();
		return true;
	}

	inline bool RemoveClip(SpriteAnimator& animator, size_t index,
		std::string& error)
	{
		if (index >= animator.Clips.size())
		{
			error = "Clip no longer exists";
			return false;
		}
		if (animator.Clips.size() == 1)
		{
			error = "An Animator must keep at least one clip";
			return false;
		}

		const std::string removedClip = animator.Clips[index].Name;
		std::unordered_set<std::string> removedStates;
		for (const AnimatorState& state : animator.States)
			if (state.Clip == removedClip)
				removedStates.emplace(state.Name);
		animator.States.erase(std::remove_if(animator.States.begin(),
			animator.States.end(), [&](const AnimatorState& state)
			{
				return state.Clip == removedClip;
			}), animator.States.end());
		animator.Transitions.erase(std::remove_if(animator.Transitions.begin(),
			animator.Transitions.end(), [&](const AnimatorTransition& transition)
			{
				return removedStates.find(transition.ToState) != removedStates.end()
					|| (!transition.AnyState && removedStates.find(transition.FromState)
						!= removedStates.end());
			}), animator.Transitions.end());
		if (animator.InitialClip == removedClip)
			animator.InitialClip.clear();
		if (removedStates.find(animator.InitialState) != removedStates.end())
			animator.InitialState.clear();
		animator.Clips.erase(animator.Clips.begin()
			+ static_cast<std::ptrdiff_t>(index));
		error.clear();
		return true;
	}

	inline bool RenameParameter(SpriteAnimator& animator, size_t index,
		std::string newName, std::string& error)
	{
		if (index >= animator.Parameters.size())
		{
			error = "Parameter no longer exists";
			return false;
		}
		if (!IsValidName(newName, error))
			return false;
		for (size_t other = 0; other < animator.Parameters.size(); ++other)
		{
			if (other != index && animator.Parameters[other].Name == newName)
			{
				error = "Parameter names must be unique";
				return false;
			}
		}

		const std::string oldName = animator.Parameters[index].Name;
		if (oldName == newName)
		{
			error.clear();
			return false;
		}
		animator.Parameters[index].Name = std::move(newName);
		const std::string& replacement = animator.Parameters[index].Name;
		for (AnimatorTransition& transition : animator.Transitions)
			for (AnimatorCondition& condition : transition.Conditions)
				if (condition.Parameter == oldName)
					condition.Parameter = replacement;
		error.clear();
		return true;
	}

	inline bool SetParameterType(SpriteAnimator& animator, size_t index,
		AnimatorParameterType type)
	{
		if (index >= animator.Parameters.size()
			|| type < AnimatorParameterType::Bool
			|| type > AnimatorParameterType::Trigger)
			return false;
		AnimatorParameter& parameter = animator.Parameters[index];
		if (parameter.Type == type)
			return false;
		parameter.Type = type;
		parameter.BoolValue = false;
		parameter.IntValue = 0;
		parameter.FloatValue = 0.0f;
		for (AnimatorTransition& transition : animator.Transitions)
		{
			for (AnimatorCondition& condition : transition.Conditions)
			{
				if (condition.Parameter == parameter.Name)
				{
					condition.Mode = DefaultConditionMode(type);
					condition.Threshold = 0.0f;
				}
			}
		}
		return true;
	}

	inline bool RemoveParameter(SpriteAnimator& animator, size_t index,
		std::string& error)
	{
		if (index >= animator.Parameters.size())
		{
			error = "Parameter no longer exists";
			return false;
		}
		const std::string removedName = animator.Parameters[index].Name;
		animator.Parameters.erase(animator.Parameters.begin()
			+ static_cast<std::ptrdiff_t>(index));
		for (AnimatorTransition& transition : animator.Transitions)
		{
			transition.Conditions.erase(std::remove_if(
				transition.Conditions.begin(), transition.Conditions.end(),
				[&](const AnimatorCondition& condition)
				{
					return condition.Parameter == removedName;
				}), transition.Conditions.end());
		}
		animator.Transitions.erase(std::remove_if(animator.Transitions.begin(),
			animator.Transitions.end(), [](const AnimatorTransition& transition)
			{
				return transition.ExitTime < 0.0f && transition.Conditions.empty();
			}), animator.Transitions.end());
		error.clear();
		return true;
	}

	inline bool RenameState(SpriteAnimator& animator, size_t index,
		std::string newName, std::string& error)
	{
		if (index >= animator.States.size())
		{
			error = "State no longer exists";
			return false;
		}
		if (!IsValidName(newName, error))
			return false;
		for (size_t other = 0; other < animator.States.size(); ++other)
		{
			if (other != index && animator.States[other].Name == newName)
			{
				error = "State names must be unique";
				return false;
			}
		}

		const std::string oldName = animator.States[index].Name;
		if (oldName == newName)
		{
			error.clear();
			return false;
		}
		animator.States[index].Name = std::move(newName);
		const std::string& replacement = animator.States[index].Name;
		if (animator.InitialState == oldName)
			animator.InitialState = replacement;
		for (AnimatorTransition& transition : animator.Transitions)
		{
			if (!transition.AnyState && transition.FromState == oldName)
				transition.FromState = replacement;
			if (transition.ToState == oldName)
				transition.ToState = replacement;
		}
		error.clear();
		return true;
	}

	inline bool RemoveState(SpriteAnimator& animator, size_t index,
		std::string& error)
	{
		if (index >= animator.States.size())
		{
			error = "State no longer exists";
			return false;
		}
		const std::string removedName = animator.States[index].Name;
		animator.States.erase(animator.States.begin()
			+ static_cast<std::ptrdiff_t>(index));
		animator.Transitions.erase(std::remove_if(animator.Transitions.begin(),
			animator.Transitions.end(), [&](const AnimatorTransition& transition)
			{
				return transition.ToState == removedName
					|| (!transition.AnyState && transition.FromState == removedName);
			}), animator.Transitions.end());
		if (animator.InitialState == removedName)
			animator.InitialState.clear();
		if (animator.States.empty())
		{
			animator.InitialState.clear();
			animator.Transitions.clear();
		}
		error.clear();
		return true;
	}

}
