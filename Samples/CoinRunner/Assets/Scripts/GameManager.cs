using System;
using System.Globalization;
using TomCat;

/// <summary>
/// CoinRunner game state: counts collected coins, drives the score text,
/// persists progress through the engine save system and ends headless demo
/// runs so CI can verify the full package loop.
/// </summary>
public sealed class GameManager : MonoBehaviour
{
    private const string SaveSlot = "progress";
    private const uint SaveVersion = 1;
    private const double HeadlessIdleLimitSeconds = 6.0;
    private const double HeadlessWinQuitDelaySeconds = 0.5;

    private static GameManager? _instance;

    public static GameManager? Instance => _instance;

    public long BestScore { get; private set; }
    public long Runs { get; private set; }
    public bool Won { get; private set; }

    private int _collected;
    private int _totalCoins;
    private UIText? _scoreText;
    private double _headlessElapsed;
    private double _winElapsed;
    private bool _quitRequested;

    private void Awake()
    {
        _instance = this;
        _totalCoins = CountCoins();
        _scoreText = FindScoreText();
        LoadProgress();
        UpdateScoreText();
        Log.Info("[CoinRunner] started: " + _totalCoins + " coins, high score "
            + BestScore.ToString(CultureInfo.InvariantCulture) + ", runs "
            + Runs.ToString(CultureInfo.InvariantCulture));
    }

    private void Update()
    {
        var deltaTime = Time.deltaTime;
        UpdateScoreText();
        if (Application.HasWindow)
            return;

        // Headless runs (used by the packaging smoke test) must terminate
        // themselves: quit shortly after winning, or after an idle limit.
        _headlessElapsed += deltaTime;
        if (Won)
        {
            _winElapsed += deltaTime;
            if (!_quitRequested && _winElapsed >= HeadlessWinQuitDelaySeconds)
            {
                _quitRequested = true;
                Application.Quit(0);
            }
        }
        else if (!_quitRequested && _headlessElapsed >= HeadlessIdleLimitSeconds)
        {
            Log.Error("[CoinRunner] headless run timed out before winning");
            _quitRequested = true;
            Application.Quit(1);
        }
    }

    private void OnDestroy()
    {
        // Persist every run, even ones that never finished.
        SaveProgress();
        if (_instance == this)
            _instance = null;
    }

    public void AddScore()
    {
        _collected++;
        if (_collected >= _totalCoins && _totalCoins > 0 && !Won)
        {
            Won = true;
            Log.Info("[CoinRunner] WIN: all " + _totalCoins + " coins collected");
            SaveProgress();
        }
    }

    /// <summary>World position of the closest remaining coin, for the demo
    /// driver. Returns false when every coin was collected.</summary>
    public bool TryGetNextCoinPosition(out Vector2 position)
    {
        position = default;
        Entity? player = World.Find("Player");
        if (player is null)
            return false;
        Transform playerTransform = player.GetComponent<Transform>();
        Vector2 playerPosition = new(playerTransform.Position.X,
            playerTransform.Position.Y);
        bool found = false;
        float bestDistance = float.MaxValue;
        foreach (Entity candidate in World.All)
        {
            if (!candidate.Name.StartsWith("Coin", StringComparison.Ordinal))
                continue;
            Transform coinTransform = candidate.GetComponent<Transform>();
            Vector2 coinPosition = new(coinTransform.Position.X,
                coinTransform.Position.Y);
            float dx = coinPosition.X - playerPosition.X;
            float dy = coinPosition.Y - playerPosition.Y;
            float distance = dx * dx + dy * dy;
            if (distance < bestDistance)
            {
                bestDistance = distance;
                position = coinPosition;
                found = true;
            }
        }
        return found;
    }

    private void LoadProgress()
    {
        SaveDocument? document = SaveData.ReadDocument(SaveSlot);
        if (document is null)
        {
            Log.Info("[CoinRunner] no previous save; starting fresh");
            return;
        }
        if (document.Version != SaveVersion)
        {
            // A future format: keep the file untouched rather than guessing.
            Log.Error("[CoinRunner] save slot '" + SaveSlot + "' has version "
                + document.Version + "; expected " + SaveVersion);
            return;
        }
        BestScore = document.TryGetLong("BestScore", out long best) ? best : 0;
        Runs = document.TryGetLong("Runs", out long runs) ? runs : 0;
        Runs++;
    }

    private void SaveProgress()
    {
        if (Won && _collected > BestScore)
            BestScore = _collected;
        var document = new SaveDocument { Version = SaveVersion };
        document.Set("BestScore", BestScore);
        document.Set("Runs", Runs);
        document.Set("Coins", (long)_collected);
        try
        {
            SaveData.Write(SaveSlot, document);
            Log.Info("[CoinRunner] saved: best "
                + BestScore.ToString(CultureInfo.InvariantCulture) + ", runs "
                + Runs.ToString(CultureInfo.InvariantCulture));
        }
        catch (TomCatException error)
        {
            Log.Error("[CoinRunner] save failed: " + error.Message);
        }
    }

    private void UpdateScoreText()
    {
        if (_scoreText is null)
            return;
        string text = Won
            ? "You Win!"
            : "Score " + _collected.ToString(CultureInfo.InvariantCulture) + " / "
                + _totalCoins.ToString(CultureInfo.InvariantCulture)
                + "   Best " + BestScore.ToString(CultureInfo.InvariantCulture);
        if (_scoreText.Text != text)
            _scoreText.Text = text;
    }

    private int CountCoins()
    {
        int count = 0;
        foreach (Entity entity in World.All)
        {
            if (entity.Name.StartsWith("Coin", StringComparison.Ordinal))
                count++;
        }
        return count;
    }

    private UIText? FindScoreText()
    {
        Entity? textEntity = World.Find("ScoreText");
        if (textEntity is null || !textEntity.TryGetComponent(out UIText text))
            return null;
        return text;
    }
}
