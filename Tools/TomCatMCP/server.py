"""TomCat MCP stdio adapter. All scene semantics live in the Editor."""
import asyncio
import json

from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp import types

from client import Client
from tools import TOOLS


def create_server(client):
    server = Server("tomcat-editor", version="0.1.0",
                    instructions="Read editor_get_status and component_get_schema first. Read the actual scene before editing. Stop Play before authoring edits; verify writes by reading entity_get. Runtime changes are discarded on Stop.")

    @server.list_tools()
    async def list_tools():
        return [types.Tool(**definition) for definition in TOOLS]

    @server.call_tool()
    async def call_tool(name, arguments):
        result = await asyncio.to_thread(client.call, name, arguments)
        return types.CallToolResult(content=[types.TextContent(type="text", text=json.dumps(result, ensure_ascii=False))],
                                    structuredContent=result, isError=not result.get("ok", False))

    return server


async def main():
    server = create_server(Client())
    async with stdio_server() as (read, write):
        await server.run(read, write, server.create_initialization_options())


if __name__ == "__main__":
    asyncio.run(main())
