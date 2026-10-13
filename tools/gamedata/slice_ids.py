#!/usr/bin/env python3
"""Write the ids of the faceted components of the player slice of the game as one JSON file.

The client gives each component of a slice instance a facet target id (protocol notes 4.2):

  K               = id of the root entity of the slice XOR entity group id
  instance entity = entity id in the slice file XOR K
  target          = (instance entity + component id) mod 2^64

The body of a component call of the client starts with the target. The backend reads this
file and checks that each target is a facet of the entity of the call.

Input: the entry `slices/player.dynamicslice` of the pak files of the game copy
(`<game>/assets/**/*.pak`): an engine object stream. The pak entry of the supported build has
the compression method 15 (Oodle), which Python cannot read. Thus the tool uses the first of
these sources that it has:

  1. The pak entry, when it is stored or deflate data.
  2. The pak entry with the Oodle helper of tools/research (oodle_loader.c), when
     SOULWARDEN_OODLE_DLL names an Oodle library that you supply (SOULWARDEN_OODLE_DEC: the helper).
  3. A file of the slice that tools/research/pakx.py extracted: SOULWARDEN_SLICE_FILE, default
     ~/Games/soulwarden/work/assets/extract/slices/player.dynamicslice.

The tool needs only the standard library of Python and the module appearance_tables.py of
this folder (pak entry reader).

Output: {"format": 1, "player": {"path": ..., "root": ID, "components": [{"entity": ID,
"component": ID, "name": TEXT}, ...]}}. An id is a text of 16 hexadecimal digits: a JSON
number cannot hold 64 bits. "name" is the name of the slice entity and the type id of the
component class.

Usage: slice_ids.py [OUTPUT.json]
Default output: <SOULWARDEN_GAMEDATA_DIR>/slice-ids.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_SLICE_FILE, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC (see above)
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import uuid
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id, plain_stream, read_entry  # noqa: E402

SLICE_PATH = "slices/player.dynamicslice"
OUTPUT_NAME = "slice-ids.json"
# Type ids of the engine classes (public engine source).
ENTITY_TYPE = uuid.UUID("75651658-8663-478d-9090-2432dfcafa44")
COMPONENT_TYPE = uuid.UUID("edfcb2cf-f75d-43be-b26b-f35821b29247")
# Zip compression method of an Oodle entry.
OODLE = 15
# Entity id that means "no entity": the parent of the root entity of a slice.
NO_ENTITY = 0xFFFFFFFF

ID_FIELD = field_id("Id")
NAME_FIELD = field_id("Name")
COMPONENTS_FIELD = field_id("Components")
BASE_CLASS_FIELDS = {field_id(f"BaseClass{number}") for number in range(1, 10)}
PARENT_FIELD = field_id("m_parentId")
LOCAL_TRANSFORM_FIELD = field_id("m_localTM")
FACET_FIELDS = (field_id("m_clientFacetPtr"), field_id("m_serverFacetPtr"))


class Element:
    __slots__ = ("name", "type_id", "value", "children")

    def __init__(self) -> None:
        self.name: int | None = None
        self.type_id = uuid.UUID(int=0)
        self.value: bytes | None = None
        self.children: list[Element] = []


def parse(data: bytes) -> list[Element]:
    """Reads a binary object stream (big-endian). @return Its root elements.

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
        element.type_id = uuid.UUID(bytes=data[position:position + 16])
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
    return roots


def find_field(component: Element, wanted: int) -> Element | None:
    """@return The first field with this name in a component or in its base classes."""
    queue = [component]
    while queue:
        node = queue.pop(0)
        for child in node.children:
            if child.name == wanted:
                return child
            if child.name in BASE_CLASS_FIELDS:
                queue.append(child)
    return None


def id_value(element: Element | None) -> int | None:
    """@return The 64-bit id of an id field: its own value, or the value of its first child."""
    if element is None:
        return None
    for candidate in [element, *element.children]:
        if candidate.value is not None and len(candidate.value) == 8:
            return int.from_bytes(candidate.value, "big")
    return None


def component_id(component: Element) -> int | None:
    """@return The id of a component: the field Id of its base class AZ::Component."""
    queue = [component]
    while queue:
        node = queue.pop(0)
        if node.type_id == COMPONENT_TYPE:
            for child in node.children:
                if child.name == ID_FIELD and child.value is not None and len(child.value) == 8:
                    return int.from_bytes(child.value, "big")
        queue.extend(child for child in node.children if child.name in BASE_CLASS_FIELDS)
    return None


def read_slice(roots: list[Element]) -> dict:
    entities: list[tuple[str, int, int | None]] = []
    components: list[dict] = []

    def walk(element: Element, depth: int) -> None:
        if element.type_id == ENTITY_TYPE and depth > 0:
            entity_id = None
            name = ""
            for child in element.children:
                if child.name == ID_FIELD:
                    entity_id = id_value(child)
                if child.name == NAME_FIELD and child.value is not None:
                    name = child.value.decode("utf-8", "replace")
            if entity_id is None:
                raise Failure(f"the entity {name or '(no name)'} of the slice has no id")
            parent = None
            for child in element.children:
                if child.name != COMPONENTS_FIELD:
                    continue
                for component in child.children:
                    if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                        parent = id_value(find_field(component, PARENT_FIELD))
                    if not any(find_field(component, facet) is not None for facet in FACET_FIELDS):
                        continue
                    identifier = component_id(component)
                    if identifier is None:
                        raise Failure(f"a faceted component of the entity {name} has no component id")
                    components.append({"entity": f"{entity_id:016X}", "component": f"{identifier:016X}",
                                       "name": f"{name}/{component.type_id}"})
            entities.append((name, entity_id, parent))
        for child in element.children:
            walk(child, depth + 1)

    for root in roots:
        walk(root, 0)
    slice_roots = [entity for entity in entities if entity[2] == NO_ENTITY]
    if not slice_roots:
        raise Failure("the slice has no root entity (an entity whose transform has no parent)")
    if not components:
        raise Failure("the slice has no faceted component")
    return {"path": SLICE_PATH, "root": f"{slice_roots[-1][1]:016X}", "rootName": slice_roots[-1][0],
            "entities": len(entities), "components": components}


def oodle_entry(pak: Path, info: zipfile.ZipInfo) -> bytes | None:
    """@return The content of an Oodle pak entry, or no value when the helper or the library is not there."""
    library = os.environ.get("SOULWARDEN_OODLE_DLL", "")
    helper = Path(os.environ.get("SOULWARDEN_OODLE_DEC") or Path.home() / "Games/soulwarden/work/assets/bin/oodle_dec")
    if not library or not helper.is_file():
        return None
    with open(pak, "rb") as handle:
        handle.seek(info.header_offset)
        header = handle.read(30)
        name_size, extra_size = struct.unpack_from("<HH", header, 26)
        handle.seek(info.header_offset + 30 + name_size + extra_size)
        data = handle.read(info.compress_size)
    result = subprocess.run([str(helper), library, str(info.file_size)], input=data, capture_output=True, check=False)
    if result.returncode != 0:
        raise Failure(f"the Oodle helper failed: {result.stderr.decode(errors='replace').strip()}")
    return plain_stream(result.stdout)


def load_slice(assets: Path, extracted: Path) -> tuple[list[Element], str]:
    """@return The root elements of the slice, and a text that names the source."""
    if not assets.is_dir():
        raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
    paks = sorted(assets.rglob("*.pak"))
    if not paks:
        raise Failure(f"no game copy: {assets} has no pak file")
    found = False
    for pak in paks:
        try:
            with zipfile.ZipFile(pak) as archive:
                entries = [info for info in archive.infolist() if info.filename.lower() == SLICE_PATH]
        except (OSError, zipfile.BadZipFile) as error:
            raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
        for info in entries:
            found = True
            try:
                if info.compress_type != OODLE:
                    return parse(read_entry(pak, info)), f"{pak.name}"
                data = oodle_entry(pak, info)
                if data is not None:
                    return parse(data), f"{pak.name} (Oodle helper)"
            except (Failure, OSError, zlib.error, struct.error, IndexError, ValueError) as error:
                raise Failure(f"{pak.name}: {info.filename}: {error}") from error
    if not found:
        raise Failure(f"the pak files of {assets} have no entry {SLICE_PATH}")
    if extracted.is_file():
        try:
            return parse(plain_stream(extracted.read_bytes())), f"extracted file {extracted}"
        except (Failure, OSError, zlib.error, struct.error, IndexError, ValueError) as error:
            raise Failure(f"{extracted}: {error}") from error
    raise Failure(f"the pak entry {SLICE_PATH} has Oodle data. Set SOULWARDEN_OODLE_DLL to an Oodle library, or extract "
                  f"the slice with tools/research/pakx.py (SOULWARDEN_SLICE_FILE, default {extracted})")


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else gamedata / OUTPUT_NAME
    extracted = Path(os.environ.get("SOULWARDEN_SLICE_FILE") or home / "Games/soulwarden/work/assets/extract" / SLICE_PATH)
    try:
        roots, source = load_slice(game / "assets", extracted)
        document = {"format": 1, "player": read_slice(roots)}
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(document, indent=1) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"slice-ids: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"slice-ids: {target}: {error}", file=sys.stderr)
        return 1
    player = document["player"]
    print(f"{target}: player slice with {player['entities']} entities, root {player['rootName']} {player['root']}, "
          f"{len(player['components'])} faceted components (source: {source})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
