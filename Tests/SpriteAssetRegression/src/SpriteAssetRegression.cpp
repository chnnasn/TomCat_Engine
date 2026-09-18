#include "TomCat/Asset/AssetManager.h"
#include "TomCat/Asset/Advanced2DAuthoringAssets.h"
#include "TomCat/Asset/SpriteAsset.h"
#include "TomCat/Asset/TextureArtifact.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Core/UUID.h"
#include "TomCat/Renderer/Renderer2D.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/SpriteAnimation.h"
#include "TomCat/Scene/SpriteAnimatorAuthoring.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"

#include "stb_image.h"
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#ifdef TC_PLATFORM_WINDOWS
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <Windows.h>
#endif

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	class ScopedCurrentPath final
	{
	public:
		explicit ScopedCurrentPath(const std::filesystem::path& path)
			: m_Previous(std::filesystem::current_path())
		{
			std::filesystem::current_path(path);
		}

		~ScopedCurrentPath()
		{
			std::error_code error;
			std::filesystem::current_path(m_Previous, error);
		}

		ScopedCurrentPath(const ScopedCurrentPath&) = delete;
		ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

	private:
		std::filesystem::path m_Previous;
	};

	std::filesystem::path GetExecutableDirectory()
	{
#ifdef TC_PLATFORM_WINDOWS
		std::array<wchar_t, 32768> buffer{};
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
			static_cast<DWORD>(buffer.size()));
		Require(length != 0 && length < buffer.size(),
			"Sprite regression executable path is unavailable");
		return std::filesystem::path(
			std::wstring(buffer.data(), length)).parent_path();
#else
		return std::filesystem::current_path();
#endif
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

	class BlockingTextureImportGate final
	{
	public:
		void EnterAndWait()
		{
			std::unique_lock lock(m_Mutex);
			m_Entered = true;
			m_Changed.notify_all();
			m_Changed.wait(lock, [this]() { return m_Released; });
		}

		bool WaitUntilEntered()
		{
			std::unique_lock lock(m_Mutex);
			return m_Changed.wait_for(lock, std::chrono::seconds(10),
				[this]() { return m_Entered; });
		}

		void Release()
		{
			{
				std::lock_guard lock(m_Mutex);
				m_Released = true;
			}
			m_Changed.notify_all();
		}

	private:
		std::mutex m_Mutex;
		std::condition_variable m_Changed;
		bool m_Entered = false;
		bool m_Released = false;
	};

	class BlockingTextureImporter final : public TomCat::IAssetImporter
	{
	public:
		BlockingTextureImporter(
			std::shared_ptr<const TomCat::IAssetImporter> delegate,
			std::shared_ptr<BlockingTextureImportGate> gate,
			TomCat::AssetHandle blockedHandle)
			: m_Delegate(std::move(delegate)), m_Gate(std::move(gate)),
			  m_BlockedHandle(blockedHandle)
		{
		}

		std::string_view GetID() const noexcept override
		{
			return "tomcat.regression.blocking-texture";
		}

		uint32_t GetVersion() const noexcept override
		{
			return 1;
		}

		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Texture2D;
		}

		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			if (request.Handle == m_BlockedHandle)
				m_Gate->EnterAndWait();
			return m_Delegate->Import(request);
		}

	private:
		std::shared_ptr<const TomCat::IAssetImporter> m_Delegate;
		std::shared_ptr<BlockingTextureImportGate> m_Gate;
		TomCat::AssetHandle m_BlockedHandle = TomCat::AssetHandle(0);
	};

	class CountingTextureImporter final : public TomCat::IAssetImporter
	{
	public:
		CountingTextureImporter(
			std::shared_ptr<const TomCat::IAssetImporter> delegate,
			size_t& importCount)
			: m_Delegate(std::move(delegate)), m_ImportCount(&importCount)
		{
		}

		std::string_view GetID() const noexcept override
		{
			return "tomcat.regression.cold-cook-texture";
		}

		uint32_t GetVersion() const noexcept override
		{
			return 1;
		}

		TomCat::AssetType GetAssetType() const noexcept override
		{
			return TomCat::AssetType::Texture2D;
		}

		TomCat::AssetImportResult Import(
			const TomCat::AssetImportRequest& request) const override
		{
			++*m_ImportCount;
			return m_Delegate->Import(request);
		}

	private:
		std::shared_ptr<const TomCat::IAssetImporter> m_Delegate;
		size_t* m_ImportCount = nullptr;
	};

	struct AtlasCookFixture
	{
		TomCat::AssetHandle Atlas = TomCat::AssetHandle(0);
		TomCat::AssetHandle Idle = TomCat::AssetHandle(0);
		TomCat::AssetHandle Scene = TomCat::AssetHandle(0);
		std::filesystem::path Source;
		TomCat::AssetMetadata Owner;
	};

	AtlasCookFixture PrepareAtlasCookFixture(TomCat::AssetManager& assets,
		TemporaryAssetProject& environment, std::string_view sceneName)
	{
		Require(assets.Initialize(environment.Assets, environment.Library),
			"concurrent Atlas AssetManager initialization failed");
		AtlasCookFixture fixture;
		fixture.Atlas = TomCat::EnsurePrimitiveSpriteAsset(assets, "Square");
		const TomCat::AssetMetadata* original =
			assets.Registry().GetMetadata(fixture.Atlas);
		Require(original != nullptr, "concurrent Atlas source metadata is missing");
		TomCat::AssetImportSettings settings = original->ImportSettings;
		settings["SpriteMode"] = "Multiple";
		settings["SpriteAtlasSchema"] = "2";
		settings["Sprite.idle.Name"] = "Idle";
		settings["Sprite.idle.Rect"] = "0,0,32,16";
		settings["Sprite.idle.Pivot"] = "0.5,0.5";
		settings["Sprite.idle.PixelsPerUnit"] = "32";
		settings["Sprite.idle.Border"] = "0,0,0,0";
		Require(assets.SetImportSettings(fixture.Atlas, settings),
			"concurrent Atlas import settings could not be saved");
		TomCat::AssetLoadOptions options;
		options.Platform = "windows-x64";
		options.Backend = "opengl";
		TomCat::AssetLoadResult imported =
			assets.LoadImportedArtifact(fixture.Atlas, options);
		Require(imported.Succeeded() && imported.Artifact.SubAssets.size() == 1,
			"concurrent Atlas importer did not produce its Sprite slice");
		fixture.Idle = imported.Artifact.SubAssets.front().Handle;
		TomCat::AssetSubAsset slice;
		Require(assets.GetDatabase().GetSubAssetSnapshot(fixture.Idle,
			fixture.Owner, slice), "concurrent Atlas Sprite snapshot is missing");
		fixture.Source = assets.Registry().GetFileSystemPath(fixture.Atlas);

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName(std::string(sceneName));
		TomCat::Entity entity = scene->CreateEntity("Sliced Sprite");
		entity.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = fixture.Idle;
		const std::filesystem::path scenePath =
			environment.Assets / (std::string(sceneName) + ".tomcat");
		Require(TomCat::SceneSerializer(scene).Serialize(scenePath),
			"concurrent Atlas Scene could not be serialized");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata != nullptr,
			"concurrent Atlas Scene metadata is missing");
		fixture.Scene = sceneMetadata->Handle;
		return fixture;
	}

	std::shared_ptr<BlockingTextureImportGate> InstallBlockingTextureImporter(
		TomCat::AssetManager& assets, TomCat::AssetHandle blockedHandle)
	{
		const std::shared_ptr<const TomCat::IAssetImporter> delegate =
			assets.GetDatabase().GetImporters().Find(TomCat::AssetType::Texture2D);
		Require(delegate != nullptr, "built-in Texture importer is missing");
		auto gate = std::make_shared<BlockingTextureImportGate>();
		Require(assets.GetDatabase().GetImporters().Register(
			std::make_shared<BlockingTextureImporter>(delegate, gate,
				blockedHandle), true),
			"blocking Texture importer could not replace the built-in importer");
		return gate;
	}

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

		AnimatorGraphLayout graphLayout;
		SynchronizeGraphLayout(animator, graphLayout);
		Require(graphLayout.StatePositions.size() == animator.States.size()
			&& graphLayout.StatePositions.at("Idle").X
				== DefaultGraphPosition(0).X
			&& graphLayout.StatePositions.at("Moving").X
				== DefaultGraphPosition(1).X,
			"Animator graph did not create a deterministic state layout");
		graphLayout.StatePositions.at("Idle") = { 431.0f, 217.0f };
		SynchronizeGraphLayout(animator, graphLayout);
		Require(graphLayout.StatePositions.at("Idle").X == 431.0f
			&& graphLayout.StatePositions.at("Idle").Y == 217.0f,
			"Animator graph synchronization discarded a dragged node position");
		RenameGraphState(graphLayout, "Idle", "Standing");
		Require(graphLayout.StatePositions.find("Idle")
				== graphLayout.StatePositions.end()
			&& graphLayout.StatePositions.at("Standing").X == 431.0f,
			"Animator graph rename did not preserve the state position");

		TomCat::SpriteAnimator graphAnimator = animator;
		const size_t transitionCount = graphAnimator.Transitions.size();
		size_t addedTransition = static_cast<size_t>(-1);
		Require(AddTransition(graphAnimator, std::nullopt, 1, &addedTransition,
			error)
			&& addedTransition == transitionCount
			&& graphAnimator.Transitions.back().AnyState
			&& graphAnimator.Transitions.back().ToState == "Moving"
			&& graphAnimator.Transitions.back().ExitTime == 1.0f,
			"Animator graph did not create a valid Any State transition");
		Require(SetTransitionEndpoints(graphAnimator, addedTransition, 0, 2, error)
			&& !graphAnimator.Transitions.back().AnyState
			&& graphAnimator.Transitions.back().FromState == "Idle"
			&& graphAnimator.Transitions.back().ToState == "Jumping",
			"Animator graph endpoint editing left invalid state references");
		const TomCat::AnimatorTransition validTransition =
			graphAnimator.Transitions.back();
		Require(!SetTransitionEndpoints(graphAnimator, addedTransition, 99, 0, error)
			&& !error.empty()
			&& graphAnimator.Transitions.back().FromState == validTransition.FromState
			&& graphAnimator.Transitions.back().ToState == validTransition.ToState,
			"Animator graph partially changed an invalid transition edit");
		Require(RemoveTransition(graphAnimator, addedTransition, error)
			&& graphAnimator.Transitions.size() == transitionCount,
			"Animator graph could not safely remove a selected transition");
		Require(!RemoveTransition(graphAnimator, addedTransition, error)
			&& !error.empty(),
			"Animator graph accepted a stale transition selection");

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

		TomCat::SpriteAnimationClip& timeline = animator.Clips[0];
		const size_t originalFrameCount = timeline.Frames.size();
		Require(InsertFrame(timeline, 1,
			{ TomCat::AssetHandle(104), 0.25f }, error)
			&& timeline.Frames.size() == originalFrameCount + 1
			&& timeline.Frames[1].SpriteHandle == TomCat::AssetHandle(104),
			"Animation timeline could not insert a Sprite frame");
		Require(MoveFrame(timeline, 1, 0, error)
			&& timeline.Frames.front().SpriteHandle == TomCat::AssetHandle(104),
			"Animation timeline could not reorder a Sprite frame");
		Require(SetFrameDuration(timeline, 0, 0.5f, error)
			&& std::abs(ClipDuration(timeline) - 1.1) < 0.0001
			&& FrameAtTime(timeline, 0.49) == 0
			&& FrameAtTime(timeline, 0.51) == 1,
			"Animation timeline sampling or frame duration editing is inconsistent");
		Require(!SetFrameDuration(timeline, 0, 0.0f, error) && !error.empty(),
			"Animation timeline accepted a zero-duration frame");
		Require(RemoveFrame(timeline, 0, error)
			&& timeline.Frames.size() == originalFrameCount,
			"Animation timeline could not remove a Sprite frame");
		TomCat::SpriteAnimationClip singleFrameClip;
		singleFrameClip.Frames.push_back({ TomCat::AssetHandle(104), 0.1f });
		Require(!RemoveFrame(singleFrameClip, 0, error) && !error.empty()
			&& singleFrameClip.Frames.size() == 1,
			"Animation timeline allowed removal of the final serializable frame");

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
		TomCat::SpriteRenderer sameOrder;
		const auto laterCell = TomCat::Renderer2D::MakeSpriteSortKey(
			sameOrder, 20, 100);
		const auto earlierCell = TomCat::Renderer2D::MakeSpriteSortKey(
			sameOrder, 20, 50);
		const auto laterRenderer = TomCat::Renderer2D::MakeSpriteSortKey(
			sameOrder, 30, 0);
		Require(TomCat::Renderer2D::SpriteSortLess(earlierCell, laterCell)
			&& TomCat::Renderer2D::SpriteSortLess(laterCell, laterRenderer),
			"per-cell Tilemap order escaped its renderer UUID group");
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
		animator.ControllerHandle = TomCat::AssetHandle(2201);
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
			&& copiedAnimator.ControllerHandle == TomCat::AssetHandle(2201)
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
			&& decodedEntity.GetComponent<TomCat::SpriteAnimator>().ControllerHandle
				== TomCat::AssetHandle(2201)
			&& decodedEntity.GetComponent<TomCat::SpriteAnimator>()
				.Clips[0].Frames[1].SpriteHandle == second
			&& decodedEntity.GetComponent<TomCat::SpriteAnimator>()
				.Parameters[2].Name == "MoveSpeed",
			"Scene v11 roundtrip changed Sprite animation or sorting data");
		bool visitedController = false;
		std::string visitorError;
		const bool visitedSceneReferences = TomCat::AssetReferenceVisitor::VisitScene(YAML::Load(sceneDocument),
			[&](const TomCat::SerializedAssetReference& reference)
			{
				if (reference.Kind
					== TomCat::SerializedAssetReferenceKind::AnimatorController)
					visitedController = reference.Handle == TomCat::AssetHandle(2201)
						&& reference.ExpectedType == TomCat::AssetType::AnimatorController;
				return true;
			}, visitorError);
		Require(visitedSceneReferences && visitedController,
			"Scene dependency traversal omitted Animator Controller");
		YAML::Node legacyScene = YAML::Load(sceneDocument);
		legacyScene["Entities"][0]["SpriteAnimator"].remove("ControllerHandle");
		bool legacyVisitedController = false;
		Require(TomCat::AssetReferenceVisitor::VisitScene(legacyScene,
			[&](const TomCat::SerializedAssetReference& reference)
			{
				legacyVisitedController = legacyVisitedController
					|| reference.Kind
						== TomCat::SerializedAssetReferenceKind::AnimatorController;
				return true;
			}, visitorError) && !legacyVisitedController,
			"legacy SpriteAnimator without ControllerHandle broke dependency traversal");

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
			&& prefabRoot.GetComponent<TomCat::SpriteAnimator>().ControllerHandle
				== TomCat::AssetHandle(2201)
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

	void TestBuiltInSpriteManifestPathsAndCook()
	{
		constexpr uint64_t expectedCircleHandle = 0x54434D5350520001ULL;
		constexpr uint64_t expectedSquareHandle = 0x54434D5350520002ULL;
		Require(TomCat::BuiltInCircleSpriteHandleValue == expectedCircleHandle
			&& TomCat::BuiltInSquareSpriteHandleValue == expectedSquareHandle,
			"built-in Sprite handle constants changed");

		const TomCat::AssetHandle circle(expectedCircleHandle);
		const TomCat::AssetHandle square(expectedSquareHandle);
		const auto manifest = TomCat::GetBuiltInSpriteAssets();
		Require(manifest.size() == 2,
			"built-in Sprite manifest must contain exactly Circle and Square");
		const TomCat::BuiltInSpriteAsset* circleAsset =
			TomCat::FindBuiltInSpriteAsset("Circle");
		const TomCat::BuiltInSpriteAsset* squareAsset =
			TomCat::FindBuiltInSpriteAsset("Square");
		Require(circleAsset && circleAsset->Handle == circle
			&& circleAsset->PackageRelativePath
				== "Resources/Sprites/TomCat/Circle.tga"
			&& squareAsset && squareAsset->Handle == square
			&& squareAsset->PackageRelativePath
				== "Resources/Sprites/TomCat/Square.tga",
			"built-in Sprite manifest name, path, or fixed handle changed");
		Require(TomCat::FindBuiltInSpriteAsset(circle) == circleAsset
			&& TomCat::FindBuiltInSpriteAsset(square) == squareAsset,
			"built-in Sprite manifest lookups disagree by name and handle");
		Require(TomCat::FindBuiltInSpriteAsset("circle") == nullptr
			&& TomCat::FindBuiltInSpriteAsset(TomCat::AssetHandle(1234)) == nullptr,
			"built-in Sprite manifest accepted an unknown identity");
		Require(TomCat::IsBuiltInAssetHandle(circle)
			&& TomCat::IsBuiltInAssetHandle(square)
			&& !TomCat::IsBuiltInAssetHandle(TomCat::AssetHandle(0))
			&& !TomCat::IsBuiltInAssetHandle(TomCat::AssetHandle(
				(std::numeric_limits<uint64_t>::max)())),
			"built-in Sprite reserved-handle predicate changed");

		const std::filesystem::path circlePath =
			std::filesystem::path("Packages/Resources/Sprites/TomCat/Circle.tga");
		const std::filesystem::path squarePath =
			std::filesystem::path("Packages/Resources/Sprites/TomCat/Square.tga");
		Require(TomCat::GetBuiltInSpriteAssetPath(circle) == circlePath
			&& TomCat::GetBuiltInSpriteAssetPath(square) == squarePath
			&& TomCat::GetBuiltInSpriteAssetPath(TomCat::AssetHandle(1234)).empty(),
			"built-in Sprite package path resolution changed");

		const std::filesystem::path executableDirectory = GetExecutableDirectory();
		std::error_code error;
		Require(std::filesystem::is_regular_file(
			executableDirectory / circlePath, error) && !error,
			"Circle package resource was not copied beside the regression executable");
		error.clear();
		Require(std::filesystem::is_regular_file(
			executableDirectory / squarePath, error) && !error,
			"Square package resource was not copied beside the regression executable");
		const ScopedCurrentPath packagedRuntime(executableDirectory);
		const std::vector<uint8_t> circleSource = ReadFileBytes(circlePath);
		const std::vector<uint8_t> squareSource = ReadFileBytes(squarePath);
		RequireDecodablePrimitive(circleSource, true);
		RequireDecodablePrimitive(squareSource, false);

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		assets.Shutdown();
		TomCat::ResolvedSpriteAsset resolved;
		Require(assets.ResolvePath(circle) == circlePath
			&& assets.ResolvePath(square) == squarePath
			&& assets.ResolveSpriteAsset(circle, resolved)
			&& resolved.TextureHandle == circle && !resolved.IsSubAsset,
			"authoring did not resolve a built-in Sprite outside the project registry");

		TemporaryAssetProject environment;
		Require(assets.Initialize(environment.Assets, environment.Library),
			"built-in Sprite Cook AssetManager initialization failed");
		Require(assets.Registry().GetMetadata(circle) == nullptr
			&& assets.Registry().GetMetadata(square) == nullptr,
			"project registry claimed an immutable built-in Sprite handle");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Built-in Sprite Cook");
		TomCat::Entity entity = scene->CreateEntity("Built-ins");
		entity.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = square;
		entity.AddComponent<TomCat::SpriteAnimator>(
			MakeAnimator(square, circle, square));
		const std::filesystem::path scenePath =
			environment.Assets / "BuiltInSprites.tomcat";
		Require(TomCat::SceneSerializer(scene).Serialize(scenePath),
			"built-in Sprite Cook Scene could not be serialized");
		const TomCat::AssetMetadata* sceneMetadata =
			assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata && sceneMetadata->Type == TomCat::AssetType::Scene,
			"built-in Sprite Cook Scene was not imported");
		const std::filesystem::path package =
			environment.Root / "Build" / "BuiltInSprites.tcpak";
		Require(assets.CookToPackage(package, sceneMetadata->Handle),
			"Cook rejected Scene references to built-in Sprites");
		assets.Shutdown();

		Require(assets.MountCookedPackage(package),
			"built-in Sprite Cook package could not be mounted");
		auto requireCookedBuiltIn = [&](TomCat::AssetHandle handle,
			const std::vector<uint8_t>& source, bool expectCircle)
		{
			TomCat::CookedAssetRange range;
			std::vector<uint8_t> cooked;
			TomCat::AssetType type = TomCat::AssetType::None;
			Require(assets.TryGetCookedAssetRange(handle, range)
				&& range.Type == TomCat::AssetType::Texture2D
				&& assets.ReadAssetBytes(handle, cooked, &type)
				&& type == TomCat::AssetType::Texture2D,
				"Cook omitted a built-in Sprite package entry");
			Require(cooked != source && TomCat::IsTextureArtifact(cooked),
				"Cook did not import a built-in Sprite into a texture artifact");
			RequireDecodablePrimitive(cooked, expectCircle);
		};
		requireCookedBuiltIn(circle, circleSource, true);
		requireCookedBuiltIn(square, squareSource, false);
		Require(assets.BeginCookedTexturePreload() == 2,
			"cooked package did not expose both built-in Sprites to texture preload");
		assets.UnmountCookedPackage();
	}

	void TestBuiltInSpriteHandlesStayReserved()
	{
		TemporaryAssetProject environment;
		const std::filesystem::path rootCollision =
			environment.Assets / "ReservedCircle.tga";
		const std::filesystem::path childCollision =
			environment.Assets / "ReservedSquareChild.tga";
		const auto writeText = [](const std::filesystem::path& path,
			const std::string& text)
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			Require(static_cast<bool>(output),
				"reserved built-in handle fixture could not be written");
		};
		writeText(rootCollision, "root collision source");
		writeText(childCollision, "child collision source");

		const std::filesystem::path rootMetadata =
			TomCat::AssetRegistry::GetMetadataPath(rootCollision);
		const std::filesystem::path childMetadata =
			TomCat::AssetRegistry::GetMetadataPath(childCollision);
		const uint64_t ordinaryOwner = 0x1020304050607080ULL;
		const std::string rootDocument =
			"SchemaVersion: 2\nAsset:\n  Handle: "
			+ std::to_string(TomCat::BuiltInCircleSpriteHandleValue)
			+ "\n  Type: Texture2D\n  ImportSettings: {}\n  SubAssets: []\n";
		const std::string childDocument =
			"SchemaVersion: 2\nAsset:\n  Handle: "
			+ std::to_string(ordinaryOwner)
			+ "\n  Type: Texture2D\n  ImportSettings: {}\n  SubAssets:\n"
				"    - Handle: "
			+ std::to_string(TomCat::BuiltInSquareSpriteHandleValue)
			+ "\n      PersistentID: reserved-square\n"
				"      Name: Reserved Square\n      Type: Texture2D\n";
		writeText(rootMetadata, rootDocument);
		writeText(childMetadata, childDocument);
		const std::string cacheDocument =
			"SchemaVersion: 2\nAssets:\n"
			"  - Handle: " + std::to_string(ordinaryOwner)
			+ "\n    Type: Texture2D\n"
				"    FilePath: ReservedSquareChild.tga\n"
				"    ImportSettings: {}\n"
				"    SubAssets:\n"
				"      - Handle: "
			+ std::to_string(TomCat::BuiltInSquareSpriteHandleValue)
			+ "\n        PersistentID: reserved-square\n"
				"        Name: Reserved Square\n"
				"        Type: Texture2D\n";
		std::filesystem::create_directories(environment.Library);
		writeText(environment.Library / "AssetRegistry.yaml", cacheDocument);

		TomCat::AssetRegistry registry;
		Require(registry.Initialize(environment.Assets, environment.Library),
			"reserved built-in handle registry initialization failed");
		Require(!registry.Refresh(),
			"registry accepted a project .tcmeta claim on built-in Sprite handles");
		Require(registry.GetMetadata(TomCat::AssetHandle(
				TomCat::BuiltInCircleSpriteHandleValue)) == nullptr
			&& registry.GetMetadata(TomCat::AssetHandle(ordinaryOwner)) == nullptr
			&& registry.GetSubAssetOwner(TomCat::AssetHandle(
				TomCat::BuiltInSquareSpriteHandleValue)) == nullptr,
			"reserved built-in Sprite handle leaked into the project registry");
		Require(ReadFileBytes(rootMetadata) == std::vector<uint8_t>(
				rootDocument.begin(), rootDocument.end())
			&& ReadFileBytes(childMetadata) == std::vector<uint8_t>(
				childDocument.begin(), childDocument.end()),
			"registry rewrote a .tcmeta file that claimed a built-in Sprite handle");
		registry.Shutdown();
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

	void TestColdDirectAtlasCookIsReadOnly()
	{
		TemporaryAssetProject environment;
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"cold direct-atlas AssetManager initialization failed");

		// Use only Registry operations: no AssetManager settings notification,
		// asynchronous reimport request, artifact load, or coordinator pump may
		// prewarm the atlas before Cook.
		const TomCat::AssetHandle atlas =
			TomCat::EnsurePrimitiveSpriteAsset(assets.Registry(), "Square");
		const TomCat::AssetMetadata* original = assets.Registry().GetMetadata(atlas);
		Require(original != nullptr, "cold direct-atlas metadata is missing");
		TomCat::AssetImportSettings settings = original->ImportSettings;
		settings["SpriteMode"] = "Multiple";
		settings["SpriteAtlasSchema"] = "2";
		settings["Sprite.idle.Name"] = "Idle";
		settings["Sprite.idle.Rect"] = "0,0,32,16";
		settings["Sprite.idle.Pivot"] = "0.5,0.5";
		settings["Sprite.idle.PixelsPerUnit"] = "32";
		settings["Sprite.idle.Border"] = "0,0,0,0";
		Require(assets.Registry().SetImportSettings(atlas, settings),
			"cold direct-atlas settings could not be written through AssetRegistry");

		const std::filesystem::path source =
			assets.Registry().GetFileSystemPath(atlas);
		const std::filesystem::path metadataPath =
			TomCat::AssetRegistry::GetMetadataPath(source);
		const std::vector<uint8_t> metadataBytesBefore =
			ReadFileBytes(metadataPath);
		const std::optional<TomCat::AssetMetadata> metadataBefore =
			assets.GetDatabase().GetMetadataSnapshot(atlas);
		Require(metadataBefore && metadataBefore->SubAssets.empty(),
			"cold direct-atlas unexpectedly had imported sub-assets before Cook");
		const TomCat::AssetDependencySnapshot graphBefore =
			assets.GetDatabase().GetDependencySnapshot(atlas);

		std::error_code cacheError;
		const std::filesystem::path derivedData =
			environment.Library / "DerivedData";
		Require(std::filesystem::is_directory(derivedData, cacheError)
			&& !cacheError && std::filesystem::is_empty(derivedData, cacheError)
			&& !cacheError,
			"cold direct-atlas DDC was not empty before Cook");

		const std::shared_ptr<const TomCat::IAssetImporter> delegate =
			assets.GetDatabase().GetImporters().Find(TomCat::AssetType::Texture2D);
		Require(delegate != nullptr, "cold direct-atlas Texture importer is missing");
		size_t importCount = 0;
		Require(assets.GetDatabase().GetImporters().Register(
			std::make_shared<CountingTextureImporter>(delegate, importCount), true),
			"cold direct-atlas counting importer could not be installed");

		const std::filesystem::path package =
			environment.Root / "Build" / "ColdDirectAtlas.tcpak";
		Require(assets.CookToPackage(package),
			"first cold standalone Cook rejected a direct Multiple atlas root");
		Require(importCount == 1,
			"cold direct-atlas Cook did not execute exactly one Texture import");

		const std::optional<TomCat::AssetMetadata> metadataAfter =
			assets.GetDatabase().GetMetadataSnapshot(atlas);
		const TomCat::AssetDependencySnapshot graphAfter =
			assets.GetDatabase().GetDependencySnapshot(atlas);
		Require(metadataAfter
			&& metadataAfter->Handle == metadataBefore->Handle
			&& metadataAfter->Type == metadataBefore->Type
			&& metadataAfter->FilePath == metadataBefore->FilePath
			&& metadataAfter->ImportSettings == metadataBefore->ImportSettings
			&& metadataAfter->IsMissing == metadataBefore->IsMissing
			&& metadataAfter->SubAssets.empty(),
			"cold direct-atlas Cook mutated Registry metadata or sub-asset state");
		Require(ReadFileBytes(metadataPath) == metadataBytesBefore,
			"cold direct-atlas Cook rewrote tcmeta bytes");
		Require(graphAfter.Revision == graphBefore.Revision
			&& graphAfter.Dependencies == graphBefore.Dependencies
			&& graphAfter.ArtifactDependencies == graphBefore.ArtifactDependencies
			&& graphAfter.SourceSHA256 == graphBefore.SourceSHA256,
			"cold direct-atlas Cook mutated the dependency graph snapshot");

		assets.Shutdown();
		Require(assets.MountCookedPackage(package),
			"cold direct-atlas package could not be mounted");
		std::vector<uint8_t> cooked;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(atlas, cooked, &cookedType)
			&& cookedType == TomCat::AssetType::Texture2D
			&& TomCat::IsTextureArtifact(cooked),
			"cold direct-atlas package omitted its target atlas artifact");
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
		const TomCat::AssetHandle sliceDependency =
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle");
		Require(static_cast<uint64_t>(sliceDependency) != 0,
			"Sprite slice dependency asset could not be created");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		scene->SetSceneName("Atlas Cook");
		TomCat::Entity entity = scene->CreateEntity("Sliced Sprite");
		entity.AddComponent<TomCat::SpriteRenderer>().SpriteHandle = idle;
		const std::filesystem::path scenePath = environment.Assets / "Atlas.tomcat";
		Require(TomCat::SceneSerializer(scene).Serialize(scenePath),
			"Atlas Scene could not be serialized");
		const TomCat::AssetMetadata* sceneMetadata = assets.Registry().GetMetadata(scenePath);
		Require(sceneMetadata != nullptr, "Atlas Scene was not imported");
		const TomCat::AssetHandle sceneHandle = sceneMetadata->Handle;
		Require(assets.GetDatabase().SetDependencies(idle, { sliceDependency }),
			"Sprite sub-asset explicit dependency could not be recorded");
		Require(assets.Refresh()
			&& assets.GetDatabase().GetDependencies(idle)
				== std::vector<TomCat::AssetHandle>{ sliceDependency },
			"Sprite sub-asset explicit dependency did not survive registry refresh");
		const std::filesystem::path package = environment.Root / "Build" / "Atlas.tcpak";
		Require(assets.CookToPackage(package, sceneHandle),
			"Cook did not accept the Sprite sub-asset dependency");
		assets.Shutdown();
		Require(assets.MountCookedPackage(package),
			"Player could not mount the Atlas package");
		std::vector<uint8_t> cooked;
		TomCat::AssetType cookedType = TomCat::AssetType::None;
		Require(assets.ReadAssetBytes(idle, cooked, &cookedType)
			&& cookedType == TomCat::AssetType::Texture2D,
			"Player package omitted the derived Sprite sub-asset");
		std::vector<uint8_t> dependencyBytes;
		Require(assets.ReadAssetBytes(sliceDependency, dependencyBytes, &cookedType)
			&& cookedType == TomCat::AssetType::Texture2D
			&& TomCat::IsTextureArtifact(dependencyBytes),
			"Cook omitted a dependency owned by the Sprite sub-asset graph node");
		TomCat::ResolvedSpriteAsset resolved;
		std::span<const uint8_t> atlasBytes;
		Require(TomCat::ParseCookedSpriteSubAsset(cooked, resolved, atlasBytes)
			&& resolved.IsSubAsset && resolved.TextureHandle == atlas
			&& resolved.Data == expectedIdleData && !atlasBytes.empty(),
			"Cooked Sprite envelope lost its atlas slice metadata or payload");
	}

	void TestAutomaticAtlasSlicingAndPacking()
	{
		constexpr uint32_t width = 8;
		constexpr uint32_t height = 6;
		std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 0);
		const auto setPixel = [&](uint32_t x, uint32_t y,
			std::array<uint8_t, 4> color)
		{
			const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
			std::copy(color.begin(), color.end(), pixels.begin() + offset);
		};
		for (uint32_t y = 1; y <= 2; ++y)
			for (uint32_t x = 1; x <= 2; ++x)
				setPixel(x, y, { 220, 10, 20, 255 });
		for (uint32_t y = 2; y <= 4; ++y)
			for (uint32_t x = 5; x <= 6; ++x)
				setPixel(x, y, { 10, 180, 40, 255 });
		setPixel(0, 5, { 255, 255, 255, 255 });
		setPixel(3, 0, { 255, 255, 255, 64 });

		TomCat::SpriteAtlasSliceOptions sliceOptions;
		sliceOptions.AlphaThreshold = 128;
		sliceOptions.MinimumOpaquePixels = 2;
		std::vector<TomCat::SpriteAtlasRect> regions;
		std::string error;
		Require(TomCat::SliceSpriteAtlasByAlpha(pixels, width, height,
			sliceOptions, regions, error) && regions == std::vector<TomCat::SpriteAtlasRect>{
				{ 1, 1, 2, 2 }, { 5, 2, 2, 3 } },
			"alpha Auto Slice did not find deterministic opaque island bounds");

		TomCat::AssetSubAsset exact;
		exact.Handle = TomCat::AssetHandle(101);
		exact.PersistentID = "sprite:hero";
		exact.Name = "Hero";
		exact.Type = TomCat::AssetType::Texture2D;
		exact.Sprite = { 1, 1, 2, 2, 0.25f, 0.75f, 32.0f,
			1.0f, 0.0f, 1.0f, 0.0f };
		TomCat::AssetSubAsset moved;
		moved.Handle = TomCat::AssetHandle(202);
		moved.PersistentID = "sprite:enemy";
		moved.Name = "Enemy";
		moved.Type = TomCat::AssetType::Texture2D;
		moved.Sprite = { 4, 2, 2, 3, 0.5f, 0.5f, 16.0f,
			0.0f, 0.0f, 0.0f, 0.0f };
		const std::array<TomCat::AssetSubAsset, 2> existing = { exact, moved };
		const std::vector<TomCat::AssetSubAsset> reconciled =
			TomCat::ReconcileSpriteAtlasSlices(regions, existing);
		Require(reconciled.size() == 2
			&& reconciled[0].Handle == exact.Handle
			&& reconciled[0].PersistentID == "sprite:hero"
			&& reconciled[0].Name == "Hero"
			&& reconciled[0].Sprite.PivotX == 0.25f
			&& reconciled[0].Sprite.PixelsPerUnit == 32.0f
			&& reconciled[1].Handle == moved.Handle
			&& reconciled[1].PersistentID == "sprite:enemy"
			&& reconciled[1].Sprite.X == 5,
			"Auto Slice reconciliation did not preserve stable IDs and metadata");
		const std::vector<TomCat::AssetSubAsset> generatedA =
			TomCat::ReconcileSpriteAtlasSlices(regions, {});
		const std::vector<TomCat::AssetSubAsset> generatedB =
			TomCat::ReconcileSpriteAtlasSlices(regions, {});
		Require(generatedA.size() == 2 && generatedB.size() == 2
			&& generatedA[0].PersistentID == generatedB[0].PersistentID
			&& generatedA[1].PersistentID == generatedB[1].PersistentID
			&& generatedA[0].PersistentID != generatedA[1].PersistentID,
			"new Auto Slice IDs are not deterministic and unique");

		TomCat::SpriteAtlasGridOptions gridOptions;
		gridOptions.CellWidth = 3;
		gridOptions.CellHeight = 2;
		gridOptions.IncludePartialCells = true;
		std::vector<TomCat::SpriteAtlasRect> grid;
		Require(TomCat::SliceSpriteAtlasGrid(7, 5, gridOptions, grid, error)
			&& grid.size() == 9 && grid.back() == TomCat::SpriteAtlasRect{ 6, 4, 1, 1 },
			"Grid Slice did not clip partial edge cells");

		TomCat::SpriteAtlasPackOptions packOptions;
		packOptions.MaximumWidth = 8;
		packOptions.MaximumHeight = 8;
		packOptions.Padding = 1;
		packOptions.PowerOfTwo = true;
		TomCat::SpriteAtlasPackedLayout layout;
		std::vector<uint8_t> packed;
		Require(TomCat::BuildPackedSpriteAtlasRGBA(pixels, width, height, regions,
			packOptions, layout, packed, error)
			&& layout.Width == 8 && layout.Height == 8
			&& layout.Placements.size() == regions.size()
			&& packed.size() == static_cast<size_t>(8 * 8 * 4),
			"deterministic Sprite Atlas packing failed");
		for (size_t index = 0; index < regions.size(); ++index)
		{
			const TomCat::SpriteAtlasRect& source = regions[index];
			const TomCat::SpriteAtlasRect& destination = layout.Placements[index];
			const size_t sourcePixel = (static_cast<size_t>(source.Y) * width
				+ source.X) * 4;
			const size_t contentPixel = (static_cast<size_t>(destination.Y)
				* layout.Width + destination.X) * 4;
			const size_t extrudedPixel = (static_cast<size_t>(destination.Y)
				* layout.Width + destination.X - 1) * 4;
			Require(std::equal(pixels.begin() + sourcePixel,
				pixels.begin() + sourcePixel + 4, packed.begin() + contentPixel)
				&& std::equal(pixels.begin() + sourcePixel,
					pixels.begin() + sourcePixel + 4, packed.begin() + extrudedPixel),
				"packed Sprite pixels or padding extrusion changed source colors");
		}
	}

	void WritePackageMarker(const std::filesystem::path& path,
		std::span<const uint8_t> bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		if (!bytes.empty())
			output.write(reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		Require(static_cast<bool>(output), "test package marker could not be written");
	}

	void TestCookRejectsReservedSubAssetDependency()
	{
		TemporaryAssetProject environment;
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		const AtlasCookFixture fixture = PrepareAtlasCookFixture(assets,
			environment, "ReservedHandle");
		const TomCat::AssetHandle reserved(
			(std::numeric_limits<uint64_t>::max)());
		Require(assets.GetDatabase().SetDependencies(fixture.Idle, { reserved }),
			"reserved Sprite dependency could not be installed for regression");

		const std::filesystem::path package =
			environment.Root / "Build" / "ReservedHandle.tcpak";
		const std::vector<uint8_t> marker = { 'l', 'a', 's', 't', '-', 'g', 'o', 'o', 'd' };
		WritePackageMarker(package, marker);
		Require(!assets.CookToPackage(package, fixture.Scene),
			"Cook accepted UINT64_MAX through a Sprite sub-asset dependency");
		Require(ReadFileBytes(package) == marker,
			"failed reserved-handle Cook replaced the last good package");
	}

	void TestAdvanced2DAuthoringAssetCodecs()
	{
		std::string document;
		std::string error;

		TomCat::AnimationClipAsset clip;
		clip.SampleRate = 24.0f;
		clip.Clip.Name = "Walk";
		clip.Clip.Loop = true;
		clip.Clip.Frames = {
			{ TomCat::AssetHandle(2101), 1.0f / 24.0f },
			{ TomCat::AssetHandle(2102), 2.0f / 24.0f }
		};
		Require(TomCat::AnimationClipAssetCodec::Encode(clip, document, error),
			"Animation Clip asset codec could not encode valid data");
		TomCat::AnimationClipAsset decodedClip;
		const std::vector<uint8_t> clipBytes(document.begin(), document.end());
		Require(TomCat::AnimationClipAssetCodec::Decode(
			std::span<const uint8_t>(clipBytes), decodedClip, error)
			&& decodedClip.Clip.Name == "Walk" && decodedClip.Clip.Loop
			&& decodedClip.SampleRate == 24.0f
			&& decodedClip.Clip.Frames.size() == 2
			&& decodedClip.Clip.Frames[1].SpriteHandle == TomCat::AssetHandle(2102),
			"Animation Clip asset roundtrip changed authored data");
		std::vector<TomCat::AuthoringAssetReference> references;
		Require(TomCat::AnimationClipAssetCodec::VisitAssetReferences(decodedClip,
			[&](const TomCat::AuthoringAssetReference& reference)
			{
				references.push_back(reference);
				return true;
			}, error) && references.size() == 2
			&& references[0].Handle == TomCat::AssetHandle(2101)
			&& references[0].ExpectedType == TomCat::AssetType::Texture2D
			&& references[0].Kind
				== TomCat::AuthoringAssetReferenceKind::AnimationFrame
			&& references[1].PropertyPath
				== "$.TomCatAnimationClip.Frames[1].SpriteHandle",
			"Animation Clip asset references were not enumerated precisely");

		TomCat::AnimationClipAsset emptyClip;
		emptyClip.Clip.Name = "New Animation";
		Require(TomCat::AnimationClipAssetCodec::Encode(emptyClip, document, error),
			"editable empty Animation Clip asset was rejected");
		const std::string invalidClip =
			"TomCatAnimationClip:\n"
			"  Version: 1\n"
			"  Name: Walk\n"
			"  Loop: true\n"
			"  SampleRate: 12\n"
			"  Frames: []\n"
			"  Unknown: true\n";
		Require(!TomCat::AnimationClipAssetCodec::Decode(
			invalidClip, decodedClip, error) && !error.empty(),
			"Animation Clip asset codec accepted an unknown key");
		clip.Clip.Frames[0].SpriteHandle = TomCat::AssetHandle(0);
		Require(!TomCat::AnimationClipAssetCodec::Encode(clip, document, error),
			"Animation Clip asset codec accepted a zero Sprite handle");

		TomCat::AnimatorControllerAsset controller;
		controller.InitialState = "Idle";
		TomCat::AnimatorParameter speed;
		speed.Name = "Speed";
		speed.Type = TomCat::AnimatorParameterType::Float;
		controller.Parameters.push_back(speed);
		controller.States = {
			{ "Idle", TomCat::AssetHandle(2201), 1.0f },
			{ "Walk", TomCat::AssetHandle(2202), 1.25f }
		};
		TomCat::AnimatorTransition transition;
		transition.FromState = "Idle";
		transition.ToState = "Walk";
		transition.ExitTime = -1.0f;
		transition.Conditions.push_back({ "Speed",
			TomCat::AnimatorConditionMode::Greater, 0.1f });
		controller.Transitions.push_back(transition);
		Require(TomCat::AnimatorControllerAssetCodec::Encode(
			controller, document, error),
			"Animator Controller asset codec could not encode valid data");
		TomCat::AnimatorControllerAsset decodedController;
		Require(TomCat::AnimatorControllerAssetCodec::Decode(
			document, decodedController, error)
			&& decodedController.InitialState == "Idle"
			&& decodedController.Parameters.size() == 1
			&& decodedController.States.size() == 2
			&& decodedController.Transitions.size() == 1
			&& decodedController.Transitions[0].Conditions[0].Parameter == "Speed",
			"Animator Controller asset roundtrip changed authored data");
		references.clear();
		Require(TomCat::AnimatorControllerAssetCodec::VisitAssetReferences(
			decodedController,
			[&](const TomCat::AuthoringAssetReference& reference)
			{
				references.push_back(reference);
				return true;
			}, error) && references.size() == 2
			&& references[0].Handle == TomCat::AssetHandle(2201)
			&& references[0].ExpectedType == TomCat::AssetType::AnimationClip
			&& references[0].Kind
				== TomCat::AuthoringAssetReferenceKind::AnimatorStateMotion
			&& references[1].PropertyPath
				== "$.TomCatAnimatorController.States[1].ClipHandle",
			"Animator Controller Animation Clip references were not enumerated precisely");
		TomCat::AnimatorControllerAsset emptyController;
		Require(TomCat::AnimatorControllerAssetCodec::Encode(
			emptyController, document, error),
			"editable empty Animator Controller asset was rejected");
		controller.Transitions[0].ToState = "Missing";
		Require(!TomCat::AnimatorControllerAssetCodec::Encode(
			controller, document, error),
			"Animator Controller asset codec accepted an unknown target state");

		TomCat::TilePaletteAsset palette;
		palette.CellSize = { 1.0f, 2.0f };
		palette.CellGap = { 0.125f, -0.25f };
		palette.Tiles = {
			{ { -2, 3 }, TomCat::AssetHandle(3101) },
			{ { 4, 5 }, TomCat::AssetHandle(3102) }
		};
		Require(TomCat::TilePaletteAssetCodec::Encode(palette, document, error),
			"Tile Palette asset codec could not encode valid data");
		TomCat::TilePaletteAsset decodedPalette;
		Require(TomCat::TilePaletteAssetCodec::Decode(
			document, decodedPalette, error)
			&& decodedPalette.CellSize == glm::vec2(1.0f, 2.0f)
			&& decodedPalette.CellGap == glm::vec2(0.125f, -0.25f)
			&& decodedPalette.Tiles.size() == 2
			&& decodedPalette.Tiles[0].Coordinate == glm::ivec2(-2, 3)
			&& decodedPalette.Tiles[1].SpriteHandle == TomCat::AssetHandle(3102),
			"Tile Palette asset roundtrip changed authored data");
		references.clear();
		Require(TomCat::TilePaletteAssetCodec::VisitAssetReferences(decodedPalette,
			[&](const TomCat::AuthoringAssetReference& reference)
			{
				references.push_back(reference);
				return true;
			}, error) && references.size() == 2
			&& references[0].Kind
				== TomCat::AuthoringAssetReferenceKind::TilePaletteEntry
			&& references[1].PropertyPath
				== "$.TomCatTilePalette.Tiles[1].SpriteHandle",
			"Tile Palette asset references were not enumerated precisely");
		palette.Tiles.push_back({ { -2, 3 }, TomCat::AssetHandle(9999) });
		Require(!TomCat::TilePaletteAssetCodec::Encode(palette, document, error),
			"Tile Palette asset codec accepted a duplicate coordinate");
		const std::string invalidPalette =
			"TomCatTilePalette:\n"
			"  Version: 1\n"
			"  CellSize: [1, 1]\n"
			"  CellGap: [0, 0]\n"
			"  Tiles:\n"
			"    - Coordinate: [0, 0]\n"
			"      SpriteHandle: 0\n";
		Require(!TomCat::TilePaletteAssetCodec::Decode(
			invalidPalette, decodedPalette, error),
			"Tile Palette asset codec accepted a zero Sprite handle");
	}

	void TestAnimatorControllerHydratesInactiveAndDynamicEntities()
	{
		TemporaryAssetProject environment;
		const auto builtIns = TomCat::GetBuiltInSpriteAssets();
		Require(!builtIns.empty(), "Animator hydration needs one built-in Sprite");

		TomCat::AnimationClipAsset clip;
		clip.Clip.Name = "External";
		clip.Clip.Frames.push_back({ builtIns.front().Handle, 1.0f / 12.0f });
		std::string document;
		std::string error;
		Require(TomCat::AnimationClipAssetCodec::Encode(clip, document, error),
			"Animator hydration clip could not be encoded");
		const std::filesystem::path clipPath = environment.Assets / "External.tcanim";
		{
			std::ofstream output(clipPath, std::ios::binary | std::ios::trunc);
			output.write(document.data(), static_cast<std::streamsize>(document.size()));
			Require(static_cast<bool>(output), "Animator hydration clip could not be written");
		}

		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		Require(assets.Initialize(environment.Assets, environment.Library),
			"Animator hydration AssetManager did not initialize");
		const TomCat::AssetMetadata* clipMetadata =
			assets.Registry().GetMetadata(clipPath);
		Require(clipMetadata && clipMetadata->Type == TomCat::AssetType::AnimationClip,
			"Animator hydration clip metadata is missing");

		TomCat::AnimatorControllerAsset controller;
		controller.InitialState = "External State";
		controller.States.push_back({ "External State", clipMetadata->Handle, 1.0f });
		Require(TomCat::AnimatorControllerAssetCodec::Encode(controller, document, error),
			"Animator hydration controller could not be encoded");
		const std::filesystem::path controllerPath =
			environment.Assets / "External.tccontroller";
		{
			std::ofstream output(controllerPath, std::ios::binary | std::ios::trunc);
			output.write(document.data(), static_cast<std::streamsize>(document.size()));
			Require(static_cast<bool>(output),
				"Animator hydration controller could not be written");
		}
		const TomCat::AssetHandle controllerHandle = assets.ImportAsset(controllerPath);
		Require(static_cast<uint64_t>(controllerHandle) != 0,
			"Animator hydration controller could not be imported");

		auto makeEmbedded = [&]()
		{
			TomCat::SpriteAnimator animator;
			animator.ControllerHandle = controllerHandle;
			TomCat::SpriteAnimationClip embedded;
			embedded.Name = "Embedded";
			embedded.Frames.push_back({ builtIns.front().Handle, 0.25f });
			animator.Clips.push_back(std::move(embedded));
			return animator;
		};

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity inactive = scene->CreateEntity("Inactive Animator");
		inactive.GetComponent<TomCat::Tag>().ActiveSelf = false;
		inactive.AddComponent<TomCat::SpriteRenderer>();
		inactive.AddComponent<TomCat::SpriteAnimator>(makeEmbedded());
		Require(scene->OnRuntimeStart(), "Animator hydration runtime did not start");
		const TomCat::SpriteAnimator& hydratedInactive =
			inactive.GetComponent<TomCat::SpriteAnimator>();
		Require(hydratedInactive.Clips.size() == 1
			&& hydratedInactive.Clips[0].Name == "External"
			&& hydratedInactive.States.size() == 1
			&& hydratedInactive.InitialState == "External State"
			&& !hydratedInactive.RuntimeInitialized,
			"inactive Animator did not hydrate its external Controller at runtime start");

		TomCat::Entity dynamic = scene->CreateEntity("Dynamic Animator");
		dynamic.AddComponent<TomCat::SpriteRenderer>();
		dynamic.AddComponent<TomCat::SpriteAnimator>(makeEmbedded());
		const TomCat::SpriteAnimator& hydratedDynamic =
			dynamic.GetComponent<TomCat::SpriteAnimator>();
		Require(hydratedDynamic.Clips.size() == 1
			&& hydratedDynamic.Clips[0].Name == "External"
			&& hydratedDynamic.States.size() == 1
			&& hydratedDynamic.RuntimeInitialized,
			"dynamically added Animator bypassed external Controller hydration");
		scene->OnRuntimeStop();
	}

	enum class ConcurrentCookMutation : uint8_t
	{
		Graph,
		Slice,
		Source
	};

	void TestCookRejectsConcurrentSubAssetMutation(ConcurrentCookMutation mutation,
		std::string_view testName)
	{
		TemporaryAssetProject environment;
		TomCat::AssetManager& assets = TomCat::AssetManager::Get();
		const AtlasCookFixture fixture =
			PrepareAtlasCookFixture(assets, environment, testName);
		const TomCat::AssetHandle tailDependency =
			TomCat::EnsurePrimitiveSpriteAsset(assets, "Circle");
		Require(static_cast<uint64_t>(tailDependency) != 0,
			"concurrent Cook tail dependency could not be created");
		Require(assets.GetDatabase().SetDependencies(fixture.Idle,
			{ tailDependency }),
			"concurrent Cook Sprite dependency could not be installed");

		std::vector<TomCat::AssetSubAsset> changedSlices = fixture.Owner.SubAssets;
		const auto changedSlice = std::find_if(changedSlices.begin(),
			changedSlices.end(), [&](const TomCat::AssetSubAsset& child)
			{
				return child.Handle == fixture.Idle;
			});
		Require(changedSlice != changedSlices.end(),
			"concurrent Cook fixture lost its Sprite definition");
		changedSlice->Sprite.X += 1;

		std::vector<uint8_t> changedSource = ReadFileBytes(fixture.Source);
		changedSource.push_back(0x5a);
		const std::shared_ptr<BlockingTextureImportGate> gate =
			InstallBlockingTextureImporter(assets, tailDependency);
		const std::filesystem::path package =
			environment.Root / "Build" / (std::string(testName) + ".tcpak");
		const std::vector<uint8_t> marker = { 'l', 'a', 's', 't', '-', 'g', 'o', 'o', 'd' };
		WritePackageMarker(package, marker);

		bool cooked = true;
		std::thread worker([&]()
			{
				cooked = assets.CookToPackage(package, fixture.Scene);
			});
		const bool entered = gate->WaitUntilEntered();
		bool mutationSucceeded = false;
		if (entered)
		{
			switch (mutation)
			{
				case ConcurrentCookMutation::Graph:
					mutationSucceeded = assets.GetDatabase().SetDependencies(
						fixture.Idle, {});
					break;
				case ConcurrentCookMutation::Slice:
					mutationSucceeded = assets.Registry().SynchronizeSubAssets(
						fixture.Atlas, changedSlices);
					break;
				case ConcurrentCookMutation::Source:
				{
					std::ofstream output(fixture.Source,
						std::ios::binary | std::ios::trunc);
					output.write(reinterpret_cast<const char*>(changedSource.data()),
						static_cast<std::streamsize>(changedSource.size()));
					output.close();
					mutationSucceeded = !output.fail();
					break;
				}
			}
		}
		gate->Release();
		worker.join();

		Require(entered,
			"Cook did not reach the blocked tail import mutation checkpoint");
		Require(mutationSucceeded,
			"concurrent Cook mutation could not be applied at the checkpoint");
		Require(!cooked,
			"Cook published a package after its pinned Sprite graph changed");
		Require(ReadFileBytes(package) == marker,
			"failed concurrent Cook replaced the last good package");
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
		TestAdvanced2DAuthoringAssetCodecs();
		TestAnimatorControllerHydratesInactiveAndDynamicEntities();
		TestSpriteAnimationSceneAndPrefabRoundtrip();
		TestBuiltInSpriteManifestPathsAndCook();
		TestBuiltInSpriteHandlesStayReserved();
		TestPrimitiveSpriteAuthoringAndCookedPackage();
		TestColdDirectAtlasCookIsReadOnly();
		TestAutomaticAtlasSlicingAndPacking();
		TestAtlasImportAndCookedSubSprite();
		TestCookRejectsReservedSubAssetDependency();
		TestCookRejectsConcurrentSubAssetMutation(
			ConcurrentCookMutation::Graph, "ConcurrentGraph");
		TestCookRejectsConcurrentSubAssetMutation(
			ConcurrentCookMutation::Slice, "ConcurrentSlice");
		TestCookRejectsConcurrentSubAssetMutation(
			ConcurrentCookMutation::Source, "ConcurrentSource");
		std::cout << "PASS built-in Sprites, Sprite Atlas, Animator state machine, Scene/Prefab, and hardened Cook closure\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAIL Sprite sorting/animation regression: "
			<< exception.what() << '\n';
		return 1;
	}
}
