#!/usr/bin/env python3
"""Extract single files from the pak files of the game (analysis only).

A pak file is a zip archive. Method 0 is stored, method 8 is deflate, method 15 is Oodle.
For Oodle the tool calls the helper `oodle_dec` (build it from oodle_loader.c) with a Windows
Oodle library that you supply. The index ~/Games/soulwarden/work/assets/pak-index.tsv gives the
pak file of each path. Output goes to ~/Games/soulwarden/work/assets/extract/<path>; the engine
stream header ("AZCS") is removed.

Usage: pakx.py PATH_REGEX [--max N] [--list]
Environment: SOULWARDEN_OODLE_DLL (path of oo2core_*_win64.dll), SOULWARDEN_OODLE_DEC (helper path).
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from azcs import decode  # noqa: E402

WORK = Path.home() / "Games/soulwarden/work/assets"
GAME_ASSETS = Path.home() / "Games/soulwarden/game/assets"
HELPER = Path(os.environ.get("SOULWARDEN_OODLE_DEC", WORK / "bin/oodle_dec"))
LIBRARY = os.environ.get("SOULWARDEN_OODLE_DLL", "")


def raw_entry(pak: Path, info: zipfile.ZipInfo) -> bytes:
    """@return The stored bytes of one entry (after its local header)."""
    with open(pak, "rb") as handle:
        handle.seek(info.header_offset)
        header = handle.read(30)
        name_size, extra_size = struct.unpack_from("<HH", header, 26)
        handle.seek(info.header_offset + 30 + name_size + extra_size)
        return handle.read(info.compress_size)


def unpack(pak: Path, info: zipfile.ZipInfo) -> bytes:
    data = raw_entry(pak, info)
    if info.compress_type == 0:
        return data
    if info.compress_type == 8:
        return zlib.decompress(data, -15)
    if info.compress_type == 15:
        if not LIBRARY:
            sys.exit("set SOULWARDEN_OODLE_DLL to an oo2core_*_win64.dll")
        result = subprocess.run([str(HELPER), LIBRARY, str(info.file_size)], input=data, capture_output=True)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.decode(errors="replace").strip())
        return result.stdout
    raise RuntimeError(f"compression method {info.compress_type} is not supported")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("pattern")
    parser.add_argument("--max", type=int, default=20)
    parser.add_argument("--list", action="store_true", help="only print the matches")
    arguments = parser.parse_args()
    pattern = re.compile(arguments.pattern)
    matches: dict[str, list[str]] = {}
    count = 0
    with open(WORK / "pak-index.tsv", encoding="utf-8", errors="replace") as index:
        for line in index:
            pak, _, rest = line.partition("\t")
            path = rest.partition("\t")[0]
            if pattern.search(path):
                matches.setdefault(pak, []).append(path)
                count += 1
                if count >= arguments.max:
                    break
    for pak, paths in matches.items():
        archive_path = GAME_ASSETS / pak
        if arguments.list:
            for path in paths:
                print(f"{pak}\t{path}")
            continue
        with zipfile.ZipFile(archive_path) as archive:
            for path in paths:
                info = archive.getinfo(path)
                try:
                    data = decode(unpack(archive_path, info))
                except (RuntimeError, ValueError) as error:
                    print(f"FAILED {path}: {error}")
                    continue
                target = WORK / "extract" / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                print(f"{target} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
