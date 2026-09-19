#include "TomCat/Core/Input.h"
#include "TomCat/Core/InputEventQueue.h"
#include "TomCat/Core/KeyCodes.h"
#include "TomCat/Core/MouseCodes.h"
#include "TomCat/Editor/EditorShortcutRouter.h"
#include "TomCat/Events/KeyEvent.h"
#include "TomCat/Events/MouseEvent.h"
#include "TomCat/ImGui/ImGuiLayer.h"
#include "TomCat/Scripting/IScriptRuntime.h"
#include "TomCat/Scripting/ScriptEngine.h"
#include "TomCat/Scripting/ScriptGlue.h"

#include <imgui/imgui.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class RuntimeProbe final : public TomCat::Scripting::IScriptRuntime
	{
	public:
		bool IsReady() const override { return true; }
		TomCat::Scripting::ScriptStatus CreateSceneRuntime(uint64_t, uint64_t) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InstantiateAll(
			std::span<const TomCat::Scripting::NativeScriptAttachmentV1>) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus ApplySerializedFields(std::string_view) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus InvokeCreateAll() override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus SetEnabled(uint64_t, bool) override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus UpdateAll(float) override
		{
			if (OnUpdate)
				OnUpdate();
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus FixedUpdateAll(float) override
		{
			if (OnFixedUpdate)
				OnFixedUpdate();
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DispatchPhysicsEvents(
			std::span<const TomCat::Scripting::NativePhysicsEventV1>) override
		{
			if (OnPhysicsEvents)
				OnPhysicsEvents();
			return TomCat::Scripting::ScriptStatus::Success;
		}
		TomCat::Scripting::ScriptStatus DestroyAll() override
		{
			return TomCat::Scripting::ScriptStatus::Success;
		}

		std::function<void()> OnUpdate;
		std::function<void()> OnFixedUpdate;
		std::function<void()> OnPhysicsEvents;
	};

	struct InputObservation
	{
		bool KeyHeld = false;
		bool KeyPressed = false;
		bool KeyReleased = false;
		bool MousePressed = false;
		bool MouseReleased = false;
		TomCat::Scripting::NativeVector2 MouseDelta{};
		TomCat::Scripting::NativeVector2 ScrollDelta{};
	};

	InputObservation ObserveInput()
	{
		auto& engine = TomCat::Scripting::ScriptEngine::Get();
		return {
			engine.IsKeyHeld(65),
			engine.WasKeyPressed(65),
			engine.WasKeyReleased(65),
			engine.WasMouseButtonPressed(0),
			engine.WasMouseButtonReleased(0),
			engine.GetMouseDelta(),
			engine.GetScrollDelta()
		};
	}

	void RequireTransitions(const InputObservation& value, bool visible,
		const char* message)
	{
		Require(value.KeyPressed == visible && value.KeyReleased == visible
			&& value.MousePressed == visible && value.MouseReleased == visible,
			message);
		Require((value.MouseDelta.X != 0.0f) == visible
			&& (value.MouseDelta.Y != 0.0f) == visible
			&& (value.ScrollDelta.X != 0.0f) == visible
			&& (value.ScrollDelta.Y != 0.0f) == visible,
			message);
	}

	void TestOrderedQueuePreservesFastTap()
	{
		using Queue = TomCat::InputEventQueue;
		Queue queue;
		Require(queue.Push(Queue::Device::Keyboard, 65, Queue::Action::Pressed, 1.25),
			"valid key press was rejected");
		Require(queue.Push(Queue::Device::Keyboard, 65, Queue::Action::Repeated, 1.30),
			"valid key repeat was rejected");
		Require(queue.Push(Queue::Device::Keyboard, 65, Queue::Action::Released, 1.50),
			"valid key release was rejected");
		Require(queue.Push(Queue::Device::Mouse, 0, Queue::Action::Pressed, 1.75)
			&& queue.Push(Queue::Device::Mouse, 0, Queue::Action::Released, 2.0),
			"valid mouse tap was rejected");
		Require(!queue.Push(Queue::Device::Keyboard, Queue::KeyCount,
			Queue::Action::Pressed, 2.1), "out-of-range key was queued");
		Require(!queue.Push(Queue::Device::Mouse, Queue::MouseButtonCount,
			Queue::Action::Pressed, 2.2), "out-of-range mouse button was queued");
		Require(queue.Push(Queue::Device::GamepadConnection, 0,
			Queue::Action::Pressed, 2.25, 2)
			&& queue.Push(Queue::Device::GamepadButton, 4,
				Queue::Action::Pressed, 2.30, 2),
			"valid gamepad transitions were rejected");
		Require(!queue.Push(Queue::Device::GamepadButton,
			Queue::GamepadButtonCount, Queue::Action::Pressed, 2.31, 2)
			&& !queue.Push(Queue::Device::GamepadConnection, 0,
				Queue::Action::Pressed, 2.32, Queue::GamepadCount),
			"out-of-range gamepad transition was queued");
		Require(!queue.Push(Queue::Device::Keyboard, 66, Queue::Action::Pressed,
			std::numeric_limits<double>::quiet_NaN()), "non-finite timestamp was queued");

		const Queue::FrameSnapshot& frame = queue.Freeze();
		Require(frame.FrameNumber == 1 && frame.Events.size() == 7,
			"frame did not freeze exactly the accepted event batch");
		Require(frame.FirstSequence == 1 && frame.LastSequence == 7,
			"frame sequence range is incorrect");
		for (size_t index = 0; index < frame.Events.size(); ++index)
			Require(frame.Events[index].Sequence == index + 1,
				"event sequence is not contiguous and ordered");
		Require(frame.Events.front().Timestamp == 1.25
			&& frame.Events[4].Timestamp == 2.0
			&& frame.Events[5].Source == Queue::Device::GamepadConnection
			&& frame.Events[6].Source == Queue::Device::GamepadButton
			&& frame.Events[6].DeviceIndex == 2,
			"event timestamps were not preserved");
		Require(frame.WasPressed(Queue::Device::Keyboard, 65)
			&& frame.WasReleased(Queue::Device::Keyboard, 65)
			&& !frame.IsHeld(Queue::Device::Keyboard, 65),
			"press+release between polls lost a keyboard edge");
		Require(frame.WasPressed(Queue::Device::Mouse, 0)
			&& frame.WasReleased(Queue::Device::Mouse, 0)
			&& !frame.IsHeld(Queue::Device::Mouse, 0),
			"press+release between polls lost a mouse edge");

		const Queue::FrameSnapshot& emptyFrame = queue.Freeze();
		Require(emptyFrame.FrameNumber == 2 && emptyFrame.Events.empty()
			&& emptyFrame.FirstSequence == 0 && emptyFrame.LastSequence == 0
			&& !emptyFrame.WasPressed(Queue::Device::Keyboard, 65)
			&& !emptyFrame.WasReleased(Queue::Device::Keyboard, 65),
			"display-frame transitions leaked into the next frozen snapshot");
	}

	void TestFocusLossSynthesizesOrderedReleases()
	{
		using Queue = TomCat::InputEventQueue;
		Queue queue;
		queue.Push(Queue::Device::Keyboard, 87, Queue::Action::Pressed, 3.0);
		queue.Push(Queue::Device::Mouse, 1, Queue::Action::Pressed, 3.1);
		queue.Freeze();
		queue.ReleaseAll(4.0);
		const Queue::FrameSnapshot& frame = queue.Freeze();
		Require(frame.Events.size() == 2 && frame.Events[0].Sequence == 3
			&& frame.Events[1].Sequence == 4
			&& frame.Events[0].Timestamp == 4.0 && frame.Events[1].Timestamp == 4.0,
			"focus-loss releases did not retain queue order and timestamp");
		Require(frame.WasReleased(Queue::Device::Keyboard, 87)
			&& frame.WasReleased(Queue::Device::Mouse, 1)
			&& !frame.IsHeld(Queue::Device::Keyboard, 87)
			&& !frame.IsHeld(Queue::Device::Mouse, 1),
			"focus loss left a digital input latched");
	}

	void TestBoundedQueuePreservesDroppedEdgeSummary()
	{
		using Queue = TomCat::InputEventQueue;
		Queue queue;
		for (size_t index = 0; index < Queue::MaximumEventsPerFrame; ++index)
		{
			Require(queue.Push(Queue::Device::Keyboard, 0,
				Queue::Action::Repeated, static_cast<double>(index)),
				"valid capacity-filling event was rejected");
		}

		// Both transitions exceed the retained-event budget. The ordered payload is
		// bounded, but the frozen digital summary must still expose both edges and
		// the final held state so high-level input queries remain correct.
		Require(queue.Push(Queue::Device::Keyboard, 65,
			Queue::Action::Pressed, 20000.0)
			&& queue.Push(Queue::Device::Keyboard, 65,
				Queue::Action::Released, 20001.0)
			&& queue.Push(Queue::Device::GamepadConnection, 0,
				Queue::Action::Pressed, 20002.0, 2)
			&& queue.Push(Queue::Device::GamepadConnection, 0,
				Queue::Action::Released, 20003.0, 2)
			&& queue.Push(Queue::Device::GamepadButton, 4,
				Queue::Action::Pressed, 20004.0, 2)
			&& queue.Push(Queue::Device::GamepadButton, 4,
				Queue::Action::Released, 20005.0, 2),
			"valid overflow transitions were rejected");

		const Queue::FrameSnapshot& frame = queue.Freeze();
		Require(frame.Events.size() == Queue::MaximumEventsPerFrame,
			"frozen event payload exceeded or undershot its hard limit");
		Require(frame.DroppedEventCount == 6,
			"frozen snapshot did not report every discarded event");
		Require(frame.FirstSequence == 1
			&& frame.LastSequence == Queue::MaximumEventsPerFrame,
			"retained sequence range is incorrect after overflow");
		for (size_t index = 0; index < frame.Events.size(); ++index)
		{
			Require(frame.Events[index].Sequence == index + 1,
				"retained event sequence is not strictly monotonic");
		}
		Require(frame.WasPressed(Queue::Device::Keyboard, 65)
			&& frame.WasReleased(Queue::Device::Keyboard, 65)
			&& !frame.IsHeld(Queue::Device::Keyboard, 65),
			"discarded tail transitions were lost from the digital summary");
		Require(frame.WasGamepadConnected(2)
			&& frame.WasGamepadDisconnected(2)
			&& frame.WasGamepadButtonPressed(2, 4)
			&& frame.WasGamepadButtonReleased(2, 4),
			"discarded gamepad transitions were lost from the edge summary");

		Require(queue.Push(Queue::Device::Mouse, 0,
			Queue::Action::Pressed, 20006.0),
			"post-overflow event was rejected");
		const Queue::FrameSnapshot& nextFrame = queue.Freeze();
		const uint64_t expectedSequence = Queue::MaximumEventsPerFrame + 7;
		Require(nextFrame.Events.size() == 1
			&& nextFrame.Events[0].Sequence == expectedSequence
			&& nextFrame.FirstSequence == expectedSequence
			&& nextFrame.LastSequence == expectedSequence
			&& nextFrame.DroppedEventCount == 0,
			"sequence or overflow state did not advance cleanly across frames");
	}

	void TestGamepadHotPlugEntryPointPreservesOrderedEdges()
	{
		using Queue = TomCat::InputEventQueue;
		TomCat::Input::ClearState();
		TomCat::Input::NotifyGamepadConnection(2, true, 30.0);
		TomCat::Input::NotifyGamepadConnection(2, true, 30.5);
		TomCat::Input::NotifyGamepadConnection(2, false, 31.0);
		TomCat::Input::BeginFrame();

		const Queue::FrameSnapshot& frame = TomCat::Input::GetFrameSnapshot();
		Require(frame.Events.size() == 2,
			"gamepad hot-plug callback did not suppress a duplicate state");
		Require(frame.Events[0].Source == Queue::Device::GamepadConnection
			&& frame.Events[1].Source == Queue::Device::GamepadConnection
			&& frame.Events[0].DeviceIndex == 2
			&& frame.Events[1].DeviceIndex == 2
			&& frame.Events[0].Transition == Queue::Action::Pressed
			&& frame.Events[1].Transition == Queue::Action::Released
			&& frame.Events[0].Sequence < frame.Events[1].Sequence,
			"gamepad hot-plug callback did not preserve ordered connection edges");
		Require(frame.Events[0].Timestamp == 30.0
			&& frame.Events[1].Timestamp == 31.0,
			"gamepad hot-plug callback did not preserve event timestamps");
		Require(frame.WasGamepadConnected(2)
			&& frame.WasGamepadDisconnected(2),
			"gamepad hot-plug callback did not publish its frame edge summary");
		auto& engine = TomCat::Scripting::ScriptEngine::Get();
		engine.CaptureInputState();
		Require(engine.WasGamepadConnected(2)
			&& engine.WasGamepadDisconnected(2),
			"ScriptEngine lost same-frame gamepad connection edges");
		Require(engine.BeginFixedStep(404),
			"could not begin gamepad fixed-step input scope");
		Require(engine.WasGamepadConnected(2)
			&& engine.WasGamepadDisconnected(2),
			"fixed input batch lost same-frame gamepad connection edges");
		engine.EndFixedStep(404);
		Require(engine.BeginFixedStep(404),
			"could not begin gamepad catch-up input scope");
		Require(!engine.WasGamepadConnected(2)
			&& !engine.WasGamepadDisconnected(2),
			"catch-up fixed step replayed gamepad connection edges");
		engine.EndFixedStep(404);
		TomCat::Input::ClearState();
	}

	void TestManagedUpdateAndFixedConsumption()
	{
		using Action = TomCat::InputEventQueue::Action;
		TomCat::Input::ClearState();
		TomCat::Input::NotifyMousePosition(12.0f, 18.0f);
		TomCat::Input::NotifyScroll(2.0f, -3.0f);
		TomCat::Input::NotifyKey(65, Action::Pressed, 10.0);
		TomCat::Input::NotifyKey(65, Action::Released, 10.1);
		TomCat::Input::NotifyMouseButton(0, Action::Pressed, 10.2);
		TomCat::Input::NotifyMouseButton(0, Action::Released, 10.3);
		TomCat::Input::BeginFrame();

		auto& engine = TomCat::Scripting::ScriptEngine::Get();
		engine.CaptureInputState();
		RequireTransitions(ObserveInput(), true,
			"display-frame reader did not receive the frozen transitions");
		Require(!engine.IsKeyHeld(65),
			"fast key tap incorrectly remained held in the frozen snapshot");

		auto runtime = std::make_shared<RuntimeProbe>();
		std::vector<InputObservation> fixedObservations;
		std::vector<InputObservation> updateObservations;
		std::vector<std::vector<TomCat::Scripting::NativeInputEventV1>> fixedEvents;
		runtime->OnFixedUpdate = [&]()
		{
			fixedObservations.push_back(ObserveInput());
			fixedEvents.push_back(engine.GetInputEvents());
		};
		runtime->OnUpdate = [&]() { updateObservations.push_back(ObserveInput()); };
		engine.SetRuntime(runtime);

		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		engine.CaptureInputState(); // Duplicate capture of the same frozen frame.
		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		engine.FixedUpdateAll(202, 1.0f / 60.0f);
		engine.UpdateAll(101, 1.0f / 60.0f);

		Require(fixedObservations.size() == 4 && updateObservations.size() == 1,
			"script runtime did not receive the expected input observation callbacks");
		RequireTransitions(fixedObservations[0], true,
			"first FixedUpdate did not receive this frame's transitions");
		Require(fixedEvents[0].size() == 4
			&& fixedEvents[0][0].Action
				== TomCat::Scripting::NativeInputActionV1::Pressed
			&& fixedEvents[0][1].Action
				== TomCat::Scripting::NativeInputActionV1::Released
			&& fixedEvents[0][0].Sequence < fixedEvents[0][1].Sequence,
			"first FixedUpdate lost ordered event details");
		RequireTransitions(fixedObservations[1], false,
			"second FixedUpdate replayed this frame's transitions");
		RequireTransitions(fixedObservations[2], false,
			"duplicate CaptureInputState re-armed fixed-step transitions");
		RequireTransitions(fixedObservations[3], true,
			"independent scene did not receive its first fixed-step transitions");
		RequireTransitions(updateObservations[0], true,
			"FixedUpdate consumption removed transitions from Update");

		TomCat::Input::BeginFrame();
		engine.CaptureInputState();
		fixedObservations.clear();
		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		Require(fixedObservations.size() == 1,
			"next-frame FixedUpdate observation was not captured");
		RequireTransitions(fixedObservations[0], false,
			"input transitions leaked into the following display frame");

		TomCat::Input::NotifyKey(65, Action::Pressed, 11.0);
		TomCat::Input::BeginFrame();
		engine.CaptureInputState();
		Require(engine.IsKeyHeld(65) && engine.WasKeyPressed(65)
			&& !engine.WasKeyReleased(65),
			"held state and new-frame pressed edge disagree");
		TomCat::Input::NotifyWindowFocus(false, 11.5);
		TomCat::Input::BeginFrame();
		engine.CaptureInputState();
		Require(!engine.IsWindowFocused() && !engine.IsKeyHeld(65)
			&& engine.WasKeyReleased(65),
			"focus loss did not expose a release and clear held state");

		// No fixed step ran in the previous display frame. Its press must be
		// carried into this scene's next fixed tick together with the later release.
		fixedObservations.clear();
		fixedEvents.clear();
		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		engine.FixedUpdateAll(101, 1.0f / 60.0f);
		Require(fixedObservations.size() == 2
			&& fixedObservations[0].KeyPressed
			&& fixedObservations[0].KeyReleased
			&& !fixedObservations[1].KeyPressed
			&& !fixedObservations[1].KeyReleased,
			"FixedUpdate did not consume cross-display-frame transitions exactly once");
		Require(fixedEvents[0].size() == 2
			&& fixedEvents[0][0].FrameNumber < fixedEvents[0][1].FrameNumber
			&& fixedEvents[0][0].Action
				== TomCat::Scripting::NativeInputActionV1::Pressed
			&& fixedEvents[0][1].Action
				== TomCat::Scripting::NativeInputActionV1::Released
			&& fixedEvents[1].empty(),
			"FixedUpdate ordered batch did not span or consume display frames correctly");

		engine.SetRuntime({});
		TomCat::Input::ClearState();
	}

	void TestFixedStepScopeIncludesPhysicsCallbacks()
	{
		using Action = TomCat::InputEventQueue::Action;
		using NativeEvent = TomCat::Scripting::NativeInputEventV1;
		TomCat::Input::ClearState();
		TomCat::Input::NotifyKey(65, Action::Pressed, 40.0);
		TomCat::Input::NotifyKey(65, Action::Released, 40.1);
		TomCat::Input::BeginFrame();

		auto& engine = TomCat::Scripting::ScriptEngine::Get();
		engine.CaptureInputState();
		auto runtime = std::make_shared<RuntimeProbe>();
		std::vector<InputObservation> fixedObservations;
		std::vector<InputObservation> physicsObservations;
		std::vector<std::vector<NativeEvent>> fixedEvents;
		std::vector<std::vector<NativeEvent>> physicsEvents;
		runtime->OnFixedUpdate = [&]()
		{
			fixedObservations.push_back(ObserveInput());
			fixedEvents.push_back(engine.GetInputEvents());
		};
		runtime->OnPhysicsEvents = [&]()
		{
			physicsObservations.push_back(ObserveInput());
			physicsEvents.push_back(engine.GetInputEvents());
		};
		engine.SetRuntime(runtime);

		const TomCat::Scripting::NativePhysicsEventV1 collision{};
		auto runSubstep = [&]()
		{
			Require(engine.BeginFixedStep(303),
				"could not begin the fixed-step input scope");
			engine.FixedUpdateAll(303, 1.0f / 60.0f);
			engine.DispatchPhysicsEvents(303,
				std::span<const TomCat::Scripting::NativePhysicsEventV1>(
					&collision, 1));
			engine.EndFixedStep(303);
		};
		runSubstep();
		runSubstep();

		Require(fixedObservations.size() == 2
			&& physicsObservations.size() == 2,
			"fixed-step callbacks were not observed twice");
		Require(fixedObservations[0].KeyPressed
			&& fixedObservations[0].KeyReleased
			&& physicsObservations[0].KeyPressed
			&& physicsObservations[0].KeyReleased,
			"physics callbacks did not share the first substep's input edges");
		Require(fixedEvents[0].size() == 2
			&& physicsEvents[0].size() == 2
			&& fixedEvents[0][0].Sequence == physicsEvents[0][0].Sequence
			&& fixedEvents[0][1].Sequence == physicsEvents[0][1].Sequence,
			"FixedUpdate and physics callbacks did not share one ordered batch");
		Require(!fixedObservations[1].KeyPressed
			&& !fixedObservations[1].KeyReleased
			&& !physicsObservations[1].KeyPressed
			&& !physicsObservations[1].KeyReleased
			&& fixedEvents[1].empty() && physicsEvents[1].empty(),
			"catch-up physics callbacks replayed a consumed one-shot batch");
		Require(engine.WasKeyPressed(65) && engine.WasKeyReleased(65),
			"ending the fixed step did not restore the display-frame input reader");

		engine.SetRuntime({});
		TomCat::Input::ClearState();
	}

	void TestFixedStepRetainsHeldStateWithoutReplayingEdges()
	{
		using Action = TomCat::InputEventQueue::Action;
		TomCat::Input::ClearState();
		TomCat::Input::NotifyKey(65, Action::Pressed, 50.0);
		TomCat::Input::BeginFrame();

		auto& engine = TomCat::Scripting::ScriptEngine::Get();
		engine.CaptureInputState();
		auto runtime = std::make_shared<RuntimeProbe>();
		std::vector<InputObservation> observations;
		runtime->OnFixedUpdate = [&]() { observations.push_back(ObserveInput()); };
		engine.SetRuntime(runtime);

		engine.FixedUpdateAll(505, 1.0f / 60.0f);
		engine.FixedUpdateAll(505, 1.0f / 60.0f);
		Require(observations.size() == 2
			&& observations[0].KeyHeld && observations[1].KeyHeld,
			"catch-up fixed step did not retain the frozen held state");
		Require(observations[0].KeyPressed && !observations[0].KeyReleased
			&& !observations[1].KeyPressed && !observations[1].KeyReleased,
			"catch-up fixed step replayed the key press");

		observations.clear();
		TomCat::Input::NotifyKey(65, Action::Released, 51.0);
		TomCat::Input::BeginFrame();
		engine.CaptureInputState();
		engine.FixedUpdateAll(505, 1.0f / 60.0f);
		engine.FixedUpdateAll(505, 1.0f / 60.0f);
		Require(observations.size() == 2
			&& !observations[0].KeyHeld && !observations[1].KeyHeld,
			"catch-up fixed step did not retain the frozen released state");
		Require(!observations[0].KeyPressed && observations[0].KeyReleased
			&& !observations[1].KeyPressed && !observations[1].KeyReleased,
			"catch-up fixed step replayed the key release");

		engine.SetRuntime({});
		TomCat::Input::ClearState();
	}

	void TestInputEventsCapability()
	{
		using namespace TomCat::Scripting;
		NativeApiV2 envelope = BuildNativeApiV2();
		const std::string name(InputEventsCapabilityName);
		NativeUtf8View nameView{ reinterpret_cast<const uint8_t*>(name.data()),
			name.size() };
		NativeInputEventsApiV1 api;
		uint32_t required = 0;
		Require(envelope.QueryCapability(nameView, 1, &api, sizeof(api), &required)
			== 0 && required == sizeof(api) && api.Version == 1
			&& api.GetBatchInfo && api.CopyEvents,
			"ordered input event capability negotiation failed");
		Require(envelope.QueryCapability(nameView, 2, nullptr, 0, &required)
			== static_cast<int32_t>(ScriptStatus::VersionMismatch),
			"ordered input event capability accepted an unsupported version");

		TomCat::Input::ClearState();
		TomCat::Input::NotifyKey(65, TomCat::InputEventQueue::Action::Pressed, 20.0);
		TomCat::Input::NotifyKey(65, TomCat::InputEventQueue::Action::Released, 20.1);
		TomCat::Input::BeginFrame();
		auto& engine = ScriptEngine::Get();
		engine.CaptureInputState();
		NativeInputEventBatchInfoV1 info;
		Require(api.GetBatchInfo(&info) == 0 && info.EventCount == 2
			&& info.FirstSequence != 0 && info.LastSequence > info.FirstSequence,
			"ordered input event batch metadata is incorrect");
		std::vector<NativeInputEventV1> events(info.EventCount);
		uint32_t count = 0;
		Require(api.CopyEvents(events.data(), static_cast<uint32_t>(events.size()),
			&count) == 0 && count == events.size()
			&& events[0].Action == NativeInputActionV1::Pressed
			&& events[1].Action == NativeInputActionV1::Released
			&& events[0].TimestampSeconds == 20.0
			&& events[1].TimestampSeconds == 20.1,
			"ordered input event ABI did not preserve sequence and timestamps");
		TomCat::Input::ClearState();
	}

	void TestEditorShortcutRouting()
	{
		using Action = TomCat::EditorShortcutAction;
		TomCat::EditorShortcutContext context;
		context.EntityContextFocused = true;
		context.HasSelection = true;
		context.EditingScene = true;

		auto resolve = [&](int keyCode, TomCat::InputModifiers modifiers = {})
		{
			context.KeyCode = keyCode;
			context.Modifiers = modifiers;
			return TomCat::ResolveEditorShortcut(context);
		};
		const TomCat::InputModifiers control{ true, false, false, false };
		const TomCat::InputModifiers controlShift{ true, true, false, false };

		Require(resolve(TomCat::Key::N, control) == Action::NewScene
			&& resolve(TomCat::Key::O, control) == Action::OpenScene
			&& resolve(TomCat::Key::S, control) == Action::SaveScene
			&& resolve(TomCat::Key::S, controlShift) == Action::SaveSceneAs,
			"global scene file shortcuts were not routed");
		Require(resolve(TomCat::Key::Q) == Action::ToolNone
			&& resolve(TomCat::Key::W) == Action::ToolTranslate
			&& resolve(TomCat::Key::E) == Action::ToolRotate
			&& resolve(TomCat::Key::R) == Action::ToolScale
			&& resolve(TomCat::Key::F) == Action::FrameSelection,
			"selected-entity scene tool shortcuts were not routed");
		Require(resolve(TomCat::Key::F2) == Action::RenameSelection
			&& resolve(TomCat::Key::Delete) == Action::DeleteSelection,
			"selected-entity rename/delete shortcuts were not routed");
		Require(resolve(TomCat::Key::X, control) == Action::CutSelection
			&& resolve(TomCat::Key::C, control) == Action::CopySelection
			&& resolve(TomCat::Key::V, control) == Action::PasteSelection
			&& resolve(TomCat::Key::D, control) == Action::DuplicateSelection,
			"selected-entity clipboard shortcuts were not routed");
		Require(resolve(TomCat::Key::Tab, control) == Action::NextWindow
			&& resolve(TomCat::Key::Tab, controlShift) == Action::PreviousWindow,
			"editor panel cycling shortcuts were not routed");
		Require(resolve(TomCat::Key::Z, control) == Action::Undo
			&& resolve(TomCat::Key::Y, control) == Action::Redo,
			"editor history shortcuts were not routed");
		context.EditingScene = false;
		Require(resolve(TomCat::Key::Z, control) == Action::None
			&& resolve(TomCat::Key::Y, control) == Action::None,
			"editor history shortcuts ran outside edit mode");
		context.EditingScene = true;

		context.WantsTextInput = true;
		Require(resolve(TomCat::Key::C, control) == Action::None
			&& resolve(TomCat::Key::Delete) == Action::None
			&& resolve(TomCat::Key::W) == Action::None,
			"text input did not retain entity shortcut ownership");
		Require(resolve(TomCat::Key::S, control) == Action::SaveScene,
			"global save shortcut was lost while editing text");
		context.WantsTextInput = false;

		context.PopupOpen = true;
		Require(resolve(TomCat::Key::S, control) == Action::None
			&& resolve(TomCat::Key::Delete) == Action::None,
			"open popup did not suspend editor shortcuts");
		context.PopupOpen = false;

		context.RepeatCount = 1;
		Require(resolve(TomCat::Key::Delete) == Action::None,
			"repeated key press retriggered an entity command");
		context.RepeatCount = 0;

		const TomCat::InputModifiers controlAlt{ true, false, true, false };
		Require(resolve(TomCat::Key::D, controlAlt) == Action::None,
			"extra modifiers triggered an entity command");

		context.HasSelection = false;
		Require(resolve(TomCat::Key::F) == Action::None
			&& resolve(TomCat::Key::F2) == Action::None
			&& resolve(TomCat::Key::Delete) == Action::None
			&& resolve(TomCat::Key::D, control) == Action::None,
			"selection-only shortcuts ran without a selected entity");
		context.HasSelection = true;

		context.TransformDragActive = true;
		Require(resolve(TomCat::Key::W) == Action::None
			&& resolve(TomCat::Key::F) == Action::None,
			"transform drag allowed a conflicting tool shortcut");
		context.TransformDragActive = false;

		context.EntityContextFocused = false;
		Require(resolve(TomCat::Key::Delete) == Action::None
			&& resolve(TomCat::Key::W) == Action::None,
			"entity shortcuts leaked into an unrelated editor panel");
	}

	void TestImGuiEventCaptureChannels()
	{
		ImGuiContext* previousContext = ImGui::GetCurrentContext();
		ImGuiContext* testContext = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.WantCaptureKeyboard = true;
		io.WantCaptureMouse = true;

		TomCat::ImGuiLayer layer;
		layer.BlockMouseEvents(true);
		layer.BlockKeyboardEvents(false);
		TomCat::KeyPressedEvent key(TomCat::Key::W, 0, {});
		TomCat::MouseButtonPressedEvent mouse(TomCat::Mouse::ButtonLeft, {});
		layer.OnEvent(key);
		layer.OnEvent(mouse);
		Require(!key.m_Handled && mouse.m_Handled,
			"keyboard shortcut channel was captured with the mouse channel");

		layer.BlockKeyboardEvents(true);
		TomCat::KeyPressedEvent capturedKey(TomCat::Key::W, 0, {});
		layer.OnEvent(capturedKey);
		Require(capturedKey.m_Handled,
			"enabled keyboard capture did not consume keyboard input");

		layer.BlockMouseEvents(false);
		TomCat::MouseButtonPressedEvent releasedMouse(
			TomCat::Mouse::ButtonLeft, {});
		layer.OnEvent(releasedMouse);
		Require(!releasedMouse.m_Handled,
			"disabled mouse capture still consumed mouse input");

		ImGui::DestroyContext(testContext);
		ImGui::SetCurrentContext(previousContext);
	}

}

int main()
{
	try
	{
		TestOrderedQueuePreservesFastTap();
		TestFocusLossSynthesizesOrderedReleases();
		TestBoundedQueuePreservesDroppedEdgeSummary();
		TestGamepadHotPlugEntryPointPreservesOrderedEdges();
		TestManagedUpdateAndFixedConsumption();
		TestFixedStepScopeIncludesPhysicsCallbacks();
		TestFixedStepRetainsHeldStateWithoutReplayingEdges();
		TestInputEventsCapability();
		TestEditorShortcutRouting();
		TestImGuiEventCaptureChannels();
		std::cout << "Input regression suite passed." << std::endl;
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "Input regression failure: " << exception.what() << std::endl;
		return 1;
	}
}
