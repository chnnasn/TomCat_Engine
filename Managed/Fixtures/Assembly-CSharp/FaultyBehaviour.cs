using TomCat;

namespace Game;

[DefaultExecutionOrder(100)]
public sealed class FaultyBehaviour : TomCatBehaviour
{
    public int Creates;
    public int Enables;
    public int Updates;
    public int Destroys;

    protected override void OnCreate() => Creates++;
    protected override void OnEnable() => Enables++;

    protected override void OnUpdate(float deltaTime)
    {
        Updates++;
        throw new InvalidOperationException("fixture failure");
    }

    protected override void OnDestroy() => Destroys++;
}
