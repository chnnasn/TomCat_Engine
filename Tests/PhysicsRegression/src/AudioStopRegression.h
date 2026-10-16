#pragma once

#include "TomCat/Audio/AudioClip.h"
#include "TomCat/Audio/AudioDevice.h"
#include "TomCat/Audio/AudioEngine.h"
#include "TomCat/Audio/AudioSceneRuntime.h"

// Included after Require / ScriptRuntimeOverride. Only the managed dispatch
// boundary is supplied by this probe; Scene teardown and audio voices are real.
namespace {

	class AudioStopRuntimeProbe final : public TomCat::Scripting::IScriptRuntime
	{
	public:
		using Status = TomCat::Scripting::ScriptStatus;
		bool IsReady() const override { return true; }
		Status CreateSceneRuntime(uint64_t, uint64_t) override { return Status::Success; }
		Status InstantiateAll(std::span<const TomCat::Scripting::NativeScriptAttachmentV1> attachments) override
		{ AttachmentCount = attachments.size(); return Status::Success; }
		Status ApplySerializedFields(std::string_view) override { return Status::Success; }
		Status InvokeCreateAll() override { return Status::Success; }
		Status SetEnabled(uint64_t, bool) override { return Status::Success; }
		Status UpdateAll(float) override { return Status::Success; }
		Status FixedUpdateAll(float) override { return Status::Success; }
		Status DispatchPhysicsEvents(std::span<const TomCat::Scripting::NativePhysicsEventV1>) override
		{ return Status::Success; }
		Status DestroyAll() override
		{
			++DestroyCount;
			if (OnDestroy) OnDestroy();
			return Status::Success;
		}
		size_t AttachmentCount = 0;
		size_t DestroyCount = 0;
		std::function<void()> OnDestroy;
	};

	void TestAudioStopTraversesSources()
	{
		using namespace TomCat;
		AudioEngine& audio = AudioEngine::Get();
		std::string error;
		Require(audio.Initialize(CreateNullAudioDevice(), error),
			"AudioStop regression could not initialize the real Null audio device");
		struct ShutdownAudio { ~ShutdownAudio() { AudioEngine::Get().Shutdown(); } } shutdown;

		// A real PCM clip, resident in the test to avoid a filesystem/import fixture.
		const std::array<uint8_t, 48> wave{
			'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',
			16,0,0,0,1,0,1,0,64,31,0,0,128,62,0,0,2,0,16,0,
			'd','a','t','a',4,0,0,0,0,0,0,0
		};
		const Ref<AudioClip> clip = AudioClip::Decode(wave, error);
		Require(clip != nullptr, "AudioStop regression PCM fixture did not decode");
		auto startVoice = [&](Entity entity)
		{
			auto& source = entity.GetComponent<AudioSource>();
			source.Clip = AssetHandle(0xa0d10);
			source.Loop = true;
			AudioVoiceSettings settings;
			settings.Loop = true;
			source.RuntimeVoice = audio.CreateVoice(clip, AudioMixerGroup::SFX, settings, error);
			source.RuntimeClipHandle = source.Clip;
			// Exercise production playback/settings after supplying the decoded voice.
			return source.RuntimeVoice != 0 && AudioSceneRuntime::Play(entity, &error)
				? source.RuntimeVoice : AudioVoiceHandle(0);
		};
		auto reset = [](const AudioSource& source)
		{
			return source.RuntimeVoice == 0
				&& static_cast<uint64_t>(source.RuntimeClipHandle) == 0
				&& !source.RuntimeStreaming && !source.RuntimeAutoPlayEvaluated;
		};

		auto first = CreateRef<Scene>();
		Scene second;
		Entity firstSource = first->CreateEntity("Audio stop first scene");
		firstSource.AddComponent<AudioSource>();
		Entity secondSource = second.CreateEntity("Audio stop second scene");
		secondSource.AddComponent<AudioSource>();
		const AudioVoiceHandle firstVoice = startVoice(firstSource);
		const AudioVoiceHandle secondVoice = startVoice(secondSource);
		Require(firstVoice != 0 && secondVoice != 0 && firstVoice != secondVoice,
			"AudioStop regression could not play independent scene voices");

		// Projection copies contain many unrelated entities and authored AudioSource
		// data, but must never acquire or release the live scene's voice handles.
		for (int index = 0; index < 128; ++index)
			first->CreateEntity("Audio stop visual " + std::to_string(index));
		{
			Ref<Scene> temporary = Scene::Copy(first);
			Require(temporary != nullptr, "AudioStop temporary Scene could not be copied");
			Entity copied = temporary->FindEntityByUUID(firstSource.GetUUID());
			Require(copied && copied.GetComponent<AudioSource>().RuntimeVoice == 0,
				"Scene copy retained a live audio voice");
			auto& silent = copied.GetComponent<AudioSource>();
			silent.RuntimeClipHandle = silent.Clip;
			silent.RuntimeStreaming = true;
			silent.RuntimeAutoPlayEvaluated = true;
			AudioSceneRuntime::Stop(*temporary);
			Require(reset(silent) && silent.Clip == firstSource.GetComponent<AudioSource>().Clip,
				"stopping a no-voice temporary Scene did not reset runtime-only fields");
		}
		Require(audio.GetState(firstVoice) == AudioPlaybackState::Playing
			&& audio.GetState(secondVoice) == AudioPlaybackState::Playing,
			"temporary Scene destruction stopped another Scene's voice");

		// Deferred removal is flushed for its owning Scene even when its component
		// no longer exists. Stopping the other Scene must not drain this queue.
		AudioSceneRuntime::DeferDestroySource(secondSource);
		secondSource.RemoveComponent<AudioSource>();
		Require(audio.HasVoice(secondVoice), "deferred voice was destroyed before its Scene flush");
		firstSource.GetComponent<Tag>().ActiveSelf = false;
		AudioSceneRuntime::Stop(*first);
		Require(!audio.HasVoice(firstVoice) && reset(firstSource.GetComponent<AudioSource>())
			&& audio.GetState(secondVoice) == AudioPlaybackState::Playing,
			"AudioStop missed an inactive source or flushed another Scene's deferred voice");
		AudioSceneRuntime::Stop(second);
		Require(!audio.HasVoice(secondVoice), "AudioStop did not flush its removed source's deferred voice");
		AudioSceneRuntime::Stop(second); // Idempotent with an empty AudioSource storage.

		auto runtime = std::make_shared<AudioStopRuntimeProbe>();
		ScriptRuntimeOverride runtimeOverride(runtime);
		Scene lifecycle;
		Entity scripted = lifecycle.CreateEntity("OnDestroy starts audio");
		scripted.AddComponent<AudioSource>().PlayOnStart = false;
		CSharpScriptEntry entry;
		entry.AttachmentID = UUID();
		entry.ScriptAsset = AssetHandle(0xa0d11);
		entry.LastKnownClassName = "Game.AudioStopProbe";
		scripted.AddComponent<CSharpScripts>().Scripts.push_back(entry);
		Require(lifecycle.OnRuntimeStart() && runtime->AttachmentCount == 1,
			"AudioStop lifecycle Scene did not start its managed attachment");
		const AudioVoiceHandle beforeStop = startVoice(scripted);
		Require(beforeStop != 0, "AudioStop lifecycle source did not begin playback");
		AudioVoiceHandle duringDestroy = 0;
		Entity createdDuringDestroy;
		bool callbackSawLiveVoice = false;
		runtime->OnDestroy = [&]
		{
			callbackSawLiveVoice = scripted.HasComponent<AudioSource>()
				&& audio.GetState(beforeStop) == AudioPlaybackState::Playing;
			// The final sweep must be constructed after this callback, not before it.
			createdDuringDestroy = lifecycle.CreateEntity("Audio created by OnDestroy");
			createdDuringDestroy.AddComponent<AudioSource>();
			duringDestroy = startVoice(createdDuringDestroy);
		};
		lifecycle.OnRuntimeStop();
		runtime->OnDestroy = {};
		Require(callbackSawLiveVoice && runtime->DestroyCount == 1 && duringDestroy != 0,
			"AudioStop ran before OnDestroy or prevented its new playback");
		Require(!audio.HasVoice(beforeStop) && !audio.HasVoice(duringDestroy)
			&& reset(scripted.GetComponent<AudioSource>())
			&& reset(createdDuringDestroy.GetComponent<AudioSource>()),
			"Scene Stop left a voice started by OnDestroy alive");
		lifecycle.OnRuntimeStop();
		Require(runtime->DestroyCount == 1, "repeated Scene Stop invoked OnDestroy again");
	}

}
