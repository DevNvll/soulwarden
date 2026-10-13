#!/usr/bin/env python3
"""Resolve an actor message type of the client: type id, type index, and codec functions.

The client registers each message type through `Amazon::Hub::InstallRegistrationHook<T>`.
This tool starts from the runtime type name of that hook and follows the code to the type
descriptor. It prints the type id, the index in typeindex.json, and the image-relative
addresses of the marshal and unmarshal functions.

Usage: hubtype.py NAME_REGEX [--max N]
Needs the Ghidra MCP server (tools/ghidra/start-mcp.sh).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import struct
import sys
from pathlib import Path

import capstone
from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nwbin import Image, game_executable  # noqa: E402

MCP_URL = "http://127.0.0.1:8765/mcp"
TEXT_END = 0x7EC8000
UUID_TEXT = re.compile(r"^\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?$")


async def find_hooks(pattern: str, limit: int):
    async with streamablehttp_client(MCP_URL) as (reader, writer, _):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            result = await session.call_tool(
                "search_symbols_by_name",
                {"binary_name": "/NewWorld.exe", "query": pattern, "limit": limit},
            )
            payload = result.structuredContent or {}
            return payload.get("symbols", payload.get("result", {}).get("symbols", []))


def is_code(image: Image, rva: int) -> bool:
    return 0x1000 <= rva < TEXT_END


def qword(image: Image, rva: int) -> int:
    return struct.unpack("<Q", image.read(rva, 8))[0]


def direct_calls(image: Image, engine, start: int, size: int) -> list[int]:
    targets = []
    for instruction in engine.disasm(image.read(start, size), start):
        if instruction.mnemonic in ("call", "jmp") and instruction.operands:
            operand = instruction.operands[0]
            if operand.type == capstone.x86.X86_OP_IMM and not start <= operand.imm < start + size:
                targets.append(operand.imm)
    return targets


def rip_targets(image: Image, engine, start: int, size: int) -> list[int]:
    targets = []
    for instruction in engine.disasm(image.read(start, size), start):
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                targets.append(instruction.address + instruction.size + operand.mem.disp)
    return targets


def function_size(image: Image, rva: int, fallback: int = 0x200) -> int:
    bounds = image.function_of(rva)
    return bounds[1] - bounds[0] if bounds and bounds[0] == rva else fallback


def resolve(image: Image, engine, type_index: dict[str, int], descriptor_rva: int):
    """Follow one hook lambda descriptor to its type getter."""
    references = image.code_references([descriptor_rva])[descriptor_rva]
    for site, _ in references:
        target_type = site - 3  # lea rax, [rip + descriptor]
        slots = image.pointer_references([target_type])[target_type]
        for slot in slots:
            do_call = qword(image, slot - 8) - image.image_base
            if not is_code(image, do_call):
                continue
            for callee in direct_calls(image, engine, do_call, function_size(image, do_call)):
                size = function_size(image, callee, 0)
                if size == 0:
                    continue
                name = None
                type_id = None
                operations = None
                for target in rip_targets(image, engine, callee, size):
                    text = image.string_at(target)
                    if text:
                        if UUID_TEXT.match(text):
                            type_id = text.strip("{}").upper()
                        elif name is None and re.match(r"^[A-Za-z_][A-Za-z0-9_:<>, ]*$", text):
                            name = text
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
                entry = {
                    "name": name,
                    "type_id": type_id,
                    "type_index": type_index.get(type_id.replace("-", "")),
                    "getter": f"0x{callee:X}",
                }
                if operations is not None:
                    entry["operations"] = f"0x{operations:X}"
                    entry["marshal"] = f"0x{qword(image, operations + 32) - image.image_base:X}"
                    entry["unmarshal"] = f"0x{qword(image, operations + 40) - image.image_base:X}"
                return entry
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pattern")
    parser.add_argument("--max", type=int, default=20)
    arguments = parser.parse_args()

    image = Image(game_executable())
    engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    engine.detail = True
    index_path = game_executable().parent.parent / "typeindex.json"
    type_index = {value: position for position, value in enumerate(json.loads(index_path.read_text())["typeIndex"])}

    symbols = asyncio.run(find_hooks(f"InstallRegistrationHook<.*{arguments.pattern}", arguments.max))
    seen = set()
    for symbol in symbols:
        namespace = symbol.get("namespace", "")
        match = re.search(r"InstallRegistrationHook<(.*)>\(void\)", namespace)
        if not match or symbol.get("name") != "RTTI_Type_Descriptor":
            continue
        label = match.group(1)
        if label in seen:
            continue
        seen.add(label)
        descriptor = int(symbol["address"], 16) - image.image_base
        entry = resolve(image, engine, type_index, descriptor)
        print(label)
        if entry is None:
            print("  not resolved")
            continue
        for key, value in entry.items():
            print(f"  {key}: {value}")


if __name__ == "__main__":
    main()
