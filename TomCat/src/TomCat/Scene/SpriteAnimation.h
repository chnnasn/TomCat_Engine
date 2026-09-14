#pragma once

#include "Components.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace TomCat {

	// Deterministic clip playback plus a deliberately small Animator state
	// machine. Transition declaration order breaks ties; elapsed-time crossings
	// are processed at their exact boundary so display-frame partitioning does not
	// change the resulting state or frame.
	class SpriteAnimatorRuntime final
	{
	public:
		static void Reset(SpriteAnimator& animator)
		{
			animator.RuntimeClipIndex = SpriteAnimator::InvalidClipIndex;
			animator.RuntimeFrameIndex = 0;
			animator.RuntimeFrameElapsed = 0.0;
			animator.RuntimePlaying = false;
			animator.RuntimeInitialized = false;
			animator.RuntimeStateIndex = SpriteAnimator::InvalidClipIndex;
			animator.RuntimeStateElapsed = 0.0;
			for (AnimatorParameter& parameter : animator.Parameters)
			{
				if (parameter.Type == AnimatorParameterType::Trigger)
					parameter.BoolValue = false;
			}
		}

		static void Initialize(SpriteAnimator& animator, SpriteRenderer& renderer)
		{
			Reset(animator);
			// Disabled animators retain the authored SpriteRenderer. Leaving the
			// runtime uninitialized makes the first enabled Update apply PlayOnStart.
			if (!animator.Enabled)
				return;
			animator.RuntimeInitialized = true;
			if (!animator.PlayOnStart)
				return;
			if (!animator.States.empty())
			{
				size_t stateIndex = 0;
				if (!animator.InitialState.empty())
				{
					const size_t found = FindState(animator, animator.InitialState);
					if (found == InvalidIndex)
						return;
					stateIndex = found;
				}
				(void)EnterState(animator, renderer, stateIndex);
				return;
			}
			if (animator.InitialClip.empty())
				(void)PlayClip(animator, renderer, size_t{ 0 }, true);
			else
				(void)PlayClipByName(animator, renderer, animator.InitialClip, true);
		}

		static bool Play(SpriteAnimator& animator, SpriteRenderer& renderer,
			std::string_view clipName, bool restart = true)
		{
			animator.RuntimeStateIndex = SpriteAnimator::InvalidClipIndex;
			animator.RuntimeStateElapsed = 0.0;
			return PlayClipByName(animator, renderer, clipName, restart);
		}

		static bool Play(SpriteAnimator& animator, SpriteRenderer& renderer,
			size_t clipIndex, bool restart = true)
		{
			animator.RuntimeStateIndex = SpriteAnimator::InvalidClipIndex;
			animator.RuntimeStateElapsed = 0.0;
			return PlayClip(animator, renderer, clipIndex, restart);
		}

		static void Stop(SpriteAnimator& animator)
		{
			animator.RuntimePlaying = false;
		}

		static bool SetBool(SpriteAnimator& animator, std::string_view name, bool value)
		{
			AnimatorParameter* parameter = FindParameter(animator, name);
			if (!parameter || parameter->Type != AnimatorParameterType::Bool)
				return false;
			parameter->BoolValue = value;
			return true;
		}

		static bool SetInt(SpriteAnimator& animator, std::string_view name, int32_t value)
		{
			AnimatorParameter* parameter = FindParameter(animator, name);
			if (!parameter || parameter->Type != AnimatorParameterType::Int)
				return false;
			parameter->IntValue = value;
			return true;
		}

		static bool SetFloat(SpriteAnimator& animator, std::string_view name, float value)
		{
			AnimatorParameter* parameter = FindParameter(animator, name);
			if (!parameter || parameter->Type != AnimatorParameterType::Float
				|| !std::isfinite(value))
				return false;
			parameter->FloatValue = value;
			return true;
		}

		static bool SetTrigger(SpriteAnimator& animator, std::string_view name)
		{
			AnimatorParameter* parameter = FindParameter(animator, name);
			if (!parameter || parameter->Type != AnimatorParameterType::Trigger)
				return false;
			parameter->BoolValue = true;
			return true;
		}

		static bool ResetTrigger(SpriteAnimator& animator, std::string_view name)
		{
			AnimatorParameter* parameter = FindParameter(animator, name);
			if (!parameter || parameter->Type != AnimatorParameterType::Trigger)
				return false;
			parameter->BoolValue = false;
			return true;
		}

		static std::string_view CurrentState(const SpriteAnimator& animator)
		{
			return animator.RuntimeStateIndex < animator.States.size()
				? std::string_view(animator.States[animator.RuntimeStateIndex].Name)
				: std::string_view{};
		}

		static void Update(SpriteAnimator& animator, SpriteRenderer& renderer,
			double deltaSeconds)
		{
			// Check Enabled before lazy initialization so an initially disabled
			// animator cannot apply its first frame from the fixed-step update.
			if (!animator.Enabled)
				return;
			if (!animator.RuntimeInitialized)
				Initialize(animator, renderer);
			if (!std::isfinite(deltaSeconds)
				|| deltaSeconds <= 0.0 || !std::isfinite(animator.Speed)
				|| animator.Speed <= 0.0f)
				return;
			// Stop must freeze both clip playback and the state-machine clock. Without
			// this guard, a stopped state continued to satisfy conditions/exit times
			// and EnterState restarted playback on a later update.
			if (!animator.RuntimePlaying)
				return;

			double remaining = deltaSeconds * static_cast<double>(animator.Speed);
			if (!std::isfinite(remaining))
			{
				Stop(animator);
				return;
			}
			if (animator.States.empty()
				|| animator.RuntimeStateIndex >= animator.States.size())
			{
				AdvanceClip(animator, renderer, remaining);
				return;
			}

			for (uint32_t transitionCount = 0;
				remaining > 0.0 && transitionCount < 64; ++transitionCount)
			{
				const size_t currentState = animator.RuntimeStateIndex;
				const AnimatorTransition* immediate = FindReadyTransition(animator,
					currentState);
				if (immediate)
				{
					if (!ApplyTransition(animator, renderer, *immediate))
						return;
					continue;
				}

				const AnimatorState& state = animator.States[currentState];
				if (!std::isfinite(state.Speed) || state.Speed <= 0.0f)
					return;
				const double scaledRemaining = remaining
					* static_cast<double>(state.Speed);
				double boundary = (std::numeric_limits<double>::infinity)();
				const AnimatorTransition* scheduled = FindNextTimedTransition(
					animator, currentState, boundary);
				if (scheduled && boundary <= animator.RuntimeStateElapsed
					+ scaledRemaining)
				{
					const double advance = std::max(0.0,
						boundary - animator.RuntimeStateElapsed);
					AdvanceClip(animator, renderer, advance);
					animator.RuntimeStateElapsed += advance;
					remaining -= advance / static_cast<double>(state.Speed);
					if (remaining < 1.0e-12)
						remaining = 0.0;
					if (!ApplyTransition(animator, renderer, *scheduled))
						return;
					continue;
				}

				AdvanceClip(animator, renderer, scaledRemaining);
				animator.RuntimeStateElapsed += scaledRemaining;
				remaining = 0.0;
			}
		}

	private:
		static constexpr size_t InvalidIndex = (std::numeric_limits<size_t>::max)();

		static AnimatorParameter* FindParameter(SpriteAnimator& animator,
			std::string_view name)
		{
			for (AnimatorParameter& parameter : animator.Parameters)
				if (parameter.Name == name)
					return &parameter;
			return nullptr;
		}

		static const AnimatorParameter* FindParameter(const SpriteAnimator& animator,
			std::string_view name)
		{
			for (const AnimatorParameter& parameter : animator.Parameters)
				if (parameter.Name == name)
					return &parameter;
			return nullptr;
		}

		static size_t FindState(const SpriteAnimator& animator, std::string_view name)
		{
			for (size_t index = 0; index < animator.States.size(); ++index)
				if (animator.States[index].Name == name)
					return index;
			return InvalidIndex;
		}

		static size_t FindClip(const SpriteAnimator& animator, std::string_view name)
		{
			for (size_t index = 0; index < animator.Clips.size(); ++index)
				if (animator.Clips[index].Name == name)
					return index;
			return InvalidIndex;
		}

		static bool PlayClipByName(SpriteAnimator& animator, SpriteRenderer& renderer,
			std::string_view clipName, bool restart)
		{
			const size_t index = FindClip(animator, clipName);
			return index != InvalidIndex && PlayClip(animator, renderer, index, restart);
		}

		static bool PlayClip(SpriteAnimator& animator, SpriteRenderer& renderer,
			size_t clipIndex, bool restart)
		{
			if (clipIndex >= animator.Clips.size() || !IsPlayable(animator.Clips[clipIndex]))
				return false;
			if (!restart && animator.RuntimeClipIndex == clipIndex)
			{
				animator.RuntimePlaying = true;
				animator.RuntimeInitialized = true;
				return true;
			}
			animator.RuntimeClipIndex = static_cast<uint32_t>(clipIndex);
			animator.RuntimeFrameIndex = 0;
			animator.RuntimeFrameElapsed = 0.0;
			animator.RuntimePlaying = true;
			animator.RuntimeInitialized = true;
			ApplyCurrentFrame(animator, renderer);
			return true;
		}

		static bool EnterState(SpriteAnimator& animator, SpriteRenderer& renderer,
			size_t stateIndex)
		{
			if (stateIndex >= animator.States.size())
				return false;
			const size_t clipIndex = FindClip(animator, animator.States[stateIndex].Clip);
			if (clipIndex == InvalidIndex || !PlayClip(animator, renderer, clipIndex, true))
				return false;
			animator.RuntimeStateIndex = static_cast<uint32_t>(stateIndex);
			animator.RuntimeStateElapsed = 0.0;
			return true;
		}

		static double ClipDuration(const SpriteAnimationClip& clip)
		{
			double duration = 0.0;
			for (const SpriteAnimationFrame& frame : clip.Frames)
				duration += frame.DurationSeconds;
			return duration;
		}

		static bool ConditionsMatch(const SpriteAnimator& animator,
			const AnimatorTransition& transition)
		{
			for (const AnimatorCondition& condition : transition.Conditions)
			{
				const AnimatorParameter* parameter = FindParameter(animator,
					condition.Parameter);
				if (!parameter)
					return false;
				bool matches = false;
				switch (parameter->Type)
				{
					case AnimatorParameterType::Bool:
					case AnimatorParameterType::Trigger:
						matches = condition.Mode == AnimatorConditionMode::If
							? parameter->BoolValue
							: condition.Mode == AnimatorConditionMode::IfNot
								&& !parameter->BoolValue;
						break;
					case AnimatorParameterType::Int:
					{
						const float value = static_cast<float>(parameter->IntValue);
						matches = CompareNumber(value, condition.Mode, condition.Threshold);
						break;
					}
					case AnimatorParameterType::Float:
						matches = CompareNumber(parameter->FloatValue,
							condition.Mode, condition.Threshold);
						break;
				}
				if (!matches)
					return false;
			}
			return true;
		}

		static bool CompareNumber(float value, AnimatorConditionMode mode,
			float threshold)
		{
			switch (mode)
			{
				case AnimatorConditionMode::Greater: return value > threshold;
				case AnimatorConditionMode::Less: return value < threshold;
				case AnimatorConditionMode::Equals: return value == threshold;
				case AnimatorConditionMode::NotEqual: return value != threshold;
				default: return false;
			}
		}

		static bool SourceMatches(const SpriteAnimator& animator,
			const AnimatorTransition& transition, size_t stateIndex)
		{
			return transition.AnyState
				? transition.ToState != animator.States[stateIndex].Name
				: transition.FromState == animator.States[stateIndex].Name;
		}

		static double TransitionBoundary(const SpriteAnimator& animator,
			const AnimatorTransition& transition)
		{
			if (transition.ExitTime < 0.0f
				|| animator.RuntimeClipIndex >= animator.Clips.size())
				return -1.0;
			return ClipDuration(animator.Clips[animator.RuntimeClipIndex])
				* static_cast<double>(transition.ExitTime);
		}

		static const AnimatorTransition* FindReadyTransition(
			const SpriteAnimator& animator, size_t stateIndex)
		{
			for (const AnimatorTransition& transition : animator.Transitions)
			{
				if (!SourceMatches(animator, transition, stateIndex)
					|| !ConditionsMatch(animator, transition))
					continue;
				const double boundary = TransitionBoundary(animator, transition);
				if (boundary < 0.0 || animator.RuntimeStateElapsed + 1.0e-12 >= boundary)
					return &transition;
			}
			return nullptr;
		}

		static const AnimatorTransition* FindNextTimedTransition(
			const SpriteAnimator& animator, size_t stateIndex, double& boundary)
		{
			const AnimatorTransition* best = nullptr;
			for (const AnimatorTransition& transition : animator.Transitions)
			{
				if (!SourceMatches(animator, transition, stateIndex)
					|| transition.ExitTime < 0.0f
					|| !ConditionsMatch(animator, transition))
					continue;
				const double candidate = TransitionBoundary(animator, transition);
				if (candidate > animator.RuntimeStateElapsed + 1.0e-12
					&& candidate < boundary)
				{
					boundary = candidate;
					best = &transition;
				}
			}
			return best;
		}

		static void ConsumeTriggers(SpriteAnimator& animator,
			const AnimatorTransition& transition)
		{
			for (const AnimatorCondition& condition : transition.Conditions)
			{
				AnimatorParameter* parameter = FindParameter(animator,
					condition.Parameter);
				if (parameter && parameter->Type == AnimatorParameterType::Trigger)
					parameter->BoolValue = false;
			}
		}

		static bool ApplyTransition(SpriteAnimator& animator, SpriteRenderer& renderer,
			const AnimatorTransition& transition)
		{
			const size_t target = FindState(animator, transition.ToState);
			if (target == InvalidIndex)
				return false;
			ConsumeTriggers(animator, transition);
			return EnterState(animator, renderer, target);
		}

		static bool IsPlayable(const SpriteAnimationClip& clip)
		{
			if (clip.Frames.empty())
				return false;
			for (const SpriteAnimationFrame& frame : clip.Frames)
				if (!std::isfinite(frame.DurationSeconds) || frame.DurationSeconds <= 0.0f)
					return false;
			return true;
		}

		static void AdvanceClip(SpriteAnimator& animator, SpriteRenderer& renderer,
			double elapsed)
		{
			if (!animator.RuntimePlaying || elapsed <= 0.0
				|| animator.RuntimeClipIndex >= animator.Clips.size())
				return;
			const SpriteAnimationClip& clip = animator.Clips[animator.RuntimeClipIndex];
			if (!IsPlayable(clip) || animator.RuntimeFrameIndex >= clip.Frames.size())
			{
				Stop(animator);
				return;
			}
			elapsed += animator.RuntimeFrameElapsed;
			if (!std::isfinite(elapsed))
			{
				Stop(animator);
				return;
			}
			if (clip.Loop)
			{
				const double duration = ClipDuration(clip);
				if (elapsed >= duration)
					elapsed = std::fmod(elapsed, duration);
			}
			bool changed = false;
			while (elapsed >= clip.Frames[animator.RuntimeFrameIndex].DurationSeconds)
			{
				elapsed -= clip.Frames[animator.RuntimeFrameIndex].DurationSeconds;
				if (animator.RuntimeFrameIndex + 1 < clip.Frames.size())
				{
					++animator.RuntimeFrameIndex;
					changed = true;
				}
				else if (clip.Loop)
				{
					animator.RuntimeFrameIndex = 0;
					changed = true;
				}
				else
				{
					animator.RuntimeFrameElapsed = clip.Frames.back().DurationSeconds;
					animator.RuntimePlaying = false;
					ApplyCurrentFrame(animator, renderer);
					return;
				}
			}
			animator.RuntimeFrameElapsed = elapsed;
			if (changed)
				ApplyCurrentFrame(animator, renderer);
		}

		static void ApplyCurrentFrame(const SpriteAnimator& animator,
			SpriteRenderer& renderer)
		{
			if (animator.RuntimeClipIndex >= animator.Clips.size())
				return;
			const SpriteAnimationClip& clip = animator.Clips[animator.RuntimeClipIndex];
			if (animator.RuntimeFrameIndex >= clip.Frames.size())
				return;
			const AssetHandle handle = clip.Frames[animator.RuntimeFrameIndex].SpriteHandle;
			if (renderer.SpriteHandle != handle)
			{
				renderer.SpriteHandle = handle;
				renderer.Sprite.reset();
			}
		}
	};

}
