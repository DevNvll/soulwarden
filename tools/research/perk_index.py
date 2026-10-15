#!/usr/bin/env python3
"""Compare the perk index list of the live client with the perk data of the backend.

A perk of an item on the wire is not an id. It is an index into a list that the client makes
at its start (ReplicationDataManager::BuildPerkList, image RVA 0x448E310; protocol notes 4.23):
entry 0 is "no perk", then the rows of each perk table in the sequence of the table map of the
perk manager, and in each table the rows in the sequence of the stored list of that table (the
copy of a hash map, 0x477D8F0). Entry N is the CRC-32 of the lower-case PerkID of the perk with
the index N. The tool tools/gamedata/perks.sh computes the same list from the data tables. A
list that is not equal gives a wrong perk on an item with no log line.

The tool reads from the memory of the client through the automation harness (tools/hx):

  the list      the pointer at image +0xA3A6610, then the pointer at that address (the
                replication data manager), then the begin and the end of the vector at +0x08 and
                +0x10 of the manager
  the tables    the pointer at image +0xA327818, then the pointer at that address (the perk
                manager); its table map is a linked list at +0x208: each node has the CRC of the
                table name at +0x10, its inner list at +0x20, and the bucket count at +0x80

The client has the lists from the main menu on.

Usage: perk_index.py [--save FILE] [--dump FILE] [--expect FILE] [--rows]
  --save FILE    also write the list of the client to a file (4 bytes for each entry)
  --dump FILE    compare a file of --save, with no game
  --expect FILE  compare with a list in a different file: a JSON list of objects with "index"
                 and "crc" (the research file perk-indices.json), not the perk data
  --rows         also count the rows of each table in the inner lists of the client (slow)
Status 0: each entry is equal. Status 1: a difference, or no read.
Environment: SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata), SOULWARDEN_DATA_DIR.
Do not run two harness tools at the same time: the harness has one command file.
"""

from __future__ import annotations

import hashlib
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
LIST_POINTER = 0xA3A6610
MANAGER_POINTER = 0xA327818
TABLE_MAP = 0x208
# The harness gives 512 bytes at most for one read.
READ_SIZE = 512
READS_IN_CALL = 48
DATA_NAME = "perk-data.json"


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


def pointer(address: int) -> int:
    data = harness([(address, 8)])[0]
    if len(data) != 8:
        sys.exit(f"perk-index: no read at 0x{address:x} (is a development run alive?)")
    return struct.unpack("<Q", data)[0]


def read_block(begin: int, size: int) -> bytes:
    data = b""
    requests = [(begin + offset, min(READ_SIZE, size - offset)) for offset in range(0, size, READ_SIZE)]
    for first in range(0, len(requests), READS_IN_CALL):
        for (_, wanted), part in zip(requests[first:first + READS_IN_CALL], harness(requests[first:first + READS_IN_CALL])):
            if len(part) != wanted:
                sys.exit("perk-index: a read of the list failed")
            data += part
    return data


def client_list() -> bytes:
    holder = pointer(pointer(IMAGE + LIST_POINTER))
    if holder == 0:
        sys.exit("perk-index: the client has no replication data manager now")
    bounds = harness([(holder + 8, 16)])[0]
    if len(bounds) != 16:
        sys.exit("perk-index: no read of the list bounds")
    begin, end = struct.unpack("<QQ", bounds)
    size = end - begin
    if size <= 0 or size > 1048576 or size % 4 != 0:
        sys.exit(f"perk-index: the list of the client has no valid size ({size} bytes)")
    return read_block(begin, size)


def client_tables(count_rows: bool) -> list[dict]:
    """@return The tables of the perk manager in the sequence of its map: name CRC, buckets, and (with count_rows) rows."""
    manager = pointer(pointer(IMAGE + MANAGER_POINTER))
    if manager == 0:
        sys.exit("perk-index: the client has no perk manager now")
    root = manager + TABLE_MAP
    node = pointer(root)
    tables = []
    while node != root:
        if len(tables) > 32:
            sys.exit("perk-index: the table map of the client has no end")
        head = harness([(node, 0x88)])[0]
        if len(head) != 0x88:
            sys.exit("perk-index: a read of a table node failed")
        following = struct.unpack_from("<Q", head, 0)[0]
        key = struct.unpack_from("<I", head, 0x10)[0]
        buckets = struct.unpack_from("<Q", head, 0x80)[0]
        entry = {"crc": key, "buckets": buckets}
        if count_rows:
            inner = node + 0x20
            current = struct.unpack_from("<Q", head, 0x20)[0]
            rows = 0
            while current != inner:
                rows += 1
                if rows > 5000:
                    sys.exit("perk-index: an inner list of the client has no end")
                current = pointer(current)
            entry["rows"] = rows
        tables.append(entry)
        node = following
    return tables


def perk_crc(perk_id: str) -> int:
    """@return The CRC-32 of a PerkID as the client computes it: only the letters A to Z go to lower case."""
    data = bytes(byte + 0x20 if 0x41 <= byte <= 0x5A else byte for byte in perk_id.encode("utf-8"))
    return zlib.crc32(data) & 0xFFFFFFFF


def main() -> int:
    arguments = sys.argv[1:]
    save = dump = expect = None
    count_rows = False
    while arguments:
        option = arguments.pop(0)
        if option == "--save" and arguments:
            save = Path(arguments.pop(0))
        elif option == "--dump" and arguments:
            dump = Path(arguments.pop(0))
        elif option == "--expect" and arguments:
            expect = Path(arguments.pop(0))
        elif option == "--rows":
            count_rows = True
        else:
            print(__doc__.strip())
            return 1
    data_home = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    names: dict[int, str] = {}
    expected_tables: list[dict] = []
    try:
        if expect:
            rows = json.loads(expect.read_text(encoding="utf-8"))
            expected = [0] + [int(row["crc"], 16) for row in sorted(rows, key=lambda row: row["index"])]
            ids = [""] + [row.get("id", "") for row in sorted(rows, key=lambda row: row["index"])]
            for row in rows:
                if "table" in row:
                    names[perk_crc(row["table"])] = row["table"]
        else:
            perk_data = json.loads((gamedata / DATA_NAME).read_text(encoding="utf-8"))
            ids = [""] + [perk[0] for perk in perk_data["perks"]]
            expected = [0] + [perk_crc(perk_id) for perk_id in ids[1:]]
            expected_tables = perk_data["tables"]
            for table in expected_tables:
                names[perk_crc(table["name"])] = table["name"]
    except (OSError, ValueError, KeyError) as error:
        sys.exit(f"perk-index: no list to compare ({error}). Make the perk data with tools/gamedata/perks.sh")
    data = dump.read_bytes() if dump else client_list()
    if save:
        save.write_bytes(data)
    client = struct.unpack(f"<{len(data) // 4}I", data)
    source = f"the file {dump.name}" if dump else "the memory of the client"
    print(f"list of the client ({source}): {len(client)} entries, SHA-256 of the 32-bit values: "
          f"{hashlib.sha256(data).hexdigest()}")
    reference = struct.pack(f"<{len(expected)}I", *expected)
    print(f"expected list: {len(expected)} entries, SHA-256 {hashlib.sha256(reference).hexdigest()}")
    different = [index for index in range(min(len(client), len(expected))) if client[index] != expected[index]]
    print(f"entries that are equal: {min(len(client), len(expected)) - len(different)}. Entries that are different: "
          f"{len(different)}.")
    for index in different[:10]:
        print(f"  index {index}: client 0x{client[index]:08X}, expected 0x{expected[index]:08X} ({ids[index]})")
    tables_equal = True
    if not dump:
        tables = client_tables(count_rows)
        print("tables of the perk manager, in the sequence of its map:")
        first = 1
        for number, table in enumerate(tables):
            name = names.get(table["crc"], f"0x{table['crc']:08X}")
            rows = table.get("rows")
            line = f"  {number + 1}. {name}: {table['buckets']} buckets"
            if rows is not None:
                line += f", {rows} rows, indexes {first} to {first + rows - 1}"
                first += rows
            if number < len(expected_tables):
                want = expected_tables[number]
                same = want["name"] == name and want["buckets"] == table["buckets"] and (rows is None or rows == want["rows"])
                tables_equal = tables_equal and same
                line += f" (perk data: {want['name']}, {want['buckets']} buckets, {want['rows']} rows)"
            print(line)
        if expected_tables and len(tables) != len(expected_tables):
            tables_equal = False
    equal = len(client) == len(expected) and not different and tables_equal
    print("result: the list of the client is equal to the expected list" if equal
          else "result: the list of the client is NOT equal to the expected list")
    return 0 if equal else 1


if __name__ == "__main__":
    sys.exit(main())
