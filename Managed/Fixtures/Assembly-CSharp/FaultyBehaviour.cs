using TomCat;

namespace Game;

[DefaultExecutionOrder(100)]
public sealed class FaultyBehaviour : MonoBehaviour
{
    public int Creates;
    public int Enables;
    public int Updates;
    public int Destroys;

    private void Awake() => Creates++;
    private void OnEnable() => Enables++;

    private void Update()
    { var deltaTime = Time.deltaTime;
        Updates++;
        throw new InvalidOperationException("fixture failure");
    }

    private void OnDestroy() => Destroys++;
}
