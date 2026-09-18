# Experimental Web Player and ImGui editor

English | [简体中文](README.zh-CN.md) · Reviewed 2026-09-18 · [All documentation](../docs/README.md)

This Emscripten target runs the existing `PlayerRuntimeLayer`, cooked TCPAK reader,
scene runtime, Renderer2D and Box2D in a browser. It is a native-only milestone,
not a full replacement for the desktop Player.

## Build

Validated on Windows with Emscripten 4.0.15, CMake and Ninja. Activate the SDK first.
Run these commands from the repository root. CMake 3.20+ is required; Node.js is
also needed for the protocol regression. The build produces modules, not a complete
browser host page or a persistence service. Earlier validation dates below are historical.

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
This module compiles the existing `SceneHierarchyPanel` (including Inspector),
`ContentBrowserPanel`, `ConsolePanel`, `ImGuiLayer` theme/icons and ImGuizmo.
The browser host supplies navigation, file selection and persistence; it does not
reimplement the panels in HTML. A Web-only layer supplies docking, the scene
framebuffer, picking and transform gizmos around these shared desktop panels.
It renders an authoring Scene through `Scene::OnUpdateEditor`; call
`tc_web_editor_boot`, `tc_web_editor_frame`, `tc_web_editor_resize`,
`tc_web_editor_error` and `tc_web_editor_shutdown` like the Player equivalents.
Terminate its pthread pool on disposal and use a fresh module for another session.

The editor boot signature is `tc_web_editor_boot(width, height)`; unlike Player
boot, it takes no TCPAK byte buffer. After boot, open the sample or create a scene
through RPC. Copy/parse returned strings before calling another state/RPC export.

Poll `tc_web_editor_state()` for lightweight JSON containing scene handle,
revision, selection, dirty and undo/redo flags. Its response shares storage with
the RPC reply. Consume `tc_web_editor_take_actions()` after each frame: bit 1
requests host save, bit 2 image import and bit 4 project export. File Save and
Ctrl+S commit the current native gesture before requesting host persistence.
Panel edits and gizmo drags use the same SceneHistory as RPC; a continuous edit
is one undo entry. RPC snapshots and mutations reject active native gestures
with `EDIT_IN_PROGRESS`, so a host cannot persist a partially committed edit.

Web fonts use the existing OpenSans files plus the licensed Noto Sans SC in
`Web/fonts`. The WebGL framebuffer uses typed clears for float/integer color
attachments. The Emscripten GLFW backend disables unavailable Vulkan/gamepad
entry points; engine gamepad input remains on its separate browser adapter.

This reuses the core desktop panels, not every tool in the desktop EditorLayer.
Project asset browsing, thumbnails and references work. File mutations in that
panel are disabled until the host can persist every asset type; import images
through the host instead. Prefab creation, specialized collider editing,
external C# editors, layout persistence and desktop Build Settings remain unavailable.
The browser now shares the desktop Play toolbar and Project Settings view source.
Scene uses the upstream tool icons; Game renders the primary camera. Play starts
a SceneManager-owned copy, Pause/Step advance only that copy, and Stop preserves
the authoring scene and its history. C# scenes and missing primary cameras are
rejected explicitly. Mutating RPCs are blocked during Play.
Project settings changes apply in MEMFS and mark the session dirty. Hosts must
persist ProjectSettings/ProjectSettings.json and PlayerSettings.json with the
scene, restore them before project.open/new, then acknowledge scene.markSaved.
Desktop display/directory controls remain visible but disabled in Web.
The C++ file dialog fallback returns cancellation in the browser.

`tc_web_editor_rpc(requestJson)` synchronously returns JSON, valid until the next
call. Requests and replies use `protocol: "tomcat.web.v1"` and `requestId`.
Requests use `type` for the command and `payload` for its arguments.
Commands: `system.capabilities`, `project.open`, `project.new`, `scene.snapshot`,
`scene.select`, `scene.transact`, `scene.loadArchive`, `scene.markSaved`,
`history.undo`, `history.redo`, `asset.list`, `asset.import`,
`preview.control` ({command: play/pause/resume/step/stop}), `preview.snapshot`.
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
hierarchy, images, invalid references, history divergence, revision conflicts,
50 preview restarts with physics stepping and authoring-state preservation,
and recovery after a missing-primary-camera startup failure.
Browser rendering and persistence still need host-level acceptance checks.

Browser acceptance on 2026-09-17 verified Chinese glyphs, Hierarchy selection,
Inspector translation edits, Sprite picker assignment, scene pixel picking,
ImGuizmo dragging, undo/redo, docking resize and native File Save followed by
host reload with the same transform and Sprite reference. This does not cover
Chinese IME composition or all desktop panel widgets.

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

The shared current TCPAK writer is v7 (per-entry SHA-256); the reader accepts
v5/v6/v7. Format compatibility does not remove the Web feature restrictions above.
The repository's Windows regression workflow does not run the Web build or WASM test.

## Acceptance observed (2026-09-16)

Browser sample cook, TCPAK mount, texture preload and entry Scene
`12007672766582721512` succeeded. Two bodies were created. Square fell from
`Y=1.500` to approximately `-0.335` and rested on the ground; rendering showed
the square and ground, with one draw call per frame. Stop/restart reset the
sample. A malformed TCPAK was rejected with a diagnostic.

This validates the checked-in PhysicsPlayground sample only; it is not evidence
that all desktop games, shaders, scripts or input devices are compatible.
