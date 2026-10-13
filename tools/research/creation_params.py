#!/usr/bin/env python3
"""Decode or build the character data text of the gateway service (analysis only).

`CharacterCreationParams` (create request) and `PublishedData` (character document) have the
same form (client writer 0x17A75B0 + 0x16A6030, client reader 0x171A500 + 0x16B3850):

    text  := base64( zlib( json ) )
    json  := {"currency":int,"visualsBlob":string,"position":{"x","y","z"},
              "currentLevel":int,"worldId":string}
    visualsBlob := binary engine object stream of the class PlayerSaveData

The JSON writer of the client copies bytes of 0x80 and more without change and writes bytes
below 0x20 as escapes. Thus the JSON text is not valid UTF-8 and a strict JSON parser rejects
it. This tool reads it as bytes.

Usage:
  creation_params.py decode FILE_OR_TEXT
      FILE can be a capture file of the backend: the first "CharacterCreationParams" or
      "PublishedData" member is used. Prints the JSON members, the PlayerSaveData fields, the
      list indexes, and the bytes of MB::PlayerAppearanceComponentReplicatedState (type 1195).
  creation_params.py make WORLD_ID [field=value ...]
      Builds a text from names, for example: gender=Female race=face03 skinTone=skin02.
      The object stream of this mode has the text fields only (no equipment). The client did
      not get such a text in a test.

The index tables are the appearance tables of the backend:
<SOULWARDEN_GAMEDATA_DIR>/appearance-tables.json, default folder ~/.local/share/soulwarden/gamedata
(tools/gamedata/appearance.sh makes the file).
"""

from __future__ import annotations

import base64
import json
import os
import re
import sys
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from objstream import parse  # noqa: E402

TABLES = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR")
              or Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share") / "soulwarden/gamedata"
              ) / "appearance-tables.json"
STRING_TYPE = uuid.UUID("03AAAB3F-5C47-5A66-9EBC-D5FA4DB353C9")
# Type id of the root element in the captures of the create request.
SAVE_DATA_TYPE = uuid.UUID("144C7576-7FBA-4250-B0C8-F06803657D79")
SAVE_DATA_VERSION = 3
# Reflection order of PlayerSaveData (0x36FDFA0).
FIELDS = ["playerName", "companyName", "entityClassName", "guildCrestName", "gender", "race", "skinTone",
          "hairstyle", "facialHair", "hairColor", "facialHairColor", "eyeColor", "faceMark", "scar", "tattoo",
          "tattooColor", "backstory", "startingFaction", "appearanceData", "equipment"]
CRC = {zlib.crc32(name.lower().encode()) & 0xFFFFFFFF: name for name in FIELDS}
DEFAULTS = {"playerName": "Soulwarden", "entityClassName": "PlayerMale", "gender": "Male", "race": "face01",
            "skinTone": "skin04", "hairstyle": "hairstyle01m", "facialHair": "None", "hairColor": "haircolor1",
            "facialHairColor": "haircolor1", "eyeColor": "eyecolor1", "faceMark": "none", "scar": "none",
            "tattoo": "none", "tattooColor": "tattoocolor1", "backstory": "Backstory1"}
ESCAPES = {ord("b"): 8, ord("f"): 12, ord("n"): 10, ord("r"): 13, ord("t"): 9, ord('"'): 34, ord("\\"): 92,
           ord("/"): 47}


def json_string(data: bytes, start: int) -> tuple[bytes, int]:
    """@return The bytes of the JSON string that starts at the quote at `start`, and its end."""
    result = bytearray()
    position = start + 1
    while data[position] != 0x22:
        byte = data[position]
        if byte != 0x5C:
            result.append(byte)
            position += 1
        elif data[position + 1] == ord("u"):
            code = int(data[position + 2:position + 6], 16)
            result += bytes([code]) if code < 0x80 else chr(code).encode("utf-8")
            position += 6
        else:
            result.append(ESCAPES[data[position + 1]])
            position += 2
    return bytes(result), position + 1


def json_escape(value: bytes) -> bytes:
    """Escape a byte string with the rules of the JSON writer of the client (rapidjson 1.0.2)."""
    result = bytearray()
    short = {8: b"\\b", 12: b"\\f", 10: b"\\n", 13: b"\\r", 9: b"\\t", 34: b'\\"', 92: b"\\\\"}
    for byte in value:
        if byte in short:
            result += short[byte]
        elif byte < 0x20:
            result += b"\\u%04X" % byte
        else:
            result.append(byte)
    return bytes(result)


def find_text(argument: str) -> str:
    path = Path(argument)
    if path.is_file():
        content = path.read_text(encoding="utf-8", errors="replace")
        match = re.search(r'"(?:CharacterCreationParams|PublishedData)"\s*:\s*"([A-Za-z0-9+/=]+)"', content)
        return match.group(1) if match else content.strip()
    return argument.strip()


def index_of(names: list[str], name: str, label: str) -> int:
    for index, entry in enumerate(names):
        if entry.lower() == name.lower():
            return index
    print(f"  ! {label} {name!r} is not in the list; index 0 is used")
    return 0


def indexes(fields: dict[str, str]) -> list[int]:
    tables = json.loads(TABLES.read_text(encoding="utf-8"))
    gender = index_of(tables["genders"], fields.get("gender", ""), "gender")
    races = tables[tables["genders"][gender]]["races"]
    race = index_of([entry["name"] for entry in races], fields.get("race", ""), "race")
    hair = tables["hairStyles"][races[race]["hairStyles"]]
    facial = tables["hairStyles"][races[race]["facialHair"]]
    return [
        gender,
        race,
        index_of(tables["skinTones"][races[race]["skinTones"]], fields.get("skinTone", ""), "skinTone"),
        index_of(hair["styles"], fields.get("hairstyle", ""), "hairstyle"),
        index_of(facial["styles"], fields.get("facialHair", ""), "facialHair"),
        index_of(hair["colors"], fields.get("hairColor", ""), "hairColor"),
        index_of(facial["colors"], fields.get("facialHairColor", ""), "facialHairColor"),
        index_of(tables["eyeColors"], fields.get("eyeColor", ""), "eyeColor"),
        index_of(tables["faceMarks"], fields.get("faceMark", ""), "faceMark"),
        index_of(tables["scars"], fields.get("scar", ""), "scar"),
        index_of(tables["tattoos"], fields.get("tattoo", ""), "tattoo"),
        index_of(tables["tattooColors"], fields.get("tattooColor", ""), "tattooColor"),
    ]


def decode(argument: str) -> None:
    document = zlib.decompress(base64.b64decode(find_text(argument)))
    key = document.find(b'"visualsBlob"')
    blob = b""
    outside = document
    if key >= 0:
        quote = document.index(b'"', document.index(b":", key))
        blob, end = json_string(document, quote)
        outside = document[:quote] + b'""' + document[end:]
    print(f"json ({len(document)} bytes, visualsBlob removed): {outside.decode('utf-8', 'replace')}")
    print(f"visualsBlob: {len(blob)} bytes, first byte 0x{blob[0]:02X}" if blob else "visualsBlob: empty")
    if not blob or blob[0] != 0:
        print("  not a binary object stream; no field print")
        return
    version, roots = parse(blob)
    root = roots[0]
    print(f"object stream version {version}, root type {{{root.type_id.upper()}}} class version {root.version}")
    fields: dict[str, str] = {}
    for element in root.children:
        name = CRC.get(element.name, f"#{element.name:08X}" if element.name is not None else "?")
        if element.children:
            print(f"  {name}: container with {len(element.children)} elements")
            for entry in element.children:
                texts = [item.value.decode("utf-8", "replace") if item.value else "" for item in entry.children]
                print(f"    {texts}")
            continue
        value = element.value or b""
        if element.type_id == str(STRING_TYPE):
            fields[name] = value.decode("utf-8", "replace")
            print(f"  {name} = {fields[name]!r}")
        else:
            print(f"  {name} = 0x{value.hex()} (type {{{element.type_id}}})")
    values = indexes(fields)
    print("indexes (gender race skinTone hairstyle facialHair hairColor facialHairColor eyeColor faceMark scar "
          "tattoo tattooColor):")
    print("  " + " ".join(str(value) for value in values))
    state = bytes([0x01, 0xFF, *values[:7], 0x1F, *values[7:]])
    print(f"state type 1195 (slot 12): {state.hex(' ').upper()}")


def element(name: str | None, type_id: uuid.UUID, value: bytes, version: int = 0) -> bytes:
    flags = 0x08 | (0x40 if name is not None else 0) | (0x80 if version else 0)
    size = b""
    if value:
        flags |= 0x10
        if len(value) < 8:
            flags |= len(value)
        else:
            width = 1 if len(value) < 0x100 else 2 if len(value) < 0x10000 else 4
            flags |= 0x20 | width
            size = len(value).to_bytes(width, "big")
    header = bytes([flags])
    if name is not None:
        header += (zlib.crc32(name.lower().encode()) & 0xFFFFFFFF).to_bytes(4, "big")
    if version:
        header += bytes([version])
    return header + type_id.bytes + size + value


def make(world_id: str, pairs: list[str]) -> None:
    fields = dict(DEFAULTS)
    for pair in pairs:
        key, _, value = pair.partition("=")
        if key not in FIELDS:
            sys.exit(f"unknown field {key}")
        fields[key] = value
    if "entityClassName" not in dict(pair.partition("=")[::2] for pair in pairs):
        fields["entityClassName"] = "PlayerFemale" if fields["gender"].lower() == "female" else "PlayerMale"
    stream = b"\x00" + (3).to_bytes(4, "big") + element(None, SAVE_DATA_TYPE, b"", SAVE_DATA_VERSION)
    for name in FIELDS:
        if name in fields:
            stream += element(name, STRING_TYPE, fields[name].encode()) + b"\x00"
    stream += b"\x00\x00"
    document = (b'{"currency":0,"visualsBlob":"' + json_escape(stream)
                + b'","position":{"x":0.0,"y":0.0,"z":0.0},"currentLevel":1,"worldId":"' + world_id.encode() + b'"}')
    print(base64.b64encode(zlib.compress(document)).decode())


def main() -> None:
    if len(sys.argv) >= 3 and sys.argv[1] == "decode":
        decode(sys.argv[2])
    elif len(sys.argv) >= 3 and sys.argv[1] == "make":
        make(sys.argv[2], sys.argv[3:])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
