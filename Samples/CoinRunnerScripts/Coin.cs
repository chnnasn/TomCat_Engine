using TomCat;

/// <summary>
/// A collectible coin. The circle trigger reports contact with the player;
/// the coin increments the game score and removes itself.
/// </summary>
public sealed class Coin : MonoBehaviour
{
    private void OnTriggerEnter2D(Trigger2D trigger)
    {
        Entity other = trigger.Self.Id == Entity.Id
            ? trigger.Other
            : trigger.Self;
        if (other.Name != "Player")
            return;
        GameManager.Instance?.AddScore();
        Entity.Destroy();
    }
}
