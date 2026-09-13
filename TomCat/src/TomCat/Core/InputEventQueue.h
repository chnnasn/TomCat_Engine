#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace TomCat {

	// Single-producer queue owned by the window thread. GLFW callbacks append
	// transitions while PollEvents runs; Freeze publishes one immutable display-
	// frame snapshot after polling has completed.
	class InputEventQueue final
	{
	public:
		static constexpr uint32_t KeyCount = 512;
		static constexpr uint32_t MouseButtonCount = 8;
		static constexpr uint32_t GamepadCount = 16;
		static constexpr uint32_t GamepadButtonCount = 15;
		static constexpr size_t MaximumEventsPerFrame = 16384;

		enum class Device : uint8_t
		{
			Keyboard,
			Mouse,
			GamepadConnection,
			GamepadButton
		};

		enum class Action : uint8_t
		{
			Pressed,
			Released,
			Repeated
		};

		struct Event
		{
			uint64_t Sequence = 0;
			double Timestamp = 0.0;
			uint32_t Code = 0;
			uint32_t DeviceIndex = 0;
			Device Source = Device::Keyboard;
			Action Transition = Action::Pressed;
		};

		struct FrameSnapshot
		{
			uint64_t FrameNumber = 0;
			uint64_t FirstSequence = 0;
			uint64_t LastSequence = 0;
			uint64_t DroppedEventCount = 0;
			std::vector<Event> Events;
			std::array<bool, KeyCount> KeysHeld{};
			std::array<bool, KeyCount> KeysPressed{};
			std::array<bool, KeyCount> KeysReleased{};
			std::array<bool, MouseButtonCount> MouseButtonsHeld{};
			std::array<bool, MouseButtonCount> MouseButtonsPressed{};
			std::array<bool, MouseButtonCount> MouseButtonsReleased{};
			std::array<bool, GamepadCount> GamepadsConnected{};
			std::array<bool, GamepadCount> GamepadsDisconnected{};
			std::array<std::array<bool, GamepadButtonCount>, GamepadCount>
				GamepadButtonsPressed{};
			std::array<std::array<bool, GamepadButtonCount>, GamepadCount>
				GamepadButtonsReleased{};

			bool IsHeld(Device device, uint32_t code) const
			{
				if (device == Device::Mouse)
					return code < MouseButtonCount && MouseButtonsHeld[code];
				return device == Device::Keyboard && code < KeyCount && KeysHeld[code];
			}

			bool WasPressed(Device device, uint32_t code) const
			{
				if (device == Device::Mouse)
					return code < MouseButtonCount && MouseButtonsPressed[code];
				return device == Device::Keyboard && code < KeyCount
					&& KeysPressed[code];
			}

			bool WasReleased(Device device, uint32_t code) const
			{
				if (device == Device::Mouse)
					return code < MouseButtonCount && MouseButtonsReleased[code];
				return device == Device::Keyboard && code < KeyCount
					&& KeysReleased[code];
			}

			bool WasGamepadConnected(uint32_t gamepad) const
			{
				return gamepad < GamepadCount && GamepadsConnected[gamepad];
			}

			bool WasGamepadDisconnected(uint32_t gamepad) const
			{
				return gamepad < GamepadCount && GamepadsDisconnected[gamepad];
			}

			bool WasGamepadButtonPressed(uint32_t gamepad, uint32_t button) const
			{
				return gamepad < GamepadCount && button < GamepadButtonCount
					&& GamepadButtonsPressed[gamepad][button];
			}

			bool WasGamepadButtonReleased(uint32_t gamepad, uint32_t button) const
			{
				return gamepad < GamepadCount && button < GamepadButtonCount
					&& GamepadButtonsReleased[gamepad][button];
			}
		};

		bool Push(Device device, uint32_t code, Action action, double timestamp,
			uint32_t deviceIndex = 0)
		{
			if (!std::isfinite(timestamp)
				|| !IsValidCode(device, code, deviceIndex))
				return false;

			const uint64_t sequence = ++m_LastSequence;
			if (device == Device::Keyboard || device == Device::Mouse)
				SetLiveHeld(device, code, action != Action::Released);
			if (action != Action::Repeated)
				SetPendingTransition(device, code, deviceIndex,
					action == Action::Pressed);
			if (m_PendingEvents.size() < MaximumEventsPerFrame)
				m_PendingEvents.push_back({ sequence, timestamp, code, deviceIndex,
					device, action });
			else
				++m_PendingDroppedEventCount;
			return true;
		}

		// Focus loss must not leave a held key/button latched. Synthetic releases
		// retain normal sequence ordering and the focus callback's timestamp.
		void ReleaseAll(double timestamp)
		{
			if (!std::isfinite(timestamp))
				return;
			for (uint32_t key = 0; key < KeyCount; ++key)
			{
				if (m_LiveKeysHeld[key])
					Push(Device::Keyboard, key, Action::Released, timestamp);
			}
			for (uint32_t button = 0; button < MouseButtonCount; ++button)
			{
				if (m_LiveMouseButtonsHeld[button])
					Push(Device::Mouse, button, Action::Released, timestamp);
			}
		}

		const FrameSnapshot& Freeze()
		{
			++m_FrameNumber;
			m_Snapshot.FrameNumber = m_FrameNumber;
			m_Snapshot.FirstSequence = m_PendingEvents.empty()
				? 0 : m_PendingEvents.front().Sequence;
			m_Snapshot.LastSequence = m_PendingEvents.empty()
				? 0 : m_PendingEvents.back().Sequence;
			m_Snapshot.DroppedEventCount = m_PendingDroppedEventCount;
			m_Snapshot.Events.swap(m_PendingEvents);
			m_PendingEvents.clear();
			m_Snapshot.KeysHeld = m_LiveKeysHeld;
			m_Snapshot.MouseButtonsHeld = m_LiveMouseButtonsHeld;
			m_Snapshot.KeysPressed = m_PendingKeysPressed;
			m_Snapshot.KeysReleased = m_PendingKeysReleased;
			m_Snapshot.MouseButtonsPressed = m_PendingMouseButtonsPressed;
			m_Snapshot.MouseButtonsReleased = m_PendingMouseButtonsReleased;
			m_Snapshot.GamepadsConnected = m_PendingGamepadsConnected;
			m_Snapshot.GamepadsDisconnected = m_PendingGamepadsDisconnected;
			m_Snapshot.GamepadButtonsPressed = m_PendingGamepadButtonsPressed;
			m_Snapshot.GamepadButtonsReleased = m_PendingGamepadButtonsReleased;
			m_PendingKeysPressed.fill(false);
			m_PendingKeysReleased.fill(false);
			m_PendingMouseButtonsPressed.fill(false);
			m_PendingMouseButtonsReleased.fill(false);
			m_PendingGamepadsConnected.fill(false);
			m_PendingGamepadsDisconnected.fill(false);
			m_PendingGamepadButtonsPressed = {};
			m_PendingGamepadButtonsReleased = {};
			m_PendingDroppedEventCount = 0;
			return m_Snapshot;
		}

		// Clears device state between window lifetimes while keeping frame and
		// event identifiers monotonic for consumers that outlive a Window.
		void ClearState()
		{
			m_PendingEvents.clear();
			m_Snapshot.Events.clear();
			m_LiveKeysHeld.fill(false);
			m_LiveMouseButtonsHeld.fill(false);
			m_Snapshot.KeysHeld.fill(false);
			m_Snapshot.KeysPressed.fill(false);
			m_Snapshot.KeysReleased.fill(false);
			m_Snapshot.MouseButtonsHeld.fill(false);
			m_Snapshot.MouseButtonsPressed.fill(false);
			m_Snapshot.MouseButtonsReleased.fill(false);
			m_Snapshot.GamepadsConnected.fill(false);
			m_Snapshot.GamepadsDisconnected.fill(false);
			m_Snapshot.GamepadButtonsPressed = {};
			m_Snapshot.GamepadButtonsReleased = {};
			m_Snapshot.FirstSequence = 0;
			m_Snapshot.LastSequence = 0;
			m_Snapshot.DroppedEventCount = 0;
			m_PendingKeysPressed.fill(false);
			m_PendingKeysReleased.fill(false);
			m_PendingMouseButtonsPressed.fill(false);
			m_PendingMouseButtonsReleased.fill(false);
			m_PendingGamepadsConnected.fill(false);
			m_PendingGamepadsDisconnected.fill(false);
			m_PendingGamepadButtonsPressed = {};
			m_PendingGamepadButtonsReleased = {};
			m_PendingDroppedEventCount = 0;
			m_Snapshot.FrameNumber = ++m_FrameNumber;
		}

		const FrameSnapshot& GetSnapshot() const { return m_Snapshot; }

		bool IsLiveHeld(Device device, uint32_t code) const
		{
			return device == Device::Mouse
				? code < MouseButtonCount && m_LiveMouseButtonsHeld[code]
				: code < KeyCount && m_LiveKeysHeld[code];
		}

	private:
		static bool IsValidCode(Device device, uint32_t code,
			uint32_t deviceIndex)
		{
			switch (device)
			{
				case Device::Keyboard:
					return deviceIndex == 0 && code < KeyCount;
				case Device::Mouse:
					return deviceIndex == 0 && code < MouseButtonCount;
				case Device::GamepadConnection:
					return deviceIndex < GamepadCount && code == 0;
				case Device::GamepadButton:
					return deviceIndex < GamepadCount && code < GamepadButtonCount;
			}
			return false;
		}

		void SetLiveHeld(Device device, uint32_t code, bool held)
		{
			if (device == Device::Mouse)
				m_LiveMouseButtonsHeld[code] = held;
			else
				m_LiveKeysHeld[code] = held;
		}

		void SetPendingTransition(Device device, uint32_t code,
			uint32_t deviceIndex, bool pressed)
		{
			switch (device)
			{
				case Device::Keyboard:
					(pressed ? m_PendingKeysPressed
						: m_PendingKeysReleased)[code] = true;
					break;
				case Device::Mouse:
					(pressed ? m_PendingMouseButtonsPressed
						: m_PendingMouseButtonsReleased)[code] = true;
					break;
				case Device::GamepadConnection:
					(pressed ? m_PendingGamepadsConnected
						: m_PendingGamepadsDisconnected)[deviceIndex] = true;
					break;
				case Device::GamepadButton:
					(pressed ? m_PendingGamepadButtonsPressed
						: m_PendingGamepadButtonsReleased)[deviceIndex][code] = true;
					break;
			}
		}

	private:
		uint64_t m_FrameNumber = 0;
		uint64_t m_LastSequence = 0;
		std::vector<Event> m_PendingEvents;
		uint64_t m_PendingDroppedEventCount = 0;
		FrameSnapshot m_Snapshot;
		std::array<bool, KeyCount> m_LiveKeysHeld{};
		std::array<bool, MouseButtonCount> m_LiveMouseButtonsHeld{};
		std::array<bool, KeyCount> m_PendingKeysPressed{};
		std::array<bool, KeyCount> m_PendingKeysReleased{};
		std::array<bool, MouseButtonCount> m_PendingMouseButtonsPressed{};
		std::array<bool, MouseButtonCount> m_PendingMouseButtonsReleased{};
		std::array<bool, GamepadCount> m_PendingGamepadsConnected{};
		std::array<bool, GamepadCount> m_PendingGamepadsDisconnected{};
		std::array<std::array<bool, GamepadButtonCount>, GamepadCount>
			m_PendingGamepadButtonsPressed{};
		std::array<std::array<bool, GamepadButtonCount>, GamepadCount>
			m_PendingGamepadButtonsReleased{};
	};

}
