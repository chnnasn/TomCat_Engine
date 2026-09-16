# Experimental Web Player

This Emscripten target runs the existing `PlayerRuntimeLayer`, cooked TCPAK reader,
scene runtime, Renderer2D and Box2D in a browser. It is a native-only milestone,
not a full replacement for the desktop Player.

## Build

Validated on Windows with Emscripten 4.0.15, CMake and Ninja. Activate the SDK first.

```powershell
git submodule update --init TomCat/vendor/Box2D TomCat/vendor/glm TomCat/vendor/spdlog TomCat/vendor/ImGuizmo
emcmake cmake -S Web -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/web -j 6
```

Serve `tomcat_player.js`, `.wasm` and `.data` from the same directory using HTTP
localhost or HTTPS. The page requires these response headers for pthread Workers:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Load the JS as a classic script and call `TomCatPlayerModule({ canvas, locateFile })`.
Await the factory before calling exports. Copy TCPAK bytes with `_malloc` and
`HEAPU8.set`; always `_free` the temporary buffer after boot. JS must retrieve
`HEAPU8` again after allocations, since memory can grow.

| C export | Contract |
| --- | --- |
| `tc_web_player_boot(width, height, bytes, size)` | Copies and validates TCPAK, activates the original Player layer; zero means success. Maximum 256 MiB / 8192 px. |
| `tc_web_player_frame(deltaSeconds)` | Called by requestAnimationFrame; simulation uses the existing fixed timestep. |
| `tc_web_player_resize(width, height)` | Resizes canvas/viewport and sends the engine resize event. |
| `tc_web_player_error()` | UTF-8 diagnostic; inspect after boot and every frame. |
| `tc_web_player_shutdown()` | Stops the scene and releases application resources. |
| `tc_web_player_stats()` | JSON diagnostic with frame/draw/body counts and sample Square height. |
| `tc_web_player_cook_sample()` | Development fixture: uses the actual cooker to write `/PhysicsPlayground.tcpak` in MEMFS. |

For a sample, call the cooker, read the file via `module.FS.readFile`, then boot
those bytes. On disposal call shutdown and `module.PThread.terminateAllThreads()`.
Create a new module and canvas for each subsequent play session. Source assets
and the development cooker are currently bundled in this experimental target.

## Native authoring API

The build also emits `tomcat_editor.js/.wasm/.data`, factory `TomCatEditorModule`.
This module renders an authoring Scene through `Scene::OnUpdateEditor`; call
`tc_web_editor_boot`, `tc_web_editor_frame`, `tc_web_editor_resize`,
`tc_web_editor_error` and `tc_web_editor_shutdown` like the Player equivalents.
Terminate its pthread pool on disposal and use a fresh module for another session.

`tc_web_editor_rpc(requestJson)` synchronously returns JSON, valid until the next
call. Requests and replies use `protocol: "tomcat.web.v1"` and `requestId`.
Commands: `system.capabilities`, `project.open`, `project.new`, `scene.snapshot`,
`scene.select`, `scene.transact`, `scene.loadArchive`, `scene.markSaved`,
`history.undo`, `history.redo`, `asset.list`, `asset.import`.
Open currently accepts `/Samples/PhysicsPlayground/Project.tcproj`; new creates a
scene using that mounted asset root. Import other scenes as canonical archives.

Scene mutations carry `sceneHandle` (uint64 decimal string) and `baseRevision`
(non-negative safe integer). Transactions additionally carry `label` and
`operations`: `entity.create/delete/rename/set-parent`,
`component.add/remove/patch`. Component patches map property IDs to typed values.
All entity/component/property/asset IDs and int64 values cross JS as strings.
Snapshots include canonical archive, entities, property schemas and history flags.
Mutations apply to a decoded scratch Scene and commit through SceneHistory only
after validation. Revisions increase on edits and undo/redo, never move backwards.
Asset type constraints, entity references, hierarchy validity and read-only
properties are checked in C++. Third-party component providers remain excluded.

For image import, write a new basename under
`/Samples/PhysicsPlayground/Assets/WebImports/`, then call `asset.import` with
`{name:"example.png"}`. The engine imports it and returns its stable handle;
persist the image and generated `.tcmeta` together. PNG/JPEG/TGA up to 2 MiB are
accepted. Imported files are separate from scene undo/redo. MEMFS metadata
publication uses create-only copies because MEMFS does not implement hard links.

Run the real WASM protocol regression without a graphics context:

```powershell
node Web/tests/editor-rpc.cjs build/web
```

The test covers transactions, rollback, uint64 boundaries, selection, components,
hierarchy, images, invalid references, history divergence and revision conflicts.
Browser rendering and persistence still need host-level acceptance checks.

## Shared port boundaries

- GLES3 replacements for desktop DSA buffer/texture operations and single-sample
  framebuffers; embedded engine shaders use GLSL ES 300 and 16 texture slots.
- Box2D user settings preserve full 64-bit entity UUIDs on wasm32. Both Box2D and
  consumers must use the same `B2_USER_SETTINGS` definition.
- Keyboard, pointer, focus, scroll and standard browser gamepad mapping feed the
  existing input snapshot queue. Gamepad hardware has not been acceptance-tested.
- C# packages explicitly fail: desktop hostfxr cannot be used in this target.
  A .NET browser runtime/native ABI integration remains separate work.
- Custom cooked SPIR-V shaders and multisample framebuffers explicitly fail.
  GLSL conversion covers embedded infrastructure shaders only.
- Audio uses the upstream NullAudioDevice fallback; this target has no audible
  WebAudio output. Do not advertise audio support.
- SharedArrayBuffer/Workers and WebGL2 are required; no single-thread fallback.
- Existing desktop builds retain their implementation. Full Windows regression
  coverage is supplied by the existing CI workflow; Web validation does not replace it.

## Acceptance observed (2026-09-16)

Browser sample cook, TCPAK mount, texture preload and entry Scene
`12007672766582721512` succeeded. Two bodies were created. Square fell from
`Y=1.500` to approximately `-0.335` and rested on the ground; rendering showed
the square and ground, with one draw call per frame. Stop/restart reset the
sample. A malformed TCPAK was rejected with a diagnostic.

This validates the checked-in PhysicsPlayground sample only; it is not evidence
that all desktop games, shaders, scripts or input devices are compatible.
