#pragma once

// Included after the shared ManagedRuntimeProbe / ScriptRuntimeOverride helpers.
// All mutations enter through the real native capability tables.
namespace {

	void TestImmediateMutationInvalidatesProjection()
	{
		using namespace TomCat;
		using namespace TomCat::Scripting;
		const NativeApiV2 envelope = BuildNativeApiV2();
		NativeGameplayApiV1 gameplay{};
		NativeAudioApiV1 audio{};
		NativeAudioSpatialApiV1 spatial{};
		NativeComponentApiV1 components{};
		auto capability = [&](std::string_view name, auto& api)
		{
			uint32_t required = 0;
			NativeUtf8View view{ reinterpret_cast<const uint8_t*>(name.data()), name.size() };
			Require(envelope.QueryCapability(view, 1, &api, sizeof(api), &required) == 0
				&& required == sizeof(api), "immediate mutation capability unavailable");
		};
		capability(GameplayCapabilityName, gameplay);
		capability(AudioCapabilityName, audio);
		capability(AudioSpatialCapabilityName, spatial);
		capability(ComponentCapabilityName, components);

		auto runtime = std::make_shared<ManagedRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		Scene scene;
		Entity sprite = scene.CreateEntity("Immediate animation projection");
		sprite.AddComponent<SpriteRenderer>();
		auto& animator = sprite.AddComponent<SpriteAnimator>();
		SpriteAnimationClip first;
		first.Name = "First";
		first.Frames.push_back({ AssetHandle(123), .1f });
		SpriteAnimationClip second;
		second.Name = "Second";
		second.Frames.push_back({ AssetHandle(456), .1f });
		animator.Clips = { first, second };
		Entity source = scene.CreateEntity("Immediate audio projection");
		source.AddComponent<AudioSource>().Clip = AssetHandle(789);
		Entity added = scene.CreateEntity("Immediate audio component projection");
		auto& engine = ScriptEngine::Get();
		constexpr uint64_t Generation = 0x1aaed1a7;
		const uint64_t session = engine.StartScene(scene, Generation);
		Require(session != 0, "could not start immediate mutation projection fixture");
		struct StopSession { uint64_t Session; ~StopSession() { ScriptEngine::Get().StopScene(Session); } } stop{ session };
		auto handle = [session](Entity entity)
		{
			return EntityHandleV1{ session, static_cast<uint64_t>(entity.GetUUID()), Generation };
		};
		const auto spriteHandle = handle(sprite), sourceHandle = handle(source), addedHandle = handle(added);
		auto read = [&](EntityHandleV1 entity, uint64_t type, uint64_t property)
		{
			NativePropertyValueV1 value{};
			Require(components.GetProperty(entity, type, property, &value) == 0,
				"registered getter failed after immediate mutation");
			return value;
		};
		float translation = 0;
		auto appendSuffix = [&]
		{
			NativePropertyValueV1 value{};
			value.Kind = static_cast<uint32_t>(NativePropertyKindV1::Vector3);
			value.Vector = { ++translation, 0, 0, 0 };
			Require(engine.QueueSetComponentProperty(spriteHandle, NativeComponentType::Transform,
				ComponentIds::TransformProperties::Translation, value),
				"could not append deferred suffix after immediate mutation");
		};
		uint64_t token = 0;
		Require(engine.BeginDeferredCallbackTransaction(spriteHandle, token),
			"could not begin immediate mutation callback");
		Require(read(spriteHandle, ComponentIds::SpriteRenderer,
			ComponentIds::SpriteRendererProperties::Sprite).Integer == 0,
			"animation fixture did not start with its authored sprite");
		for (const auto& clip : animator.Clips)
		{
			NativeUtf8View name{ reinterpret_cast<const uint8_t*>(clip.Name.data()), clip.Name.size() };
			Require(gameplay.SpriteAnimatorPlay(spriteHandle, name, 1) == 1,
				"real animator Play rejected the fixture clip");
			appendSuffix();
			Require(read(spriteHandle, ComponentIds::SpriteRenderer,
				ComponentIds::SpriteRendererProperties::Sprite).Integer
				== static_cast<int64_t>(static_cast<uint64_t>(clip.Frames.front().SpriteHandle)),
				"incremental projection retained the sprite from before Animator.Play");
		}
		Require(gameplay.SpriteAnimatorStop(spriteHandle) == 0 && !animator.RuntimePlaying,
			"real animator Stop failed");

		// Every check first retains a cached projection, then mixes immediate audio
		// mutation with an unrelated deferred suffix before using the generic getter.
		auto integer = [&](uint64_t property, auto mutate, int64_t expected)
		{
			(void)read(sourceHandle, ComponentIds::AudioSource, property);
			Require(mutate() == 0, "real immediate audio integer setter failed");
			appendSuffix();
			Require(read(sourceHandle, ComponentIds::AudioSource, property).Integer == expected,
				"incremental projection retained an immediate audio integer value");
		};
		auto number = [&](uint64_t property, auto mutate, double expected)
		{
			(void)read(sourceHandle, ComponentIds::AudioSource, property);
			Require(mutate() == 0, "real immediate audio number setter failed");
			appendSuffix();
			Require(std::abs(read(sourceHandle, ComponentIds::AudioSource, property).Number - expected) < 1e-6,
				"incremental projection retained an immediate audio number value");
		};
		using namespace ComponentIds::AudioSourceProperties;
		integer(Clip, [&] { return audio.SetClip(sourceHandle, 0); }, 0);
		integer(Enabled, [&] { return audio.SetEnabled(sourceHandle, 0); }, 0);
		integer(PlayOnStart, [&] { return audio.SetPlayOnStart(sourceHandle, 0); }, 0);
		integer(Loop, [&] { return audio.SetLoop(sourceHandle, 1); }, 1);
		integer(MixerGroup, [&] { return audio.SetMixerGroup(sourceHandle, 1); }, 1);
		integer(Streaming, [&] { return spatial.SetStreaming(sourceHandle, 1); }, 1);
		number(Volume, [&] { return audio.SetVolume(sourceHandle, .25f); }, .25);
		number(Pitch, [&] { return audio.SetPitch(sourceHandle, .5f); }, .5);
		number(SpatialBlend, [&] { return spatial.SetSpatialBlend(sourceHandle, .75f); }, .75);
		number(MinDistance, [&] { return spatial.SetMinDistance(sourceHandle, .25f); }, .25);
		number(MaxDistance, [&] { return spatial.SetMaxDistance(sourceHandle, 200); }, 200);

		auto presence = [&](uint64_t type, auto add, auto remove)
		{
			Require(components.Has(addedHandle, type) == 0 && add() == 0,
				"real immediate audio Add failed");
			appendSuffix();
			Require(components.Has(addedHandle, type) == 1,
				"incremental projection missed immediate audio Add");
			Require(remove() == 0, "real immediate audio Remove failed");
			appendSuffix();
			Require(components.Has(addedHandle, type) == 0,
				"incremental projection retained immediate audio Remove");
		};
		presence(ComponentIds::AudioSource, [&] { return audio.AddSource(addedHandle); },
			[&] { return audio.RemoveSource(addedHandle); });
		presence(ComponentIds::AudioListener, [&] { return audio.AddListener(addedHandle); },
			[&]
			{
				(void)read(addedHandle, ComponentIds::AudioListener, ComponentIds::AudioListenerProperties::Enabled);
				Require(audio.SetListenerEnabled(addedHandle, 0) == 0,
					"real immediate listener Enabled setter failed");
				appendSuffix();
				Require(read(addedHandle, ComponentIds::AudioListener, ComponentIds::AudioListenerProperties::Enabled).Integer == 0,
					"incremental projection retained listener Enabled");
				Require(audio.SetListenerPrimary(addedHandle, 0) == 0,
					"real immediate listener Primary setter failed");
				appendSuffix();
				Require(read(addedHandle, ComponentIds::AudioListener, ComponentIds::AudioListenerProperties::Primary).Integer == 0,
					"incremental projection retained listener Primary");
				return audio.RemoveListener(addedHandle);
			});

		// Validation must never invalidate the projection it is currently building.
		(void)read(sourceHandle, ComponentIds::AudioSource, Volume);
		const auto beforeValidation = engine.GetProjectionStatistics();
		{
			ComponentMutationPhaseScope validation(ComponentMutationPhase::Validation);
			engine.NotifyImmediateMutation();
		}
		(void)read(sourceHandle, ComponentIds::AudioSource, Volume);
		Require(engine.GetProjectionStatistics().SceneCopies == beforeValidation.SceneCopies,
			"validation notification discarded a valid projection");
		Require(engine.CompleteDeferredCallbackTransaction(token),
			"immediate mutation callback did not complete");
		Require(sprite.GetComponent<Transform>()._Translation.x == translation,
			"immediate invalidation lost a deferred transform suffix at commit");
	}

}
