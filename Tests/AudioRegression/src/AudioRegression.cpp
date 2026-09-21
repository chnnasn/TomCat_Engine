#include "TomCat/Audio/AudioClip.h"
#include "TomCat/Audio/AudioDevice.h"
#include "TomCat/Audio/AudioEngine.h"
#include "TomCat/Audio/AudioSceneRuntime.h"
#include "TomCat/Asset/ContentHash.h"
#include "TomCat/Core/Log.h"
#include "TomCat/Scene/ComponentRegistry.h"
#include "TomCat/Scene/Components.h"
#include "TomCat/Scene/Entity.h"
#include "TomCat/Scene/Scene.h"
#include "TomCat/Scene/SceneSerializer.h"
#include "TomCat/Scene/Serialization/AssetReferenceVisitor.h"
#include "TomCat/Scene/Serialization/PrefabArchiveCodec.h"
#include "TomCat/Scripting/ScriptGlue.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

	void Require(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	void AppendU16(std::vector<uint8_t>& bytes, uint16_t value)
	{
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
	}

	void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
	{
		bytes.push_back(static_cast<uint8_t>(value));
		bytes.push_back(static_cast<uint8_t>(value >> 8));
		bytes.push_back(static_cast<uint8_t>(value >> 16));
		bytes.push_back(static_cast<uint8_t>(value >> 24));
	}

	void AppendFourCC(std::vector<uint8_t>& bytes, const char* value)
	{
		bytes.insert(bytes.end(), value, value + 4);
	}

	std::vector<uint8_t> MakeWave(uint16_t channels = 1,
		uint32_t sampleRate = 8000, uint32_t frames = 800)
	{
		const uint16_t bits = 16;
		const uint16_t blockAlign = channels * bits / 8;
		const uint32_t dataSize = frames * blockAlign;
		std::vector<uint8_t> result;
		AppendFourCC(result, "RIFF"); AppendU32(result, 36 + dataSize);
		AppendFourCC(result, "WAVE"); AppendFourCC(result, "fmt ");
		AppendU32(result, 16); AppendU16(result, 1); AppendU16(result, channels);
		AppendU32(result, sampleRate); AppendU32(result, sampleRate * blockAlign);
		AppendU16(result, blockAlign); AppendU16(result, bits);
		AppendFourCC(result, "data"); AppendU32(result, dataSize);
		for (uint32_t frame = 0; frame < frames; ++frame)
		{
			const int16_t sample = static_cast<int16_t>((frame % 64) * 300 - 9600);
			for (uint16_t channel = 0; channel < channels; ++channel)
				AppendU16(result, static_cast<uint16_t>(sample));
		}
		return result;
	}

	class FailingAudioDevice final : public TomCat::IAudioDevice
	{
	public:
		bool Initialize(std::string& error) override
		{
			m_Inner = TomCat::CreateNullAudioDevice();
			m_Operational = m_Inner && m_Inner->Initialize(error);
			return m_Operational;
		}
		void Shutdown() override
		{
			if (m_Inner) m_Inner->Shutdown();
			m_Inner.reset();
			m_Operational = false;
		}
		bool IsHardwareAvailable() const override { return false; }
		bool IsOperational() const override
		{ return m_Operational && m_Inner && m_Inner->IsOperational(); }
		const char* GetBackendName() const override { return "FailingAudioDevice"; }
		TomCat::AudioVoiceHandle CreateVoice(TomCat::Ref<TomCat::AudioClip> clip,
			const TomCat::AudioVoiceSettings& settings, std::string& error) override
		{ return m_Inner->CreateVoice(std::move(clip), settings, error); }
		TomCat::AudioVoiceHandle CreateStreamingVoice(
			std::unique_ptr<TomCat::IAudioStream> stream,
			const TomCat::AudioVoiceSettings& settings, std::string& error) override
		{ return m_Inner->CreateStreamingVoice(std::move(stream), settings, error); }
		bool DestroyVoice(TomCat::AudioVoiceHandle voice) override
		{
			++m_DestroyVoiceCalls;
			return m_Inner->DestroyVoice(voice);
		}
		bool Play(TomCat::AudioVoiceHandle voice) override { return m_Inner->Play(voice); }
		bool Pause(TomCat::AudioVoiceHandle voice) override { return m_Inner->Pause(voice); }
		bool Stop(TomCat::AudioVoiceHandle voice) override { return m_Inner->Stop(voice); }
		bool SetLoop(TomCat::AudioVoiceHandle voice, bool value) override
		{
			++m_SettingCalls;
			return !m_RejectSettingChanges && m_Inner->SetLoop(voice, value);
		}
		bool SetVolume(TomCat::AudioVoiceHandle voice, float value) override
		{
			++m_SettingCalls;
			return !m_RejectSettingChanges && m_Inner->SetVolume(voice, value);
		}
		bool SetPitch(TomCat::AudioVoiceHandle voice, float value) override
		{
			++m_SettingCalls;
			return !m_RejectSettingChanges && m_Inner->SetPitch(voice, value);
		}
		bool SetSpatial(TomCat::AudioVoiceHandle voice,
			const TomCat::AudioSpatialSettings& value) override
		{
			++m_SettingCalls;
			return !m_RejectSettingChanges && m_Inner->SetSpatial(voice, value);
		}
		TomCat::AudioPlaybackState GetState(TomCat::AudioVoiceHandle voice) const override
		{ return m_Inner->GetState(voice); }
		double GetPlaybackSeconds(TomCat::AudioVoiceHandle voice) const override
		{ return m_Inner->GetPlaybackSeconds(voice); }
		void Update(double deltaSeconds) override
		{
			m_Inner->Update(deltaSeconds);
			if (m_FailNextUpdate) m_Operational = false;
		}
		void FailNextUpdate() { m_FailNextUpdate = true; }
		void RejectSettingChanges(bool reject) { m_RejectSettingChanges = reject; }
		size_t GetSettingCallCount() const { return m_SettingCalls; }
		size_t GetDestroyVoiceCallCount() const { return m_DestroyVoiceCalls; }
	private:
		std::unique_ptr<TomCat::IAudioDevice> m_Inner;
		bool m_Operational = false;
		bool m_FailNextUpdate = false;
		bool m_RejectSettingChanges = false;
		size_t m_SettingCalls = 0;
		size_t m_DestroyVoiceCalls = 0;
	};

	void TestWaveDecode()
	{
		std::string error;
		TomCat::AudioDecodeStatus status{};
		auto clip = TomCat::AudioClip::Decode(MakeWave(2), error, &status, "valid.wav");
		Require(clip && error.empty() && status == TomCat::AudioDecodeStatus::Success,
			"valid WAV did not decode");
		Require(clip->GetChannels() == 2 && clip->GetSampleRate() == 8000
			&& clip->GetBitsPerSample() == 16 && clip->GetFrameCount() == 800,
			"decoded WAV metadata is wrong");
		Require(std::abs(clip->GetDurationSeconds() - 0.1) < 1.0e-9,
			"decoded WAV duration is wrong");

		auto truncated = MakeWave();
		truncated.pop_back();
		Require(!TomCat::AudioClip::Decode(truncated, error, &status)
			&& status == TomCat::AudioDecodeStatus::MalformedData,
			"truncated WAV was accepted");
		const std::vector<uint8_t> ogg{ 'O','g','g','S',0,2,3,4 };
		Require(!TomCat::AudioClip::Decode(ogg, error, &status)
			&& status == TomCat::AudioDecodeStatus::UnsupportedEncoding
			&& error.find("not available") != std::string::npos,
			"OGG must report explicit unsupported encoding");
	}

	void TestNullDeviceStateMachine()
	{
		std::string error;
		auto device = TomCat::CreateNullAudioDevice();
		Require(device && device->Initialize(error) && error.empty(),
			"NullAudioDevice did not initialize");
		auto clip = TomCat::AudioClip::Decode(MakeWave(), error);
		TomCat::AudioVoiceSettings settings;
		settings.Volume = 0.5f; settings.Pitch = 2.0f;
		const auto voice = device->CreateVoice(clip, settings, error);
		Require(voice != 0 && device->Play(voice), "Null voice did not play");
		device->Update(0.02);
		Require(std::abs(device->GetPlaybackSeconds(voice) - 0.04) < 1.0e-9,
			"pitch did not affect Null playback cursor");
		Require(device->Pause(voice), "Null voice did not pause");
		device->Update(1.0);
		Require(std::abs(device->GetPlaybackSeconds(voice) - 0.04) < 1.0e-9,
			"paused Null voice advanced");
		Require(device->Play(voice) && device->SetLoop(voice, true),
			"Null voice did not resume/loop");
		device->Update(0.08);
		Require(device->GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& device->GetPlaybackSeconds(voice) < clip->GetDurationSeconds(),
			"looping Null voice stopped");
		Require(device->SetVolume(voice, 4.0f) && !device->SetVolume(voice, -0.1f)
			&& device->SetPitch(voice, 0.25f) && !device->SetPitch(voice, 0.0f),
			"Null voice parameter validation failed");
		Require(device->Stop(voice)
			&& device->GetState(voice) == TomCat::AudioPlaybackState::Stopped
			&& device->GetPlaybackSeconds(voice) == 0.0,
			"Null voice did not stop/reset");
		device->Shutdown();
	}

	void TestBoundedPcmWaveStreaming()
	{
		std::string error;
		std::vector<uint8_t> wave = MakeWave(2, 48000, 48000 * 90);
		const size_t encodedBytes = wave.size();
		const TomCat::ContentSHA256Digest waveDigest =
			TomCat::ComputeContentSHA256Digest(wave);
		const std::vector<uint8_t> packagePrefix(37, 0xA5);
		const std::filesystem::path path = std::filesystem::temp_directory_path()
			/ ("TomCatAudioRegression-" + std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
		struct Cleanup
		{
			std::filesystem::path Path;
			~Cleanup() { std::error_code ignored; std::filesystem::remove(Path, ignored); }
		} cleanup{ path };
		{
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			Require(static_cast<bool>(output), "long WAV fixture could not be created");
			output.write(reinterpret_cast<const char*>(packagePrefix.data()),
				static_cast<std::streamsize>(packagePrefix.size()));
			output.write(reinterpret_cast<const char*>(wave.data()),
				static_cast<std::streamsize>(wave.size()));
			Require(static_cast<bool>(output), "long WAV fixture could not be written");
		}
		wave.clear();
		wave.shrink_to_fit();
		auto source = TomCat::AudioStreamSource::OpenFileRange(path,
			packagePrefix.size(),
			encodedBytes, error, "long-music.wav", &waveDigest);
		Require(source && error.empty()
			&& source->GetEncodedByteCount() == encodedBytes
			&& source->IsFileBacked() && source->GetResidentByteCount() == 0,
			"long PCM WAV source retained PCM instead of opening a file range");
		const std::streamoff damagedOffset = static_cast<std::streamoff>(
			packagePrefix.size() + encodedBytes / 2);
		auto flipFixtureByte = [&]()
		{
			std::fstream file(path, std::ios::binary | std::ios::in
				| std::ios::out);
			Require(static_cast<bool>(file),
				"long WAV fixture could not be reopened for digest testing");
			file.seekg(damagedOffset);
			char value = 0;
			file.read(&value, 1);
			Require(static_cast<bool>(file),
				"long WAV fixture digest byte could not be read");
			value ^= 0x01;
			file.seekp(damagedOffset);
			file.write(&value, 1);
			file.flush();
			Require(static_cast<bool>(file),
				"long WAV fixture digest byte could not be written");
		};
		flipFixtureByte();
		Require(!TomCat::AudioStreamSource::OpenFileRange(path,
			packagePrefix.size(), encodedBytes, error, "damaged-music.wav",
			&waveDigest)
			&& error.find("SHA-256") != std::string::npos,
			"file-backed audio accepted bytes that no longer matched the tcpak digest");
		flipFixtureByte();
		auto reader = source->CreateReader();
		Require(reader && reader->GetDecoderBufferedByteCount() == 0,
			"PCM stream decoder retained an unexpected decoded history");
		std::vector<uint8_t> chunk(64 * 1024);
		size_t total = 0;
		size_t maximumRead = 0;
		for (;;)
		{
			const size_t read = reader->ReadFrames(chunk);
			if (read == 0) break;
			total += read;
			maximumRead = (std::max)(maximumRead, read);
		}
		Require(total == source->GetFrameCount() * source->GetFormat().BlockAlign
			&& maximumRead <= chunk.size(),
			"PCM stream did not read the long WAV through bounded chunks");
		Require(reader->SeekFrame(0) && reader->GetFramePosition() == 0,
			"PCM stream seek-to-loop boundary failed");

		TomCat::AudioEngine engine;
		Require(engine.Initialize(TomCat::CreateNullAudioDevice(), error),
			"stream test could not initialize NullAudioDevice");
		TomCat::AudioVoiceSettings settings;
		settings.Loop = true;
		const auto voice = engine.CreateStreamingVoice(source,
			TomCat::AudioMixerGroup::Music, settings, error);
		Require(voice != 0 && engine.Play(voice),
			"long PCM WAV streaming voice did not start");
		engine.Update(100.0);
		Require(engine.GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& engine.Pause(voice) && engine.Play(voice),
			"streaming loop/pause/resume state machine failed");

		std::vector<uint8_t> ogg{ 'O','g','g','S',0,2,3,4 };
		Require(!TomCat::AudioStreamSource::Open(std::move(ogg), error)
			&& error.find("not available") != std::string::npos,
			"OGG streaming must remain an explicit unsupported extension point");
	}

	void TestSpatializationMath()
	{
		const TomCat::AudioSpatialSettings right = TomCat::CalculateAudioSpatial2D(
			10.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 20.0f);
		Require(std::abs(right.Pan - 1.0f) < 1.0e-6f
			&& right.DistanceGain > 0.0f && right.DistanceGain < 1.0f,
			"right-side spatial pan/distance attenuation is wrong");
		const TomCat::AudioSpatialSettings twoD = TomCat::CalculateAudioSpatial2D(
			1000.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 20.0f);
		Require(twoD.Pan == 0.0f && twoD.DistanceGain == 1.0f,
			"SpatialBlend zero must preserve transform-independent 2D audio");

		std::vector<float> matrix;
		Require(TomCat::BuildAudioOutputMatrix(2, 2, twoD, matrix)
			&& matrix == std::vector<float>({ 1.0f, 0.0f, 0.0f, 1.0f }),
			"2D stereo routing must preserve an identity channel matrix");

		TomCat::AudioSpatialSettings hardRight;
		hardRight.Pan = 1.0f;
		hardRight.DistanceGain = 0.25f;
		Require(TomCat::BuildAudioOutputMatrix(1, 2, hardRight, matrix)
			&& std::abs(matrix[0]) < 1.0e-6f
			&& std::abs(matrix[1] - 0.25f) < 1.0e-6f,
			"mono point-source spatialization did not route to the correct channel");
		Require(TomCat::BuildAudioOutputMatrix(2, 2, hardRight, matrix)
			&& matrix == std::vector<float>({ 0.25f, 0.0f, 0.0f, 0.25f }),
			"stereo spatial routing collapsed authored channels instead of preserving them");

		matrix = { 9.0f };
		Require(!TomCat::BuildAudioOutputMatrix(0, 2, twoD, matrix)
			&& matrix.empty(),
			"invalid audio matrix dimensions were not rejected transactionally");
	}

	void TestDeviceLossRecovery()
	{
		std::string error;
		auto injected = std::make_unique<FailingAudioDevice>();
		FailingAudioDevice* failure = injected.get();
		TomCat::AudioEngine engine;
		Require(engine.Initialize(std::move(injected), error),
			"failure-injection audio device did not initialize");
		auto clip = TomCat::AudioClip::Decode(MakeWave(1, 8000, 8000), error);
		auto stream = TomCat::AudioStreamSource::Open(
			MakeWave(1, 8000, 8000 * 2), error, "recovery-stream.wav");
		TomCat::AudioVoiceSettings settings;
		settings.Loop = true;
		const auto voice = engine.CreateVoice(clip, TomCat::AudioMixerGroup::Music,
			settings, error);
		const auto streamVoice = engine.CreateStreamingVoice(stream,
			TomCat::AudioMixerGroup::Music, settings, error);
		Require(voice != 0 && streamVoice != 0
			&& engine.Play(voice) && engine.Play(streamVoice),
			"device-loss fixture voice did not start");
		const uint64_t generation = engine.GetDeviceGeneration();
		failure->FailNextUpdate();
		engine.Update(0.01);
		Require(engine.GetDeviceGeneration() == generation + 1
			&& std::string(engine.GetBackendName()) == "NullAudioDevice"
			&& engine.HasVoice(voice)
			&& engine.HasVoice(streamVoice)
			&& engine.GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& engine.GetState(streamVoice) == TomCat::AudioPlaybackState::Playing,
			"device loss did not rebuild active clip and streaming voices on NullAudioDevice");
	}

	void TestNoDeviceEngineAndMixers()
	{
		std::string error;
		TomCat::AudioEngine engine;
		Require(engine.Initialize(TomCat::CreateNullAudioDevice(), error),
			"AudioEngine rejected NullAudioDevice");
		Require(!engine.IsHardwareAvailable()
			&& std::string(engine.GetBackendName()) == "NullAudioDevice",
			"headless audio state is wrong");
		auto clip = TomCat::AudioClip::Decode(MakeWave(), error);
		TomCat::AudioVoiceSettings settings;
		const auto voice = engine.CreateVoice(clip, TomCat::AudioMixerGroup::Music,
			settings, error);
		Require(voice != 0 && engine.SetMixerVolume(TomCat::AudioMixerGroup::Master, 0.5f)
			&& engine.SetMixerVolume(TomCat::AudioMixerGroup::Music, 0.25f)
			&& engine.Play(voice), "headless mixer/voice startup failed");
		engine.Update(0.2);
		Require(engine.GetState(voice) == TomCat::AudioPlaybackState::Stopped,
			"non-looping Null engine voice did not finish");
	}

	void TestDefaultBackendColdStart()
	{
		std::string fallbackReason;
		auto device = TomCat::CreateDefaultAudioDevice(fallbackReason);
		Require(device != nullptr, "default audio factory returned no device");
		if (device->IsHardwareAvailable())
			Require(std::string(device->GetBackendName()) == "Windows XAudio2"
				&& fallbackReason.empty(), "hardware backend identity is wrong");
		else
			Require(std::string(device->GetBackendName()) == "NullAudioDevice"
				&& !fallbackReason.empty(), "no-device fallback did not explain itself");
		device->Shutdown();
	}

	void TestScenePrefabAndReferenceRoundTrip()
	{
		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntity("AudioEntity");
		auto& source = entity.AddComponent<TomCat::AudioSource>();
		source.Clip = TomCat::AssetHandle(4242);
		source.Enabled = false; source.PlayOnStart = false; source.Loop = true;
		source.Volume = 0.75f; source.Pitch = 1.25f; source.MixerGroup = 1;
		source.Streaming = true; source.SpatialBlend = 0.6f;
		source.MinDistance = 2.0f; source.MaxDistance = 40.0f;
		auto& listener = entity.AddComponent<TomCat::AudioListener>();
		listener.Enabled = true; listener.Primary = false;

		std::string document, error;
		Require(TomCat::SceneSerializer(scene).SerializeDocument(document, error),
			"Audio scene did not serialize");
		Require(document.find("AudioSource") != std::string::npos
			&& document.find("AudioListener") != std::string::npos,
			"Audio components are missing from scene document");
		auto decoded = TomCat::CreateRef<TomCat::Scene>();
		const std::vector<uint8_t> bytes(document.begin(), document.end());
		Require(TomCat::SceneSerializer(decoded).DeserializeDocument(bytes,
			"AudioRegression.tcscene", false), "Audio scene did not deserialize");
		TomCat::Entity loaded = decoded->FindEntityByUUID(entity.GetUUID());
		Require(loaded.HasComponent<TomCat::AudioSource>()
			&& loaded.HasComponent<TomCat::AudioListener>(),
			"Audio components did not round-trip");
		const auto& loadedSource = loaded.GetComponent<TomCat::AudioSource>();
		Require(static_cast<uint64_t>(loadedSource.Clip) == 4242
			&& loadedSource.Loop && loadedSource.MixerGroup == 1
			&& loadedSource.Streaming
			&& std::abs(loadedSource.SpatialBlend - 0.6f) < 1.0e-6f
			&& loadedSource.MinDistance == 2.0f
			&& loadedSource.MaxDistance == 40.0f
			&& loadedSource.RuntimeVoice == 0,
			"AudioSource authoring/runtime fields round-tripped incorrectly");

		TomCat::PrefabArchive archive;
		Require(TomCat::PrefabArchiveCodec::CaptureSubtree(scene, entity, archive, error),
			"Audio prefab capture failed");
		std::string prefab;
		Require(TomCat::PrefabArchiveCodec::Encode(archive, prefab, error),
			"Audio prefab encode failed");
		TomCat::PrefabArchive decodedPrefab;
		Require(TomCat::PrefabArchiveCodec::Decode(
			std::vector<uint8_t>(prefab.begin(), prefab.end()),
			"AudioRegression.tcprefab", decodedPrefab, error),
			"Audio prefab decode failed");
		TomCat::Entity prefabEntity = decodedPrefab.TemplateScene->FindEntityByUUID(
			TomCat::UUID(decodedPrefab.RootLocalID));
		Require(prefabEntity.HasComponent<TomCat::AudioSource>()
			&& static_cast<uint64_t>(prefabEntity.GetComponent<TomCat::AudioSource>().Clip)
				== 4242, "Audio prefab component did not round-trip");

		const YAML::Node root = YAML::Load(document);
		bool visited = false;
		Require(TomCat::AssetReferenceVisitor::VisitScene(root,
			[&](const TomCat::SerializedAssetReference& reference)
			{
				if (reference.Kind == TomCat::SerializedAssetReferenceKind::AudioSource)
				{
					visited = reference.ExpectedType == TomCat::AssetType::Audio
						&& static_cast<uint64_t>(reference.Handle) == 4242;
				}
				return true;
			}, error) && visited, "Cook traversal missed AudioSource.Clip");
	}

	void TestCapabilityTable()
	{
		TomCat::Scripting::NativeApiV2 envelope =
			TomCat::Scripting::BuildNativeApiV2();
		Require(envelope.V1.Size == sizeof(TomCat::Scripting::NativeApiV2),
			"NativeApiV2 envelope size is wrong");
		const std::string name(TomCat::Scripting::AudioCapabilityName);
		TomCat::Scripting::NativeAudioApiV1 audio{};
		uint32_t required = 0;
		const TomCat::Scripting::NativeUtf8View view{
			reinterpret_cast<const uint8_t*>(name.data()), name.size() };
		Require(envelope.QueryCapability(view, 1, &audio, sizeof(audio), &required) == 0
			&& required == sizeof(audio) && audio.Version == 1
			&& audio.Play && audio.SetVolume && audio.HasListener
			&& audio.SetMixerVolume, "TomCat.AudioApiV1 query/table failed");
		Require(envelope.QueryCapability(view, 2, &audio, sizeof(audio), &required)
			== static_cast<int32_t>(TomCat::Scripting::ScriptStatus::VersionMismatch),
			"audio capability version negotiation failed");
		const std::string spatialName(
			TomCat::Scripting::AudioSpatialCapabilityName);
		TomCat::Scripting::NativeAudioSpatialApiV1 spatial{};
		const TomCat::Scripting::NativeUtf8View spatialView{
			reinterpret_cast<const uint8_t*>(spatialName.data()), spatialName.size() };
		Require(envelope.QueryCapability(spatialView, 1, &spatial, sizeof(spatial),
			&required) == 0 && required == sizeof(spatial)
			&& spatial.Version == 1 && spatial.GetStreaming
			&& spatial.SetSpatialBlend && spatial.SetMaxDistance,
			"TomCat.AudioSpatialApiV1 query/table failed");
	}

	void TestTransactionalAudioComponentMutation()
	{
		TomCat::AudioEngine& engine = TomCat::AudioEngine::Get();
		engine.Shutdown();
		struct EngineShutdown
		{
			TomCat::AudioEngine& Engine;
			~EngineShutdown() { Engine.Shutdown(); }
		} shutdown{ engine };

		std::string error;
		auto injected = std::make_unique<FailingAudioDevice>();
		FailingAudioDevice* device = injected.get();
		Require(engine.Initialize(std::move(injected), error),
			"transaction test could not initialize its audio device");
		auto clip = TomCat::AudioClip::Decode(MakeWave(1, 8000, 8000), error);
		Require(clip != nullptr, "transaction test audio clip did not decode");
		TomCat::AudioVoiceSettings settings;
		const TomCat::AudioVoiceHandle voice = engine.CreateVoice(clip,
			TomCat::AudioMixerGroup::SFX, settings, error);
		Require(voice != 0 && engine.Play(voice),
			"transaction test live voice did not start");

		auto scene = TomCat::CreateRef<TomCat::Scene>();
		TomCat::Entity entity = scene->CreateEntity("Transactional audio");
		auto& source = entity.AddComponent<TomCat::AudioSource>();
		source.Clip = TomCat::AssetHandle(7001);
		source.RuntimeClipHandle = source.Clip;
		source.RuntimeVoice = voice;
		source.RuntimeAutoPlayEvaluated = true;

		const TomCat::ComponentDescriptor* descriptor =
			TomCat::ComponentRegistry::Get().Find(
				TomCat::UUID(TomCat::ComponentIds::AudioSource));
		Require(descriptor && descriptor->Remove,
			"AudioSource descriptor is unavailable");
		auto findProperty = [&](uint64_t propertyId)
			-> const TomCat::PropertyDescriptor&
		{
			const auto found = std::find_if(descriptor->Properties.begin(),
				descriptor->Properties.end(),
				[propertyId](const TomCat::PropertyDescriptor& property)
				{
					return static_cast<uint64_t>(property.PropertyId) == propertyId;
				});
			Require(found != descriptor->Properties.end(),
				"AudioSource property is unavailable");
			return *found;
		};
		const auto& loopProperty = findProperty(
			TomCat::ComponentIds::AudioSourceProperties::Loop);
		const auto& volumeProperty = findProperty(
			TomCat::ComponentIds::AudioSourceProperties::Volume);
		const TomCat::PropertyValue loopEnabled = true;
		const TomCat::PropertyValue invalidVolume = -1.0f;

		TomCat::Ref<TomCat::Scene> staged;
		{
			TomCat::ComponentMutationPhaseScope validation(
				TomCat::ComponentMutationPhase::Validation);
			staged = TomCat::Scene::Copy(scene);
		}
		Require(staged != nullptr, "AudioSource validation snapshot failed");
		TomCat::Entity stagedEntity = staged->FindEntityByUUID(entity.GetUUID());
		Require(stagedEntity
			&& stagedEntity.GetComponent<TomCat::AudioSource>().RuntimeVoice == 0,
			"AudioSource validation snapshot retained a live voice");
		{
			TomCat::ComponentMutationPhaseScope validation(
				TomCat::ComponentMutationPhase::Validation);
			error.clear();
			Require(loopProperty.Set(stagedEntity, loopEnabled, error),
				"valid staged AudioSource property was rejected");
			error.clear();
			Require(!volumeProperty.Set(stagedEntity, invalidVolume, error),
				"invalid staged AudioSource property was accepted");
		}
		Require(!source.Loop && source.RuntimeVoice == voice
			&& engine.HasVoice(voice)
			&& engine.GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& device->GetSettingCallCount() == 0
			&& device->GetDestroyVoiceCallCount() == 0,
			"aborted AudioSource property validation changed live ECS or voice state");

		TomCat::Ref<TomCat::Scene> stagedRemoval;
		{
			TomCat::ComponentMutationPhaseScope validation(
				TomCat::ComponentMutationPhase::Validation);
			stagedRemoval = TomCat::Scene::Copy(scene);
			Require(stagedRemoval != nullptr,
				"AudioSource removal validation snapshot failed");
			TomCat::Entity stagedRemovalEntity =
				stagedRemoval->FindEntityByUUID(entity.GetUUID());
			error.clear();
			Require(descriptor->Remove(stagedRemovalEntity, error),
				"staged AudioSource removal failed");
		}
		Require(entity.HasComponent<TomCat::AudioSource>()
			&& source.RuntimeVoice == voice && engine.HasVoice(voice)
			&& engine.GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& device->GetDestroyVoiceCallCount() == 0,
			"aborted AudioSource removal changed live ECS or voice state");

		device->RejectSettingChanges(true);
		error.clear();
		Require(loopProperty.Set(entity, loopEnabled, error) && source.Loop
			&& device->GetSettingCallCount() == 0,
			"AudioSource live replay consulted the audio backend");
		TomCat::AudioSceneRuntime::Update(*scene, 0.0);
		Require(device->GetSettingCallCount() > 0 && engine.HasVoice(voice)
			&& engine.GetState(voice) == TomCat::AudioPlaybackState::Playing,
			"post-commit AudioSource reconciliation did not report/retry backend settings");

		const size_t destroysBeforeRemove = device->GetDestroyVoiceCallCount();
		error.clear();
		Require(descriptor->Remove(entity, error)
			&& !entity.HasComponent<TomCat::AudioSource>()
			&& engine.HasVoice(voice)
			&& engine.GetState(voice) == TomCat::AudioPlaybackState::Playing
			&& device->GetDestroyVoiceCallCount() == destroysBeforeRemove,
			"AudioSource removal destroyed its voice during ECS replay");
		TomCat::AudioSceneRuntime::Update(*scene, 0.0);
		Require(!engine.HasVoice(voice)
			&& device->GetDestroyVoiceCallCount() == destroysBeforeRemove + 1,
			"AudioSource deferred voice destroy was not reconciled after commit");
	}

	void TestInactiveHierarchyStopsAudio()
	{
		TomCat::Scene scene;
		TomCat::Entity parent = scene.CreateEntity("Disabled audio parent");
		TomCat::Entity child = scene.CreateEntity("Audio child");
		Require(scene.SetParent(child, parent),
			"could not create audio hierarchy fixture");
		auto& source = child.AddComponent<TomCat::AudioSource>();
		source.Clip = TomCat::AssetHandle(777);
		source.RuntimeClipHandle = source.Clip;
		source.RuntimeVoice = 123;
		source.RuntimeAutoPlayEvaluated = true;
		parent.GetComponent<TomCat::Tag>().ActiveSelf = false;
		Require(!scene.IsActiveInHierarchy(child),
			"disabled parent did not deactivate child");
		TomCat::AudioSceneRuntime::Update(scene, 0.0);
		Require(source.RuntimeVoice == 0
			&& static_cast<uint64_t>(source.RuntimeClipHandle) == 0
			&& !source.RuntimeAutoPlayEvaluated,
			"inactive hierarchy did not stop and reset AudioSource");
	}

}

int main()
{
	try
	{
		TomCat::Log::Init();
		TestWaveDecode();
		TestNullDeviceStateMachine();
		TestBoundedPcmWaveStreaming();
		TestSpatializationMath();
		TestDeviceLossRecovery();
		TestNoDeviceEngineAndMixers();
		TestDefaultBackendColdStart();
		TestScenePrefabAndReferenceRoundTrip();
		TestCapabilityTable();
		TestTransactionalAudioComponentMutation();
		TestInactiveHierarchyStopsAudio();
		std::cout << "AudioRegression: PASS\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "AudioRegression: FAIL: " << error.what() << '\n';
		return 1;
	}
}
