namespace TomCat;

public enum AudioPlaybackState
{
	Stopped = 0,
	Playing = 1,
	Paused = 2
}

public enum AudioMixerGroup
{
	Master = 0,
	Music = 1,
	SFX = 2
}

/// <summary>Entity-bound 2D audio playback.</summary>
public sealed unsafe class AudioSource : IEntityComponent
{
	internal AudioSource(Entity entity) => Entity = entity;
	public Entity Entity { get; }

	public AssetRef<AudioAsset> Clip
	{
		get => new(NativeBridge.AudioGetClip(Entity));
		set => NativeBridge.AudioSetClip(Entity, value.Handle);
	}

	public bool Enabled
	{
		get => NativeBridge.AudioGetSourceBool(Entity, NativeBridge.AudioGetEnabled,
			"AudioSource.Enabled");
		set => NativeBridge.AudioSetSourceBool(Entity, value,
			NativeBridge.AudioSetEnabled, "AudioSource.Enabled");
	}

	public bool PlayOnStart
	{
		get => NativeBridge.AudioGetSourceBool(Entity,
			NativeBridge.AudioGetPlayOnStart, "AudioSource.PlayOnStart");
		set => NativeBridge.AudioSetSourceBool(Entity, value,
			NativeBridge.AudioSetPlayOnStart, "AudioSource.PlayOnStart");
	}

	public bool Loop
	{
		get => NativeBridge.AudioGetSourceBool(Entity, NativeBridge.AudioGetLoop,
			"AudioSource.Loop");
		set => NativeBridge.AudioSetSourceBool(Entity, value,
			NativeBridge.AudioSetLoop, "AudioSource.Loop");
	}

	/// <summary>
	/// Streams PCM WAV through a bounded native queue instead of decoding the
	/// complete clip into a second in-memory PCM buffer.
	/// </summary>
	public bool Streaming
	{
		get => NativeBridge.AudioGetSpatialBool(Entity,
			NativeBridge.AudioGetStreaming, "AudioSource.Streaming");
		set => NativeBridge.AudioSetSpatialBool(Entity, value,
			NativeBridge.AudioSetStreaming, "AudioSource.Streaming");
	}

	public float Volume
	{
		get => NativeBridge.AudioGetSourceFloat(Entity, NativeBridge.AudioGetVolume,
			"AudioSource.Volume");
		set => NativeBridge.AudioSetSourceFloat(Entity, value,
			NativeBridge.AudioSetVolume, "AudioSource.Volume");
	}

	public float Pitch
	{
		get => NativeBridge.AudioGetSourceFloat(Entity, NativeBridge.AudioGetPitch,
			"AudioSource.Pitch");
		set => NativeBridge.AudioSetSourceFloat(Entity, value,
			NativeBridge.AudioSetPitch, "AudioSource.Pitch");
	}

	public float SpatialBlend
	{
		get => NativeBridge.AudioGetSpatialFloat(Entity,
			NativeBridge.AudioGetSpatialBlend, "AudioSource.SpatialBlend");
		set => NativeBridge.AudioSetSpatialFloat(Entity, value,
			NativeBridge.AudioSetSpatialBlend, "AudioSource.SpatialBlend");
	}

	public float MinDistance
	{
		get => NativeBridge.AudioGetSpatialFloat(Entity,
			NativeBridge.AudioGetMinDistance, "AudioSource.MinDistance");
		set => NativeBridge.AudioSetSpatialFloat(Entity, value,
			NativeBridge.AudioSetMinDistance, "AudioSource.MinDistance");
	}

	public float MaxDistance
	{
		get => NativeBridge.AudioGetSpatialFloat(Entity,
			NativeBridge.AudioGetMaxDistance, "AudioSource.MaxDistance");
		set => NativeBridge.AudioSetSpatialFloat(Entity, value,
			NativeBridge.AudioSetMaxDistance, "AudioSource.MaxDistance");
	}

	/// <summary>Current world-space source position from the entity Transform.</summary>
	public Vector3 Position => Entity.GetComponent<Transform>().Position;

	public AudioMixerGroup MixerGroup
	{
		get => NativeBridge.AudioGetMixerGroup(Entity);
		set => NativeBridge.AudioSetMixerGroup(Entity, value);
	}

	public AudioPlaybackState State => NativeBridge.AudioGetPlaybackState(Entity);
	public bool IsPlaying => State == AudioPlaybackState.Playing;

	public void Play() => NativeBridge.AudioSourceCommand(Entity,
		NativeBridge.AudioPlay, "AudioSource.Play");
	public void Pause() => NativeBridge.AudioSourceCommand(Entity,
		NativeBridge.AudioPause, "AudioSource.Pause");
	public void Stop() => NativeBridge.AudioSourceCommand(Entity,
		NativeBridge.AudioStop, "AudioSource.Stop");
}

/// <summary>Marks the transform used as the primary audio-listener origin.</summary>
public sealed unsafe class AudioListener : IEntityComponent
{
	internal AudioListener(Entity entity) => Entity = entity;
	public Entity Entity { get; }

	public bool Enabled
	{
		get => NativeBridge.AudioGetSourceBool(Entity,
			NativeBridge.AudioGetListenerEnabled, "AudioListener.Enabled");
		set => NativeBridge.AudioSetSourceBool(Entity, value,
			NativeBridge.AudioSetListenerEnabled, "AudioListener.Enabled");
	}

	public bool Primary
	{
		get => NativeBridge.AudioGetSourceBool(Entity,
			NativeBridge.AudioGetListenerPrimary, "AudioListener.Primary");
		set => NativeBridge.AudioSetSourceBool(Entity, value,
			NativeBridge.AudioSetListenerPrimary, "AudioListener.Primary");
	}

	public Vector3 Position => Entity.GetComponent<Transform>().Position;

	/// <summary>World-space 2D forward direction derived from Transform Z rotation.</summary>
	public Vector3 Forward
	{
		get
		{
			float radians = Entity.GetComponent<Transform>().RotationEuler.Z;
			return new Vector3(-MathF.Sin(radians), MathF.Cos(radians), 0.0f);
		}
	}
}

public static class AudioSystem
{
	public static bool IsHardwareAvailable => NativeBridge.AudioHardwareAvailable();
	public static string BackendName => NativeBridge.AudioBackendName();

	public static float GetMixerVolume(AudioMixerGroup group) =>
		NativeBridge.AudioGetMixerVolume(group);

	public static void SetMixerVolume(AudioMixerGroup group, float volume) =>
		NativeBridge.AudioSetMixerVolume(group, volume);
}
