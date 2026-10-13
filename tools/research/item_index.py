#!/usr/bin/env python3
"""Compare the item index list of the live client with the item catalog of the backend.

An item on the wire is an index into a list of the client (`ItemDataManager::m_indexToIdMap`,
protocol notes 4.10): entry N is the CRC-32 of the lower-case id of the item with the index
N. The tool tools/gamedata/items.sh computes the same list from the data tables. A list that
is not equal gives wrong items in the bag with no log line.

The tool reads the list from the memory of the client through the automation harness
(tools/hx): the pointer at image +0xA327838, then the pointer at that address (the manager),
then the begin and the end of the list at manager +0x3C8 and +0x3D0. The client has the list
from the main menu on.

Usage: item_index.py [--save FILE] [--dump FILE]
  --save FILE   also write the list of the client to a file (4 bytes for each entry)
  --dump FILE   compare a file of --save, with no game
Status 0: each entry is equal. Status 1: a difference, or no read.
Environment: SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata), SOULWARDEN_DATA_DIR.
Do not run two harness tools at the same time: the harness has one command file.
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

HX = Path(__file__).resolve().parents[1] / "hx"
IMAGE = 0x140000000
MANAGER_POINTER = 0xA327838
LIST_BEGIN = 0x3C8
# The harness gives 512 bytes at most for one read.
READ_SIZE = 512
READS_IN_CALL = 48
CATALOG_NAME = "item-catalog.json"


def harness(requests: list[tuple[int, int]]) -> list[bytes]:
    """@return The bytes of each (address, size) read of one harness call."""
    commands = [f"read 0x{address:x} {size}" for address, size in requests]
    output = subprocess.run([str(HX), *commands], capture_output=True, text=True, check=False).stdout
    result = []
    for line in output.splitlines():
        try:
            result.append(bytes.fromhex(line.strip()))
        except ValueError:
            result.append(b"")
    return result + [b""] * (len(requests) - len(result))


def client_list() -> bytes:
    holder = harness([(IMAGE + MANAGER_POINTER, 8)])[0]
    if len(holder) != 8:
        sys.exit("item-index: no read of the client memory (is a development run alive?)")
    manager = harness([(struct.unpack("<Q", holder)[0], 8)])[0]
    if len(manager) != 8 or manager == bytes(8):
        sys.exit("item-index: the client has no item data manager now")
    bounds = harness([(struct.unpack("<Q", manager)[0] + LIST_BEGIN, 16)])[0]
    if len(bounds) != 16:
        sys.exit("item-index: no read of the list bounds")
    begin, end = struct.unpack("<QQ", bounds)
    size = end - begin
    if size <= 0 or size > 4 * 1048576 or size % 4 != 0:
        sys.exit(f"item-index: the list of the client has no valid size ({size} bytes)")
    data = b""
    requests = [(begin + offset, min(READ_SIZE, size - offset)) for offset in range(0, size, READ_SIZE)]
    for first in range(0, len(requests), READS_IN_CALL):
        for (_, wanted), part in zip(requests[first:first + READS_IN_CALL], harness(requests[first:first + READS_IN_CALL])):
            if len(part) != wanted:
                sys.exit("item-index: a read of the list failed")
            data += part
    return data


def item_id_crc(item_id: str) -> int:
    """@return The CRC-32 of an item id as the client computes it: only the letters A to Z go to lower case."""
    data = bytes(byte + 0x20 if 0x41 <= byte <= 0x5A else byte for byte in item_id.encode("utf-8"))
    return zlib.crc32(data) & 0xFFFFFFFF


def main() -> int:
    arguments = sys.argv[1:]
    save = dump = None
    while arguments:
        option = arguments.pop(0)
        if option == "--save" and arguments:
            save = Path(arguments.pop(0))
        elif option == "--dump" and arguments:
            dump = Path(arguments.pop(0))
        else:
            print(__doc__.strip())
            return 1
    data_home = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    try:
        catalog = json.loads((gamedata / CATALOG_NAME).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        sys.exit(f"item-index: no item catalog ({error}). Make it with tools/gamedata/items.sh")
    data = dump.read_bytes() if dump else client_list()
    if save:
        save.write_bytes(data)
    client = struct.unpack(f"<{len(data) // 4}I", data)
    items = catalog["items"]
    expected = [0] + [item_id_crc(item[0]) for item in items]
    source = f"the file {dump.name}" if dump else "the memory of the client"
    print(f"list of the client ({source}): {len(client)} entries. Catalog: {len(expected)} entries.")
    different = [index for index in range(min(len(client), len(expected))) if client[index] != expected[index]]
    print(f"entries that are equal: {min(len(client), len(expected)) - len(different)}. Entries that are different: "
          f"{len(different)}.")
    for index in different[:10]:
        print(f"  index {index}: client 0x{client[index]:08X}, catalog 0x{expected[index]:08X} ({items[index - 1][0]})")
    print(f"  {'index':>6}  {'client':<10}  {'catalog':<10}  item id (table)")
    print(f"  {0:>6}  0x{client[0]:08X}  0x{expected[0]:08X}  no item")
    for table in catalog["tables"]:
        for index in sorted({table["first"], table["last"]}):
            if table["last"] < table["first"] or index >= len(client):
                continue
            print(f"  {index:>6}  0x{client[index]:08X}  0x{expected[index]:08X}  {items[index - 1][0]} ({table['name']})")
    equal = len(client) == len(expected) and not different
    print("result: the list of the client is equal to the catalog" if equal
          else "result: the list of the client is NOT equal to the catalog")
    return 0 if equal else 1


if __name__ == "__main__":
    sys.exit(main())
