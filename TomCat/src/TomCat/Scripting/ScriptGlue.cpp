#include "tcpch.h"
#include "ScriptGlue.h"

#include "ScriptEngine.h"
#include "ScriptDiagnosticSink.h"
#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Math/Math.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneManager.h"
#include "TomCat/Utils/PathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
			return result.find('\0') == std::string::npos;
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
			return Guard([&]() { return Resolve(handle) ? 1 : 0; });
		}

		int32_t EntityGetNameCallback(EntityHandleV1 handle, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				return entity ? WriteUtf8(entity.GetName(), buffer, capacity, required)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t EntitySetNameCallback(EntityHandleV1 handle, NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); Scene* scene = ResolveScene(handle);
				std::string name;
				if (!entity || !scene) return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(value, name) || name.empty()) return Code(ScriptStatus::InvalidArgument);
				return scene->RenameEntity(entity, name) ? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidState);
			});
		}

		int32_t EntityGetTagCallback(EntityHandleV1 handle, uint8_t* buffer,
			uint32_t capacity, uint32_t* required) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				return entity ? WriteUtf8(entity.GetGameplayTag(), buffer, capacity, required)
					: Code(ScriptStatus::NotFound);
			});
		}

		int32_t EntitySetTagCallback(EntityHandleV1 handle, NativeUtf8View value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); std::string tag;
				if (!entity) return Code(ScriptStatus::NotFound);
				if (!ReadUtf8(value, tag) || tag.empty()) return Code(ScriptStatus::InvalidArgument);
				return entity.SetGameplayTag(tag) ? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t EntityGetLayerCallback(EntityHandleV1 handle, uint32_t* layer) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !layer) return Code(ScriptStatus::InvalidArgument);
				*layer = entity.GetLayer(); return Code(ScriptStatus::Success);
			});
		}

		int32_t EntitySetLayerCallback(EntityHandleV1 handle, uint32_t layer) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity) return Code(ScriptStatus::NotFound);
				return layer <= std::numeric_limits<uint8_t>::max()
					&& entity.SetLayer(static_cast<uint8_t>(layer))
					? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t DestroyEntityDeferredCallback(EntityHandleV1 handle) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueDestroyEntity(handle)
					? Code(ScriptStatus::Success) : Code(ScriptStatus::NotFound);
			});
		}

		template<typename T>
		int32_t HasComponent(Entity entity)
		{
			return entity.HasComponent<T>() ? 1 : 0;
		}

		int32_t HasComponentCallback(EntityHandleV1 handle, int32_t componentType) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity) return Code(ScriptStatus::NotFound);
				switch (static_cast<NativeComponentType>(componentType))
				{
					case NativeComponentType::Transform: return HasComponent<Transform>(entity);
					case NativeComponentType::Rigidbody2D: return HasComponent<Rigidbody2D>(entity);
					case NativeComponentType::BoxCollider2D: return HasComponent<BoxCollider2D>(entity);
					case NativeComponentType::CircleCollider2D: return HasComponent<CircleCollider2D>(entity);
					case NativeComponentType::DistanceJoint2D: return HasComponent<DistanceJoint2D>(entity);
					case NativeComponentType::SpriteRenderer: return HasComponent<SpriteRenderer>(entity);
				}
				return Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t AddComponentDeferredCallback(EntityHandleV1 handle, int32_t type) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueAddComponent(handle,
					static_cast<NativeComponentType>(type)) ? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t RemoveComponentDeferredCallback(EntityHandleV1 handle, int32_t type) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				return ScriptEngine::Get().QueueRemoveComponent(handle,
					static_cast<NativeComponentType>(type)) ? Code(ScriptStatus::Success)
					: Code(ScriptStatus::InvalidArgument);
			});
		}

		template<typename Getter>
		int32_t GetTransformVector(EntityHandleV1 handle, NativeVector3* output,
			Getter getter) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !output || !entity.HasComponent<Transform>())
					return Code(ScriptStatus::NotFound);
				*output = ToNative(getter(entity.GetComponent<Transform>()));
				return Code(ScriptStatus::Success);
			});
		}

		template<typename Setter>
		int32_t SetTransformVector(EntityHandleV1 handle, NativeVector3 input,
			Setter setter) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle); Scene* scene = ResolveScene(handle);
				if (!entity || !scene || !entity.HasComponent<Transform>())
					return Code(ScriptStatus::NotFound);
				auto transform = entity.GetComponent<Transform>();
				setter(transform, ToGlm(input));
				return scene->SetWorldTransform(entity, transform.GetTransform())
					? Code(ScriptStatus::Success) : Code(ScriptStatus::InvalidArgument);
			});
		}

		int32_t TransformGetPositionCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, [](const Transform& t) { return t._Translation; }); }
		int32_t TransformSetPositionCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, [](Transform& t, glm::vec3 x) { t._Translation = x; }); }
		int32_t TransformGetRotationCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, [](const Transform& t) { return t._Rotation; }); }
		int32_t TransformSetRotationCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, [](Transform& t, glm::vec3 x) { t._Rotation = x; }); }
		int32_t TransformGetScaleCallback(EntityHandleV1 h, NativeVector3* v) noexcept
		{ return GetTransformVector(h, v, [](const Transform& t) { return t._Scale; }); }
		int32_t TransformSetScaleCallback(EntityHandleV1 h, NativeVector3 v) noexcept
		{ return SetTransformVector(h, v, [](Transform& t, glm::vec3 x) { t._Scale = x; }); }

		int32_t TransformGetWorldMatrixCallback(EntityHandleV1 handle, NativeMatrix4* value) noexcept
		{
			return Guard([&]()
			{
				if (!RequireMainThread()) return Code(ScriptStatus::WrongThread);
				Entity entity = Resolve(handle);
				if (!entity || !value || !entity.HasComponent<Transform>())
					return Code(ScriptStatus::NotFound);
				std::memcpy(value->Values, glm::value_ptr(entity.GetComponent<Transform>().GetTransform()),
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
					return Code(ScriptStatus::InvalidArgument);
				return ScriptEngine::Get().QueueInstantiatePrefab(context,
					prefabHandle, worldPosition, parent) ? 1 : 0;
			});
		}

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

}
