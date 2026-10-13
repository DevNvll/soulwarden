#!/usr/bin/env python3
"""Call one tool on the Ghidra MCP server and print the result.

Usage:
  mcp_call.py tools
  mcp_call.py decompile ADDRESS_OR_NAME...     decompile functions (image-relative RVA or name)
  mcp_call.py xrefs ADDRESS_OR_NAME...         cross-references to each target
  mcp_call.py symbols QUERY [LIMIT]            symbol search
  mcp_call.py strings QUERY [LIMIT]            string search
  mcp_call.py dis ADDRESS [COUNT]              disassembly
  mcp_call.py bytes ADDRESS [SIZE]             raw bytes
  mcp_call.py raw TOOL JSON_ARGUMENTS          any tool with explicit arguments

A bare hexadecimal value below the image base is taken as an RVA.
"""

import asyncio
import json
import os
import sys

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

URL = os.environ.get("SOULWARDEN_GHIDRA_MCP_URL", "http://127.0.0.1:8765/mcp")
BINARY = os.environ.get("SOULWARDEN_GHIDRA_BINARY", "/NewWorld.exe")
IMAGE_BASE = 0x140000000


def absolute(target: str) -> str:
    text = target[2:] if target.lower().startswith("0x") else target
    try:
        value = int(text, 16)
    except ValueError:
        return target
    if value < IMAGE_BASE:
        value += IMAGE_BASE
    return f"0x{value:x}"


async def call(tool: str, arguments: dict):
    async with streamablehttp_client(URL) as (reader, writer, _):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            if tool == "tools":
                listing = await session.list_tools()
                for entry in listing.tools:
                    print(f"{entry.name}: {(entry.description or '').splitlines()[0]}")
                return None
            return await session.call_tool(tool, arguments)


def show(result) -> None:
    if result is None:
        return
    payload = result.structuredContent
    if payload is None:
        for item in result.content:
            print(getattr(item, "text", item))
        return
    payload = payload.get("result", payload)
    if isinstance(payload, list):
        for entry in payload:
            show_entry(entry)
    else:
        show_entry(payload)


def show_entry(entry) -> None:
    if not isinstance(entry, dict):
        print(entry)
        return
    if "code" in entry:
        print(f"// ===== {entry.get('name')} {entry.get('signature') or ''}")
        if entry.get("error"):
            print(f"// ERROR {entry['error']}")
        print(entry.get("code") or "")
        for key in ("callees", "referenced_strings", "xrefs"):
            if entry.get(key):
                print(f"// {key}: {json.dumps(entry[key])[:4000]}")
        return
    if "cross_references" in entry:
        print(f"TARGET {entry.get('target')} {entry.get('error') or ''}")
        for reference in entry["cross_references"]:
            print(f"  {reference.get('from_address')} {reference.get('ref_type')} in {reference.get('function_name')}")
        return
    if "listing" in entry:
        print(entry["listing"])
        return
    print(json.dumps(entry, indent=1)[:20000])


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    command, arguments = sys.argv[1], sys.argv[2:]
    if command == "tools":
        asyncio.run(call("tools", {}))
        return
    if command == "decompile":
        request = ("decompile_function", {"binary_name": BINARY, "name_or_address": [absolute(a) for a in arguments], "timeout_sec": 120})
    elif command == "xrefs":
        request = ("list_xrefs", {"binary_name": BINARY, "name_or_address": [absolute(a) for a in arguments]})
    elif command == "symbols":
        request = ("search_symbols_by_name", {"binary_name": BINARY, "query": arguments[0], "limit": int(arguments[1]) if len(arguments) > 1 else 50})
    elif command == "strings":
        request = ("search_strings", {"binary_name": BINARY, "query": arguments[0], "limit": int(arguments[1]) if len(arguments) > 1 else 50})
    elif command == "dis":
        request = ("disassemble", {"binary_name": BINARY, "address": absolute(arguments[0]), "count": int(arguments[1]) if len(arguments) > 1 else 40})
    elif command == "bytes":
        request = ("read_bytes", {"binary_name": BINARY, "address": absolute(arguments[0]), "size": int(arguments[1]) if len(arguments) > 1 else 64})
    elif command == "raw":
        request = (arguments[0], json.loads(arguments[1]))
    else:
        sys.exit(__doc__)
    show(asyncio.run(call(*request)))


if __name__ == "__main__":
    main()
