# Physics2D authoring and validation

Discover IDs using `component_get_schema`; stable names below describe the engine
semantics, not hard-coded IDs. Read current entity values before modifying them.

For a falling rectangle, create an entity, set its `TomCat.Transform` Translation
and Scale, add `TomCat.SpriteRenderer`, `TomCat.Rigidbody2D` and
`TomCat.BoxCollider2D`. Set Rigidbody2D `BodyType` to `1` (Dynamic); `0` is Static,
`2` is Kinematic. A ground entity can use BoxCollider2D without a Rigidbody2D:
TomCat supplies an implicit static body. BoxCollider2D `Size` contains half-extents
in local coordinates; Transform Scale also affects the world collider size.

Use nonzero positive scales for the initial exercise. Rotation is in radians.
Place the test objects away from unrelated scene objects. SpriteRenderer with no
Sprite asset handle uses the engine's untextured primitive; do not guess a texture
path or asset handle.

Save the authoring scene, then call `editor_play` and `editor_pause`. Play follows
the ordinary project's Build Settings and script compilation checks. If rejected,
read Console diagnostics; don't bypass build-scene or script errors.

Each `editor_step` completes one fixed 1/60 second step before responding. Read
the dynamic entity's Transform periodically to verify descent and eventual
settling on the ground. Runtime readback is stronger evidence than merely having
added a component; a position sample alone is not proof of every collision rule.

Call `editor_stop`, then confirm the authored starting transform was restored.
Save authoring changes in Edit mode; never claim a runtime position was persisted
unless a separate authoring edit actually applied it.
