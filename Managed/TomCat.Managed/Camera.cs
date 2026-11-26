namespace TomCat;

public sealed partial class Camera
{
    /// <summary>
    /// Half of <see cref="OrthographicSize"/>: the distance from the camera to the
    /// top (or bottom) edge of the view in world units. Use this when porting
    /// Unity or Godot values, whose orthographic size is already half height.
    /// </summary>
    public float OrthographicHalfHeight
    {
        get => OrthographicSize * 0.5f;
        set => OrthographicSize = value * 2.0f;
    }
}
