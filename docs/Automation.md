# Native editor automation

The Windows Editor exposes an opt-in loopback service for AI clients and regression
runners. It uses native project, scene, component, asset, compiler and build code;
it does not synthesize operating-system clicks or keystrokes. Normal Editor/Player
behavior is unchanged when the service is disabled.

## Start and connect

```powershell
$env:TOMCAT_AUTOMATION_PORT = '8091'
$env:TOMCAT_AUTOMATION_TOKEN = [guid]::NewGuid().ToString('N')
$env:TOMCAT_AUTOMATION_WORKSPACE = 'C:/Projects'
# Start with no arguments for project_create, or pass an existing .tcproj.
& ./Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe
```

Keep the token private. No port means no server. A bad token/port or occupied port
fails closed and logs a diagnostic. Workspace configuration enables project
creation/open; their paths are relative to that directory. Asset paths are relative
to the active project's Assets. Project creation refuses nonempty destinations;
switching projects/scenes refuses unsaved authoring state.

The companion [TomCat Engine Skills](https://github.com/chnnasn/TomCat_Engine_Skills)
provides Python, CLI, MCP and a JSON scenario runner. Configure its `project` as
an explicit empty string when connecting to an Editor with no project. Successful
project creation/open returns the new project path and the client rebinds to it.
The live `automation_get_capabilities` response is authoritative; client tool lists
alone do not establish what an older Editor implements.

## Wire contract

Authenticated `GET /health` returns status. `POST /call` receives JSON:

```json
{
  "tool": "entity_create",
  "arguments": {"name": "Actor"},
  "project": "C:/Projects/Game/Project.tcproj",
  "session_id": "from-health",
  "scene_version": "from-health",
  "request_id": "a-unique-id-for-this-write"
}
```

Use `Authorization: Bearer <token>`. IDs/uint64 values are decimal strings. The
response is `{"ok":true,"data":...}` or `{"ok":false,"error":{"code":...,"message":...}}`.
The protocol envelope is version 1; the workflow extension advertises version 1.
[AutomationTools.json](AutomationTools.json) lists argument schemas and read-only
annotations. `request_id`/`scene_version` in these client schemas are lifted to the
wire envelope by the companion client, not included in `arguments`.

The listener accepts loopback only, rejects browser Origin headers and unsupported
framing, limits headers to 8 KiB and bodies to 1 MiB, and handles one connection at
a time. Work is pumped at the Editor's main-thread safe point, including minimized
windows. There is no arbitrary shell/eval endpoint.

Writes require current session and scene version. Scene authoring stages a copy,
validates it, records history, then publishes. Prefab Apply shares the Inspector's
file-and-scene undo implementation. Project/settings/asset writes, captures and
build outputs are not scene undo operations. Replacing a text asset requires exact
`expected_text`; scene version alone is not a file concurrency precondition.

The last 256 write bodies/results are cached. Retry an ambiguous write with its
original ID and identical body, within the same session. Evicted IDs remain
tombstoned; at 65,536 writes save and restart. Queued-but-unstarted calls can be
cancelled on timeout. A dispatched call may finish after the 60-second transport
deadline, especially a build: a timeout is not a rollback. Inspect/recover the
original response before issuing another write.

## Complete workflow

1. `project_create` or `project_open`; inspect `component_get_schema`.
2. Author with entity/component operations. Use the canonical scene archive for
   complex serialized arrays. Stage binary sources with file tools, then use
   `asset_import` and `asset_validate`; keep `.tcmeta` identities engine-owned.
3. Write C# with `asset_write_text`, start `script_compile`, poll
   `script_get_status` until `current=true` and `building=false`, then `script_attach`.
4. `scene_save_as`, discover its handle, and configure `build_set_scenes`.
5. `runtime_start`, inject `runtime_step` events, inspect/assert, capture artifacts.
6. `editor_stop`, verify authoring state, save, `build_player` and run the resulting
   executable with `--validate-package <Game.tcpak>`.

Build uses the normal current-version Player Template, script validation and
atomic publication path. `Scripts/Build-PlayerTemplate.ps1 -Build` prepares a
template; its default/explicit destination must match the Editor's Packages path.

## Runtime tests

`runtime_start` enters paused Play before any simulation step. It isolates physical
input and the OS clipboard, and publishes game-data paths beneath
`Library/Automation/<session>` before scripts start. Ordinary `editor_play` uses
normal focus routing and normal game save directories.

`runtime_step` advances 1..600 fixed 1/60-second steps. Validate the entire input
batch before any step; events apply only to the first frame, while held state
persists. Supported events:

| type | fields |
| --- | --- |
| key / button | code, action: press / release / repeat |
| pointer / scroll | x, y |
| text | text: committed Unicode |
| focus | focused: boolean; losing focus releases held controls |

Pointer and `runtime_get_ui.center` use a top-left game-pixel origin. Click with
separate press/release frames. `runtime_assert` compares registered properties,
with optional floating/vector tolerance. `runtime_capture` returns a PPM file,
rendered without simulation advancement. Test input does not emulate platform IME
composition or hardware gamepads. Fixed stepping does not seed game randomness or
make network/async IO completion deterministic.

## Specialist surfaces

| Area | Native operations |
| --- | --- |
| Prefabs | save, instantiate, inspect overrides, update/revert, apply/unpack, property apply/revert with file undo |
| Runtime UI | registered authoring plus canonical scene arrays, layout/text inspection, pointer/keyboard/scroll/text replay |
| Assets | list/read/write/import/validate/settings/move/reference-checked delete, directories |
| Animation | canonical Clip/Controller source and artifact validation, runtime clip/parameter/trigger controls |
| Tilemap | registered Grid/Tilemap configuration and transactional cell batches |
| Streaming | real async load/unload/cancel, activation gates/budgets/progress; runtime_step advances transitions |
| Profiler | recording independent of panel visibility, frame summaries, Chrome trace export |
| Scripts | async compile/status, diagnostics, metadata-based attachment; fields via canonical scene archive |
| Project/build | tags/layers/collision masks, Player settings, scene lists, native Player pipeline |
| Saves | isolated native slot write/list/read/delete, digest/backup behavior; reads return payload hex |
| Modules | loaded-module/command discovery and invocation of published commands; source/DLL compilation uses existing build tools |

Native module ABI, platform support and current-format-only validation remain the
engine's normal rules. A successful text write is not proof of valid code or asset
content: compile/validate and inspect diagnostics. Provider-owned components remain
subject to advertised writable capabilities. Published module commands execute
their own native code, as they do from the Editor menu.

## Regression

```powershell
./Scripts/Run-AutomationRegression.ps1
# If prerequisites already match this checkout:
./Scripts/Run-AutomationRegression.ps1 -SkipBuild -SkipTemplate
```

The stdlib-only Python regression starts from an empty Editor and tests project
creation, component/archive rejection, source conflict detection, compilation and
attachment, Prefab file undo, Tilemap, animation assets, Unicode input/retained
keys/invalid-batch atomicity, UI clicks/scroll/backspace, capture, isolated saves,
scene streaming, profiler export and packaged Player validation. Artifacts and
logs remain in a unique temporary directory; only the launched Editor is stopped.
`Tests/InputRegression` additionally verifies physical-input isolation and snapshot
lifecycle. The companion repository tests Python request recovery and MCP stdio.
