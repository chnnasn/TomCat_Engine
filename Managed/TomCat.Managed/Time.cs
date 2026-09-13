namespace TomCat;

/// <summary>Frame and fixed-step timing for the active Play scene.</summary>
public static class Time
{
	public static float DeltaTime => TimeRuntime.DeltaTime;
	public static float FixedDeltaTime => TimeRuntime.FixedDeltaTime;
	public static double ElapsedTime => TimeRuntime.ElapsedTime;
	public static double FixedElapsedTime => TimeRuntime.FixedElapsedTime;
	public static ulong FrameCount => TimeRuntime.FrameCount;
	public static ulong FixedFrameCount => TimeRuntime.FixedFrameCount;
	public static bool InFixedUpdate => TimeRuntime.InFixedUpdate;
}

// The managed host owns the clock. User code receives a read-only view and
// therefore cannot make timing differ between behaviours in the same frame.
internal static class TimeRuntime
{
	internal static float DeltaTime { get; private set; }
	internal static float FixedDeltaTime { get; private set; } = 1.0f / 60.0f;
	internal static double ElapsedTime { get; private set; }
	internal static double FixedElapsedTime { get; private set; }
	internal static ulong FrameCount { get; private set; }
	internal static ulong FixedFrameCount { get; private set; }
	internal static bool InFixedUpdate { get; private set; }

	internal static void BeginFrame(float deltaTime)
	{
		DeltaTime = deltaTime;
		ElapsedTime += deltaTime;
		FrameCount++;
		InFixedUpdate = false;
	}

	internal static void BeginFixedStep(float fixedDeltaTime)
	{
		FixedDeltaTime = fixedDeltaTime;
		FixedElapsedTime += fixedDeltaTime;
		FixedFrameCount++;
		InFixedUpdate = true;
	}

	internal static void EndFixedStep() => InFixedUpdate = false;
}
