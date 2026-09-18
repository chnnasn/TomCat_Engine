# TomCat Editor MCP + Skills (V1)

An external AI Agent can inspect and edit a running TomCat Editor through either
MCP stdio tools or a Skill using the Python client. Both use the same loopback HTTP
API and main-thread Editor operations. The Editor does not embed an LLM.

```text
Agent + tomcat-editor Skill
    ├── MCP stdio → server.py ──┐
    └── Python → client.py ────┴→ 127.0.0.1 HTTP → main thread → Editor/Scene
```

## Build and start

Windows x64; build the Editor from this branch using the normal prerequisites.
The client requires Python 3.10+; MCP additionally requires the pinned SDK range.

From the repository root:

```powershell
& vendor/premake/bin/premake5.exe --file=Editor/premake5.lua vs2022
# Build Editor/Editor.sln, Release x64, with Visual Studio or MSBuild.
python -m venv Tools/TomCatMCP/.venv
Tools/TomCatMCP/.venv/Scripts/python.exe -m pip install -r Tools/TomCatMCP/requirements.txt

$env:TOMCAT_AUTOMATION_PORT = '8091'
$env:TOMCAT_AUTOMATION_TOKEN = [guid]::NewGuid().ToString('N')
$env:TOMCAT_PROJECT = (Resolve-Path Samples/PhysicsPlayground/Project.tcproj).Path
$env:TOMCAT_AUTOMATION_HOME = (Resolve-Path Tools/TomCatMCP).Path
& Editor/bin/Release-windows-x86_64/TomCatInut/TomCatInut.exe $env:TOMCAT_PROJECT
```

The server is disabled unless `TOMCAT_AUTOMATION_PORT` is set. Startup rejects
invalid ports, short tokens and occupied ports; it never silently selects another
Editor. Use a distinct port per instance. The token and port must be inherited by
the Editor and client; restarting the Editor is required to change them. Keep
tokens in local environment/client settings, not version control.

## Connect an MCP client

Configure a stdio server using absolute paths. The client host must receive the
same token created above; the values below are placeholders to replace locally.

```json
{
  "mcpServers": {
    "tomcat-editor": {
      "command": "E:/Github/TomCat_Engine/Tools/TomCatMCP/.venv/Scripts/python.exe",
      "args": ["E:/Github/TomCat_Engine/Tools/TomCatMCP/server.py"],
      "env": {
        "TOMCAT_PROJECT": "C:/MyGame/Project.tcproj",
        "TOMCAT_AUTOMATION_PORT": "8091",
        "TOMCAT_AUTOMATION_TOKEN": "REPLACE_WITH_THE_EDITOR_TOKEN"
      }
    }
  }
}
```

This is a generic MCP JSON configuration example; adapt the outer configuration
format to the chosen client. Run `editor_get_status`, `component_get_schema` and
`scene_get_tree` first. SDK lifecycle, validation and JSON-RPC are handled by the
official `mcp` package, not a custom protocol implementation.

## Use the Skill or CLI

Copy `skills/tomcat-editor` into the Agent's supported skills directory. Set
`TOMCAT_AUTOMATION_HOME` to this repository's `Tools/TomCatMCP` directory; the
bundled launcher uses the same client instead of maintaining a second schema.
With MCP available, the Skill uses its tools directly.

```powershell
python Tools/TomCatMCP/client.py list
python Tools/TomCatMCP/client.py editor_get_status
python Tools/TomCatMCP/client.py scene_get_tree '{"limit":20}'
```

The CLI itself needs only the Python standard library. Keep a single Python
`Client` instance for multi-step tasks and recoverable retries; separate CLI
invocations do not share the request cache.

## Supported operations

18 tools cover status, paginated entity trees, entity/property readback, component
schema, structured Console entries, entity create/delete/reparent, component
add/remove/set, Play/Pause/Step/Stop, existing-path scene save and Undo/Redo.

The registered schema supplies real component/property IDs and supported writes.
IDs and int64/uint64 values are decimal strings, avoiding JSON number precision
loss. Transform operations preserve local/world hierarchy semantics. Edit writes
are staged on a Scene copy, validated, serialized into history, then published as
one scene edit; invalid operations leave the live scene unchanged. Only
engine-owned script-accessible component descriptors are writable in V1.

`editor_step` runs exactly one fixed step on a paused runtime scene before
returning. `editor_stop` restores authoring state. The main-thread dispatch also
runs while minimized; rendering remains suspended normally.

## Wire contract and behavior

- `GET /health` returns `editor_get_status`.
- `POST /call` takes `tool`, `arguments`, and the bound absolute `project` path.
- Writes additionally require `session_id`, `scene_version`, and `request_id`.
  The client obtains status automatically; callers may supply the version from
  an earlier inspection for stricter optimistic concurrency.
- Every request requires `Authorization: Bearer <token>`. The server binds only
  loopback, rejects browser Origin headers and unsupported HTTP framing, caps
  headers at 8 KiB and bodies at 1 MiB, and processes one connection at a time.
- Results are `{ "ok": true, "data": ... }` or
  `{ "ok": false, "error": { "code": ..., "message": ... } }`.
  MCP maps the latter to `isError=true` and retains structured content.
- A session retains the last 256 write request bodies/results. An identical ID
  and body return the cached result; changed bodies are rejected. The Python
  client preserves the body on explicit retry. No automatic write retries occur.
  Expired IDs remain tombstoned and cannot execute again, including when another
  client evicts a cached response. A session accepts at most 65,536 distinct write
  IDs; after that, save and restart the Editor to begin a new session.
- A transport timeout may have an unknown outcome. Retry using the returned ID
  in the same client session; after cache loss/restart, inspect actual state.
  Queued requests not yet dispatched are cancelled on server timeout/shutdown.
- Scene edits are blocked while playing, during active undo transactions, or
  while destructive/recovery/migration dialogs are pending.

## Verification

```powershell
Tools/TomCatMCP/.venv/Scripts/python.exe -m unittest discover -s Tools/TomCatMCP/tests -p 'test_*.py' -v
& Scripts/Run-AutomationRegression.ps1
```

The integration runner builds Release (or accepts `-SkipBuild`), copies only the
sample's source project files into a unique temporary directory, starts its own
hidden Editor, and verifies real HTTP editing, physics and MCP stdio discovery
and calls. It stops only the process it started and keeps the isolated project
and logs for diagnosis. It never edits the repository sample.

V1 does not yet expose screenshots, asset import, script edits, build jobs, batch
transactions, persistent audit/retry storage or automatic instance discovery.
Untitled scenes require a manual Save As once. Scene copies make this first
version best suited to small and medium scenes; each edit costs a scene copy and
history serialization. Existing TomCatCLI build/cook remains a separate tool.
