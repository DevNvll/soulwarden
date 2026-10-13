#!/usr/bin/env python3
"""Build the table of all actor message types of the client.

Output: JSON list with name, type id, type index, and codec function addresses.
Usage: build_hubtypes.py OUTPUT_JSON
Needs the Ghidra MCP server (tools/ghidra/start-mcp.sh).
"""

from __future__ import annotations

import asyncio
import json
import re
import struct
import sys
from pathlib import Path

import capstone
import numpy as np
from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from hubtype import MCP_URL, direct_calls, function_size, is_code, qword, rip_targets, UUID_TEXT  # noqa: E402
from nwbin import Image, game_executable  # noqa: E402


async def all_hooks():
    async with streamablehttp_client(MCP_URL) as (reader, writer, _):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            result = await session.call_tool(
                "search_symbols_by_name",
                {"binary_name": "/NewWorld.exe", "query": "InstallRegistrationHook<", "limit": 20000},
            )
            payload = result.structuredContent or {}
            return payload.get("symbols", payload.get("result", {}).get("symbols", []))


def main() -> None:
    output = Path(sys.argv[1])
    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    index_path = game_executable().parent.parent / "typeindex.json"
    type_index = {value: position for position, value in enumerate(json.loads(index_path.read_text())["typeIndex"])}

    labels: dict[int, str] = {}
    for symbol in asyncio.run(all_hooks()):
        match = re.search(r"Install(?:Actor)?RegistrationHook<(.*)>\(void\)", symbol.get("namespace", ""))
        if match and symbol.get("name") == "RTTI_Type_Descriptor":
            labels[int(symbol["address"], 16) - image.image_base] = match.group(1)
    print(f"{len(labels)} hooks", file=sys.stderr)

    descriptors = sorted(labels)
    references = image.code_references(descriptors)
    target_types = {}
    for descriptor in descriptors:
        for site, _ in references[descriptor]:
            target_types[site - 3] = descriptor
    slots = image.pointer_references(sorted(target_types))

    entries = []
    for target_type, descriptor in target_types.items():
        for slot in slots[target_type]:
            do_call = qword(image, slot - 8) - image.image_base
            if not is_code(image, do_call):
                continue
            resolved = None
            for callee in direct_calls(image, engine, do_call, function_size(image, do_call)):
                size = function_size(image, callee, 0)
                if size == 0:
                    continue
                type_id = None
                operations = None
                for target in rip_targets(image, engine, callee, size):
                    text = image.string_at(target)
                    if text:
                        if UUID_TEXT.match(text):
                            type_id = text.strip("{}").upper()
                        continue
                    section = image.section_of(target)
                    if section and section[0] == ".rdata" and operations is None:
                        try:
                            first = qword(image, target) - image.image_base
                            fifth = qword(image, target + 40) - image.image_base
                        except (ValueError, struct.error):
                            continue
                        if is_code(image, first) and is_code(image, fifth):
                            operations = target
                if type_id is None:
                    continue
                resolved = {
                    "name": labels[descriptor],
                    "type_id": type_id,
                    "type_index": type_index.get(type_id.replace("-", "")),
                    "getter": callee,
                }
                if operations is not None:
                    resolved["marshal"] = qword(image, operations + 32) - image.image_base
                    resolved["unmarshal"] = qword(image, operations + 40) - image.image_base
                break
            if resolved:
                entries.append(resolved)
                break
    entries.sort(key=lambda entry: (entry["type_index"] is None, entry["type_index"] or 0))
    output.write_text(json.dumps(entries, indent=0))
    print(f"{len(entries)} types resolved", file=sys.stderr)


if __name__ == "__main__":
    main()
