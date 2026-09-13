#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/SpriteAnimation.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include "stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class TemporaryAssetProject final
	{
	public:
		TemporaryAssetProject()
		{
			TomCat::AssetManager::Get().Shutdown();
			Root = std::filesystem::temp_directory_path() /
				("tomcat_sprite_assets_" +
					std::to_string(static_cast<uint64_t>(TomCat::UUID())));
			Assets = Root / "Assets";
			Library = Root / "Library";
			std::filesystem::create_directories(Assets);
		}

		~TemporaryAssetProject()
		{
			// The process-global manager may retain the package's Windows file handle.
			TomCat::AssetManager::Get().Shutdown();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		std::filesystem::path Root;
		std::filesystem::path Assets;
		std::filesystem::path Library;
	};

	std::string RequireSetting(const TomCat::AssetMetadata& metadata,
		const char* key)
	{
		const auto iterator = metadata.ImportSettings.find(key);
		Require(iterator != metadata.ImportSettings.end(),
			"primitive Sprite metadata omitted a required import setting");
		return iterator->second;
	}

	std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path)
	{
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		Require(static_cast<bool>(input), "primitive Sprite source could not be opened");
		const std::streamoff end = input.tellg();
		Require(end >= 0 && static_cast<uintmax_t>(end) <=
			static_cast<uintmax_t>((std::numeric_limits<size_t>::max)()),
			"primitive Sprite source size is invalid");
		std::vector<uint8_t> bytes(static_cast<size_t>(end));
		input.seekg(0, std::ios::beg);
		if (!bytes.empty())
			input.read(reinterpret_cast<char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(input) || bytes.empty(),
			"primitive Sprite source could not be read completely");
		return bytes;
	}

	TomCat::SpriteAnimator MakeAnimator(TomCat::AssetHandle first,
		TomCat::AssetHandle second, TomCat::AssetHandle third)
	{
		TomCat::SpriteAnimator animator;
		animator.InitialClip = "Run";
		animator.Speed = 1.0f;
		TomCat::SpriteAnimationClip run;
		run.Name = "Run";
		run.Loop = true;
		run.Frames = {
			{ first, 0.1f },
			{ second, 0.2f },
			{ third, 0.3f }
		};
		TomCat::SpriteAnimationClip once;
		once.Name = "Once";
		once.Loop = false;
		once.Frames = {
			{ second, 0.05f },
			{ third, 0.05f }
		};
		animator.Clips = { std::move(run), std::move(once) };
		return animator;
	}

	void AddStateMachine(TomCat::SpriteAnimator& animator)
	{
		animator.InitialState = "Idle";
		animator.Parameters = {
			{ "Grounded", TomCat::AnimatorParameterType::Bool, false, 0, 0.0f },
			{ "Lives", TomCat::AnimatorParameterType::Int, false, 3, 0.0f },
			{ "MoveSpeed", TomCat::AnimatorParameterType::Float, false, 0, 0.0f },
			{ "Jump", TomCat::AnimatorParameterType::Trigger, false, 0, 0.0f }
		};
		animator.States = {
			{ "Idle", "Run", 1.0f },
			{ "Moving", "Run", 1.0f },
			{ "Jumping", "Once", 1.0f }
		};
		animator.Transitions = {
			{ "Idle", "Moving", false, -1.0f,
				{ { "MoveSpeed", TomCat::AnimatorConditionMode::Greater, 0.5f } } },
			{ "", "Jumping", true, -1.0f,
				{ { "Jump", TomCat::AnimatorConditionMode::If, 0.0f } } },
			{ "Jumping", "Idle", false, 1.0f, {} }
		};
	}

	void TestSpriteAnimatorAuthoringReferences()
	{
		using namespace TomCat::SpriteAnimatorAuthoring;
		TomCat::SpriteAnimator animator = MakeAnimator(TomCat::AssetHandle(101),
			TomCat::AssetHandle(102), TomCat::AssetHandle(103));
		AddStateMachine(animator);
		std::string error;

		Require(RenameClip(animator, 0, "Locomotion", error)
			&& animator.InitialClip == "Locomotion"
			&& animator.States[0].Clip == "Locomotion"
			&& animator.States[1].Clip == "Locomotion",
			"Animator editor clip rename did not update references atomically");
		Require(!RenameClip(animator, 0, "Once", error) && !error.empty()
			&& animator.Clips[0].Name == "Locomotion",
			"Animator editor accepted a duplicate clip name");
		Require(!RenameClip(animator, 0, " Locomotion", error) && !error.empty(),
			"Animator editor accepted an ambiguous whitespace-padded name");

		Require(RenameParameter(animator, 2, "Velocity", error)
			&& animator.Transitions[0].Conditions[0].Parameter == "Velocity",
			"Animator editor parameter rename left a dangling condition");
		Require(SetParameterType(animator, 2, TomCat::AnimatorParameterType::Bool)
			&& animator.Transitions[0].Conditions[0].Mode
				== TomCat::AnimatorConditionMode::If
			&& animator.Transitions[0].Conditions[0].Threshold == 0.0f,
			"Animator editor parameter type change left an incompatible condition");

		Require(RenameState(animator, 0, "Standing", error)
			&& animator.InitialState == "Standing"
			&& animator.Transitions[0].FromState == "Standing"
			&& animator.Transitions[2].ToState == "Standing",
			"Animator editor state rename did not update transition references");
		Require(RemoveParameter(animator, 3, error)
			&& animator.Transitions.size() == 2,
			"Animator editor parameter removal retained an invalid transition");
		Require(RemoveClip(animator, 1, error)
			&& animator.Clips.size() == 1 && animator.States.size() == 2
			&& animator.Transitions.size() == 1,
			"Animator editor clip removal left dependent states or transitions");
		Require(RemoveParameter(animator, 2, error) && animator.Transitions.empty(),
			"Animator editor condition removal retained a conditionless transition");
		const std::string unique = MakeUniqueName(animator.States,
			std::string("Standing"), [](const TomCat::AnimatorState& state)
			{
				return state.Name;
			});
		Require(unique == "Standing 2",
			"Animator editor did not generate a stable readable unique name");
		Require(RemoveState(animator, 1, error) && animator.States.size() == 1,
			"Animator editor state removal did not preserve unrelated states");
		Require(RemoveState(animator, 0, error) && animator.States.empty()
			&& animator.InitialState.empty() && animator.Transitions.empty(),
			"Animator editor final state removal did not restore direct-clip mode");
		Require(!RemoveClip(animator, 0, error) && !error.empty()
			&& animator.Clips.size() == 1,
			"Animator editor allowed removal of the final required clip");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntity("Authoring result");
		entity.AddComponent<TomCat::SpriteRenderer>();
		entity.AddComponent<TomCat::SpriteAnimator>(animator);
		std::string document;
		Require(TomCat::SceneSerializer(scene).SerializeDocument(document, error)
			&& !document.empty(),
			"Animator editor produced data rejected by strict Scene serialization");
	}

	void TestStableSpriteOrdering()
	{
		struct Entry
		{
			uint64_t ID = 0;
			TomCat::Renderer2D::SpriteSortKey Key;
		};
		auto make = [](uint64_t id, int32_t layer, int32_t order)
		{
			TomCat::SpriteRenderer sprite;
			sprite.SortingLayer = layer;
			sprite.OrderInLayer = order;
			return Entry{ id, TomCat::Renderer2D::MakeSpriteSortKey(sprite, id) };
		};
		std::vector<Entry> entries = {
			make(20, 1, 0), make(999, 0, 10),
			make(10, 1, 0), make(100, 1, -1)
		};
		std::sort(entries.begin(), entries.end(),
			[](const Entry& left, const Entry& right)
			{
				return TomCat::Renderer2D::SpriteSortLess(left.Key, right.Key);
			});
		Require(entries[0].ID == 999 && entries[1].ID == 100
			&& entries[2].ID == 10 && entries[3].ID == 20,
			"Sprite transparent order is not layer/order/stable-UUID deterministic");
		Require(!TomCat::Renderer2D::SpriteSortLess(entries[2].Key, entries[2].Key),
			"Sprite ordering comparator is not strict");
	}

	void TestConservativeSpriteCulling()
	{
		const glm::mat4 camera = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f,
			-10.0f, 10.0f);
		Require(TomCat::Renderer2D::IsQuadVisible(glm::mat4(1.0f), camera),
			"centered Sprite was culled");
		const glm::mat4 outside = glm::translate(glm::mat4(1.0f),
			glm::vec3(20.0f, 0.0f, 0.0f));
		Require(!TomCat::Renderer2D::IsQuadVisible(outside, camera),
			"fully off-camera Sprite was retained");
		glm::mat4 intersecting = glm::translate(glm::mat4(1.0f),
			glm::vec3(5.2f, 0.0f, 0.0f));
		intersecting = glm::rotate(intersecting, glm::radians(45.0f),
			glm::vec3(0.0f, 0.0f, 1.0f));
		intersecting = glm::scale(intersecting, glm::vec3(2.0f));
		Require(TomCat::Renderer2D::IsQuadVisible(intersecting, camera),
			"rotated Sprite intersecting the camera edge was culled");
	}

	void TestSpriteAnimatorRuntime()
	{
		const TomCat::AssetHandle first(101);
		const TomCat::AssetHandle second(102);
		const TomCat::AssetHandle third(103);
		TomCat::SpriteAnimator singleUpdate = MakeAnimator(first, second, third);
		TomCat::SpriteAnimator partitioned = MakeAnimator(first, second, third);
		TomCat::SpriteRenderer singleRenderer;
		TomCat::SpriteRenderer partitionedRenderer;
		TomCat::SpriteAnimatorRuntime::Initialize(singleUpdate, singleRenderer);
		TomCat::SpriteAnimatorRuntime::Initialize(partitioned, partitionedRenderer);
		Require(singleRenderer.SpriteHandle == first
			&& partitionedRenderer.SpriteHandle == first,
			"PlayOnStart did not apply the first Sprite frame");

		TomCat::SpriteAnimatorRuntime::Update(singleUpdate, singleRenderer, 0.37);
		for (double delta : { 0.03, 0.04, 0.07, 0.11, 0.12 })
			TomCat::SpriteAnimatorRuntime::Update(partitioned, partitionedRenderer, delta);
		Require(singleUpdate.RuntimeFrameIndex == 2
			&& partitioned.RuntimeFrameIndex == singleUpdate.RuntimeFrameIndex
			&& singleRenderer.SpriteHandle == third
			&& partitionedRenderer.SpriteHandle == singleRenderer.SpriteHandle
			&& std::abs(partitioned.RuntimeFrameElapsed
				- singleUpdate.RuntimeFrameElapsed) < 1.0e-9,
			"variable frame partitions changed deterministic Animator state");

		const uint32_t stoppedFrame = partitioned.RuntimeFrameIndex;
		const double stoppedElapsed = partitioned.RuntimeFrameElapsed;
		TomCat::SpriteAnimatorRuntime::Stop(partitioned);
		TomCat::SpriteAnimatorRuntime::Update(partitioned, partitionedRenderer, 10.0);
		Require(!partitioned.RuntimePlaying
			&& partitioned.RuntimeFrameIndex == stoppedFrame
			&& partitioned.RuntimeFrameElapsed == stoppedElapsed,
			"Stop did not freeze Animator playback");

		partitioned.Speed = 2.0f;
		Require(TomCat::SpriteAnimatorRuntime::Play(
			partitioned, partitionedRenderer, "Once"),
			"Play could not select a clip by stable name");
		Require(partitionedRenderer.SpriteHandle == second,
			"Play did not apply the selected clip's first frame");
		TomCat::SpriteAnimatorRuntime::Update(partitioned, partitionedRenderer, 0.06);
		Require(!partitioned.RuntimePlaying && partitioned.RuntimeFrameIndex == 1
			&& partitionedRenderer.SpriteHandle == third,
			"non-looping clip did not stop on its final frame at Speed 2");
		const uint32_t completedFrame = partitioned.RuntimeFrameIndex;
		Require(!TomCat::SpriteAnimatorRuntime::Play(
			partitioned, partitionedRenderer, "Missing")
			&& partitioned.RuntimeFrameIndex == completedFrame,
			"Play mutated state for an unknown clip");

		struct ScenePlaybackState
		{
			uint32_t Frame = 0;
			double Elapsed = 0.0;
			TomCat::AssetHandle Handle{ 0 };
		};
		auto runSceneAtDisplayRate = [&](uint32_t displayHz)
		{
			TomCat::Scene scene;
			TomCat::Entity entity = scene.CreateEntity("Fixed-step Animator");
			auto& renderer = entity.AddComponent<TomCat::SpriteRenderer>();
			entity.AddComponent<TomCat::SpriteAnimator>(
				MakeAnimator(first, second, third));
			Require(scene.OnRuntimeStart(),
				"Scene could not start its SpriteAnimator runtime");
			for (uint32_t frame = 0; frame < displayHz; ++frame)
				scene.OnUpdateRuntime(TomCat::Timestep(
					1.0f / static_cast<float>(displayHz)));
			const auto& state = entity.GetComponent<TomCat::SpriteAnimator>();
			const ScenePlaybackState result{
				state.RuntimeFrameIndex, state.RuntimeFrameElapsed,
				renderer.SpriteHandle
			};
			scene.OnRuntimeStop();
			return result;
		};
		const ScenePlaybackState at30Hz = runSceneAtDisplayRate(30);
		const ScenePlaybackState at144Hz = runSceneAtDisplayRate(144);
		Require(at30Hz.Frame == at144Hz.Frame
			&& at30Hz.Handle == at144Hz.Handle
			&& at30Hz.Elapsed == at144Hz.Elapsed,
			"Scene display frame rate changed fixed-step SpriteAnimator output");
	}

	void TestAnimatorStateMachine()
	{
		const TomCat::AssetHandle first(501);
		const TomCat::AssetHandle second(502);
		const TomCat::AssetHandle third(503);
		auto make = [&]()
		{
			TomCat::SpriteAnimator animator = MakeAnimator(first, second, third);
			AddStateMachine(animator);
			return animator;
		};
		TomCat::SpriteAnimator animator = make();
		TomCat::SpriteRenderer renderer;
		TomCat::SpriteAnimatorRuntime::Initialize(animator, renderer);
		Require(TomCat::SpriteAnimatorRuntime::CurrentState(animator) == "Idle",
			"Animator did not enter InitialState");
		Require(TomCat::SpriteAnimatorRuntime::SetFloat(animator, "MoveSpeed", 1.0f),
			"Animator rejected a declared Float parameter");
		TomCat::SpriteAnimatorRuntime::Update(animator, renderer, 0.01);
		Require(TomCat::SpriteAnimatorRuntime::CurrentState(animator) == "Moving",
			"Animator numeric condition did not transition");
		Require(TomCat::SpriteAnimatorRuntime::SetInt(animator, "Lives", 2)
			&& TomCat::SpriteAnimatorRuntime::SetBool(animator, "Grounded", true)
			&& !TomCat::SpriteAnimatorRuntime::SetFloat(animator, "Missing", 1.0f),
			"Animator typed parameter mutation is not strict");

		TomCat::SpriteAnimator single = make();
		TomCat::SpriteAnimator partitioned = make();
		TomCat::SpriteRenderer singleRenderer;
		TomCat::SpriteRenderer partitionedRenderer;
		TomCat::SpriteAnimatorRuntime::Initialize(single, singleRenderer);
		TomCat::SpriteAnimatorRuntime::Initialize(partitioned, partitionedRenderer);
		Require(TomCat::SpriteAnimatorRuntime::SetTrigger(single, "Jump")
			&& TomCat::SpriteAnimatorRuntime::SetTrigger(partitioned, "Jump"),
			"Animator rejected a declared Trigger parameter");
		TomCat::SpriteAnimatorRuntime::Update(single, singleRenderer, 0.14);
		for (double delta : { 0.01, 0.02, 0.03, 0.08 })
			TomCat::SpriteAnimatorRuntime::Update(partitioned,
				partitionedRenderer, delta);
		Require(TomCat::SpriteAnimatorRuntime::CurrentState(single) == "Idle"
			&& TomCat::SpriteAnimatorRuntime::CurrentState(partitioned) == "Idle"
			&& std::abs(single.RuntimeStateElapsed
				- partitioned.RuntimeStateElapsed) < 1.0e-9
			&& single.RuntimeFrameIndex == partitioned.RuntimeFrameIndex
			&& singleRenderer.SpriteHandle == partitionedRenderer.SpriteHandle,
			"Animator exit-time transition changed with variable frame partitioning");
		Require(!single.Parameters[3].BoolValue
			&& !partitioned.Parameters[3].BoolValue,
			"Animator did not consume a Trigger used by AnyState");

		TomCat::SpriteAnimator stoppedBeforeTrigger = make();
		TomCat::SpriteRenderer stoppedBeforeTriggerRenderer;
		TomCat::SpriteAnimatorRuntime::Initialize(stoppedBeforeTrigger,
			stoppedBeforeTriggerRenderer);
		Require(TomCat::SpriteAnimatorRuntime::SetTrigger(stoppedBeforeTrigger,
			"Jump"), "Animator rejected the Stop regression Trigger");
		const double stoppedInitialElapsed = stoppedBeforeTrigger.RuntimeStateElapsed;
		const uint32_t stoppedInitialFrame = stoppedBeforeTrigger.RuntimeFrameIndex;
		TomCat::SpriteAnimatorRuntime::Stop(stoppedBeforeTrigger);
		TomCat::SpriteAnimatorRuntime::Update(stoppedBeforeTrigger,
			stoppedBeforeTriggerRenderer, 1.0);
		Require(!stoppedBeforeTrigger.RuntimePlaying
			&& TomCat::SpriteAnimatorRuntime::CurrentState(stoppedBeforeTrigger) == "Idle"
			&& stoppedBeforeTrigger.RuntimeStateElapsed == stoppedInitialElapsed
			&& stoppedBeforeTrigger.RuntimeFrameIndex == stoppedInitialFrame
			&& stoppedBeforeTrigger.Parameters[3].BoolValue,
			"Stop did not freeze a pending state-machine Trigger transition");

		TomCat::SpriteAnimator stoppedBeforeExitTime = make();
		TomCat::SpriteRenderer stoppedBeforeExitTimeRenderer;
		TomCat::SpriteAnimatorRuntime::Initialize(stoppedBeforeExitTime,
			stoppedBeforeExitTimeRenderer);
		Require(TomCat::SpriteAnimatorRuntime::SetTrigger(stoppedBeforeExitTime,
			"Jump"), "Animator rejected the exit-time Stop regression Trigger");
		TomCat::SpriteAnimatorRuntime::Update(stoppedBeforeExitTime,
			stoppedBeforeExitTimeRenderer, 0.01);
		Require(TomCat::SpriteAnimatorRuntime::CurrentState(stoppedBeforeExitTime)
			== "Jumping", "Animator did not enter the exit-time Stop fixture state");
		const double stoppedStateElapsed = stoppedBeforeExitTime.RuntimeStateElapsed;
		const double stoppedFrameElapsed = stoppedBeforeExitTime.RuntimeFrameElapsed;
		const uint32_t stoppedFrame = stoppedBeforeExitTime.RuntimeFrameIndex;
		const TomCat::AssetHandle stoppedSprite =
			stoppedBeforeExitTimeRenderer.SpriteHandle;
		TomCat::SpriteAnimatorRuntime::Stop(stoppedBeforeExitTime);
		TomCat::SpriteAnimatorRuntime::Update(stoppedBeforeExitTime,
			stoppedBeforeExitTimeRenderer, 10.0);
		Require(!stoppedBeforeExitTime.RuntimePlaying
			&& TomCat::SpriteAnimatorRuntime::CurrentState(stoppedBeforeExitTime)
				== "Jumping"
			&& stoppedBeforeExitTime.RuntimeStateElapsed == stoppedStateElapsed
			&& stoppedBeforeExitTime.RuntimeFrameElapsed == stoppedFrameElapsed
			&& stoppedBeforeExitTime.RuntimeFrameIndex == stoppedFrame
			&& stoppedBeforeExitTimeRenderer.SpriteHandle == stoppedSprite,
			"Stop did not freeze a state machine before its exit-time transition");
	}

	void TestSpriteAnimationSceneAndPrefabRoundtrip()
	{
		const TomCat::AssetHandle first(1101);
		const TomCat::AssetHandle second(1102);
		const TomCat::AssetHandle third(1103);
		auto source = TomCat::CreateRef<TomCat::Scene>();
		source->SetSceneName("Sprite animation roundtrip");
		TomCat::Entity entity = source->CreateEntity("Animated Sprite");
		auto& sprite = entity.AddComponent<TomCat::SpriteRenderer>();
		sprite.SpriteHandle = first;
		sprite.SortingLayer = -7;
		sprite.OrderInLayer = 42;
		auto& animator = entity.AddComponent<TomCat::SpriteAnimator>(
			MakeAnimator(first, second, third));
		AddStateMachine(animator);
		animator.RuntimeClipIndex = 1;
		animator.RuntimeFrameIndex = 1;
		animator.RuntimeFrameElapsed = 0.025;
		animator.RuntimePlaying = true;
		animator.RuntimeInitialized = true;

		auto copied = TomCat::Scene::Copy(source);
		Require(copied != nullptr, "Scene::Copy rejected SpriteAnimator data");
		TomCat::Entity copiedEntity = copied->FindEntityByUUID(entity.GetUUID());
		Require(copiedEntity && copiedEntity.HasComponent<TomCat::SpriteRenderer>()
			&& copiedEntity.HasComponent<TomCat::SpriteAnimator>(),
			"Scene::Copy omitted SpriteRenderer or SpriteAnimator");
		const auto& copiedSprite = copiedEntity.GetComponent<TomCat::SpriteRenderer>();
		const auto& copiedAnimator = copiedEntity.GetComponent<TomCat::SpriteAnimator>();
		Require(copiedSprite.SortingLayer == -7 && copiedSprite.OrderInLayer == 42
			&& copiedAnimator.Clips.size() == 2
			&& copiedAnimator.States.size() == 3
			&& copiedAnimator.Transitions.size() == 3
			&& copiedAnimator.Clips[0].Frames[2].SpriteHandle == third,
			"Scene::Copy changed Sprite sorting or animation authoring data");
		Require(!copiedAnimator.RuntimeInitialized && !copiedAnimator.RuntimePlaying
			&& copiedAnimator.RuntimeClipIndex == TomCat::SpriteAnimator::InvalidClipIndex,
			"Scene::Copy retained transient Animator playback state");

		std::string sceneDocument;
		std::string error;
		Require(TomCat::SceneSerializer(source).SerializeDocument(sceneDocument, error),
			"Scene v11 could not encode SpriteAnimator data");
		const std::vector<uint8_t> sceneBytes(sceneDocument.begin(), sceneDocument.end());
		Require(TomCat::SceneSerializer::ValidateCurrentFormat(
			sceneBytes, "SpriteAnimationRoundtrip.tomcat"),
			"strict Scene v11 validation rejected SpriteAnimator data");
		auto decoded = TomCat::CreateRef<TomCat::Scene>();
		Require(TomCat::SceneSerializer(decoded).DeserializeDocument(
			sceneBytes, "SpriteAnimationRoundtrip.tomcat", false),
			"Scene v11 could not decode SpriteAnimator data");
		TomCat::Entity decodedEntity = decoded->FindEntityByUUID(entity.GetUUID());
		Require(decodedEntity && decodedEntity.HasComponent<TomCat::SpriteAnimator>()
			&& decodedEntity.GetComponent<TomCat::SpriteRenderer>().SortingLayer == -7
			&& decodedEntity.GetComponent<TomCat::SpriteRenderer>().OrderInLayer == 42
			&& decodedEntity.GetComponent<TomCat::SpriteAnimator>()
				.Clips[0].Frames[1].SpriteHandle == second
			&& decodedEntity.GetComponent<TomCat::SpriteAnimator>()
				.Parameters[2].Name == "MoveSpeed",
			"Scene v11 roundtrip changed Sprite animation or sorting data");

		TomCat::PrefabArchive captured;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(
			source, entity, captured, error),
			"Prefab capture rejected SpriteAnimator data");
		std::string prefabDocument;
		Require(TomCat::PrefabArchiveCodec::Encode(
			captured, prefabDocument, error),
			"Prefab encode rejected SpriteAnimator data");
		TomCat::PrefabArchive decodedPrefab;
		const std::vector<uint8_t> prefabBytes(
			prefabDocument.begin(), prefabDocument.end());
		Require(TomCat::PrefabArchiveCodec::Decode(prefabBytes,
			"SpriteAnimationRoundtrip.tcprefab", decodedPrefab, error),
			"Prefab decode rejected SpriteAnimator data");
		TomCat::Entity prefabRoot = decodedPrefab.TemplateScene->FindEntityByUUID(
			TomCat::UUID(decodedPrefab.RootLocalID));
		Require(prefabRoot && prefabRoot.HasComponent<TomCat::SpriteAnimator>()
			&& prefabRoot.GetComponent<TomCat::SpriteAnimator>()
				.Clips[1].Frames[1].SpriteHandle == third
			&& prefabRoot.GetComponent<TomCat::SpriteAnimator>()
				.States[2].Name == "Jumping",
			"Prefab roundtrip omitted SpriteAnimator data");
	}

	void RequireDecodablePrimitive(const std::vector<uint8_t>& bytes,
		bool expectCircle)
	{
		Require(!bytes.empty() &&
			bytes.size() <= static_cast<size_t>((std::numeric_limits<int>::max)()),
			"primitive Sprite payload has an invalid decoder size");
		int width = 0;
		int height = 0;
		std::vector<uint8_t> artifactPixels;
		stbi_uc* sourcePixels = nullptr;
		const uint8_t* pixels = nullptr;
		if (TomCat::IsTextureArtifact(bytes))
		{
			TomCat::TextureArtifactView artifact;
			std::string error;
			Require(TomCat::ParseTextureArtifact(bytes, artifact, error)
				&& !artifact.Mips.empty(),
				"primitive Sprite texture artifact is invalid");
			Require(TomCat::DecompressTextureMip(artifact.Mips.front(),
				artifact.Format, artifactPixels, error),
				"primitive Sprite texture artifact cannot be decoded");
			width = static_cast<int>(artifact.Width);
			height = static_cast<int>(artifact.Height);
			pixels = artifactPixels.data();
		}
		else
		{
			int sourceChannels = 0;
			sourcePixels = stbi_load_from_memory(bytes.data(),
				static_cast<int>(bytes.size()), &width, &height, &sourceChannels, 4);
			Require(sourcePixels != nullptr,
				"primitive Sprite source payload is not decodable by stb_image");
			pixels = sourcePixels;
		}

		const bool dimensionsMatch = width == 64 && height == 64;
		const uint8_t cornerAlpha = pixels[3];
		const size_t center = (static_cast<size_t>(height / 2) * width + width / 2) * 4;
		const uint8_t centerAlpha = pixels[center + 3];
		if (sourcePixels)
			stbi_image_free(sourcePixels);

		Require(dimensionsMatch, "primitive Sprite dimensions changed");
		Require(centerAlpha == 255, "primitive Sprite center is not opaque");
		if (expectCircle)
			Require(cornerAlpha == 0, "Circle primitive corner is not transparent");
		else
			Require(cornerAlpha == 255, "Square primitive corner is not opaque");
	}

	std::vector<uint8_t> RequirePrimitive(TomCat::AssetRegistry& registry,
		TomCat::AssetHandle handle, const char* primitiveName)
	{
		Require(static_cast<uint64_t>(handle) != 0,
			"primitive Sprite has a zero AssetHandle");
		const TomCat::AssetMetadata* metadata = registry.GetMetadata(handle);
		Require(metadata && metadata->Type == TomCat::AssetType::Texture2D &&
			!metadata->IsMissing, "primitive Sprite is not a live Texture2D asset");
		Require(RequireSetting(*metadata, "Usage") == "Sprite",
			"primitive Sprite Usage setting changed");
		Require(RequireSetting(*metadata, "Primitive") == primitiveName,
			"primitive Sprite kind setting changed");
		Require(RequireSetting(*metadata, "PixelsPerUnit") == "100",
			"primitive Sprite PixelsPerUnit setting changed");

		const std::filesystem::path source = registry.GetFileSystemPath(handle);
		std::error_code error;
		Require(std::filesystem::is_regular_file(source, error) && !error,
			"primitive Sprite source file was not created");
		std::vector<uint8_t> bytes = ReadFileBytes(source);
		RequireDecodablePrimitive(bytes, std::string_view(primitiveName) == "Circle");
		return bytes;
	}

	void TestPrimitiveSpriteAuthoringAndCookedPackage()
	{
		TemporaryAssetProject environment;

		// Builder creates starter assets through a standalone registry. Exercise
		// Square first because that was the reported failing menu action.
		TomCat::AssetRegistry builderRegistry;
		Require(builderRegistry.Initialize(environment.Assets, environment.Library),
			"Builder-style AssetRegistry initialization failed");
		const TomCat::AssetHandle square = TomCat::EnsurePrimitiveSpriteAsset(
			builderRegistry, "Square");
		const TomCat::AssetHandle circle = TomCat::EnsurePrimitiveSpriteAsset(
			builderRegistry, "Circle");
		Require(square != circle, "Square and Circle reused one AssetHandle");
		const std::vector<uint8_t> squareBytes = RequirePrimitive(
			builderRegistry, square, "Square");
		const std::vector<uint8_t> circleBytes = RequirePrimitive(
			builderRegistry, circle, "Circle");
		builderRegistry.Shutdown();

		// Editor opens the same project through AssetManager. Existing primitives
		// must be discovered by metadata and remain stable across refreshes.
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"Editor-style AssetManager initialization failed");
		Require(TomCat::FindPrimitiveSpriteAsset(assets.Registry(), "Square") == square &&
			TomCat::FindPrimitiveSpriteAsset(assets.Registry(), "Circle") == circle,
			"Editor did not recover Builder-created primitive handles");
		Require(TomCat::EnsurePrimitiveSpriteAsset(assets, "Square") == square &&
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle") == circle,
			"repeated primitive creation changed an AssetHandle");
		Require(assets.Refresh(), "primitive Sprite registry refresh failed");
		Require(TomCat::EnsurePrimitiveSpriteAsset(assets, "Square") == square &&
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle") == circle,
			"primitive handles changed after a registry refresh");

		auto animatedScene = TomCat::CreateRef<TomCat::Scene>();
		animatedScene->SetSceneName("Cooked Sprite Animation");
		TomCat::Entity animated = animatedScene->CreateEntity("Animated");
		animated.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = square;
		auto cookedAnimator = MakeAnimator(square, circle, square);
		// Circle is referenced only by animation frames. Its presence in the
		// package below therefore proves that Cook follows the new closure seam.
		animated.AddComponent<TomCat::SpriteAnimator>(std::move(cookedAnimator));
		const std::filesystem::path scenePath = environment.Assets / "Animated.tomcat";
		Require(TomCat::SceneSerializer(animatedScene).Serialize(scenePath),
			"animated Sprite Scene could not be saved");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"animated Sprite Scene was not imported");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		const std::filesystem::path prefabPath =
			environment.Assets / "Animated.tcprefab";
		TomCat::AssetHandle prefabHandle{ 0 };
		Require(TomCat::PrefabArchiveCodec::SaveSubtree(
			animatedScene, animated, prefabPath, &prefabHandle)
			&& static_cast<uint64_t>(prefabHandle) != 0,
			"animated Sprite Prefab could not be saved/imported");
		const std::vector<TomCat::AssetReference> circleReferences =
			assets.FindReferences(circle);
		const auto hasAnimationReferenceFrom = [&](TomCat::AssetHandle owner)
		{
			return std::any_of(circleReferences.begin(), circleReferences.end(),
				[&](const TomCat::AssetReference& reference)
				{
					return reference.ReferencingAsset == owner
						&& reference.PropertyPath.find(".SpriteAnimator.Clips[")
							!= std::string::npos;
				});
		};
		Require(hasAnimationReferenceFrom(sceneHandle)
			&& hasAnimationReferenceFrom(prefabHandle),
			"Scene/Prefab reference graph omitted SpriteAnimator frame handles");

		const std::filesystem::path packagePath = environment.Root / "Build" / "Sprites.tcpak";
		Require(assets.CookToPackage(packagePath, sceneHandle),
			"primitive Sprites could not be cooked into a Player package");
		assets.Shutdown();

		Require(assets.MountCookedPackage(packagePath),
			"Cooked Player could not mount the primitive Sprite package");
		auto requireCooked = [&](TomCat::AssetHandle handle,
			const std::vector<uint8_t>& expected, bool expectCircle)
		{
			std::vector<uint8_t> cooked;
			TomCat::AssetType type = TomCat::AssetType::None;
			Require(assets.ReadAssetBytes(handle, cooked, &type) &&
				type == TomCat::AssetType::Texture2D,
				"Cooked Player could not resolve a primitive Sprite handle");
			Require(cooked != expected && TomCat::IsTextureArtifact(cooked),
				"cooking did not replace the primitive source with a texture artifact");
			RequireDecodablePrimitive(cooked, expectCircle);
		};
		requireCooked(square, squareBytes, false);
		requireCooked(circle, circleBytes, true);

		// Player startup must enqueue package textures without synchronously
		// decoding or touching OpenGL. This regression intentionally never creates
		// a renderer; it waits only for the worker-side read/validation stage, then
		// verifies package unmount cancels and drains all retained state.
		Require(assets.BeginCookedTexturePreload() >= 2,
			"cooked texture preload did not discover package textures");
		Require(assets.BeginCookedTexturePreload() == 0,
			"repeated cooked texture preload duplicated pending reads");
		TomCat::TextureStreamingStats streaming;
		for (uint32_t attempt = 0; attempt < 500; ++attempt)
		{
			(void)assets.PumpTexturePublishes(0, 0);
			streaming = assets.GetTextureStreamingStats();
			if (streaming.PreparedCount >= 2 && streaming.JobsInFlight == 0)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		Require(streaming.PreparedCount >= 2
			&& streaming.PreparedBytes != 0 && streaming.BacklogCount == 0,
			"cooked texture preload did not prepare artifacts asynchronously");
		assets.UnmountCookedPackage();
		streaming = assets.GetTextureStreamingStats();
		Require(streaming.BacklogCount == 0 && streaming.PendingCount == 0
			&& streaming.PreparedCount == 0 && streaming.PreparedBytes == 0
			&& streaming.JobsInFlight == 0,
			"package unmount retained texture preload jobs or artifact memory");
	}

	void TestAtlasImportAndCookedSubSprite()
	{
		TemporaryAssetProject environment;
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"Atlas AssetManager initialization failed");
		const TomCat::AssetHandle atlas = TomCat::EnsurePrimitiveSpriteAsset(
			assets, "Square");
		const TomCat::AssetMetadata* original = assets.Registry().GetMetadata(atlas);
		Require(original != nullptr, "Atlas source metadata is missing");
		TomCat::AssetImportSettings settings = original->ImportSettings;
		settings["SpriteMode"] = "Multiple";
		settings["SpriteAtlasSchema"] = "2";
		settings["Sprite.idle.Name"] = "Idle";
		settings["Sprite.idle.Rect"] = "0,0,32,16";
		settings["Sprite.idle.Pivot"] = "0.25,0.75";
		settings["Sprite.idle.PixelsPerUnit"] = "16";
		settings["Sprite.idle.Border"] = "1,2,3,4";
		settings["Sprite.run.Name"] = "Run";
		settings["Sprite.run.Rect"] = "32,0,32,16";
		settings["Sprite.run.Pivot"] = "0.5,0.5";
		settings["Sprite.run.PixelsPerUnit"] = "32";
		settings["Sprite.run.Border"] = "0,0,0,0";
		Require(assets.SetImportSettings(atlas, settings),
			"Atlas import settings could not be saved");
		TomCat::AssetLoadOptions options;
		options.Platform = "windows-x64";
		options.Backend = "opengl";
		TomCat::AssetLoadResult first = assets.LoadImportedArtifact(atlas, options);
		Require(first.Succeeded() && first.Artifact.SubAssets.size() == 2,
			"Sprite Atlas importer did not produce two sub-assets");
		const TomCat::AssetHandle idle = first.Artifact.SubAssets[0].Handle;
		const TomCat::AssetHandle run = first.Artifact.SubAssets[1].Handle;
		Require(static_cast<uint64_t>(idle) != 0 && static_cast<uint64_t>(run) != 0
			&& idle != run, "Sprite Atlas sub-asset handles are invalid");
		const TomCat::AssetSubAsset* idleSprite = assets.Registry().GetSubAsset(idle);
		Require(idleSprite && idleSprite->PersistentID == "sprite:idle"
			&& idleSprite->Sprite.Width == 32 && idleSprite->Sprite.Height == 16
			&& idleSprite->Sprite.PivotX == 0.25f
			&& idleSprite->Sprite.BorderTop == 4.0f,
			"tcmeta v2 did not preserve complete Sprite slice metadata");
		const TomCat::SpriteSubAssetData expectedIdleData = idleSprite->Sprite;

		TomCat::SpriteRenderGeometry geometry;
		Require(TomCat::BuildSpriteRenderGeometry(expectedIdleData, 64, 64,
			geometry) && geometry.Width == 2.0f && geometry.Height == 1.0f
			&& geometry.OffsetX == 0.5f && geometry.OffsetY == -0.25f
			&& geometry.UMin == 0.0f && geometry.UMax == 0.5f
			&& geometry.VMin == 0.75f && geometry.VMax == 1.0f
			&& geometry.BorderRight == 3.0f / 16.0f,
			"Sprite Rect/Pivot/PPU/Border render geometry is incorrect");

		const std::filesystem::path source = assets.Registry().GetFileSystemPath(atlas);
		std::error_code timeError;
		const auto oldTime = std::filesystem::last_write_time(source, timeError);
		Require(!timeError, "Atlas source mtime could not be read");
		std::filesystem::last_write_time(source, oldTime + std::chrono::seconds(2),
			timeError);
		Require(!timeError, "Atlas source mtime could not be changed");
		TomCat::AssetLoadResult second = assets.LoadImportedArtifact(atlas, options);
		Require(second.Succeeded() && second.Artifact.FromCache
			&& second.Artifact.ArtifactKey
			== first.Artifact.ArtifactKey && second.Artifact.SubAssets[0].Handle == idle
			&& second.Artifact.SubAssets[1].Handle == run,
			"mtime-only change rebuilt or renumbered deterministic Sprite sub-assets");
		assets.Shutdown();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"Atlas project could not reopen after tcmeta v2 import");
		const TomCat::AssetSubAsset* reopenedIdle = assets.Registry().GetSubAsset(idle);
		const TomCat::AssetSubAsset* reopenedRun = assets.Registry().GetSubAsset(run);
		Require(reopenedIdle && reopenedRun && reopenedIdle->Sprite == expectedIdleData
			&& reopenedIdle->PersistentID == "sprite:idle"
			&& reopenedRun->PersistentID == "sprite:run",
			"tcmeta v2 reopen changed Sprite handles or slice metadata");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Atlas Cook");
		TomCat::Entity entity = scene->CreateEntity("Sliced Sprite");
		entity.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = idle;
		const std::filesystem::path scenePath = environment.Assets / "Atlas.tomcat";
		Require(TomCat::SceneSerializer(scene).Serialize(scenePath),
			"Atlas Scene could not be serialized");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata != nullptr, "Atlas Scene was not imported");
		const std::filesystem::path package = environment.Root / "Build" / "Atlas.tcpak";
		Require(assets.CookToPackage(package, sceneMetadata->Handle),
			"Cook did not accept the Sprite sub-asset dependency");
		assets.Shutdown();
		Require(assets.MountCookedPackage(package),
			"Player could not mount the Atlas package");
		std::vector<uint8_t> cooked;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(idle, cooked, &cookedType)
			&& cookedType == TomCat::AssetType::Texture2D,
			"Player package omitted the derived Sprite sub-asset");
		TomCat::ResolvedSpriteAsset resolved;
		std::span<const uint8_t> atlasBytes;
		Require(TomCat::ParseCookedSpriteSubAsset(cooked, resolved, atlasBytes)
			&& resolved.IsSubAsset && resolved.TextureHandle == atlas
			&& resolved.Data == expectedIdleData && !atlasBytes.empty(),
			"Cooked Sprite envelope lost its atlas slice metadata or payload");
	}

}

int main()
{
	TomCat::Log::Init();
	try
	{
		TestStableSpriteOrdering();
		TestConservativeSpriteCulling();
		TestSpriteAnimatorRuntime();
		TestAnimatorStateMachine();
		TestSpriteAnimatorAuthoringReferences();
		TestSpriteAnimationSceneAndPrefabRoundtrip();
		TestPrimitiveSpriteAuthoringAndCookedPackage();
		TestAtlasImportAndCookedSubSprite();
		std::cout << "PASS Sprite Atlas, Animator state machine, Scene/Prefab, and Cook closure\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL Sprite sorting/animation regression: "
			<< exception.what() << '\n';
		return 1;
	}
}
