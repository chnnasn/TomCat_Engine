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
public sealed unsafe partial class AudioSource
{
	/// <summary>Current world-space source position from the entity Transform.</summary>
	public Vector3 Position => Entity.GetComponent<Transform>().Position;

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
public sealed unsafe partial class AudioListener
{
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
