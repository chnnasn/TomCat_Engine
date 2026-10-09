using System;
using TomCat;

/// <summary>
/// Drives the player body. With a window the player is moved with A/D or the
/// arrow keys and Space to jump. In headless runs (packaging smoke test) the
/// controller auto-drives toward the closest remaining coin so the whole loop
/// - physics, triggers, scoring, saving and shutdown - is exercised without
/// user input.
/// </summary>
public sealed class PlayerController : MonoBehaviour
{
    public float MoveSpeed = 3.5f;
    private const float JumpSpeed = 6.5f;

    private Rigidbody2D _body = null!;
    private bool _headless;
    private float _groundedCooldown;

    private void Awake()
    {
        _body = GetComponent<Rigidbody2D>();
        _headless = !Application.HasWindow;
    }

    private void FixedUpdate()
    {
        var fixedDeltaTime = Time.fixedDeltaTime;
        _groundedCooldown = MathF.Max(0.0f, _groundedCooldown - fixedDeltaTime);
        if (_headless)
        {
            UpdateDemo(fixedDeltaTime);
            return;
        }

        float direction = 0.0f;
        if (Input.GetKey(KeyCode.A) || Input.GetKey(KeyCode.Left))
            direction -= 1.0f;
        if (Input.GetKey(KeyCode.D) || Input.GetKey(KeyCode.Right))
            direction += 1.0f;
        float vertical = _body.LinearVelocity.Y;
        if ((Input.GetKey(KeyCode.Space) || Input.GetKey(KeyCode.W)
            || Input.GetKey(KeyCode.Up)) && _groundedCooldown <= 0.0f)
        {
            vertical = JumpSpeed;
            _groundedCooldown = 0.25f;
        }
        _body.LinearVelocity = new Vector2(direction * MoveSpeed, vertical);
    }

    private void UpdateDemo(float fixedDeltaTime)
    {
        GameManager? game = GameManager.Instance;
        Vector2 velocity = _body.LinearVelocity;
        if (game is null || !game.TryGetNextCoinPosition(out Vector2 target))
        {
            velocity.X = 0.0f;
            _body.LinearVelocity = velocity;
            return;
        }

        Transform selfTransform = GetComponent<Transform>();
        Vector2 position = new(selfTransform.Position.X, selfTransform.Position.Y);
        velocity.X = MathF.Abs(target.X - position.X) < 0.15f
            ? 0.0f
            : MathF.Sign(target.X - position.X) * MoveSpeed;
        // Hop periodically: coins sit slightly above the ground and trigger
        // contact needs a little vertical movement too.
        if (_groundedCooldown <= 0.0f && MathF.Abs(velocity.X) < 0.01f)
        {
            velocity.Y = JumpSpeed * 0.6f;
            _groundedCooldown = 0.4f;
        }
        else if (_groundedCooldown <= 0.0f)
        {
            velocity.Y = JumpSpeed * 0.35f;
            _groundedCooldown = 0.5f;
        }
        _body.LinearVelocity = velocity;
    }
}
