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
    public static void ConfigureBuses(string yamlOrJson) => NativeBridge.ConfigureAudioBuses(yamlOrJson);
    public static void SetBus(uint id,float volume=1,bool muted=false,bool solo=false) => NativeBridge.SetAudioBus(id,volume,muted,solo);
    public static void SetMuted(AudioMixerGroup group, bool muted) => NativeBridge.MixerMute(group, muted);
    public static void SetSolo(AudioMixerGroup group, bool solo) => NativeBridge.MixerSolo(group, solo);
    public static void ApplySnapshot(float master, float music, float sfx, double fadeSeconds = 0, uint mutedMask = 0, uint soloMask = 0) => NativeBridge.MixerSnapshot(master,music,sfx,mutedMask,soloMask,fadeSeconds);
    public static void SetDucking(AudioMixerGroup trigger, AudioMixerGroup target, float gain, double attackSeconds = .05, double releaseSeconds = .3) => NativeBridge.MixerDuck(trigger,target,gain,attackSeconds,releaseSeconds);
    public static void ClearDucking() => NativeBridge.MixerClearDucking();
	public static bool IsHardwareAvailable => NativeBridge.AudioHardwareAvailable();
	public static string BackendName => NativeBridge.AudioBackendName();

	public static float GetMixerVolume(AudioMixerGroup group) =>
		NativeBridge.AudioGetMixerVolume(group);

	public static void SetMixerVolume(AudioMixerGroup group, float volume) =>
		NativeBridge.AudioSetMixerVolume(group, volume);
}
