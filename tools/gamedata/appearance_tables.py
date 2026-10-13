#!/usr/bin/env python3
"""Write the name lists of the character creation databases of the game as one JSON file.

The world state `MB::PlayerAppearanceComponentReplicatedState` has list indexes. The creation
data of a character has names (`PlayerSaveData`). The backend reads this file and converts
each name to its index (protocol notes 2.2.5).

Input: the pak files of the game copy (`<game>/assets/**/*.pak`). A pak file is a zip archive.
The databases are the entries of the folder
`sharedassets/genericassets/character/charactercreationdatabase/`. Each entry is an engine
object stream with the "AZCS" header and zlib data. The tool needs only the standard library
of Python and no other file.

Usage: appearance_tables.py [OUTPUT.json]
Default output: <SOULWARDEN_GAMEDATA_DIR>/appearance-tables.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata)
"""

from __future__ import annotations

import json
import os
import struct
import sys
import zipfile
import zlib
from pathlib import Path, PurePosixPath

DATABASE_FOLDER = "sharedassets/genericassets/character/charactercreationdatabase/"
OUTPUT_NAME = "appearance-tables.json"
# Compressor id of zlib in the "AZCS" header.
AZCS_ZLIB = 0x73887D3A
# Zip compression methods of a pak entry.
STORED = 0
DEFLATE = 8
# Offset of the path text in the value of an asset reference field.
ASSET_HINT_OFFSET = 56


class Failure(Exception):
    """A condition that stops the tool. The text goes to the user."""


def field_id(label: str) -> int:
    """@return The number of a field in an object stream: the CRC-32 of its name in lower case."""
    return zlib.crc32(label.lower().encode()) & 0xFFFFFFFF


class Element:
    __slots__ = ("name", "value", "children")

    def __init__(self) -> None:
        self.name: int | None = None
        self.value = b""
        self.children: list[Element] = []

    def child(self, label: str) -> Element | None:
        wanted = field_id(label)
        return next((entry for entry in self.children if entry.name == wanted), None)

    def text(self, label: str) -> str:
        entry = self.child(label)
        return "" if entry is None else entry.value.decode("utf-8", "replace")


def plain_stream(data: bytes) -> bytes:
    """@return The content of an engine compressed stream: "AZCS" compressor:u32 size:u64, data."""
    if data[:4] != b"AZCS":
        return data
    compressor, size = struct.unpack_from(">IQ", data, 4)
    if compressor != AZCS_ZLIB:
        raise Failure(f"stream compressor 0x{compressor:08X} is not supported")
    # After the header: the number of seek points (u32), then the zlib data.
    plain = zlib.decompressobj().decompress(data[20:])
    if len(plain) != size:
        raise Failure(f"stream size {len(plain)} is not the header size {size}")
    return plain


def parse(data: bytes) -> Element:
    """Reads a binary object stream (big-endian). @return Its first root element.

    stream  := tag:u8 = 0, version:u32, element*
    element := flags:u8 [name:u32 if 0x40] [classVersion:u8 if 0x80] typeId:16 bytes
               [value if 0x10] element* 0x00
    value   := size bytes. The size is the low 3 bits of the flags, or with flag 0x20 a number
               that has so many bytes.
    """
    if len(data) < 5 or data[0] != 0:
        raise Failure("not a binary object stream")
    position = 5
    roots: list[Element] = []
    stack: list[Element] = []
    while position < len(data):
        flags = data[position]
        position += 1
        if flags == 0:
            if not stack:
                break
            stack.pop()
            continue
        element = Element()
        if flags & 0x40:
            element.name = struct.unpack_from(">I", data, position)[0]
            position += 4
        if flags & 0x80:
            position += 1
        position += 16
        if flags & 0x10:
            size = flags & 7
            if flags & 0x20:
                size = int.from_bytes(data[position:position + size], "big")
                position += flags & 7
            element.value = data[position:position + size]
            position += size
        (stack[-1].children if stack else roots).append(element)
        stack.append(element)
    if not roots:
        raise Failure("the object stream has no element")
    return roots[0]


def read_entry(pak: Path, info: zipfile.ZipInfo) -> bytes:
    """@return The content of one pak entry."""
    with open(pak, "rb") as handle:
        handle.seek(info.header_offset)
        header = handle.read(30)
        name_size, extra_size = struct.unpack_from("<HH", header, 26)
        handle.seek(info.header_offset + 30 + name_size + extra_size)
        data = handle.read(info.compress_size)
    if info.compress_type == DEFLATE:
        data = zlib.decompress(data, -15)
    elif info.compress_type != STORED:
        raise Failure(f"compression method {info.compress_type} is not supported")
    return plain_stream(data)


def load_databases(assets: Path) -> dict[str, Element]:
    """@return The root element of each database, by the file name of its entry."""
    if not assets.is_dir():
        raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
    paks = sorted(assets.rglob("*.pak"))
    if not paks:
        raise Failure(f"no game copy: {assets} has no pak file")
    databases: dict[str, Element] = {}
    for pak in paks:
        try:
            with zipfile.ZipFile(pak) as archive:
                entries = [info for info in archive.infolist() if info.filename.lower().startswith(DATABASE_FOLDER)]
        except (OSError, zipfile.BadZipFile) as error:
            raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
        for info in entries:
            name = PurePosixPath(info.filename.lower()).name
            try:
                databases[name] = parse(read_entry(pak, info))
            except (Failure, OSError, zlib.error, struct.error, IndexError) as error:
                raise Failure(f"{pak.name}: {info.filename}: {error}") from error
    if not databases:
        raise Failure(f"the pak files of {assets} have no entry in {DATABASE_FOLDER}")
    return databases


def entry_names(root: Element, label: str) -> list[str]:
    """@return The value of the name field of each entry of one list field."""
    container = root.child(label)
    return [] if container is None else [entry.text("name") for entry in container.children]


def asset_stem(element: Element | None) -> str:
    """@return The file name (no folder, no extension) of an asset reference field."""
    if element is None or len(element.value) <= ASSET_HINT_OFFSET:
        return ""
    return PurePosixPath(element.value[ASSET_HINT_OFFSET:].decode("ascii", "replace")).stem


def build_tables(databases: dict[str, Element]) -> dict:
    tables: dict = {"genders": ["Male", "Female"], "skinTones": {}, "hairStyles": {}}
    for name in sorted(databases):
        root = databases[name]
        stem, _, suffix = name.rpartition(".")
        if suffix == "charcreatedb":
            races = []
            container = root.child("Races")
            for entry in ([] if container is None else container.children):
                races.append({
                    "name": entry.text("name"),
                    "skinTones": asset_stem(entry.child("Skin Tone Database Style Sheet")),
                    "hairStyles": asset_stem(entry.child("Hair Style Database Style Sheet")),
                    "facialHair": asset_stem(entry.child("Facial Hair Database Style Sheet")),
                    "defaultSkinTone": entry.text("Default Skin Tone"),
                })
            tables[root.text("gender")] = {"races": races}
        elif suffix == "skintonedb":
            tables["skinTones"][stem] = entry_names(root, "Skin Tones")
        elif suffix == "hairstyledb":
            tables["hairStyles"][stem] = {
                "styles": entry_names(root, "Hair Styles"),
                "colors": entry_names(root, "Hair Colors"),
            }
        elif suffix == "eyecolordb":
            tables["eyeColors"] = entry_names(root, "Eye Colors")
        elif suffix == "facemarkdb":
            tables["faceMarks"] = entry_names(root, "Face Mark Styles")
        elif suffix == "scardb":
            tables["scars"] = entry_names(root, "Scar Styles")
        elif suffix == "tattoodb":
            tables["tattoos"] = entry_names(root, "Tattoo Styles")
            tables["tattooColors"] = entry_names(root, "Tattoo Colors")
    return tables


def check_tables(tables: dict) -> None:
    """Stops the tool when a list that the backend uses is absent or empty."""
    problems = []
    for gender in tables["genders"]:
        races = tables.get(gender, {}).get("races", [])
        if not races:
            problems.append(f"no race of the gender {gender}")
        for race in races:
            label = f"{gender} {race['name'] or '(no name)'}"
            if not race["name"]:
                problems.append(f"a race of the gender {gender} has no name")
            if not tables["skinTones"].get(race["skinTones"]):
                problems.append(f"no skin tone list of {label}")
            for key, text in (("hairStyles", "hairstyle"), ("facialHair", "facial hair")):
                lists = tables["hairStyles"].get(race[key], {})
                if not lists.get("styles") or not lists.get("colors"):
                    problems.append(f"no {text} list of {label}")
    for key in ("eyeColors", "faceMarks", "scars", "tattoos", "tattooColors"):
        if not tables.get(key):
            problems.append(f"no list {key}")
    if problems:
        shown = "; ".join(problems[:5])
        more = f" (and {len(problems) - 5} more)" if len(problems) > 5 else ""
        raise Failure(f"the databases of the game copy are not complete: {shown}{more}")


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else gamedata / OUTPUT_NAME
    try:
        tables = build_tables(load_databases(game / "assets"))
        check_tables(tables)
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(tables, indent=1) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"appearance: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"appearance: {target}: {error}", file=sys.stderr)
        return 1
    print(f"{target}: {len(tables['Male']['races'])} male races, {len(tables['Female']['races'])} female races, "
          f"{len(tables['skinTones'])} skin tone lists, {len(tables['hairStyles'])} hair lists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
