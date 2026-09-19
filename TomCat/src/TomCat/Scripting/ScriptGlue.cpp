#include "tcpch.h"
#include "ScriptGlue.h"

#include "ScriptEngine.h"
#include "ScriptDiagnosticSink.h"
#include "TomCat/Audio/AudioEngine.h"
#include "TomCat/Audio/AudioSceneRuntime.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/ApplicationPaths.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Scene/SpriteAnimation.h"
#include "TomCat/Runtime/RuntimeUI.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>

#include <glm/gtc/type_ptr.hpp>

namespace TomCat::Scripting {

	namespace {

		int32_t Code(ScriptStatus status)
		{
			return static_cast<int32_t>(status);
		}

		template<typename Callback>
		int32_t Guard(Callback&& callback) noexcept
		{
			try
			{
				return callback();
			}
			catch (const std::exception& error)
			{
				TC_Core_Error("NativeApiV1 callback failed: {0}", error.what());
				return Code(ScriptStatus::InvalidState);
			}
			catch (...)
			{
				TC_Core_Error("NativeApiV1 callback failed with an unknown exception");
				return Code(ScriptStatus::InvalidState);
			}
		}

		bool RequireMainThread()
		{
			return ScriptEngine::Get().IsMainThread();
		}

		thread_local bool ApplyComponentPropertyImmediately = false;

		class ImmediateComponentPropertyScope
		{
		public:
			ImmediateComponentPropertyScope()
				: m_Previous(ApplyComponentPropertyImmediately)
			{
				ApplyComponentPropertyImmediately = true;
			}

			~ImmediateComponentPropertyScope()
			{
				ApplyComponentPropertyImmediately = m_Previous;
			}

		private:
			bool m_Previous = false;
		};

		int32_t RejectDeferredMutation(const EntityHandleV1& context,
			ScriptStatus status, std::string reason)
		{
			if (!ApplyComponentPropertyImmediately && RequireMainThread())
				ScriptEngine::Get().MarkDeferredCommandBatchFailed(
					context, std::move(reason));
			return Code(status);
		}

		bool IsValidUtf8(std::string_view value)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
			const size_t size = value.size();
			auto continuation = [&](size_t index)
			{
				return index < size && (bytes[index] & 0xc0u) == 0x80u;
			};
			for (size_t index = 0; index < size;)
			{
				const uint8_t first = bytes[index];
				if (first <= 0x7fu)
				{
					++index;
					continue;
				}
				if (first >= 0xc2u && first <= 0xdfu
					&& continuation(index + 1))
				{
					index += 2;
					continue;
				}
				if (first == 0xe0u && index + 2 < size
					&& bytes[index + 1] >= 0xa0u && bytes[index + 1] <= 0xbfu
					&& continuation(index + 2))
				{
					index += 3;
					continue;
				}
				if (((first >= 0xe1u && first <= 0xecu)
					|| (first >= 0xeeu && first <= 0xefu))
					&& continuation(index + 1) && continuation(index + 2))
				{
					index += 3;
					continue;
				}
				if (first == 0xedu && index + 2 < size
					&& bytes[index + 1] >= 0x80u && bytes[index + 1] <= 0x9fu
					&& continuation(index + 2))
				{
					index += 3;
					continue;
				}
				if (first == 0xf0u && index + 3 < size
					&& bytes[index + 1] >= 0x90u && bytes[index + 1] <= 0xbfu
					&& continuation(index + 2) && continuation(index + 3))
				{
					index += 4;
					continue;
				}
				if (first >= 0xf1u && first <= 0xf3u
					&& continuation(index + 1) && continuation(index + 2)
					&& continuation(index + 3))
				{
					index += 4;
					continue;
				}
				if (first == 0xf4u && index + 3 < size
					&& bytes[index + 1] >= 0x80u && bytes[index + 1] <= 0x8fu
					&& continuation(index + 2) && continuation(index + 3))
				{
					index += 4;
					continue;
				}
				return false;
			}
			return true;
		}

		bool ReadUtf8(NativeUtf8View value, std::string& result)
		{
			if ((!value.Data && value.Length != 0)
				|| value.Length > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
				return false;
			if (value.Length == 0)
			{
				result.clear();
				return true;
			}
			result.assign(reinterpret_cast<const char*>(value.Data),
				static_cast<size_t>(value.Length));
			return result.find('\0') == std::string::npos && IsValidUtf8(result);
		}

		int32_t WriteUtf8(const std::string& value, uint8_t* buffer,
			uint32_t capacity, uint32_t* required)
		{
			if (!required || value.size() > std::numeric_limits<uint32_t>::max())
				return Code(ScriptStatus::InvalidArgument);
		*required = static_cast<uint32_t>(value.size());
		if (capacity < value.size() || (!buffer && !value.empty()))
			return Code(ScriptStatus::BufferTooSmall);
		if (!value.empty())
			std::memcpy(buffer, value.data(), value.size());
		return Code(ScriptStatus::Success);
	}

		Entity Resolve(EntityHandleV1 handle)
		{
			bool alive = false;
			if (!ScriptEngine::Get().GetProjectedEntityLiveness(handle, alive) || !alive)
				return {};
			return ScriptEngine::Get().ResolveEntity(handle);
		}

		Scene* ResolveScene(EntityHandleV1 handle)
		{
			return ScriptEngine::Get().ResolveScene(handle);
		}

		NativeVector2 ToNative(const glm::vec2& value) { return { value.x, value.y }; }
		NativeVector3 ToNative(const glm::vec3& value) { return { value.x, value.y, value.z }; }
		glm::vec2 ToGlm(NativeVector2 value) { return { value.X, value.Y }; }
		glm::vec3 ToGlm(NativeVector3 value) { return { value.X, value.Y, value.Z }; }

		int32_t LogCallback(int32_t level, NativeUtf8View message) noexcept
		{
			return Guard([&]()
			{
				std::string text;
				if (!ReadUtf8(message, text)) return Code(ScriptStatus::InvalidArgument);
				switch (level)
				{
					case 0: TC_Trace("{0}", text); break;
					case 1: TC_Info("{0}", text); break;
					case 2: TC_Warn("{0}", text); break;
					case 3: TC_Error("{0}", text); break;
					default: TC_Error("{0}", text); break;
				}
				return Code(ScriptStatus::Success);
			});
		}

		int32_t EmitDiagnosticCallback(const NativeDiagnosticV1* value) noexcept
		{
			return Guard([&]()
			{
				if (!value) return Code(ScriptStatus::InvalidArgument);
				std::string message, file;
				if (!ReadUtf8(value->Message, message) || !ReadUtf8(value->File, file))
					return Code(ScriptStatus::InvalidArgument);
				std::string formatted = file.empty() ? message : file + ":"
					+ std::to_string(value->Line) + ":" + std::to_string(value->Column)
					+ ": " + message;
				if (value->Severity >= 2) TC_Error("C#: {0}", formatted);
				else if (value->Severity == 1) TC_Warn("C#: {0}", formatted);
				else TC_Info("C#: {0}", formatted);

				ScriptDiagnostic diagnostic;
				diagnostic.Severity = value->Severity >= 2
					? ScriptDiagnosticSeverity::Error
					: value->Severity == 1
						? ScriptDiagnosticSeverity::Warning
						: ScriptDiagnosticSeverity::Info;
				diagnostic.Message = std::move(message);
				diagnostic.File = file.empty() ? std::filesystem::path{} : UTF8ToPath(file);
				diagnostic.Line = value->Line > 0
					? static_cast<uint32_t>(value->Line) : 0;
				diagnostic.Column = value->Column > 0
					? static_cast<uint32_t>(value->Column) : 0;
				PublishScriptDiagnostic(diagnostic);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t IsMainThreadCallback() noexcept
		{
			return ScriptEngine::Get().IsMainThread() ? 1 : 0;
		}

		int32_t EntityIsAliveCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				bool alive = false;
				return ScriptEngine::Get().GetProjectedEntityLiveness(handle, alive)
					? (alive ? 1 : 0) : 0;
			});
		}

		int32_t EntityGetNameCallback(EntityHandleV1 handle, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				std::string name;
				return ScriptEngine::Get().GetProjectedEntityName(handle, name)
					? WriteUtf8(name, buffer, capacity, required)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t EntitySetNameCallback(EntityHandleV1 handle, NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				std::string name;
				if (!ReadUtf8(value, name) || name.empty())
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument, "Entity name is invalid");
				return ScriptEngine::Get().QueueSetEntityName(handle, std::move(name))
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::NotFound,
						"Entity name target is unavailable");
			});
		}

		int32_t EntityGetTagCallback(EntityHandleV1 handle, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				std::string tag;
				return ScriptEngine::Get().GetProjectedGameplayTag(handle, tag)
					? WriteUtf8(tag, buffer, capacity, required)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t EntitySetTagCallback(EntityHandleV1 handle, NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				std::string tag;
				if (!ReadUtf8(value, tag) || tag.empty())
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument, "Gameplay tag is invalid");
				return ScriptEngine::Get().QueueSetGameplayTag(handle, std::move(tag))
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::NotFound,
						"Gameplay tag target is unavailable");
			});
		}

		int32_t EntityGetLayerCallback(EntityHandleV1 handle, uint32_t* layer) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!layer) return Code(ScriptStatus::InvalidArgument);
				return ScriptEngine::Get().GetProjectedLayer(handle, *layer)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t EntitySetLayerCallback(EntityHandleV1 handle, uint32_t layer) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueSetLayer(handle, layer)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::InvalidArgument,
						"Entity layer target or value is invalid");
			});
		}

		int32_t DestroyEntityDeferredCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueDestroyEntity(handle)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::NotFound,
						"Entity destroy target is unavailable");
			});
		}

		int32_t HasComponentCallback(EntityHandleV1 handle, int32_t componentType) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const NativeComponentType type = static_cast<NativeComponentType>(componentType);
				switch (type)
				{
					case NativeComponentType::Transform:
					case NativeComponentType::Rigidbody2D:
					case NativeComponentType::BoxCollider2D:
					case NativeComponentType::CircleCollider2D:
					case NativeComponentType::DistanceJoint2D:
					case NativeComponentType::SpriteRenderer:
					case NativeComponentType::Camera:
					case NativeComponentType::SpriteAnimator:
						break;
					default: return Code(ScriptStatus::InvalidArgument);
				}
				bool present = false;
				return ScriptEngine::Get().GetProjectedComponentPresence(handle, type, present)
					? (present ? 1 : 0) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t AddComponentDeferredCallback(EntityHandleV1 handle, int32_t type) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueAddComponent(handle,
					static_cast<NativeComponentType>(type)) ? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::InvalidArgument,
						"Component add target or type is invalid");
			});
		}

		int32_t RemoveComponentDeferredCallback(EntityHandleV1 handle, int32_t type) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueRemoveComponent(handle,
					static_cast<NativeComponentType>(type)) ? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::InvalidArgument,
						"Component remove target or type is invalid");
			});
		}

		int32_t GetTransformVector(EntityHandleV1 handle, NativeVector3* output,
			uint32_t propertyId) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!output) return Code(ScriptStatus::InvalidArgument);
				return ScriptEngine::Get().GetProjectedTransformProperty(
					handle, propertyId, *output)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t SetTransformVector(EntityHandleV1 handle, NativeVector3 input,
			uint32_t propertyId) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!std::isfinite(input.X) || !std::isfinite(input.Y)
					|| !std::isfinite(input.Z))
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument, "Transform value is not finite");
				NativePropertyValueV1 value{};
				value.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector3);
				value.Vector = { input.X, input.Y, input.Z, 0.0f };
				return ScriptEngine::Get().QueueSetComponentProperty(handle,
					NativeComponentType::Transform, propertyId, value)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::NotFound,
						"Transform target is unavailable");
			});
		}

		int32_t TransformGetPositionCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Translation)); }
		int32_t TransformSetPositionCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Translation)); }
		int32_t TransformGetRotationCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Rotation)); }
		int32_t TransformSetRotationCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Rotation)); }
		int32_t TransformGetScaleCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Scale)); }
		int32_t TransformSetScaleCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::Scale)); }

		int32_t TransformGetWorldMatrixCallback(EntityHandleV1 handle,
			NativeMatrix4* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!value) return Code(ScriptStatus::InvalidArgument);
				NativeVector3 translation{}, rotation{}, scale{};
				if (!ScriptEngine::Get().GetProjectedTransformProperty(handle,
						static_cast<uint32_t>(
							ComponentIds::TransformProperties::Translation),
						translation)
					|| !ScriptEngine::Get().GetProjectedTransformProperty(handle,
						static_cast<uint32_t>(
							ComponentIds::TransformProperties::Rotation),
						rotation)
					|| !ScriptEngine::Get().GetProjectedTransformProperty(handle,
						static_cast<uint32_t>(
							ComponentIds::TransformProperties::Scale),
						scale))
					return Code(ScriptStatus::NotFound);
				const glm::mat4 matrix = Math::ComposeTransform(ToGlm(translation),
					ToGlm(rotation), ToGlm(scale));
				std::memcpy(value->Values, glm::value_ptr(matrix),
					sizeof(value->Values));
				return Code(ScriptStatus::Success);
			});
		}

		int32_t InputIsKeyHeldCallback(uint32_t key) noexcept
		{ return RequireMainThread() ? (ScriptEngine::Get().IsKeyHeld(key) ? 1 : 0) : Code(ScriptStatus::WrongThread); }
		int32_t InputWasKeyPressedCallback(uint32_t key) noexcept
		{ return RequireMainThread() ? (ScriptEngine::Get().WasKeyPressed(key) ? 1 : 0) : Code(ScriptStatus::WrongThread); }
		int32_t InputWasKeyReleasedCallback(uint32_t key) noexcept
		{ return RequireMainThread() ? (ScriptEngine::Get().WasKeyReleased(key) ? 1 : 0) : Code(ScriptStatus::WrongThread); }
		int32_t InputGetMousePositionCallback(NativeVector2* value) noexcept
		{ if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); if (!value) return Code(ScriptStatus::InvalidArgument); *value = ScriptEngine::Get().GetMousePosition(); return 0; }
		int32_t InputGetMouseDeltaCallback(NativeVector2* value) noexcept
		{ if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); if (!value) return Code(ScriptStatus::InvalidArgument); *value = ScriptEngine::Get().GetMouseDelta(); return 0; }
		int32_t InputGetModifiersCallback(uint32_t* value) noexcept
		{ if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); if (!value) return Code(ScriptStatus::InvalidArgument); *value = ScriptEngine::Get().GetModifiers(); return 0; }

		int32_t InputIsMouseButtonHeldCallback(uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (button >= 8) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().IsMouseButtonHeld(button) ? 1 : 0;
		}

		int32_t InputWasMouseButtonPressedCallback(uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (button >= 8) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasMouseButtonPressed(button) ? 1 : 0;
		}

		int32_t InputWasMouseButtonReleasedCallback(uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (button >= 8) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasMouseButtonReleased(button) ? 1 : 0;
		}

		int32_t InputGetScrollDeltaCallback(NativeVector2* value) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (!value) return Code(ScriptStatus::InvalidArgument);
			*value = ScriptEngine::Get().GetScrollDelta();
			return 0;
		}

		int32_t InputIsWindowFocusedCallback() noexcept
		{
			return RequireMainThread()
				? (ScriptEngine::Get().IsWindowFocused() ? 1 : 0)
				: Code(ScriptStatus::WrongThread);
		}

		int32_t InputIsGamepadConnectedCallback(uint32_t gamepad) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().IsGamepadConnected(gamepad) ? 1 : 0;
		}

		int32_t InputWasGamepadConnectedCallback(uint32_t gamepad) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasGamepadConnected(gamepad) ? 1 : 0;
		}

		int32_t InputWasGamepadDisconnectedCallback(uint32_t gamepad) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasGamepadDisconnected(gamepad) ? 1 : 0;
		}

		int32_t InputIsGamepadButtonHeldCallback(uint32_t gamepad, uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16 || button >= 15) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().IsGamepadButtonHeld(gamepad, button) ? 1 : 0;
		}

		int32_t InputWasGamepadButtonPressedCallback(uint32_t gamepad,
			uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16 || button >= 15) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasGamepadButtonPressed(gamepad, button) ? 1 : 0;
		}

		int32_t InputWasGamepadButtonReleasedCallback(uint32_t gamepad,
			uint32_t button) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16 || button >= 15) return Code(ScriptStatus::InvalidArgument);
			return ScriptEngine::Get().WasGamepadButtonReleased(gamepad, button) ? 1 : 0;
		}

		int32_t InputGetGamepadAxisCallback(uint32_t gamepad, uint32_t axis,
			float* value) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (!value || gamepad >= 16 || axis >= 6)
				return Code(ScriptStatus::InvalidArgument);
			*value = ScriptEngine::Get().GetGamepadAxis(gamepad, axis);
			return 0;
		}

		int32_t InputGetGamepadNameCallback(uint32_t gamepad, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (gamepad >= 16) return Code(ScriptStatus::InvalidArgument);
			return WriteUtf8(ScriptEngine::Get().GetGamepadName(gamepad), buffer,
				capacity, required);
		}

		NativeInputApiV1 BuildInputApiV1()
		{
			NativeInputApiV1 api;
			api.IsMouseButtonHeld = &InputIsMouseButtonHeldCallback;
			api.WasMouseButtonPressed = &InputWasMouseButtonPressedCallback;
			api.WasMouseButtonReleased = &InputWasMouseButtonReleasedCallback;
			api.GetScrollDelta = &InputGetScrollDeltaCallback;
			api.IsWindowFocused = &InputIsWindowFocusedCallback;
			api.IsGamepadConnected = &InputIsGamepadConnectedCallback;
			api.WasGamepadConnected = &InputWasGamepadConnectedCallback;
			api.WasGamepadDisconnected = &InputWasGamepadDisconnectedCallback;
			api.IsGamepadButtonHeld = &InputIsGamepadButtonHeldCallback;
			api.WasGamepadButtonPressed = &InputWasGamepadButtonPressedCallback;
			api.WasGamepadButtonReleased = &InputWasGamepadButtonReleasedCallback;
			api.GetGamepadAxis = &InputGetGamepadAxisCallback;
			api.GetGamepadName = &InputGetGamepadNameCallback;
			return api;
		}

		int32_t InputEventsGetBatchInfoCallback(
			NativeInputEventBatchInfoV1* value) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (!value) return Code(ScriptStatus::InvalidArgument);
			*value = ScriptEngine::Get().GetInputEventBatchInfo();
			return Code(ScriptStatus::Success);
		}

		int32_t InputEventsCopyCallback(NativeInputEventV1* events,
			uint32_t capacity, uint32_t* required) noexcept
		{
			if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
			if (!required) return Code(ScriptStatus::InvalidArgument);
			const auto& source = ScriptEngine::Get().GetInputEvents();
			if (source.size() > std::numeric_limits<uint32_t>::max())
				return Code(ScriptStatus::InvalidState);
			*required = static_cast<uint32_t>(source.size());
			if (capacity < source.size() || (!events && !source.empty()))
				return Code(ScriptStatus::BufferTooSmall);
			if (!source.empty())
				std::memcpy(events, source.data(),
					source.size() * sizeof(NativeInputEventV1));
			return Code(ScriptStatus::Success);
		}

		NativeInputEventsApiV1 BuildInputEventsApiV1()
		{
			NativeInputEventsApiV1 api;
			api.GetBatchInfo = &InputEventsGetBatchInfoCallback;
			api.CopyEvents = &InputEventsCopyCallback;
			return api;
		}

		int32_t WriteRuntimeDirectory(
			const std::optional<std::filesystem::path>& directory,
			uint8_t* buffer, uint32_t capacity, uint32_t* required)
		{
			if (!required)
				return Code(ScriptStatus::InvalidArgument);
			if (!directory)
			{
				*required = 0;
				return Code(ScriptStatus::NotFound);
			}
			return WriteUtf8(PathToUTF8(*directory), buffer, capacity, required);
		}

		int32_t ApplicationPathsGetSaveDirectoryCallback(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return WriteRuntimeDirectory(ApplicationPaths::GetRuntimeSaveDirectory(),
					buffer, capacity, required);
			});
		}

		int32_t ApplicationPathsGetLogDirectoryCallback(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return WriteRuntimeDirectory(ApplicationPaths::GetRuntimeLogDirectory(),
					buffer, capacity, required);
			});
		}

		int32_t ApplicationPathsGetCrashDirectoryCallback(uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return WriteRuntimeDirectory(ApplicationPaths::GetRuntimeCrashDirectory(),
					buffer, capacity, required);
			});
		}

		NativeApplicationPathsApiV1 BuildApplicationPathsApiV1()
		{
			NativeApplicationPathsApiV1 api;
			api.GetSaveDirectory = &ApplicationPathsGetSaveDirectoryCallback;
			api.GetLogDirectory = &ApplicationPathsGetLogDirectoryCallback;
			api.GetCrashDirectory = &ApplicationPathsGetCrashDirectoryCallback;
			return api;
		}

		const PropertyDescriptor* FindProperty(const ComponentDescriptor& component,
			uint64_t propertyId)
		{
			for (const PropertyDescriptor& property : component.Properties)
			{
				if (static_cast<uint64_t>(property.PropertyId) == propertyId)
					return &property;
			}
			return nullptr;
		}

		bool WriteNativeProperty(PropertyKind kind, const PropertyValue& source,
			NativePropertyValueV1& output)
		{
			output = {};
			switch (kind)
			{
				case PropertyKind::Bool:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Bool);
					output.Integer = std::get<bool>(source) ? 1 : 0;
					return true;
				case PropertyKind::Int32:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
					output.Integer = std::get<int32_t>(source);
					return true;
				case PropertyKind::Int64:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int64);
					output.Integer = std::get<int64_t>(source);
					return true;
				case PropertyKind::UInt32:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt32);
					output.Integer = static_cast<int64_t>(std::get<uint32_t>(source));
					return true;
				case PropertyKind::UInt64:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt64);
					const uint64_t value = std::get<uint64_t>(source);
					std::memcpy(&output.Integer, &value, sizeof(value));
					return true;
				}
				case PropertyKind::Float:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Float);
					output.Number = std::get<float>(source);
					return true;
				case PropertyKind::Double:
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Double);
					output.Number = std::get<double>(source);
					return true;
				case PropertyKind::Vector2:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector2);
					const glm::vec2 value = std::get<glm::vec2>(source);
					output.Vector = { value.x, value.y, 0.0f, 0.0f };
					return true;
				}
				case PropertyKind::Vector3:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector3);
					const glm::vec3 value = std::get<glm::vec3>(source);
					output.Vector = { value.x, value.y, value.z, 0.0f };
					return true;
				}
				case PropertyKind::Vector4:
				{
					output.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector4);
					const glm::vec4 value = std::get<glm::vec4>(source);
					output.Vector = { value.x, value.y, value.z, value.w };
					return true;
				}
				case PropertyKind::String:
					return false;
			}
			return false;
		}

		bool ReadNativeProperty(PropertyKind kind, const NativePropertyValueV1& source,
			PropertyValue& output)
		{
			if (source.Reserved != 0)
				return false;
			switch (kind)
			{
				case PropertyKind::Bool:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Bool)
						|| (source.Integer != 0 && source.Integer != 1)) return false;
					output = source.Integer != 0;
					return true;
				case PropertyKind::Int32:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Int32)
						|| source.Integer < std::numeric_limits<int32_t>::min()
						|| source.Integer > std::numeric_limits<int32_t>::max()) return false;
					output = static_cast<int32_t>(source.Integer);
					return true;
				case PropertyKind::Int64:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Int64))
						return false;
					output = source.Integer;
					return true;
				case PropertyKind::UInt32:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::UInt32)
						|| source.Integer < 0
						|| static_cast<uint64_t>(source.Integer)
							> std::numeric_limits<uint32_t>::max()) return false;
					output = static_cast<uint32_t>(source.Integer);
					return true;
				case PropertyKind::UInt64:
				{
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::UInt64))
						return false;
					uint64_t value = 0;
					std::memcpy(&value, &source.Integer, sizeof(value));
					output = value;
					return true;
				}
				case PropertyKind::Float:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Float)
						|| !std::isfinite(source.Number)
						|| source.Number < -std::numeric_limits<float>::max()
						|| source.Number > std::numeric_limits<float>::max()) return false;
					output = static_cast<float>(source.Number);
					return true;
				case PropertyKind::Double:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Double)
						|| !std::isfinite(source.Number)) return false;
					output = source.Number;
					return true;
				case PropertyKind::Vector2:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector2))
						return false;
					output = glm::vec2(source.Vector.X, source.Vector.Y);
					return true;
				case PropertyKind::Vector3:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector3))
						return false;
					output = glm::vec3(source.Vector.X, source.Vector.Y, source.Vector.Z);
					return true;
				case PropertyKind::Vector4:
					if (source.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector4))
						return false;
					output = glm::vec4(source.Vector.X, source.Vector.Y, source.Vector.Z,
						source.Vector.W);
					return true;
				case PropertyKind::String:
					return false;
			}
			return false;
		}

		const ComponentDescriptor* FindScriptComponent(uint64_t typeId)
		{
			const ComponentDescriptor* descriptor =
				ComponentRegistry::Get().Find(UUID(typeId));
			return descriptor && descriptor->ScriptAccessible ? descriptor : nullptr;
		}

		int32_t ComponentHasCallback(EntityHandleV1 handle, uint64_t typeId) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor) return Code(ScriptStatus::InvalidArgument);
				bool present = false;
				return ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
					handle, typeId, present) ? (present ? 1 : 0)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t ComponentAddCallback(EntityHandleV1 handle, uint64_t typeId) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component type is unavailable");
				return ScriptEngine::Get().QueueAddRegisteredComponent(handle, typeId)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::InvalidState,
						"Registered component could not be added");
			});
		}

		int32_t ComponentRemoveCallback(EntityHandleV1 handle, uint64_t typeId) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component type is unavailable");
				if (!descriptor->Removable)
					return RejectDeferredMutation(handle, ScriptStatus::InvalidState,
						"Registered component is not removable");
				return ScriptEngine::Get().QueueRemoveRegisteredComponent(handle, typeId)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::InvalidState,
						"Registered component could not be removed");
			});
		}

		int32_t ComponentGetPropertyCallback(EntityHandleV1 handle, uint64_t typeId,
			uint64_t propertyId, NativePropertyValueV1* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!value) return Code(ScriptStatus::InvalidArgument);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor) return Code(ScriptStatus::InvalidArgument);
				const PropertyDescriptor* property = FindProperty(*descriptor, propertyId);
				if (!property) return Code(ScriptStatus::InvalidArgument);
				bool present = false;
				if (!ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
					handle, typeId, present) || !present)
					return Code(ScriptStatus::NotFound);
				NativePropertyValueV1 projected{};
				bool useDefault = false;
				if (ScriptEngine::Get().TryGetProjectedRegisteredComponentProperty(
					handle, typeId, propertyId, projected, useDefault))
				{
					*value = projected;
					return Code(ScriptStatus::Success);
				}
				if (useDefault)
				{
					return property->DefaultValue
						&& WriteNativeProperty(property->Kind,
							*property->DefaultValue, *value)
						? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidState);
				}
				Entity entity = Resolve(handle);
				if (!entity || !descriptor->Has(entity)) return Code(ScriptStatus::NotFound);
				return WriteNativeProperty(property->Kind, property->Get(entity), *value)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::Unavailable);
			});
		}

		int32_t ComponentSetPropertyCallback(EntityHandleV1 handle, uint64_t typeId,
			uint64_t propertyId, NativePropertyValueV1 value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component type is unavailable");
				const PropertyDescriptor* property = FindProperty(*descriptor, propertyId);
				if (!property)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component property is unavailable");
				PropertyValue decoded;
				if (!ReadNativeProperty(property->Kind, value, decoded))
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component property value is invalid");
				if (!ApplyComponentPropertyImmediately)
				{
					bool present = false;
					if (!ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
						handle, typeId, present) || !present)
						return RejectDeferredMutation(handle,
							ScriptStatus::NotFound,
							"Registered component property target is unavailable");
					return ScriptEngine::Get().QueueSetRegisteredComponentProperty(
						handle, typeId, propertyId, value)
						? Code(ScriptStatus::Success)
						: RejectDeferredMutation(handle, ScriptStatus::InvalidState,
							"Registered component property could not be queued");
				}
				Entity entity = ScriptEngine::Get().ResolveEntity(handle);
				if (!entity || !descriptor->Has(entity))
					return Code(ScriptStatus::NotFound);
				std::string error;
				return property->Set(entity, decoded, error)
					? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidArgument);
			});
		}

		NativeComponentApiV1 BuildComponentApiV1()
		{
			NativeComponentApiV1 api;
			api.Has = &ComponentHasCallback;
			api.Add = &ComponentAddCallback;
			api.Remove = &ComponentRemoveCallback;
			api.GetProperty = &ComponentGetPropertyCallback;
			api.SetProperty = &ComponentSetPropertyCallback;
			return api;
		}

		int32_t ComponentStringGetPropertyCallback(EntityHandleV1 handle,
			uint64_t typeId, uint64_t propertyId, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!required) return Code(ScriptStatus::InvalidArgument);
				*required = 0;
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor) return Code(ScriptStatus::InvalidArgument);
				const PropertyDescriptor* property = FindProperty(*descriptor, propertyId);
				if (!property || property->Kind != PropertyKind::String)
					return Code(ScriptStatus::InvalidArgument);
				bool present = false;
				if (!ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
					handle, typeId, present) || !present)
					return Code(ScriptStatus::NotFound);
				std::string projected;
				bool useDefault = false;
				if (ScriptEngine::Get()
					.TryGetProjectedRegisteredComponentStringProperty(
						handle, typeId, propertyId, projected, useDefault))
				{
					if (projected.size() > ComponentStringMaximumBytesV1
						|| !IsValidUtf8(projected))
						return Code(ScriptStatus::InvalidState);
					return WriteUtf8(projected, buffer, capacity, required);
				}
				if (useDefault)
				{
					if (!property->DefaultValue)
						return Code(ScriptStatus::InvalidState);
					const std::string* text = std::get_if<std::string>(
						&*property->DefaultValue);
					if (!text || text->size() > ComponentStringMaximumBytesV1
						|| !IsValidUtf8(*text))
						return Code(ScriptStatus::InvalidState);
					return WriteUtf8(*text, buffer, capacity, required);
				}
				Entity entity = Resolve(handle);
				if (!entity || !descriptor->Has(entity))
					return Code(ScriptStatus::NotFound);
				const PropertyValue value = property->Get(entity);
				const std::string* text = std::get_if<std::string>(&value);
				if (!text || text->size() > ComponentStringMaximumBytesV1
					|| !IsValidUtf8(*text))
					return Code(ScriptStatus::InvalidState);
				return WriteUtf8(*text, buffer, capacity, required);
			});
		}

		int32_t ComponentStringSetPropertyCallback(EntityHandleV1 handle,
			uint64_t typeId, uint64_t propertyId, NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const ComponentDescriptor* descriptor = FindScriptComponent(typeId);
				if (!descriptor)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered component type is unavailable");
				const PropertyDescriptor* property = FindProperty(*descriptor, propertyId);
				if (!property || property->Kind != PropertyKind::String
					|| value.Length > ComponentStringMaximumBytesV1)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered string property is unavailable or too large");
				std::string decoded;
				if (!ReadUtf8(value, decoded))
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Registered string property is invalid UTF-8");
				if (!ApplyComponentPropertyImmediately)
				{
					bool present = false;
					if (!ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
						handle, typeId, present) || !present)
						return RejectDeferredMutation(handle,
							ScriptStatus::NotFound,
							"Registered string property target is unavailable");
					return ScriptEngine::Get().QueueSetRegisteredComponentStringProperty(
						handle, typeId, propertyId, std::move(decoded))
						? Code(ScriptStatus::Success)
						: RejectDeferredMutation(handle, ScriptStatus::InvalidState,
							"Registered string property could not be queued");
				}
				Entity entity = ScriptEngine::Get().ResolveEntity(handle);
				if (!entity || !descriptor->Has(entity))
					return Code(ScriptStatus::NotFound);
				std::string error;
				return property->Set(entity, PropertyValue(std::move(decoded)), error)
					? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidArgument);
			});
		}

		NativeComponentStringApiV1 BuildComponentStringApiV1()
		{
			NativeComponentStringApiV1 api;
			api.GetProperty = &ComponentStringGetPropertyCallback;
			api.SetProperty = &ComponentStringSetPropertyCallback;
			return api;
		}

		NativeUtf8View BorrowComponentSchemaString(const std::string& value)
		{
			return {
				reinterpret_cast<const uint8_t*>(value.data()),
				static_cast<uint64_t>(value.size())
			};
		}

		uint32_t ToNativeSchemaPropertyKind(PropertyKind kind)
		{
			switch (kind)
			{
				case PropertyKind::Bool:
					return static_cast<uint32_t>(NativePropertyKindV1::Bool);
				case PropertyKind::Int32:
					return static_cast<uint32_t>(NativePropertyKindV1::Int32);
				case PropertyKind::Int64:
					return static_cast<uint32_t>(NativePropertyKindV1::Int64);
				case PropertyKind::UInt32:
					return static_cast<uint32_t>(NativePropertyKindV1::UInt32);
				case PropertyKind::UInt64:
					return static_cast<uint32_t>(NativePropertyKindV1::UInt64);
				case PropertyKind::Float:
					return static_cast<uint32_t>(NativePropertyKindV1::Float);
				case PropertyKind::Double:
					return static_cast<uint32_t>(NativePropertyKindV1::Double);
				case PropertyKind::String:
					return static_cast<uint32_t>(NativePropertyKindV1::String);
				case PropertyKind::Vector2:
					return static_cast<uint32_t>(NativePropertyKindV1::Vector2);
				case PropertyKind::Vector3:
					return static_cast<uint32_t>(NativePropertyKindV1::Vector3);
				case PropertyKind::Vector4:
					return static_cast<uint32_t>(NativePropertyKindV1::Vector4);
			}
			return 0;
		}

		int32_t ComponentSchemaGetComponentCountCallback(uint32_t* count) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!count) return Code(ScriptStatus::InvalidArgument);
				const size_t size = ComponentRegistry::Get().GetDescriptors().size();
				if (size > std::numeric_limits<uint32_t>::max())
					return Code(ScriptStatus::Unavailable);
				*count = static_cast<uint32_t>(size);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t ComponentSchemaGetComponentCallback(uint32_t index,
			NativeComponentSchemaInfoV1* component) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!component) return Code(ScriptStatus::InvalidArgument);
				const std::span<const ComponentDescriptor> descriptors =
					ComponentRegistry::Get().GetDescriptors();
				if (index >= descriptors.size()) return Code(ScriptStatus::NotFound);
				const ComponentDescriptor& descriptor = descriptors[index];
				*component = {};
				component->TypeId = static_cast<uint64_t>(descriptor.TypeId);
				component->ProviderId = static_cast<uint64_t>(descriptor.ProviderId);
				component->SchemaVersion = descriptor.SchemaVersion;
				if (descriptor.Properties.size()
					> std::numeric_limits<uint32_t>::max())
					return Code(ScriptStatus::Unavailable);
				component->PropertyCount =
					static_cast<uint32_t>(descriptor.Properties.size());
				if (descriptor.ScriptAccessible)
					component->Flags |= static_cast<uint32_t>(
						NativeComponentSchemaFlagsV1::ScriptAccessible);
				if (descriptor.InspectorVisible)
					component->Flags |= static_cast<uint32_t>(
						NativeComponentSchemaFlagsV1::InspectorVisible);
				component->StableName =
					BorrowComponentSchemaString(descriptor.StableName);
				component->DisplayName =
					BorrowComponentSchemaString(descriptor.DisplayName);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t ComponentSchemaGetPropertyCountCallback(uint64_t componentTypeId,
			uint32_t* count) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!count) return Code(ScriptStatus::InvalidArgument);
				const ComponentDescriptor* descriptor =
					ComponentRegistry::Get().Find(UUID(componentTypeId));
				if (!descriptor) return Code(ScriptStatus::NotFound);
				if (descriptor->Properties.size()
					> std::numeric_limits<uint32_t>::max())
					return Code(ScriptStatus::Unavailable);
				*count = static_cast<uint32_t>(descriptor->Properties.size());
				return Code(ScriptStatus::Success);
			});
		}

		int32_t ComponentSchemaGetPropertyCallback(uint64_t componentTypeId,
			uint32_t index, NativeComponentPropertySchemaInfoV1* property) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!property) return Code(ScriptStatus::InvalidArgument);
				const ComponentDescriptor* descriptor =
					ComponentRegistry::Get().Find(UUID(componentTypeId));
				if (!descriptor) return Code(ScriptStatus::NotFound);
				if (index >= descriptor->Properties.size())
					return Code(ScriptStatus::NotFound);
				const PropertyDescriptor& descriptorProperty =
					descriptor->Properties[index];
				*property = {};
				property->ComponentTypeId = componentTypeId;
				property->PropertyId =
					static_cast<uint64_t>(descriptorProperty.PropertyId);
				property->Kind = ToNativeSchemaPropertyKind(
					descriptorProperty.Kind);
				if (descriptorProperty.AssetReference)
					property->Flags |= static_cast<uint32_t>(
						NativeComponentPropertyFlagsV1::AssetReference);
				if (descriptorProperty.EntityReference)
					property->Flags |= static_cast<uint32_t>(
						NativeComponentPropertyFlagsV1::EntityReference);
				property->StableName =
					BorrowComponentSchemaString(descriptorProperty.StableName);
				property->DisplayName =
					BorrowComponentSchemaString(descriptorProperty.DisplayName);
				return property->Kind != 0 ? Code(ScriptStatus::Success)
					: Code(ScriptStatus::Unavailable);
			});
		}

		NativeComponentSchemaApiV1 BuildComponentSchemaApiV1()
		{
			NativeComponentSchemaApiV1 api;
			api.GetComponentCount = &ComponentSchemaGetComponentCountCallback;
			api.GetComponent = &ComponentSchemaGetComponentCallback;
			api.GetPropertyCount = &ComponentSchemaGetPropertyCountCallback;
			api.GetProperty = &ComponentSchemaGetPropertyCallback;
			return api;
		}

		AudioSource* ResolveAudioSource(EntityHandleV1 handle, Entity* resolved = nullptr)
		{
			Entity entity = Resolve(handle);
			if (resolved)
				*resolved = entity;
			return entity && entity.HasComponent<AudioSource>()
				? &entity.GetComponent<AudioSource>() : nullptr;
		}

		AudioListener* ResolveAudioListener(EntityHandleV1 handle,
			Entity* resolved = nullptr)
		{
			Entity entity = Resolve(handle);
			if (resolved)
				*resolved = entity;
			return entity && entity.HasComponent<AudioListener>()
				? &entity.GetComponent<AudioListener>() : nullptr;
		}

		bool IsBool(int32_t value) { return value == 0 || value == 1; }

		bool IsAudioAssetHandle(uint64_t rawHandle)
		{
			if (rawHandle == 0)
				return true;
			AssetManager& assets = AssetManager::Get();
			if (const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(
				AssetHandle(rawHandle)))
				return !metadata->IsMissing && metadata->Type == AssetType::Audio;
			if (!assets.IsCookedPackageMounted())
				return false;
			AssetType type = AssetType::None;
			std::vector<uint8_t> bytes;
			return assets.ReadAssetBytes(AssetHandle(rawHandle), bytes, &type)
				&& type == AssetType::Audio;
		}

		int32_t AudioHardwareAvailableCallback() noexcept
		{
			return Guard([]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return AudioEngine::Get().Initialize()
					&& AudioEngine::Get().IsHardwareAvailable() ? 1 : 0;
			});
		}

		int32_t AudioBackendNameCallback(uint8_t* buffer, uint32_t capacity,
			uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!AudioEngine::Get().Initialize())
					return Code(ScriptStatus::Unavailable);
				return WriteUtf8(AudioEngine::Get().GetBackendName(), buffer,
					capacity, required);
			});
		}

		int32_t AudioHasSourceCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				return entity ? (entity.HasComponent<AudioSource>() ? 1 : 0)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t AudioAddSourceCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity) return Code(ScriptStatus::NotFound);
				if (!entity.HasComponent<AudioSource>()) entity.AddComponent<AudioSource>();
				return Code(ScriptStatus::Success);
			});
		}

		int32_t AudioRemoveSourceCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity;
				if (!ResolveAudioSource(handle, &entity)) return Code(ScriptStatus::NotFound);
				AudioSceneRuntime::DestroySource(entity);
				entity.RemoveComponent<AudioSource>();
				return Code(ScriptStatus::Success);
			});
		}

		int32_t AudioGetClipCallback(EntityHandleV1 handle, uint64_t* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				AudioSource* source = ResolveAudioSource(handle);
				if (!source) return Code(ScriptStatus::NotFound);
				if (!value) return Code(ScriptStatus::InvalidArgument);
				*value = static_cast<uint64_t>(source->Clip);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t AudioSetClipCallback(EntityHandleV1 handle, uint64_t value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity;
				AudioSource* source = ResolveAudioSource(handle, &entity);
				if (!source) return Code(ScriptStatus::NotFound);
				if (!IsAudioAssetHandle(value)) return Code(ScriptStatus::InvalidArgument);
				if (source->Clip != AssetHandle(value))
				{
					AudioSceneRuntime::DestroySource(entity);
					source->Clip = AssetHandle(value);
				}
				return Code(ScriptStatus::Success);
			});
		}

		int32_t AudioGetEnabledCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); return s ? (s->Enabled?1:0) : Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetEnabledCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); s->Enabled=value!=0; if(!s->Enabled)AudioSceneRuntime::DestroySource(e); return 0; }); }
		int32_t AudioGetPlayOnStartCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); return s ? (s->PlayOnStart?1:0) : Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetPlayOnStartCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); s->PlayOnStart=value!=0; return 0; }); }
		int32_t AudioGetLoopCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); return s ? (s->Loop?1:0) : Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetLoopCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); s->Loop=value!=0; return s->RuntimeVoice==0||AudioSceneRuntime::ApplySettings(e)?0:Code(ScriptStatus::InvalidState); }); }
		int32_t AudioGetVolumeCallback(EntityHandleV1 handle, float* value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->Volume; return 0; }); }
		int32_t AudioSetVolumeCallback(EntityHandleV1 handle, float value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(!std::isfinite(value)||value<0.0f||value>4.0f)return Code(ScriptStatus::InvalidArgument); s->Volume=value; return s->RuntimeVoice==0||AudioSceneRuntime::ApplySettings(e)?0:Code(ScriptStatus::InvalidState); }); }
		int32_t AudioGetPitchCallback(EntityHandleV1 handle, float* value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->Pitch; return 0; }); }
		int32_t AudioSetPitchCallback(EntityHandleV1 handle, float value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(!std::isfinite(value)||value<0.25f||value>4.0f)return Code(ScriptStatus::InvalidArgument); s->Pitch=value; return s->RuntimeVoice==0||AudioSceneRuntime::ApplySettings(e)?0:Code(ScriptStatus::InvalidState); }); }
		int32_t AudioGetMixerGroupCallback(EntityHandleV1 handle, int32_t* value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->MixerGroup; return 0; }); }
		int32_t AudioSetMixerGroupCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(value<0||value>2)return Code(ScriptStatus::InvalidArgument); s->MixerGroup=static_cast<uint8_t>(value); return s->RuntimeVoice==0||AudioSceneRuntime::ApplySettings(e)?0:Code(ScriptStatus::InvalidState); }); }

		int32_t AudioPlayCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); return !e?Code(ScriptStatus::NotFound):(AudioSceneRuntime::Play(e)?0:Code(ScriptStatus::InvalidState)); }); }
		int32_t AudioPauseCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); return !e?Code(ScriptStatus::NotFound):(AudioSceneRuntime::Pause(e)?0:Code(ScriptStatus::InvalidState)); }); }
		int32_t AudioStopCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); return !e?Code(ScriptStatus::NotFound):(AudioSceneRuntime::Stop(e)?0:Code(ScriptStatus::InvalidState)); }); }
		int32_t AudioGetStateCallback(EntityHandleV1 handle, int32_t* value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); if(!e)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=static_cast<int32_t>(AudioSceneRuntime::GetState(e)); return 0; }); }

		int32_t AudioHasListenerCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); return e?(e.HasComponent<AudioListener>()?1:0):Code(ScriptStatus::NotFound); }); }
		int32_t AudioAddListenerCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); if(!e)return Code(ScriptStatus::NotFound); if(!e.HasComponent<AudioListener>())e.AddComponent<AudioListener>(); return 0; }); }
		int32_t AudioRemoveListenerCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Entity e=Resolve(handle); if(!e||!e.HasComponent<AudioListener>())return Code(ScriptStatus::NotFound); e.RemoveComponent<AudioListener>(); return 0; }); }
		int32_t AudioGetListenerEnabledCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioListener* l=ResolveAudioListener(handle); return l?(l->Enabled?1:0):Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetListenerEnabledCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioListener* l=ResolveAudioListener(handle); if(!l)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); l->Enabled=value!=0; return 0; }); }
		int32_t AudioGetListenerPrimaryCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioListener* l=ResolveAudioListener(handle); return l?(l->Primary?1:0):Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetListenerPrimaryCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioListener* l=ResolveAudioListener(handle); if(!l)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); l->Primary=value!=0; return 0; }); }

		int32_t AudioGetMixerVolumeCallback(int32_t group, float* value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); if(!value||group<0||group>2)return Code(ScriptStatus::InvalidArgument); *value=AudioEngine::Get().GetMixerVolume(static_cast<AudioMixerGroup>(group)); return 0; }); }
		int32_t AudioSetMixerVolumeCallback(int32_t group, float value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); return AudioEngine::Get().SetMixerVolume(static_cast<AudioMixerGroup>(group),value)?0:Code(ScriptStatus::InvalidArgument); }); }

		NativeAudioApiV1 BuildAudioApiV1()
		{
			NativeAudioApiV1 api;
			api.IsHardwareAvailable=&AudioHardwareAvailableCallback; api.GetBackendName=&AudioBackendNameCallback;
			api.HasSource=&AudioHasSourceCallback; api.AddSource=&AudioAddSourceCallback; api.RemoveSource=&AudioRemoveSourceCallback;
			api.GetClip=&AudioGetClipCallback; api.SetClip=&AudioSetClipCallback; api.GetEnabled=&AudioGetEnabledCallback; api.SetEnabled=&AudioSetEnabledCallback;
			api.GetPlayOnStart=&AudioGetPlayOnStartCallback; api.SetPlayOnStart=&AudioSetPlayOnStartCallback; api.GetLoop=&AudioGetLoopCallback; api.SetLoop=&AudioSetLoopCallback;
			api.GetVolume=&AudioGetVolumeCallback; api.SetVolume=&AudioSetVolumeCallback; api.GetPitch=&AudioGetPitchCallback; api.SetPitch=&AudioSetPitchCallback;
			api.GetMixerGroup=&AudioGetMixerGroupCallback; api.SetMixerGroup=&AudioSetMixerGroupCallback; api.Play=&AudioPlayCallback; api.Pause=&AudioPauseCallback; api.Stop=&AudioStopCallback; api.GetPlaybackState=&AudioGetStateCallback;
			api.HasListener=&AudioHasListenerCallback; api.AddListener=&AudioAddListenerCallback; api.RemoveListener=&AudioRemoveListenerCallback;
			api.GetListenerEnabled=&AudioGetListenerEnabledCallback; api.SetListenerEnabled=&AudioSetListenerEnabledCallback; api.GetListenerPrimary=&AudioGetListenerPrimaryCallback; api.SetListenerPrimary=&AudioSetListenerPrimaryCallback;
			api.GetMixerVolume=&AudioGetMixerVolumeCallback; api.SetMixerVolume=&AudioSetMixerVolumeCallback;
			return api;
		}

		int32_t AudioGetStreamingCallback(EntityHandleV1 handle) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); return s?(s->Streaming?1:0):Code(ScriptStatus::NotFound); }); }
		int32_t AudioSetStreamingCallback(EntityHandleV1 handle, int32_t value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); Entity e; AudioSource* s=ResolveAudioSource(handle,&e); if(!s)return Code(ScriptStatus::NotFound); if(!IsBool(value))return Code(ScriptStatus::InvalidArgument); const bool next=value!=0; if(s->Streaming!=next){AudioSceneRuntime::DestroySource(e);s->Streaming=next;} return 0; }); }
		int32_t AudioGetSpatialBlendCallback(EntityHandleV1 handle, float* value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->SpatialBlend; return 0; }); }
		int32_t AudioSetSpatialBlendCallback(EntityHandleV1 handle, float value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!std::isfinite(value)||value<0.0f||value>1.0f)return Code(ScriptStatus::InvalidArgument); s->SpatialBlend=value; return 0; }); }
		int32_t AudioGetMinDistanceCallback(EntityHandleV1 handle, float* value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->MinDistance; return 0; }); }
		int32_t AudioSetMinDistanceCallback(EntityHandleV1 handle, float value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!std::isfinite(value)||value<0.0f||value>=s->MaxDistance)return Code(ScriptStatus::InvalidArgument); s->MinDistance=value; return 0; }); }
		int32_t AudioGetMaxDistanceCallback(EntityHandleV1 handle, float* value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!value)return Code(ScriptStatus::InvalidArgument); *value=s->MaxDistance; return 0; }); }
		int32_t AudioSetMaxDistanceCallback(EntityHandleV1 handle, float value) noexcept
		{ return Guard([&]() { if(!RequireMainThread())return Code(ScriptStatus::WrongThread); AudioSource* s=ResolveAudioSource(handle); if(!s)return Code(ScriptStatus::NotFound); if(!std::isfinite(value)||value<=s->MinDistance)return Code(ScriptStatus::InvalidArgument); s->MaxDistance=value; return 0; }); }

		NativeAudioSpatialApiV1 BuildAudioSpatialApiV1()
		{
			NativeAudioSpatialApiV1 api;
			api.GetStreaming = &AudioGetStreamingCallback;
			api.SetStreaming = &AudioSetStreamingCallback;
			api.GetSpatialBlend = &AudioGetSpatialBlendCallback;
			api.SetSpatialBlend = &AudioSetSpatialBlendCallback;
			api.GetMinDistance = &AudioGetMinDistanceCallback;
			api.SetMinDistance = &AudioSetMinDistanceCallback;
			api.GetMaxDistance = &AudioGetMaxDistanceCallback;
			api.SetMaxDistance = &AudioSetMaxDistanceCallback;
			return api;
		}

		bool IsRuntimeUITextType(uint64_t typeId)
		{
			return typeId == ComponentIds::TextRenderer
				|| typeId == ComponentIds::UIText;
		}

		std::string* ResolveRuntimeUIText(Entity entity, uint64_t typeId)
		{
			if (!entity || !IsRuntimeUITextType(typeId))
				return nullptr;
			if (typeId == ComponentIds::TextRenderer)
				return entity.HasComponent<TextRenderer>()
					? &entity.GetComponent<TextRenderer>().Text : nullptr;
			return entity.HasComponent<UIText>()
				? &entity.GetComponent<UIText>().Text : nullptr;
		}

		int32_t RuntimeUIGetTextCallback(EntityHandleV1 handle, uint64_t typeId,
			uint8_t* buffer, uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!IsRuntimeUITextType(typeId))
					return Code(ScriptStatus::InvalidArgument);
				std::string* text = ResolveRuntimeUIText(Resolve(handle), typeId);
				return text ? WriteUtf8(*text, buffer, capacity, required)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t RuntimeUISetTextCallback(EntityHandleV1 handle, uint64_t typeId,
			NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!IsRuntimeUITextType(typeId) || value.Length > 65536)
					return Code(ScriptStatus::InvalidArgument);
				std::string decoded;
				if (!ReadUtf8(value, decoded)) return Code(ScriptStatus::InvalidArgument);
				std::string* text = ResolveRuntimeUIText(Resolve(handle), typeId);
				if (!text) return Code(ScriptStatus::NotFound);
				*text = std::move(decoded);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t RuntimeUIWasButtonClickedCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<UIButton>())
					return Code(ScriptStatus::NotFound);
				return RuntimeUISystem::WasButtonClicked(entity) ? 1 : 0;
			});
		}

		int32_t RuntimeUIGetButtonClickSerialCallback(EntityHandleV1 handle,
			uint64_t* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!value) return Code(ScriptStatus::InvalidArgument);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<UIButton>())
					return Code(ScriptStatus::NotFound);
				*value = RuntimeUISystem::GetButtonClickSerial(entity);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t RuntimeUIFocusButtonCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Scene* scene = ResolveScene(handle);
				Entity entity = Resolve(handle);
				if (!scene || !entity || !entity.HasComponent<UIButton>())
					return Code(ScriptStatus::NotFound);
				return RuntimeUISystem::FocusButton(*scene, entity)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidState);
			});
		}

		int32_t RuntimeUIGetRectCallback(EntityHandleV1 handle,
			NativeVector4* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!value) return Code(ScriptStatus::InvalidArgument);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<RectTransform>())
					return Code(ScriptStatus::NotFound);
				const glm::vec4& rectangle = entity.GetComponent<RectTransform>().RuntimeRect;
				*value = { rectangle.x, rectangle.y, rectangle.z, rectangle.w };
				return Code(ScriptStatus::Success);
			});
		}

		int32_t RuntimeUIIsGameplayInputCapturedCallback() noexcept
		{
			return Guard([]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return RuntimeUISystem::IsGameplayInputCaptured() ? 1 : 0;
			});
		}

		NativeRuntimeUIApiV1 BuildRuntimeUIApiV1()
		{
			NativeRuntimeUIApiV1 api;
			api.GetText = &RuntimeUIGetTextCallback;
			api.SetText = &RuntimeUISetTextCallback;
			api.WasButtonClicked = &RuntimeUIWasButtonClickedCallback;
			api.GetButtonClickSerial = &RuntimeUIGetButtonClickSerialCallback;
			api.FocusButton = &RuntimeUIFocusButtonCallback;
			api.GetRect = &RuntimeUIGetRectCallback;
			api.IsGameplayInputCaptured = &RuntimeUIIsGameplayInputCapturedCallback;
			return api;
		}

		std::vector<Entity> CollectSceneEntities(Scene& scene)
		{
			std::vector<Entity> result;
			std::vector<UUID> pending = scene.GetRootEntityUUIDs();
			while (!pending.empty())
			{
				const UUID id = pending.back();
				pending.pop_back();
				Entity entity = scene.FindEntityByUUID(id);
				if (!entity)
					continue;
				result.push_back(entity);
				auto children = scene.GetChildrenUUIDs(entity);
				pending.insert(pending.end(), children.rbegin(), children.rend());
			}
			return result;
		}

		EntityHandleV1 ToHandle(const EntityHandleV1& context, Entity entity)
		{
			return entity ? EntityHandleV1{ context.SceneSessionId,
				static_cast<uint64_t>(entity.GetUUID()), context.RuntimeGeneration }
				: EntityHandleV1{};
		}

		int32_t GameplayCreateEntityCallback(EntityHandleV1 context,
			NativeUtf8View nameView, NativeVector3 worldPosition,
			EntityHandleV1 parent, EntityHandleV1* reserved) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!reserved)
					return RejectDeferredMutation(context,
						ScriptStatus::InvalidArgument,
						"CreateEntity reservation output is null");
				std::string name;
				if (!ReadUtf8(nameView, name))
					return RejectDeferredMutation(context,
						ScriptStatus::InvalidArgument,
						"CreateEntity name is invalid UTF-8");
				return ScriptEngine::Get().QueueCreateEntity(context, std::move(name),
					worldPosition, parent, *reserved)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(context, ScriptStatus::InvalidArgument,
						"CreateEntity target, parent, or value is invalid");
			});
		}

		int32_t GameplayFindEntityCallback(EntityHandleV1 context,
			NativeUtf8View nameView, EntityHandleV1* output) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				std::string name;
				if (!output || !ReadUtf8(nameView, name) || name.empty())
					return Code(ScriptStatus::InvalidArgument);
				std::vector<EntityHandleV1> entities;
				if (!ScriptEngine::Get().GetProjectedEntities(context, entities))
					return Code(ScriptStatus::NotFound);
				for (const EntityHandleV1& entity : entities)
				{
					std::string candidate;
					if (ScriptEngine::Get().GetProjectedEntityName(entity, candidate)
						&& candidate == name)
					{
						*output = entity;
						return Code(ScriptStatus::Success);
					}
				}
				*output = {};
				return Code(ScriptStatus::NotFound);
			});
		}

		bool MatchesGameplayQueryProjected(const EntityHandleV1& entity,
			int32_t componentType, uint64_t registeredTypeId)
		{
			bool present = false;
			if (registeredTypeId != 0)
				return ScriptEngine::Get().GetProjectedRegisteredComponentPresence(
					entity, registeredTypeId, present) && present;
			if (componentType == 0)
				return true;
			return ScriptEngine::Get().GetProjectedComponentPresence(entity,
				static_cast<NativeComponentType>(componentType), present) && present;
		}

		int32_t GameplayQueryEntitiesCallback(EntityHandleV1 context,
			int32_t componentType, uint64_t registeredTypeId, EntityHandleV1* output,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!required || componentType < 0
					|| (componentType != 0 && registeredTypeId != 0)
					|| (capacity != 0 && !output))
					return Code(ScriptStatus::InvalidArgument);
				std::vector<EntityHandleV1> entities;
				if (!ScriptEngine::Get().GetProjectedEntities(context, entities))
					return Code(ScriptStatus::NotFound);
				std::vector<EntityHandleV1> matches;
				matches.reserve(entities.size());
				for (const EntityHandleV1& entity : entities)
				{
					if (MatchesGameplayQueryProjected(entity, componentType,
						registeredTypeId))
						matches.push_back(entity);
				}
				if (matches.size() > std::numeric_limits<uint32_t>::max())
					return Code(ScriptStatus::InvalidState);
				*required = static_cast<uint32_t>(matches.size());
				const uint32_t count = std::min(capacity, *required);
				if (count != 0)
					std::copy_n(matches.begin(), count, output);
				return capacity < *required ? Code(ScriptStatus::BufferTooSmall)
					: Code(ScriptStatus::Success);
			});
		}

		int32_t GameplayGetParentCallback(EntityHandleV1 handle,
			EntityHandleV1* output) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!output) return Code(ScriptStatus::InvalidArgument);
				if (!ScriptEngine::Get().GetProjectedParent(handle, *output))
					return Code(ScriptStatus::NotFound);
				return Code(ScriptStatus::Success);
			});
		}

		int32_t GameplaySetParentCallback(EntityHandleV1 entity,
			EntityHandleV1 parent) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueSetParent(entity, parent)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t GameplayGetChildrenCallback(EntityHandleV1 handle,
			EntityHandleV1* output, uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!required || (capacity != 0 && !output))
					return Code(ScriptStatus::InvalidArgument);
				std::vector<EntityHandleV1> children;
				if (!ScriptEngine::Get().GetProjectedChildren(handle, children))
					return Code(ScriptStatus::NotFound);
				if (children.size() > std::numeric_limits<uint32_t>::max())
					return Code(ScriptStatus::InvalidState);
				*required = static_cast<uint32_t>(children.size());
				const uint32_t count = std::min(capacity, *required);
				if (count != 0)
					std::copy_n(children.begin(), count, output);
				return capacity < *required ? Code(ScriptStatus::BufferTooSmall)
					: Code(ScriptStatus::Success);
			});
		}

		int32_t GameplayGetActiveSelfCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				bool active = false;
				return ScriptEngine::Get().GetProjectedActiveSelf(handle, active)
					? (active ? 1 : 0) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySetActiveSelfCallback(EntityHandleV1 handle,
			int32_t active) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (active != 0 && active != 1)
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"ActiveSelf value must be zero or one");
				return ScriptEngine::Get().QueueSetActiveSelf(handle, active != 0)
					? Code(ScriptStatus::Success)
					: RejectDeferredMutation(handle, ScriptStatus::NotFound,
						"ActiveSelf target is unavailable");
			});
		}

		int32_t GameplayGetActiveInHierarchyCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				bool active = false;
				return ScriptEngine::Get().GetProjectedActiveInHierarchy(
					handle, active) ? (active ? 1 : 0)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplayGetLocalPositionCallback(EntityHandleV1 h,
			NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalTranslation)); }
		int32_t GameplaySetLocalPositionCallback(EntityHandleV1 h,
			NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalTranslation)); }
		int32_t GameplayGetLocalRotationCallback(EntityHandleV1 h,
			NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalRotation)); }
		int32_t GameplaySetLocalRotationCallback(EntityHandleV1 h,
			NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalRotation)); }
		int32_t GameplayGetLocalScaleCallback(EntityHandleV1 h,
			NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalScale)); }
		int32_t GameplaySetLocalScaleCallback(EntityHandleV1 h,
			NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, static_cast<uint32_t>(
			ComponentIds::TransformProperties::LocalScale)); }

		NativePropertyValueV1 GameplayBool(bool value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Bool);
			result.Integer = value ? 1 : 0;
			return result;
		}

		NativePropertyValueV1 GameplayInt32(int32_t value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Int32);
			result.Integer = value;
			return result;
		}

		NativePropertyValueV1 GameplayUInt32(uint32_t value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt32);
			result.Integer = static_cast<int64_t>(value);
			return result;
		}

		NativePropertyValueV1 GameplayUInt64(uint64_t value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::UInt64);
			result.Integer = static_cast<int64_t>(value);
			return result;
		}

		NativePropertyValueV1 GameplayFloat(float value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Float);
			result.Number = value;
			return result;
		}

		NativePropertyValueV1 GameplayVector2(const glm::vec2& value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector2);
			result.Vector = { value.x, value.y, 0.0f, 0.0f };
			return result;
		}

		NativePropertyValueV1 GameplayVector3(const glm::vec3& value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector3);
			result.Vector = { value.x, value.y, value.z, 0.0f };
			return result;
		}

		NativePropertyValueV1 GameplayVector4(const glm::vec4& value)
		{
			NativePropertyValueV1 result;
			result.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector4);
			result.Vector = { value.x, value.y, value.z, value.w };
			return result;
		}

		uint64_t GameplayRegisteredTypeId(NativeComponentType type)
		{
			switch (type)
			{
				case NativeComponentType::Rigidbody2D: return ComponentIds::Rigidbody2D;
				case NativeComponentType::BoxCollider2D: return ComponentIds::BoxCollider2D;
				case NativeComponentType::CircleCollider2D: return ComponentIds::CircleCollider2D;
				case NativeComponentType::DistanceJoint2D: return ComponentIds::DistanceJoint2D;
				case NativeComponentType::SpriteRenderer: return ComponentIds::SpriteRenderer;
				case NativeComponentType::Camera: return ComponentIds::Camera;
				case NativeComponentType::SpriteAnimator: return ComponentIds::SpriteAnimator;
				default: return 0;
			}
		}

		bool GameplayReadBool(const NativePropertyValueV1& value, bool& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Bool)
				|| (value.Integer != 0 && value.Integer != 1))
				return false;
			output = value.Integer != 0;
			return true;
		}

		bool GameplayReadInt32(const NativePropertyValueV1& value, int32_t& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Int32)
				|| value.Integer < std::numeric_limits<int32_t>::min()
				|| value.Integer > std::numeric_limits<int32_t>::max())
				return false;
			output = static_cast<int32_t>(value.Integer);
			return true;
		}

		bool GameplayReadUInt32(const NativePropertyValueV1& value, uint32_t& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::UInt32)
				|| value.Integer < 0
				|| static_cast<uint64_t>(value.Integer) > std::numeric_limits<uint32_t>::max())
				return false;
			output = static_cast<uint32_t>(value.Integer);
			return true;
		}

		bool GameplayReadUInt64(const NativePropertyValueV1& value, uint64_t& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::UInt64))
				return false;
			output = static_cast<uint64_t>(value.Integer);
			return true;
		}

		bool GameplayReadFloat(const NativePropertyValueV1& value, float& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Float)
				|| !std::isfinite(value.Number)
				|| value.Number < -std::numeric_limits<float>::max()
				|| value.Number > std::numeric_limits<float>::max())
				return false;
			output = static_cast<float>(value.Number);
			return true;
		}

		bool GameplayReadVector2(const NativePropertyValueV1& value, glm::vec2& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector2)
				|| !std::isfinite(value.Vector.X) || !std::isfinite(value.Vector.Y))
				return false;
			output = { value.Vector.X, value.Vector.Y };
			return true;
		}

		bool GameplayReadVector3(const NativePropertyValueV1& value, glm::vec3& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector3)
				|| !std::isfinite(value.Vector.X) || !std::isfinite(value.Vector.Y)
				|| !std::isfinite(value.Vector.Z))
				return false;
			output = { value.Vector.X, value.Vector.Y, value.Vector.Z };
			return true;
		}

		bool GameplayReadVector4(const NativePropertyValueV1& value, glm::vec4& output)
		{
			if (value.Kind != static_cast<uint32_t>(NativePropertyKindV1::Vector4)
				|| !std::isfinite(value.Vector.X) || !std::isfinite(value.Vector.Y)
				|| !std::isfinite(value.Vector.Z) || !std::isfinite(value.Vector.W))
				return false;
			output = { value.Vector.X, value.Vector.Y, value.Vector.Z, value.Vector.W };
			return true;
		}

		int32_t GameplayGetComponentPropertyCallback(EntityHandleV1 handle,
			int32_t componentType, uint32_t propertyId,
			NativePropertyValueV1* output) noexcept
		{
			return Guard([&]()
			{
				using namespace GameplayPropertyIds;
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!output) return Code(ScriptStatus::InvalidArgument);
				const NativeComponentType nativeType =
					static_cast<NativeComponentType>(componentType);
				bool present = false;
				if (!ScriptEngine::Get().GetProjectedComponentPresence(handle,
					nativeType, present))
					return Code(ScriptStatus::InvalidArgument);
				if (!present)
					return Code(ScriptStatus::NotFound);
				if (nativeType == NativeComponentType::Transform)
				{
					NativeVector3 projected{};
					if (!ScriptEngine::Get().GetProjectedTransformProperty(
						handle, propertyId, projected))
						return Code(ScriptStatus::InvalidArgument);
					*output = GameplayVector3(ToGlm(projected));
					return Code(ScriptStatus::Success);
				}
				bool useDefault = false;
				if (ScriptEngine::Get().TryGetProjectedComponentProperty(handle,
					nativeType, propertyId, *output, useDefault))
					return Code(ScriptStatus::Success);
				if (useDefault)
				{
					if (nativeType == NativeComponentType::SpriteAnimator
						&& propertyId == AnimatorIsPlaying)
					{
						*output = GameplayBool(false);
						return Code(ScriptStatus::Success);
					}
					if (nativeType == NativeComponentType::SpriteAnimator
						&& propertyId == AnimatorCurrentFrame)
					{
						*output = GameplayUInt32(0);
						return Code(ScriptStatus::Success);
					}
					const ComponentDescriptor* descriptor = FindScriptComponent(
						GameplayRegisteredTypeId(nativeType));
					const PropertyDescriptor* property = descriptor
						? FindProperty(*descriptor, propertyId) : nullptr;
					return property && property->DefaultValue
						&& WriteNativeProperty(property->Kind,
							*property->DefaultValue, *output)
						? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidState);
				}
				Entity entity = Resolve(handle);
				if (!entity) return Code(ScriptStatus::NotFound);
				switch (nativeType)
				{
					case NativeComponentType::Transform:
					{
						NativeVector3 projected{};
						if (!ScriptEngine::Get().GetProjectedTransformProperty(
							handle, propertyId, projected))
							return Code(ScriptStatus::InvalidArgument);
						*output = GameplayVector3(ToGlm(projected));
						return Code(ScriptStatus::Success);
					}
					case NativeComponentType::Rigidbody2D:
					{
						if (!entity.HasComponent<Rigidbody2D>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<Rigidbody2D>();
						if (propertyId == RigidbodyEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == RigidbodyBodyType) *output = GameplayInt32(static_cast<int32_t>(value.Type));
						else if (propertyId == RigidbodyFixedRotation) *output = GameplayBool(value.FixedRotation);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::SpriteRenderer:
					{
						if (!entity.HasComponent<SpriteRenderer>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<SpriteRenderer>();
						if (propertyId == SpriteEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == SpriteColor) *output = GameplayVector4(value._Color);
						else if (propertyId == SpriteAsset) *output = GameplayUInt64(static_cast<uint64_t>(value.SpriteHandle));
						else if (propertyId == SpriteTilingFactor) *output = GameplayFloat(value.TilingFactor);
						else if (propertyId == SpriteSortingLayer) *output = GameplayInt32(value.SortingLayer);
						else if (propertyId == SpriteOrderInLayer) *output = GameplayInt32(value.OrderInLayer);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::SpriteAnimator:
					{
						if (!entity.HasComponent<SpriteAnimator>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<SpriteAnimator>();
						if (propertyId == AnimatorEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == AnimatorSpeed) *output = GameplayFloat(value.Speed);
						else if (propertyId == AnimatorIsPlaying) *output = GameplayBool(value.RuntimePlaying);
						else if (propertyId == AnimatorCurrentFrame) *output = GameplayUInt32(value.RuntimeFrameIndex);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::Camera:
					{
						if (!entity.HasComponent<C_Camera>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<C_Camera>();
						if (propertyId == CameraPrimary) *output = GameplayBool(value.Primary);
						else if (propertyId == CameraFixedAspectRatio) *output = GameplayBool(value.FixedAspectRatio);
						else if (propertyId == CameraBackgroundColor) *output = GameplayVector4(value.BackgroundColor);
						else if (propertyId == CameraProjectionType) *output = GameplayInt32(static_cast<int32_t>(value._Camera.GetProjectionType()));
						else if (propertyId == CameraOrthographicSize) *output = GameplayFloat(value._Camera.GetOrthographicSize());
						else if (propertyId == CameraOrthographicNear) *output = GameplayFloat(value._Camera.GetOrthographicNearClip());
						else if (propertyId == CameraOrthographicFar) *output = GameplayFloat(value._Camera.GetOrthographicFarClip());
						else if (propertyId == CameraPerspectiveFov) *output = GameplayFloat(value._Camera.GetPerspectiveVerticalFOV());
						else if (propertyId == CameraPerspectiveNear) *output = GameplayFloat(value._Camera.GetPerspectiveNearClip());
						else if (propertyId == CameraPerspectiveFar) *output = GameplayFloat(value._Camera.GetPerspectiveFarClip());
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::BoxCollider2D:
					{
						if (!entity.HasComponent<BoxCollider2D>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<BoxCollider2D>();
						if (propertyId == BoxEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == BoxIsTrigger) *output = GameplayBool(value.IsTrigger);
						else if (propertyId == BoxCollisionLayer) *output = GameplayUInt32(value.CollisionLayer);
						else if (propertyId == BoxCollisionMask) *output = GameplayUInt32(value.CollisionMask);
						else if (propertyId == BoxOffset) *output = GameplayVector2(value.Offset);
						else if (propertyId == BoxSize) *output = GameplayVector2(value.Size);
						else if (propertyId == BoxDensity) *output = GameplayFloat(value.Density);
						else if (propertyId == BoxFriction) *output = GameplayFloat(value.Friction);
						else if (propertyId == BoxRestitution) *output = GameplayFloat(value.Restitution);
						else if (propertyId == BoxRestitutionThreshold) *output = GameplayFloat(value.RestitutionThreshold);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::CircleCollider2D:
					{
						if (!entity.HasComponent<CircleCollider2D>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<CircleCollider2D>();
						if (propertyId == CircleEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == CircleIsTrigger) *output = GameplayBool(value.IsTrigger);
						else if (propertyId == CircleCollisionLayer) *output = GameplayUInt32(value.CollisionLayer);
						else if (propertyId == CircleCollisionMask) *output = GameplayUInt32(value.CollisionMask);
						else if (propertyId == CircleOffset) *output = GameplayVector2(value.Offset);
						else if (propertyId == CircleRadius) *output = GameplayFloat(value.Radius);
						else if (propertyId == CircleDensity) *output = GameplayFloat(value.Density);
						else if (propertyId == CircleFriction) *output = GameplayFloat(value.Friction);
						else if (propertyId == CircleRestitution) *output = GameplayFloat(value.Restitution);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::DistanceJoint2D:
					{
						if (!entity.HasComponent<DistanceJoint2D>()) return Code(ScriptStatus::NotFound);
						const auto& value = entity.GetComponent<DistanceJoint2D>();
						if (propertyId == JointEnabled) *output = GameplayBool(value.Enabled);
						else if (propertyId == JointConnectedEntity) *output = GameplayUInt64(static_cast<uint64_t>(value.ConnectedEntity));
						else if (propertyId == JointAnchor) *output = GameplayVector2(value.Anchor);
						else if (propertyId == JointConnectedAnchor) *output = GameplayVector2(value.ConnectedAnchor);
						else if (propertyId == JointDistance) *output = GameplayFloat(value.Distance);
						else if (propertyId == JointFrequency) *output = GameplayFloat(value.Frequency);
						else if (propertyId == JointDamping) *output = GameplayFloat(value.Damping);
						else if (propertyId == JointCollideConnected) *output = GameplayBool(value.CollideConnected);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					default: return Code(ScriptStatus::InvalidArgument);
				}
			});
		}

		// Defined with the frozen V1 asset callbacks below. The Gameplay setter
		// reuses its strict authoring/cooked type resolution for Sprite references.
		int32_t AssetGetTypeCallback(uint64_t handle, int32_t* output) noexcept;

		bool ValidateGameplayPropertyInput(EntityHandleV1 handle,
			NativeComponentType componentType, uint32_t propertyId,
			const NativePropertyValueV1& input)
		{
			using namespace GameplayPropertyIds;
			bool boolean = false;
			int32_t integer = 0;
			uint32_t unsignedInteger = 0;
			uint64_t unsigned64 = 0;
			float number = 0.0f;
			glm::vec2 vector2{};
			glm::vec3 vector3{};
			glm::vec4 vector4{};
			switch (componentType)
			{
				case NativeComponentType::Transform:
					return propertyId >= static_cast<uint32_t>(
							ComponentIds::TransformProperties::Translation)
						&& propertyId <= static_cast<uint32_t>(
							ComponentIds::TransformProperties::LocalScale)
						&& GameplayReadVector3(input, vector3);
				case NativeComponentType::Rigidbody2D:
					return (propertyId == RigidbodyEnabled && GameplayReadBool(input, boolean))
						|| (propertyId == RigidbodyFixedRotation && GameplayReadBool(input, boolean))
						|| (propertyId == RigidbodyBodyType && GameplayReadInt32(input, integer)
							&& integer >= 0 && integer <= 2);
				case NativeComponentType::SpriteRenderer:
					if ((propertyId == SpriteEnabled && GameplayReadBool(input, boolean))
						|| (propertyId == SpriteColor && GameplayReadVector4(input, vector4))
						|| (propertyId == SpriteTilingFactor && GameplayReadFloat(input, number)
							&& number > 0.0f)
						|| ((propertyId == SpriteSortingLayer || propertyId == SpriteOrderInLayer)
							&& GameplayReadInt32(input, integer)))
						return true;
					if (propertyId != SpriteAsset || !GameplayReadUInt64(input, unsigned64))
						return false;
					if (unsigned64 == 0)
						return true;
					{
						int32_t assetType = 0;
						return AssetGetTypeCallback(unsigned64, &assetType) == 0
							&& static_cast<AssetType>(assetType) == AssetType::Texture2D;
					}
				case NativeComponentType::SpriteAnimator:
					return (propertyId == AnimatorEnabled && GameplayReadBool(input, boolean))
						|| (propertyId == AnimatorSpeed && GameplayReadFloat(input, number)
							&& number >= 0.0f);
				case NativeComponentType::Camera:
					return ((propertyId == CameraPrimary || propertyId == CameraFixedAspectRatio)
							&& GameplayReadBool(input, boolean))
						|| (propertyId == CameraBackgroundColor
							&& GameplayReadVector4(input, vector4))
						|| (propertyId == CameraProjectionType && GameplayReadInt32(input, integer)
							&& integer >= 0 && integer <= 1)
						|| (propertyId == CameraOrthographicSize && GameplayReadFloat(input, number)
							&& number > 0.0f)
						|| ((propertyId == CameraOrthographicNear
							|| propertyId == CameraOrthographicFar)
							&& GameplayReadFloat(input, number))
						|| (propertyId == CameraPerspectiveFov && GameplayReadFloat(input, number)
							&& number > 0.0f && number < 3.14159265358979323846f)
						|| ((propertyId == CameraPerspectiveNear
							|| propertyId == CameraPerspectiveFar)
							&& GameplayReadFloat(input, number) && number > 0.0f);
				case NativeComponentType::BoxCollider2D:
					return ((propertyId == BoxEnabled || propertyId == BoxIsTrigger)
							&& GameplayReadBool(input, boolean))
						|| (propertyId == BoxCollisionLayer
							&& GameplayReadUInt32(input, unsignedInteger)
							&& unsignedInteger > 0 && unsignedInteger <= 0xffff)
						|| (propertyId == BoxCollisionMask
							&& GameplayReadUInt32(input, unsignedInteger)
							&& unsignedInteger <= 0xffff)
						|| (propertyId == BoxOffset && GameplayReadVector2(input, vector2))
						|| (propertyId == BoxSize && GameplayReadVector2(input, vector2)
							&& vector2.x > 0.0f && vector2.y > 0.0f)
						|| ((propertyId == BoxDensity || propertyId == BoxFriction
							|| propertyId == BoxRestitutionThreshold)
							&& GameplayReadFloat(input, number) && number >= 0.0f)
						|| (propertyId == BoxRestitution && GameplayReadFloat(input, number)
							&& number >= 0.0f && number <= 1.0f);
				case NativeComponentType::CircleCollider2D:
					return ((propertyId == CircleEnabled || propertyId == CircleIsTrigger)
							&& GameplayReadBool(input, boolean))
						|| (propertyId == CircleCollisionLayer
							&& GameplayReadUInt32(input, unsignedInteger)
							&& unsignedInteger > 0 && unsignedInteger <= 0xffff)
						|| (propertyId == CircleCollisionMask
							&& GameplayReadUInt32(input, unsignedInteger)
							&& unsignedInteger <= 0xffff)
						|| (propertyId == CircleOffset && GameplayReadVector2(input, vector2))
						|| (propertyId == CircleRadius && GameplayReadFloat(input, number)
							&& number > 0.0f)
						|| ((propertyId == CircleDensity || propertyId == CircleFriction)
							&& GameplayReadFloat(input, number) && number >= 0.0f)
						|| (propertyId == CircleRestitution && GameplayReadFloat(input, number)
							&& number >= 0.0f && number <= 1.0f);
				case NativeComponentType::DistanceJoint2D:
					if ((propertyId == JointEnabled || propertyId == JointCollideConnected)
						&& GameplayReadBool(input, boolean)) return true;
					if ((propertyId == JointAnchor || propertyId == JointConnectedAnchor)
						&& GameplayReadVector2(input, vector2)) return true;
					if ((propertyId == JointDistance && GameplayReadFloat(input, number)
							&& number > 0.0f)
						|| (propertyId == JointFrequency && GameplayReadFloat(input, number)
							&& number >= 0.0f)
						|| (propertyId == JointDamping && GameplayReadFloat(input, number)
							&& number >= 0.0f && number <= 1.0f)) return true;
					if (propertyId != JointConnectedEntity
						|| !GameplayReadUInt64(input, unsigned64)
						|| unsigned64 == handle.EntityId) return false;
					if (unsigned64 == 0) return true;
					{
						const EntityHandleV1 target{ handle.SceneSessionId, unsigned64,
							handle.RuntimeGeneration };
						if (ApplyComponentPropertyImmediately)
							return static_cast<bool>(
								ScriptEngine::Get().ResolveEntity(target));
						bool targetAlive = false;
						return ScriptEngine::Get().GetProjectedEntityLiveness(
							target, targetAlive) && targetAlive;
					}
				default: return false;
			}
		}

		int32_t GameplaySetComponentPropertyCallback(EntityHandleV1 handle,
			int32_t componentType, uint32_t propertyId,
			NativePropertyValueV1 input) noexcept
		{
			return Guard([&]()
			{
				using namespace GameplayPropertyIds;
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				const NativeComponentType nativeType =
					static_cast<NativeComponentType>(componentType);
				if (!ValidateGameplayPropertyInput(handle, nativeType, propertyId, input))
					return RejectDeferredMutation(handle,
						ScriptStatus::InvalidArgument,
						"Component property identifier or value is invalid");
				if (!ApplyComponentPropertyImmediately)
				{
					bool present = false;
					if (!ScriptEngine::Get().GetProjectedComponentPresence(handle,
						nativeType, present) || !present)
						return RejectDeferredMutation(handle,
							ScriptStatus::NotFound,
							"Component property target is unavailable");
					return ScriptEngine::Get().QueueSetComponentProperty(handle,
						nativeType, propertyId, input)
						? Code(ScriptStatus::Success)
						: RejectDeferredMutation(handle, ScriptStatus::InvalidState,
							"Component property could not be queued");
				}
				Entity entity = ScriptEngine::Get().ResolveEntity(handle);
				Scene* scene = ScriptEngine::Get().ResolveScene(handle);
				if (!entity || !scene)
					return Code(ScriptStatus::NotFound);
				bool boolean = false; int32_t integer = 0; uint32_t unsignedInteger = 0;
				uint64_t unsigned64 = 0; float number = 0.0f;
				glm::vec2 vector2{}; glm::vec3 vector3{}; glm::vec4 vector4{};
				switch (static_cast<NativeComponentType>(componentType))
				{
					case NativeComponentType::Transform:
					{
						if (!entity.HasComponent<Transform>()
							|| !GameplayReadVector3(input, vector3))
							return Code(ScriptStatus::NotFound);
						auto transform = entity.GetComponent<Transform>();
						using namespace ComponentIds::TransformProperties;
						if (propertyId == static_cast<uint32_t>(Translation))
							transform._Translation = vector3;
						else if (propertyId == static_cast<uint32_t>(Rotation))
							transform._Rotation = vector3;
						else if (propertyId == static_cast<uint32_t>(Scale))
							transform._Scale = vector3;
						else if (propertyId == static_cast<uint32_t>(LocalTranslation))
							transform._LocalTranslation = vector3;
						else if (propertyId == static_cast<uint32_t>(LocalRotation))
							transform._LocalRotation = vector3;
						else if (propertyId == static_cast<uint32_t>(LocalScale))
							transform._LocalScale = vector3;
						else
							return Code(ScriptStatus::InvalidArgument);
						const bool worldProperty =
							propertyId <= static_cast<uint32_t>(Scale);
						return (worldProperty
							? scene->SetWorldTransform(entity, transform.GetTransform())
							: scene->SetLocalTransform(entity,
								transform.GetLocalTransform()))
							? Code(ScriptStatus::Success)
							: Code(ScriptStatus::InvalidArgument);
					}
					case NativeComponentType::Rigidbody2D:
					{
						if (!entity.HasComponent<Rigidbody2D>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<Rigidbody2D>();
						if (propertyId == RigidbodyEnabled && GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == RigidbodyFixedRotation && GameplayReadBool(input, boolean)) value.FixedRotation = boolean;
						else if (propertyId == RigidbodyBodyType && GameplayReadInt32(input, integer)
							&& integer >= 0 && integer <= 2) value.Type = static_cast<Rigidbody2D::BodyType>(integer);
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::SpriteRenderer:
					{
						if (!entity.HasComponent<SpriteRenderer>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<SpriteRenderer>();
						if (propertyId == SpriteEnabled && GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == SpriteColor && GameplayReadVector4(input, vector4)) value._Color = vector4;
						else if (propertyId == SpriteTilingFactor && GameplayReadFloat(input, number) && number > 0.0f) value.TilingFactor = number;
						else if (propertyId == SpriteSortingLayer && GameplayReadInt32(input, integer)) value.SortingLayer = integer;
						else if (propertyId == SpriteOrderInLayer && GameplayReadInt32(input, integer)) value.OrderInLayer = integer;
						else if (propertyId == SpriteAsset && GameplayReadUInt64(input, unsigned64))
						{
							if (unsigned64 != 0)
							{
								int32_t assetType = 0;
								if (AssetGetTypeCallback(unsigned64, &assetType) != 0
									|| static_cast<AssetType>(assetType) != AssetType::Texture2D)
									return Code(ScriptStatus::InvalidArgument);
							}
							value.SpriteHandle = AssetHandle(unsigned64);
							value.Sprite.reset();
						}
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::SpriteAnimator:
					{
						if (!entity.HasComponent<SpriteAnimator>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<SpriteAnimator>();
						if (propertyId == AnimatorEnabled
							&& GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == AnimatorSpeed
							&& GameplayReadFloat(input, number) && number >= 0.0f) value.Speed = number;
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::Camera:
					{
						if (!entity.HasComponent<C_Camera>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<C_Camera>();
						if (propertyId == CameraPrimary && GameplayReadBool(input, boolean)) value.Primary = boolean;
						else if (propertyId == CameraFixedAspectRatio && GameplayReadBool(input, boolean)) value.FixedAspectRatio = boolean;
						else if (propertyId == CameraBackgroundColor && GameplayReadVector4(input, vector4)) value.BackgroundColor = vector4;
						else if (propertyId == CameraProjectionType && GameplayReadInt32(input, integer)
							&& value._Camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(integer))) {}
						else if (propertyId == CameraOrthographicSize && GameplayReadFloat(input, number)
							&& value._Camera.SetOrthographicSize(number)) {}
						else if (propertyId == CameraOrthographicNear && GameplayReadFloat(input, number)
							&& value._Camera.SetOrthographicNearClip(number)) {}
						else if (propertyId == CameraOrthographicFar && GameplayReadFloat(input, number)
							&& value._Camera.SetOrthographicFarClip(number)) {}
						else if (propertyId == CameraPerspectiveFov && GameplayReadFloat(input, number)
							&& value._Camera.SetPerspectiveVerticalFOV(number)) {}
						else if (propertyId == CameraPerspectiveNear && GameplayReadFloat(input, number)
							&& value._Camera.SetPerspectiveNearClip(number)) {}
						else if (propertyId == CameraPerspectiveFar && GameplayReadFloat(input, number)
							&& value._Camera.SetPerspectiveFarClip(number)) {}
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::BoxCollider2D:
					{
						if (!entity.HasComponent<BoxCollider2D>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<BoxCollider2D>();
						if (propertyId == BoxEnabled && GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == BoxIsTrigger && GameplayReadBool(input, boolean)) value.IsTrigger = boolean;
						else if (propertyId == BoxCollisionLayer && GameplayReadUInt32(input, unsignedInteger) && unsignedInteger > 0 && unsignedInteger <= 0xffff) value.CollisionLayer = static_cast<uint16_t>(unsignedInteger);
						else if (propertyId == BoxCollisionMask && GameplayReadUInt32(input, unsignedInteger) && unsignedInteger <= 0xffff) value.CollisionMask = static_cast<uint16_t>(unsignedInteger);
						else if (propertyId == BoxOffset && GameplayReadVector2(input, vector2)) value.Offset = vector2;
						else if (propertyId == BoxSize && GameplayReadVector2(input, vector2) && vector2.x > 0.0f && vector2.y > 0.0f) value.Size = vector2;
						else if (propertyId == BoxDensity && GameplayReadFloat(input, number) && number >= 0.0f) value.Density = number;
						else if (propertyId == BoxFriction && GameplayReadFloat(input, number) && number >= 0.0f) value.Friction = number;
						else if (propertyId == BoxRestitution && GameplayReadFloat(input, number) && number >= 0.0f && number <= 1.0f) value.Restitution = number;
						else if (propertyId == BoxRestitutionThreshold && GameplayReadFloat(input, number) && number >= 0.0f) value.RestitutionThreshold = number;
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::CircleCollider2D:
					{
						if (!entity.HasComponent<CircleCollider2D>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<CircleCollider2D>();
						if (propertyId == CircleEnabled && GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == CircleIsTrigger && GameplayReadBool(input, boolean)) value.IsTrigger = boolean;
						else if (propertyId == CircleCollisionLayer && GameplayReadUInt32(input, unsignedInteger) && unsignedInteger > 0 && unsignedInteger <= 0xffff) value.CollisionLayer = static_cast<uint16_t>(unsignedInteger);
						else if (propertyId == CircleCollisionMask && GameplayReadUInt32(input, unsignedInteger) && unsignedInteger <= 0xffff) value.CollisionMask = static_cast<uint16_t>(unsignedInteger);
						else if (propertyId == CircleOffset && GameplayReadVector2(input, vector2)) value.Offset = vector2;
						else if (propertyId == CircleRadius && GameplayReadFloat(input, number) && number > 0.0f) value.Radius = number;
						else if (propertyId == CircleDensity && GameplayReadFloat(input, number) && number >= 0.0f) value.Density = number;
						else if (propertyId == CircleFriction && GameplayReadFloat(input, number) && number >= 0.0f) value.Friction = number;
						else if (propertyId == CircleRestitution && GameplayReadFloat(input, number) && number >= 0.0f && number <= 1.0f) value.Restitution = number;
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					case NativeComponentType::DistanceJoint2D:
					{
						if (!entity.HasComponent<DistanceJoint2D>()) return Code(ScriptStatus::NotFound);
						auto& value = entity.GetComponent<DistanceJoint2D>();
						if (propertyId == JointEnabled && GameplayReadBool(input, boolean)) value.Enabled = boolean;
						else if (propertyId == JointCollideConnected && GameplayReadBool(input, boolean)) value.CollideConnected = boolean;
						else if (propertyId == JointAnchor && GameplayReadVector2(input, vector2)) value.Anchor = vector2;
						else if (propertyId == JointConnectedAnchor && GameplayReadVector2(input, vector2)) value.ConnectedAnchor = vector2;
						else if (propertyId == JointDistance && GameplayReadFloat(input, number) && number > 0.0f) value.Distance = number;
						else if (propertyId == JointFrequency && GameplayReadFloat(input, number) && number >= 0.0f) value.Frequency = number;
						else if (propertyId == JointDamping && GameplayReadFloat(input, number) && number >= 0.0f && number <= 1.0f) value.Damping = number;
						else if (propertyId == JointConnectedEntity && GameplayReadUInt64(input, unsigned64))
						{
							if (unsigned64 != 0 && (!scene->FindEntityByUUID(UUID(unsigned64))
								|| unsigned64 == static_cast<uint64_t>(entity.GetUUID())))
								return Code(ScriptStatus::InvalidArgument);
							value.ConnectedEntity = UUID(unsigned64);
						}
						else return Code(ScriptStatus::InvalidArgument);
						return 0;
					}
					default: return Code(ScriptStatus::InvalidArgument);
				}
			});
		}

		int32_t GameplaySpriteAnimatorPlayCallback(EntityHandleV1 handle,
			NativeUtf8View clipNameView, int32_t restart) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (restart != 0 && restart != 1)
					return Code(ScriptStatus::InvalidArgument);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<SpriteAnimator>()
					|| !entity.HasComponent<SpriteRenderer>())
					return Code(ScriptStatus::NotFound);
				std::string clipName;
				if (!ReadUtf8(clipNameView, clipName) || clipName.empty())
					return Code(ScriptStatus::InvalidArgument);
				auto& animator = entity.GetComponent<SpriteAnimator>();
				auto& renderer = entity.GetComponent<SpriteRenderer>();
				return SpriteAnimatorRuntime::Play(animator, renderer, clipName,
					restart != 0) ? 1 : 0;
			});
		}

		int32_t GameplaySpriteAnimatorStopCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				SpriteAnimatorRuntime::Stop(entity.GetComponent<SpriteAnimator>());
				return Code(ScriptStatus::Success);
			});
		}

		int32_t GameplaySpriteAnimatorSetBoolCallback(EntityHandleV1 handle,
			NativeUtf8View parameterView, int32_t value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (value != 0 && value != 1) return Code(ScriptStatus::InvalidArgument);
				Entity entity = Resolve(handle); std::string parameter;
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(parameterView, parameter) || parameter.empty())
					return Code(ScriptStatus::InvalidArgument);
				return SpriteAnimatorRuntime::SetBool(
					entity.GetComponent<SpriteAnimator>(), parameter, value != 0)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySpriteAnimatorSetIntCallback(EntityHandleV1 handle,
			NativeUtf8View parameterView, int32_t value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); std::string parameter;
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(parameterView, parameter) || parameter.empty())
					return Code(ScriptStatus::InvalidArgument);
				return SpriteAnimatorRuntime::SetInt(
					entity.GetComponent<SpriteAnimator>(), parameter, value)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySpriteAnimatorSetFloatCallback(EntityHandleV1 handle,
			NativeUtf8View parameterView, float value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); std::string parameter;
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(parameterView, parameter) || parameter.empty())
					return Code(ScriptStatus::InvalidArgument);
				return SpriteAnimatorRuntime::SetFloat(
					entity.GetComponent<SpriteAnimator>(), parameter, value)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySpriteAnimatorSetTriggerCallback(EntityHandleV1 handle,
			NativeUtf8View parameterView) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); std::string parameter;
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(parameterView, parameter) || parameter.empty())
					return Code(ScriptStatus::InvalidArgument);
				return SpriteAnimatorRuntime::SetTrigger(
					entity.GetComponent<SpriteAnimator>(), parameter)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySpriteAnimatorResetTriggerCallback(EntityHandleV1 handle,
			NativeUtf8View parameterView) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); std::string parameter;
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(parameterView, parameter) || parameter.empty())
					return Code(ScriptStatus::InvalidArgument);
				return SpriteAnimatorRuntime::ResetTrigger(
					entity.GetComponent<SpriteAnimator>(), parameter)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t GameplaySpriteAnimatorGetCurrentStateCallback(EntityHandleV1 handle,
			uint8_t* buffer, uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !entity.HasComponent<SpriteAnimator>())
					return Code(ScriptStatus::NotFound);
				return WriteUtf8(std::string(SpriteAnimatorRuntime::CurrentState(
					entity.GetComponent<SpriteAnimator>())), buffer, capacity, required);
			});
		}

		int32_t AbortDeferredCommandBatchCallback(EntityHandleV1 context,
			NativeUtf8View reasonView) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread())
					return Code(ScriptStatus::WrongThread);
				std::string reason;
				if (reasonView.Length > DeferredCommandFailureMaximumBytesV1
					|| !ReadUtf8(reasonView, reason))
					reason = "Managed script callback faulted";
				return ScriptEngine::Get().MarkDeferredCommandBatchFailed(
					context, std::move(reason))
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		NativeDeferredCommandsApiV1 BuildDeferredCommandsApiV1()
		{
			NativeDeferredCommandsApiV1 api;
			api.AbortBatch = &AbortDeferredCommandBatchCallback;
			return api;
		}

		int32_t BeginDeferredCallbackTransactionCallback(
			EntityHandleV1 context, uint64_t* token) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread())
					return Code(ScriptStatus::WrongThread);
				if (!token)
					return Code(ScriptStatus::InvalidArgument);
				*token = 0;
				return ScriptEngine::Get().BeginDeferredCallbackTransaction(
					context, *token)
					? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidState);
			});
		}

		int32_t CompleteDeferredCallbackTransactionCallback(
			uint64_t token) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread())
					return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().CompleteDeferredCallbackTransaction(token)
					? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidState);
			});
		}

		NativeDeferredCallbackTransactionsApiV1
			BuildDeferredCallbackTransactionsApiV1()
		{
			NativeDeferredCallbackTransactionsApiV1 api;
			api.BeginCallback = &BeginDeferredCallbackTransactionCallback;
			api.CompleteCallback = &CompleteDeferredCallbackTransactionCallback;
			return api;
		}

		NativeGameplayApiV1 BuildGameplayApiV1()
		{
			NativeGameplayApiV1 api;
			api.CreateEntityDeferred = &GameplayCreateEntityCallback;
			api.FindEntityByName = &GameplayFindEntityCallback;
			api.QueryEntities = &GameplayQueryEntitiesCallback;
			api.GetParent = &GameplayGetParentCallback;
			api.SetParentDeferred = &GameplaySetParentCallback;
			api.GetChildren = &GameplayGetChildrenCallback;
			api.GetActiveSelf = &GameplayGetActiveSelfCallback;
			api.SetActiveSelf = &GameplaySetActiveSelfCallback;
			api.GetActiveInHierarchy = &GameplayGetActiveInHierarchyCallback;
			api.TransformGetLocalPosition = &GameplayGetLocalPositionCallback;
			api.TransformSetLocalPosition = &GameplaySetLocalPositionCallback;
			api.TransformGetLocalRotationEuler = &GameplayGetLocalRotationCallback;
			api.TransformSetLocalRotationEuler = &GameplaySetLocalRotationCallback;
			api.TransformGetLocalScale = &GameplayGetLocalScaleCallback;
			api.TransformSetLocalScale = &GameplaySetLocalScaleCallback;
			api.GetComponentProperty = &GameplayGetComponentPropertyCallback;
			api.SetComponentProperty = &GameplaySetComponentPropertyCallback;
			api.SpriteAnimatorPlay = &GameplaySpriteAnimatorPlayCallback;
			api.SpriteAnimatorStop = &GameplaySpriteAnimatorStopCallback;
			api.SpriteAnimatorSetBool = &GameplaySpriteAnimatorSetBoolCallback;
			api.SpriteAnimatorSetInt = &GameplaySpriteAnimatorSetIntCallback;
			api.SpriteAnimatorSetFloat = &GameplaySpriteAnimatorSetFloatCallback;
			api.SpriteAnimatorSetTrigger = &GameplaySpriteAnimatorSetTriggerCallback;
			api.SpriteAnimatorResetTrigger = &GameplaySpriteAnimatorResetTriggerCallback;
			api.SpriteAnimatorGetCurrentState =
				&GameplaySpriteAnimatorGetCurrentStateCallback;
			return api;
		}

		NativeSceneApiV1 BuildSceneApiV1()
		{
			NativeSceneApiV1 api;
			api.RequestLoad = +[](uint64_t handle, int32_t index, uint32_t mode, int32_t asynchronous) noexcept -> int32_t
			{
				return Guard([&]()
				{
					if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
					auto* manager = SceneManager::GetRuntime();
					if (!manager) return Code(ScriptStatus::Unavailable);
					if (mode > 1 || (!handle && index < 0)) return 0;
					const auto loadMode = static_cast<SceneLoadMode>(mode);
					const bool result = asynchronous
						? (handle ? manager->RequestLoadSceneAsync(AssetHandle(handle), loadMode) : manager->RequestLoadSceneAsync(static_cast<uint32_t>(index), loadMode))
						: (handle ? manager->RequestLoadScene(AssetHandle(handle), loadMode) : manager->RequestLoadScene(static_cast<uint32_t>(index), loadMode));
					return result ? 1 : 0;
				});
			};
			api.RequestUnload = +[](uint64_t handle) noexcept -> int32_t
			{
				return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); auto* manager = SceneManager::GetRuntime(); return manager ? (manager->RequestUnloadScene(AssetHandle(handle)) ? 1 : 0) : Code(ScriptStatus::Unavailable); });
			};
			api.SetActive = +[](uint64_t handle) noexcept -> int32_t
			{
				return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); auto* manager = SceneManager::GetRuntime(); return manager ? (manager->SetActiveScene(AssetHandle(handle)) ? 1 : 0) : Code(ScriptStatus::Unavailable); });
			};
			api.SetPersistent = +[](EntityHandleV1 entity, int32_t persistent) noexcept -> int32_t
			{
				return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); auto* manager = SceneManager::GetRuntime(); return manager ? (manager->SetEntityPersistent(ScriptEngine::Get().ResolveEntity(entity), persistent != 0) ? 1 : 0) : Code(ScriptStatus::Unavailable); });
			};
			api.GetLoadStatus = +[](uint32_t* state, float* progress, int32_t* allow) noexcept -> int32_t
			{
				return Guard([&]()
				{
					if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
					if (!state || !progress || !allow) return Code(ScriptStatus::InvalidArgument);
					auto* manager = SceneManager::GetRuntime();
					if (!manager) return Code(ScriptStatus::Unavailable);
					*state = static_cast<uint32_t>(manager->GetLoadState()); *progress = manager->GetLoadProgress(); *allow = manager->GetAllowSceneActivation() ? 1 : 0;
					return Code(ScriptStatus::Success);
				});
			};
			api.SetAllowActivation = +[](int32_t allow) noexcept -> int32_t
			{
				return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); auto* manager = SceneManager::GetRuntime(); if (!manager) return Code(ScriptStatus::Unavailable); manager->SetAllowSceneActivation(allow != 0); return Code(ScriptStatus::Success); });
			};
			api.CancelLoad = +[]() noexcept -> int32_t
			{
				return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); auto* manager = SceneManager::GetRuntime(); return manager ? (manager->CancelPendingLoad() ? 1 : 0) : Code(ScriptStatus::Unavailable); });
			};
			api.GetLoadedScenes = +[](uint64_t* handles, uint32_t capacity, uint32_t* required) noexcept -> int32_t
			{
				return Guard([&]()
				{
					if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
					if (!required) return Code(ScriptStatus::InvalidArgument);
					auto* manager = SceneManager::GetRuntime();
					if (!manager) return Code(ScriptStatus::Unavailable);
					const auto& loaded = manager->GetLoadedSceneHandles();
					*required = static_cast<uint32_t>(loaded.size());
					if (capacity < loaded.size() || (!handles && !loaded.empty())) return Code(ScriptStatus::BufferTooSmall);
					for (size_t index = 0; index < loaded.size(); ++index) handles[index] = static_cast<uint64_t>(loaded[index]);
					return Code(ScriptStatus::Success);
				});
			};
			api.GetLastError = +[](uint8_t* buffer, uint32_t capacity, uint32_t* required) noexcept -> int32_t
			{
				return Guard([&]()
				{
					if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
					if (!required) return Code(ScriptStatus::InvalidArgument);
					auto* manager = SceneManager::GetRuntime();
					if (!manager) return Code(ScriptStatus::Unavailable);
					const auto& error = manager->GetLastError(); *required = static_cast<uint32_t>(error.size());
					if (capacity < error.size() || (!buffer && !error.empty())) return Code(ScriptStatus::BufferTooSmall);
					if (!error.empty()) std::memcpy(buffer, error.data(), error.size());
					return Code(ScriptStatus::Success);
				});
			};
			return api;
		}

		int32_t QueryCapabilityCallback(NativeUtf8View name, uint32_t minimumVersion,
			void* output, uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				std::string capability;
				if (!required || !ReadUtf8(name, capability))
					return Code(ScriptStatus::InvalidArgument);
				if (capability == SceneCapabilityName)
				{
					const NativeSceneApiV1 api = BuildSceneApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version) return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api)) return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == InputCapabilityName)
				{
					const NativeInputApiV1 api = BuildInputApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == InputEventsCapabilityName)
				{
					const NativeInputEventsApiV1 api = BuildInputEventsApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == ApplicationPathsCapabilityName)
				{
					const NativeApplicationPathsApiV1 api = BuildApplicationPathsApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == ComponentCapabilityName)
				{
					const NativeComponentApiV1 api = BuildComponentApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == ComponentStringCapabilityName)
				{
					const NativeComponentStringApiV1 api = BuildComponentStringApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == ComponentSchemaCapabilityName)
				{
					const NativeComponentSchemaApiV1 api =
						BuildComponentSchemaApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == DeferredCommandsCapabilityName)
				{
					const NativeDeferredCommandsApiV1 api =
						BuildDeferredCommandsApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == DeferredCallbackTransactionsCapabilityName)
				{
					const NativeDeferredCallbackTransactionsApiV1 api =
						BuildDeferredCallbackTransactionsApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == AudioCapabilityName)
				{
					const NativeAudioApiV1 api = BuildAudioApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == AudioSpatialCapabilityName)
				{
					const NativeAudioSpatialApiV1 api = BuildAudioSpatialApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == RuntimeUICapabilityName)
				{
					const NativeRuntimeUIApiV1 api = BuildRuntimeUIApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				if (capability == GameplayCapabilityName)
				{
					const NativeGameplayApiV1 api = BuildGameplayApiV1();
					*required = sizeof(api);
					if (minimumVersion > api.Version)
						return Code(ScriptStatus::VersionMismatch);
					if (!output || capacity < sizeof(api))
						return Code(ScriptStatus::BufferTooSmall);
					std::memcpy(output, &api, sizeof(api));
					return Code(ScriptStatus::Success);
				}
				*required = 0;
				return Code(ScriptStatus::NotFound);
			});
		}

		int32_t RigidbodyGetVelocityCallback(EntityHandleV1 handle, NativeVector2* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Scene* scene = ResolveScene(handle); if (!scene || !value) return Code(ScriptStatus::NotFound);
				auto velocity = scene->GetLinearVelocity2D(UUID(handle.EntityId));
				if (!velocity) return Code(ScriptStatus::InvalidState);
				*value = ToNative(*velocity); return 0;
			});
		}

		int32_t RigidbodySetVelocityCallback(EntityHandleV1 handle, NativeVector2 value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Scene* s=ResolveScene(handle); return s && s->SetLinearVelocity2D(UUID(handle.EntityId),ToGlm(value)) ? 0 : Code(ScriptStatus::InvalidState); }); }
		int32_t RigidbodyApplyForceCallback(EntityHandleV1 handle, NativeVector2 value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Scene* s=ResolveScene(handle); return s && s->ApplyForce2D(UUID(handle.EntityId),ToGlm(value)) ? 0 : Code(ScriptStatus::InvalidState); }); }
		int32_t RigidbodyApplyImpulseCallback(EntityHandleV1 handle, NativeVector2 value) noexcept
		{ return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); Scene* s=ResolveScene(handle); return s && s->ApplyLinearImpulse2D(UUID(handle.EntityId),ToGlm(value)) ? 0 : Code(ScriptStatus::InvalidState); }); }

		int32_t PhysicsRaycastCallback(EntityHandleV1 context, NativeVector2 start,
			NativeVector2 end, uint32_t mask, int32_t triggers, NativeRaycastHit2D* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Scene* scene = ResolveScene(context); if (!scene || !value) return Code(ScriptStatus::InvalidArgument);
				auto hit = scene->Raycast2D(ToGlm(start), ToGlm(end), static_cast<uint16_t>(mask), triggers != 0);
				if (!hit) return Code(ScriptStatus::NotFound);
				value->Entity = { context.SceneSessionId, static_cast<uint64_t>(hit->EntityID), context.RuntimeGeneration };
				value->Point = ToNative(hit->Point); value->Normal = ToNative(hit->Normal);
				value->Fraction = hit->Fraction; value->IsTrigger = hit->IsTrigger ? 1 : 0;
				value->CollisionLayer = hit->CollisionLayer; return 0;
			});
		}

		int32_t PhysicsQueryAabbCallback(EntityHandleV1 context, NativeVector2 minimum,
			NativeVector2 maximum, uint32_t mask, int32_t triggers,
			NativePhysicsQueryHit2D* output, uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Scene* scene = ResolveScene(context); if (!scene || !required) return Code(ScriptStatus::InvalidArgument);
				auto hits = scene->QueryAABB2D(ToGlm(minimum), ToGlm(maximum),
					static_cast<uint16_t>(mask), triggers != 0);
				if (hits.size() > std::numeric_limits<uint32_t>::max()) return Code(ScriptStatus::InvalidState);
				*required = static_cast<uint32_t>(hits.size());
				const uint32_t writeCount = std::min(capacity, *required);
				if (writeCount != 0 && !output) return Code(ScriptStatus::InvalidArgument);
				for (uint32_t i = 0; i < writeCount; ++i)
				{
					output[i].Entity = { context.SceneSessionId, static_cast<uint64_t>(hits[i].EntityID), context.RuntimeGeneration };
					output[i].IsTrigger = hits[i].IsTrigger ? 1 : 0;
					output[i].CollisionLayer = hits[i].CollisionLayer;
				}
				return capacity < *required ? Code(ScriptStatus::BufferTooSmall) : 0;
			});
		}

		int32_t AssetIsValidCallback(uint64_t handle) noexcept
		{
			return Guard([&]()
			{
				if (handle == 0) return 0;
				AssetManager& assets = AssetManager::Get();
				if (const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(AssetHandle(handle)))
					return !metadata->IsMissing ? 1 : 0;
				if (const AssetSubAsset* child =
					assets.GetRegistry().GetSubAsset(AssetHandle(handle)))
					return child->Type != AssetType::None ? 1 : 0;
				if (!assets.IsCookedPackageMounted()) return 0;
				AssetType type = AssetType::None; std::vector<uint8_t> bytes;
				return assets.ReadAssetBytes(AssetHandle(handle), bytes, &type) ? 1 : 0;
			});
		}

		int32_t AssetGetTypeCallback(uint64_t handle, int32_t* output) noexcept
		{
			return Guard([&]()
			{
				if (!output || handle == 0) return Code(ScriptStatus::InvalidArgument);
				AssetManager& assets = AssetManager::Get(); AssetType type = AssetType::None;
				if (const AssetMetadata* metadata = assets.GetRegistry().GetMetadata(AssetHandle(handle))) type = metadata->Type;
				else if (const AssetSubAsset* child = assets.GetRegistry().GetSubAsset(AssetHandle(handle))) type = child->Type;
				else { std::vector<uint8_t> bytes; if (!assets.ReadAssetBytes(AssetHandle(handle), bytes, &type)) return Code(ScriptStatus::NotFound); }
				*output = static_cast<int32_t>(type); return 0;
			});
		}

		int32_t BehaviourGetEnabledCallback(uint64_t handle) noexcept
		{
			return Guard([&]() { bool enabled=false; return ScriptEngine::Get().GetBehaviourEnabled(handle,enabled) ? (enabled?1:0) : Code(ScriptStatus::NotFound); });
		}

		int32_t BehaviourSetEnabledCallback(uint64_t handle, int32_t enabled) noexcept
		{
			return Guard([&]() { if (!RequireMainThread()) return Code(ScriptStatus::WrongThread); return ScriptEngine::Get().QueueBehaviourEnabled(handle,enabled!=0) ? 0 : Code(ScriptStatus::NotFound); });
		}

		int32_t BehaviourRemoveCallback(uint64_t handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueRemoveBehaviour(handle)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		int32_t SceneGetActiveHandleCallback(uint64_t* sceneHandle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!sceneHandle) return Code(ScriptStatus::InvalidArgument);
				SceneManager* manager = SceneManager::GetRuntime();
				if (!manager) return Code(ScriptStatus::Unavailable);
				*sceneHandle = static_cast<uint64_t>(manager->GetActiveSceneHandle());
				return Code(ScriptStatus::Success);
			});
		}

		int32_t SceneGetActiveBuildIndexCallback(int32_t* buildIndex) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (!buildIndex) return Code(ScriptStatus::InvalidArgument);
				SceneManager* manager = SceneManager::GetRuntime();
				if (!manager) return Code(ScriptStatus::Unavailable);
				*buildIndex = manager->GetActiveBuildIndex();
				return Code(ScriptStatus::Success);
			});
		}

		int32_t SceneRequestLoadHandleCallback(uint64_t sceneHandle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				SceneManager* manager = SceneManager::GetRuntime();
				if (!manager) return Code(ScriptStatus::Unavailable);
				if (sceneHandle == 0) return 0;
				return manager->RequestLoadScene(AssetHandle(sceneHandle)) ? 1 : 0;
			});
		}

		int32_t SceneRequestLoadIndexCallback(int32_t buildIndex) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				SceneManager* manager = SceneManager::GetRuntime();
				if (!manager) return Code(ScriptStatus::Unavailable);
				if (buildIndex < 0) return 0;
				return manager->RequestLoadScene(static_cast<uint32_t>(buildIndex)) ? 1 : 0;
			});
		}

		int32_t SceneRequestReloadCallback() noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				SceneManager* manager = SceneManager::GetRuntime();
				if (!manager) return Code(ScriptStatus::Unavailable);
				return manager->RequestReload() ? 1 : 0;
			});
		}

		int32_t PrefabInstantiateDeferredCallback(EntityHandleV1 context,
			uint64_t prefabHandle, NativeVector3 worldPosition,
			EntityHandleV1 parent) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				if (prefabHandle == 0 || !std::isfinite(worldPosition.X)
					|| !std::isfinite(worldPosition.Y)
					|| !std::isfinite(worldPosition.Z))
					return RejectDeferredMutation(context,
						ScriptStatus::InvalidArgument,
						"Prefab handle or world position is invalid");
				return ScriptEngine::Get().QueueInstantiatePrefab(context,
					prefabHandle, worldPosition, parent) ? 1
					: RejectDeferredMutation(context, ScriptStatus::InvalidArgument,
						"Prefab context or parent is unavailable");
			});
		}

	}

	int32_t ApplyGameplayComponentPropertyNow(EntityHandleV1 entity,
		NativeComponentType componentType, uint32_t propertyId,
		NativePropertyValueV1 value) noexcept
	{
		ImmediateComponentPropertyScope immediate;
		return GameplaySetComponentPropertyCallback(entity,
			static_cast<int32_t>(componentType), propertyId, value);
	}

	int32_t ApplyRegisteredComponentPropertyNow(EntityHandleV1 entity,
		uint64_t componentTypeId, uint64_t propertyId,
		NativePropertyValueV1 value) noexcept
	{
		ImmediateComponentPropertyScope immediate;
		return ComponentSetPropertyCallback(entity, componentTypeId,
			propertyId, value);
	}

	int32_t ApplyRegisteredComponentStringPropertyNow(EntityHandleV1 entity,
		uint64_t componentTypeId, uint64_t propertyId,
		NativeUtf8View value) noexcept
	{
		ImmediateComponentPropertyScope immediate;
		return ComponentStringSetPropertyCallback(entity, componentTypeId,
			propertyId, value);
	}

	NativeApiV1 BuildNativeApiV1()
	{
		NativeApiV1 api;
		api.Log = &LogCallback;
		api.EmitDiagnostic = &EmitDiagnosticCallback;
		api.IsMainThread = &IsMainThreadCallback;
		api.EntityIsAlive = &EntityIsAliveCallback;
		api.EntityGetName = &EntityGetNameCallback;
		api.EntitySetName = &EntitySetNameCallback;
		api.EntityGetTag = &EntityGetTagCallback;
		api.EntitySetTag = &EntitySetTagCallback;
		api.EntityGetLayer = &EntityGetLayerCallback;
		api.EntitySetLayer = &EntitySetLayerCallback;
		api.DestroyEntityDeferred = &DestroyEntityDeferredCallback;
		api.HasComponent = &HasComponentCallback;
		api.AddComponentDeferred = &AddComponentDeferredCallback;
		api.RemoveComponentDeferred = &RemoveComponentDeferredCallback;
		api.TransformGetPosition = &TransformGetPositionCallback;
		api.TransformSetPosition = &TransformSetPositionCallback;
		api.TransformGetRotationEuler = &TransformGetRotationCallback;
		api.TransformSetRotationEuler = &TransformSetRotationCallback;
		api.TransformGetScale = &TransformGetScaleCallback;
		api.TransformSetScale = &TransformSetScaleCallback;
		api.TransformGetWorldMatrix = &TransformGetWorldMatrixCallback;
		api.InputIsKeyHeld = &InputIsKeyHeldCallback;
		api.InputWasKeyPressed = &InputWasKeyPressedCallback;
		api.InputWasKeyReleased = &InputWasKeyReleasedCallback;
		api.InputGetMousePosition = &InputGetMousePositionCallback;
		api.InputGetMouseDelta = &InputGetMouseDeltaCallback;
		api.InputGetModifiers = &InputGetModifiersCallback;
		api.RigidbodyGetLinearVelocity = &RigidbodyGetVelocityCallback;
		api.RigidbodySetLinearVelocity = &RigidbodySetVelocityCallback;
		api.RigidbodyApplyForce = &RigidbodyApplyForceCallback;
		api.RigidbodyApplyLinearImpulse = &RigidbodyApplyImpulseCallback;
		api.PhysicsRaycast = &PhysicsRaycastCallback;
		api.PhysicsQueryAabb = &PhysicsQueryAabbCallback;
		api.AssetIsValid = &AssetIsValidCallback;
		api.AssetGetType = &AssetGetTypeCallback;
		api.BehaviourGetEnabled = &BehaviourGetEnabledCallback;
		api.BehaviourSetEnabledDeferred = &BehaviourSetEnabledCallback;
		api.BehaviourRemoveDeferred = &BehaviourRemoveCallback;
		api.SceneGetActiveHandle = &SceneGetActiveHandleCallback;
		api.SceneGetActiveBuildIndex = &SceneGetActiveBuildIndexCallback;
		api.SceneRequestLoadHandle = &SceneRequestLoadHandleCallback;
		api.SceneRequestLoadIndex = &SceneRequestLoadIndexCallback;
		api.SceneRequestReload = &SceneRequestReloadCallback;
		api.PrefabInstantiateDeferred = &PrefabInstantiateDeferredCallback;
		return api;
	}

	NativeApiV2 BuildNativeApiV2()
	{
		NativeApiV2 api;
		api.V1 = BuildNativeApiV1();
		api.V1.Size = sizeof(NativeApiV2);
		api.QueryCapability = &QueryCapabilityCallback;
		return api;
	}

}
