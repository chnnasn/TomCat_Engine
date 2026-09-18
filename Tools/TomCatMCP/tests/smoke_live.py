"""Integration regression. Run ONLY against the disposable project created by Run-AutomationRegression.ps1."""
import asyncio
import json
import os
from pathlib import Path
import sys
import time
import urllib.request
import urllib.error

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from client import Client


def main():
    assert "TomCat-Automation-" in os.environ["TOMCAT_PROJECT"], "Use the isolated regression launcher"
    client = Client(timeout=70)
    deadline = time.monotonic() + 60
    while True:
        status = client.call("editor_get_status")
        if status.get("ok"):
            break
        if time.monotonic() >= deadline:
            raise AssertionError(status)
        time.sleep(0.25)

    def call(tool_name, **arguments):
        result = client.call(tool_name, arguments)
        assert result.get("ok"), (tool_name, result)
        return result["data"]

    def fails(tool_name, code, **arguments):
        result = client.call(tool_name, arguments)
        assert not result["ok"] and result["error"]["code"] == code, result

    # The token gate must run before any operation is queued.
    request = urllib.request.Request(client.url + "/health")
    try:
        client.http.open(request)
        raise AssertionError("Unauthenticated request was accepted")
    except urllib.error.HTTPError as error:
        assert error.code == 403

    components = {item["name"]: item for item in call("component_get_schema")["components"]}
    assert components["TomCat.Transform"]["properties"]

    def add(entity, component):
        call("component_add", entity_id=entity, component_id=components["TomCat." + component]["id"])

    def set_value(entity, component, prop, value):
        desc = components["TomCat." + component]
        field = next(item for item in desc["properties"] if item["name"] == prop)
        call("component_set", entity_id=entity, component_id=desc["id"], property_id=field["id"], value=value)

    def value(entity, component, prop):
        desc = next(item for item in call("entity_get", entity_id=entity)["components"] if item["name"] == "TomCat." + component)
        return next(item["value"] for item in desc["properties"] if item["name"] == prop)

    old_version = call("editor_get_status")["scene_version"]
    created = client.call("entity_create", {"name": "AI 方块"})
    assert created["ok"], created
    original_request = dict(client.requests[created["request_id"]])
    cube = created["data"]["id"]
    duplicate = client.call("entity_create", {"name": "AI 方块", "request_id": created["request_id"]})
    assert duplicate["data"]["id"] == cube
    assert sum(item["id"] == cube for item in call("scene_get_tree")["entities"]) == 1
    fails("entity_create", "SCENE_CHANGED", name="stale", scene_version=old_version)
    set_value(cube, "Transform", "Translation", [8, 3, 0])
    assert value(cube, "Transform", "Translation") == [8, 3, 0]
    assert value(cube, "Transform", "LocalTranslation") == [8, 3, 0]
    call("history_undo")
    assert value(cube, "Transform", "Translation") == [0, 0, 0]
    call("history_redo")
    assert value(cube, "Transform", "Translation") == [8, 3, 0]
    parent = call("entity_create", name="Parent")["id"]
    set_value(parent, "Transform", "Translation", [10, 0, 0])
    call("entity_reparent", entity_id=cube, parent_id=parent)
    set_value(cube, "Transform", "LocalTranslation", [1, 2, 0])
    assert value(cube, "Transform", "Translation") == [11, 2, 0]
    fails("entity_reparent", "EDIT_REJECTED", entity_id=parent, parent_id=cube)
    call("entity_reparent", entity_id=cube, parent_id=None)
    call("entity_delete", entity_id=parent)
    set_value(cube, "Transform", "Translation", [8, 3, 0])
    add(cube, "SpriteRenderer")
    add(cube, "Rigidbody2D")
    add(cube, "BoxCollider2D")
    set_value(cube, "Rigidbody2D", "BodyType", 1)
    desc = components["TomCat.Rigidbody2D"]
    field = next(item for item in desc["properties"] if item["name"] == "BodyType")
    fails("component_set", "EDIT_REJECTED", entity_id=cube, component_id=desc["id"], property_id=field["id"], value=99)
    assert value(cube, "Rigidbody2D", "BodyType") == 1
    ground = call("entity_create", name="AI Ground")["id"]
    set_value(ground, "Transform", "Translation", [8, -1, 0])
    set_value(ground, "Transform", "Scale", [6, 0.5, 1])
    add(ground, "SpriteRenderer")
    add(ground, "BoxCollider2D")
    call("scene_save")
    assert not call("editor_get_status")["dirty"]
    call("editor_play")
    call("editor_pause")
    fails("entity_create", "EDIT_MODE_REQUIRED", name="runtime edit")
    for _ in range(150):
        call("editor_step")
    position = value(cube, "Transform", "Translation")
    assert -0.5 < position[1] < 0.2, position  # half-height cube resting on floor
    call("editor_stop")
    assert value(cube, "Transform", "Translation") == [8, 3, 0]
    call("console_get_entries")
    print("PASS native editing, undo/redo and fixed-step physics", flush=True)
    # Another client can evict a response even while the first client retains its
    # request. The server must reject the old ID, not execute the write again.
    other = Client()
    for _ in range(256):
        assert other.call("editor_stop")["ok"]
    expired = client._send("/call", original_request)
    assert expired["error"]["code"] == "REQUEST_EXPIRED", expired
    print("PASS native HTTP: auth, schema, Unicode/uint64, deduplication/expiry, stale version, hierarchy, atomic rejection, undo/redo, physics, save/stop", flush=True)
    asyncio.run(mcp_smoke(cube))


async def mcp_smoke(entity_id):
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client
    server = str(Path(__file__).resolve().parents[1] / "server.py")
    params = StdioServerParameters(command=sys.executable, args=[server], env=dict(os.environ))
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            assert len(tools.tools) == 18
            result = await session.call_tool("entity_get", {"entity_id": entity_id})
            assert not result.isError, result
            assert json.loads(result.content[0].text)["data"]["id"] == entity_id
            result = await session.call_tool("entity_get", {"entity_id": "0"})
            assert result.isError, result
    print("PASS MCP stdio: initialize, tools/list, tools/call, structured errors")


if __name__ == "__main__":
    main()
