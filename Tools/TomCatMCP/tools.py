"""Public V1 tool catalog, shared by MCP, CLI help and tests."""

ID = {"type": "string", "pattern": "^[0-9]+$", "description": "Decimal uint64 string; never convert IDs to floating point."}
ENTITY = {"entity_id": ID}
COMPONENT = {**ENTITY, "component_id": ID}
READ_ONLY = {"editor_get_status", "scene_get_tree", "entity_get", "component_get_schema", "console_get_entries"}


def tool(name, description, properties=None, required=()):
    properties = dict(properties or {})
    if name not in READ_ONLY:
        properties["request_id"] = {"type": "string", "description": "Only for retrying an ambiguous call: use its returned request_id in this same MCP session. Omit for a new operation."}
        properties["scene_version"] = {"type": "string", "description": "Optional optimistic precondition from editor_get_status or scene_get_tree."}
    return {"name": name, "description": description,
            "inputSchema": {"type": "object", "properties": properties, "required": list(required), "additionalProperties": False},
            "annotations": {"readOnlyHint": name in READ_ONLY, "openWorldHint": False,
                            "destructiveHint": name not in READ_ONLY}}


TOOLS = [
    tool("editor_get_status", "Read connected project, session ID, scene version, edit/play/pause state and dirty flag."),
    tool("scene_get_tree", "Read active scene entities with parent IDs and component IDs (runtime scene during Play).",
         {"offset": {"type": "integer", "minimum": 0}, "limit": {"type": "integer", "minimum": 1, "maximum": 500}}),
    tool("entity_get", "Read one active-scene entity and its registered component property values.", ENTITY, ["entity_id"]),
    tool("component_get_schema", "Discover actual component/property IDs, kinds, names and supported writes. Query before editing; do not guess IDs."),
    tool("console_get_entries", "Read compiler/runtime diagnostics. Severity 0=trace, 1=info, 2=warning, 3=error. Play may clear older entries.",
         {"after": ID, "limit": {"type": "integer", "minimum": 1, "maximum": 500}}),
    tool("entity_create", "Create an entity in the Edit scene with one undo entry; returns its actual unique name and ID.",
         {"name": {"type": "string", "minLength": 1, "maxLength": 256}}, ["name"]),
    tool("entity_delete", "Delete an entity subtree in Edit mode, using the engine structural command validator.", ENTITY, ["entity_id"]),
    tool("entity_reparent", "Change parent in Edit mode; parent_id=null makes a root. Cycles are rejected.",
         {**ENTITY, "parent_id": {"anyOf": [ID, {"type": "null"}]}}, ["entity_id", "parent_id"]),
    tool("component_add", "Add an engine-owned component supported by the discovered schema, in Edit mode.", COMPONENT, list(COMPONENT)),
    tool("component_remove", "Remove a removable engine-owned component in Edit mode.", COMPONENT, list(COMPONENT)),
    tool("component_set", "Set one property through its registered validator. Vectors are arrays; int64/uint64 are decimal strings. Edit mode only.",
         {**COMPONENT, "property_id": ID, "value": {"anyOf": [{"type": "boolean"}, {"type": "number"}, {"type": "string"},
            {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 4}]}},
         [*COMPONENT, "property_id", "value"]),
    tool("editor_play", "Start the current scene using normal Editor Play checks. Read console diagnostics on failure."),
    tool("editor_pause", "Pause Play; an already-paused scene stays paused."),
    tool("editor_step", "Advance a paused runtime scene by exactly one fixed 1/60 second step; returns after the step completes."),
    tool("editor_stop", "Stop Play and restore the authoring scene; runtime modifications are discarded."),
    tool("scene_save", "Save the authoring scene to its existing path in Edit mode. Unsaved scenes require one manual Save As first."),
    tool("history_undo", "Undo one scene edit in Edit mode, including manual edits. Inspect scene before undoing."),
    tool("history_redo", "Redo one scene edit in Edit mode."),
]
BY_NAME = {item["name"]: item for item in TOOLS}
