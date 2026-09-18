---
name: tomcat-editor
description: Drive the TomCat 2D Editor through its local automation API or MCP tools to inspect scenes, create entities, edit registered components, control Play, and read diagnostics. Use when asked to operate a running TomCat project; conceptual engine questions do not require a connection.
---

# TomCat Editor automation

Use the connected TomCat MCP tools if available. Otherwise use the dependency-free
client via `scripts/tomcat.py`; `TOMCAT_AUTOMATION_HOME` must point to the repository's
`Tools/TomCatMCP` directory. Both paths require `TOMCAT_PROJECT` (absolute `.tcproj`
path), `TOMCAT_AUTOMATION_PORT`, and `TOMCAT_AUTOMATION_TOKEN`. The Editor must have
been started with the same port and token. Never echo the token in responses.

## Discover and inspect

- Call `editor_get_status` first. Confirm project, state and scene path. A project
  mismatch means the wrong instance is connected; choose its configured port.
- Query `component_get_schema` before component work. Use returned component and
  property IDs and `writable`/`addable`/`removable` flags. The running engine is the
  source of truth; unsupported capabilities must not be invented.
- Read `scene_get_tree` (paginated, at most 500 per page) and `entity_get` before
  changing existing content. All IDs and 64-bit integer values are decimal
  strings, including asset handles; never pass them through floating-point math.
- If using the client, `python scripts/tomcat.py list` returns tool schemas;
  `python scripts/tomcat.py editor_get_status` checks the connection. For a series
  of calls, use one Python process and one `Client` instance so retry state is kept:

```python
import os, sys
sys.path.insert(0, os.environ["TOMCAT_AUTOMATION_HOME"])
from client import Client
editor = Client()
status = editor.call("editor_get_status")
schema = editor.call("component_get_schema")
```

## Edit and verify

Authoring writes require Edit mode. `editor_stop` restores the authoring scene;
runtime changes are discarded. Each successful scene edit produces an undo entry
when the serialized scene changes. Multi-call tasks are not atomic batches.

Use `component_set` for Transform and physics fields as well as other writable
properties. Transform rotations use radians; `Translation`/`Rotation`/`Scale` are
world space, and their `Local*` counterparts are relative to the parent. Vectors
are numeric arrays with exactly the dimension reported by the schema.

For concurrent manual editing, pass `scene_version` from the inspected scene to
the write. `SCENE_CHANGED` means reread and reconsider the edit. `EDITOR_BUSY`
means an editor interaction or dialog is active; let that interaction finish.

After writes, read back the affected entities. A returned success is not evidence
that the intended visual or runtime behavior is correct. For physics authoring
and validation, read [physics2d.md](references/physics2d.md).

`scene_save` saves only to an existing scene path. An untitled scene needs one
Editor Save As. `history_undo` also includes manual edits, so inspect state before
using it to recover from an AI mistake.

## Failures and retries

- `PLAY_FAILED`: inspect `console_get_entries`; do not assume the runtime started.
- `OUTCOME_UNKNOWN`: a write may have executed. In the same MCP/client session,
  retry with the returned `request_id` and identical tool arguments. No automatic
  write retry occurs. The server and client retain only the last 256 write IDs.
- `REQUEST_EXPIRED` or a lost client session: inspect actual scene state before
  deciding what remains to be done; do not submit a blind duplicate.
- `SESSION_MISMATCH`: the Editor restarted. Reconnect, re-inspect and make a new
  plan; cached calls belong to the old session.
- Read errors and actual property schemas before correcting rejected parameters.

V1 supports 18 tools: scene/entity inspection, registered component authoring,
hierarchy changes, Play/Pause/Step/Stop, Console, existing-path Save and Undo/Redo.
It does not expose screenshots, asset import, script editing, builds, batch
transactions or arbitrary code execution through MCP. Ordinary source-code work
and the existing build CLI remain separate workflows.
