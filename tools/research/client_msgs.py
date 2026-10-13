#!/usr/bin/env python3
"""List the client actor messages of a captured run.

Usage: client_msgs.py RUN_DIRECTORY [TYPE_INDEX] [--max N] [--session N]

RUN_DIRECTORY is the capture directory of a run (DATA_DIR/captures). The game server keeps the
files of each session in its own folder: rep/s0001, rep/s0002, ... (a run from before stage
0.6a has all files in rep). Without --session the tool reads all sessions in sequence, and
each file name has its session folder.

Each NNNNN-msg-LEN.bin file is one client frame: crc32:u32 size:u32 route:16 bytes
envelope flags:u8 [8 bytes if bit 0] [8 bytes if bit 1] present:u8 type:size body.
Without TYPE_INDEX the tool prints a count for each type. With TYPE_INDEX it prints the
body of each message of that type.
"""

import argparse
import collections
import json
import sys
from pathlib import Path

WORK = Path.home() / "Games/soulwarden/work"


def read_size(data: bytes, position: int):
    """Prefix code of the hub: N leading one bits give N more bytes, low bits first."""
    first = data[position]
    extra = 0
    while extra < 4 and first & (0x80 >> extra):
        extra += 1
    value = first & (0x7F >> extra)
    shift = 7 - extra
    for index in range(extra):
        value |= data[position + 1 + index] << shift
        shift += 8
    return value, position + 1 + extra


def session_folders(rep_directory: Path, session: int | None = None) -> list[Path]:
    """Capture folders of the game server in the sequence of the sessions.

    `rep_directory` is the folder DATA_DIR/captures/rep. A run from before stage 0.6a has no
    session folders: the result is then the folder itself.
    """
    folders = sorted(path for path in rep_directory.glob("s[0-9]*") if path.is_dir())
    if not folders:
        return [rep_directory]
    if session is not None:
        return [path for path in folders if path.name == f"s{session:04d}"]
    return folders


def capture_files(rep_directory: Path, kind: str, session: int | None = None) -> list[tuple[str, Path]]:
    """Capture files of one kind ("msg", "in", "out") as (label, path), in sequence.

    The label is the file name, with the session folder before it when the run has session
    folders ("s0001/00012-msg-508.bin").
    """
    files = []
    for folder in session_folders(rep_directory, session):
        prefix = f"{folder.name}/" if folder != rep_directory else ""
        files.extend((prefix + path.name, path) for path in sorted(folder.glob(f"*-{kind}-*.bin")))
    return files


def messages(directory: Path, session: int | None = None):
    """Client messages of a run: (label, type index, body). `directory` is DATA_DIR/captures."""
    for label, path in capture_files(directory / "rep", "msg", session):
        data = path.read_bytes()
        position = 8 + 16
        flags = data[position]
        position += 1
        if flags & 1:
            position += 8
        if flags & 2:
            position += 8
        if flags & 4 or data[position] != 1:
            continue
        type_index, position = read_size(data, position + 1)
        yield label, type_index, data[position:]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("run")
    parser.add_argument("type", nargs="?", type=int)
    parser.add_argument("--max", type=int, default=20)
    parser.add_argument("--session", type=int, help="read only the folder of this session (1, 2, ...)")
    arguments = parser.parse_args()
    names = {entry["type_index"]: entry["name"] for entry in json.loads((WORK / "hubtypes.json").read_text())}
    if arguments.type is None:
        counts = collections.Counter()
        sizes = collections.defaultdict(set)
        for _, type_index, body in messages(Path(arguments.run), arguments.session):
            counts[type_index] += 1
            sizes[type_index].add(len(body))
        for type_index, count in counts.most_common():
            print(f"{type_index:5d} x{count:<5d} sizes {sorted(sizes[type_index])[:8]} {names.get(type_index, '?')}")
        return
    shown = 0
    for name, type_index, body in messages(Path(arguments.run), arguments.session):
        if type_index != arguments.type:
            continue
        print(f"{name} {len(body):4d} {body.hex()}")
        shown += 1
        if shown >= arguments.max:
            break


if __name__ == "__main__":
    main()
