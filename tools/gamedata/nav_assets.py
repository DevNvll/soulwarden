#!/usr/bin/env python3
"""Read the files of the game copy for the navigation tools.

This module has the three readers that each navigation tool needs:

  1. `Paks`: the pak files of the game copy (zip archives). It gives the plain content of one
     entry. The compression methods are 0 (stored), 8 (deflate), and 15 (Oodle). For Oodle the
     reader uses the first source that it has: an Oodle library of this computer
     (SOULWARDEN_OODLE_LIB, a shared library with the function OodleLZ_Decompress), or the
     Oodle helper of tools/research (SOULWARDEN_OODLE_DLL and SOULWARDEN_OODLE_DEC), or the
     extracted file (SOULWARDEN_EXTRACT_DIR).
  2. `parse`: a binary object stream of the engine (a slice, a region file).
  3. `Catalog`: the asset catalog, which gives the path of an asset id.

The index of the pak files is kept in a cache file (the pak files have 1.4 million entries),
with the size and the time of each pak file as its key.

The module needs only the standard library of Python.

Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC,
             SOULWARDEN_EXTRACT_DIR (default ~/Games/soulwarden/work/assets/extract)
"""

from __future__ import annotations

import ctypes
import os
import pickle
import struct
import subprocess
import sys
import uuid
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

STORED = 0
DEFLATE = 8
OODLE = 15
AZCS_ZLIB = 0x73887D3A
CATALOG_PATH = "assetcatalog.catalog"


class Failure(Exception):
    """A source is not there, or a file does not decode. The tool stops with status 1."""


def field_id(label: str) -> int:
    """@return The number of a field in an object stream: the CRC-32 of its name in lower case."""
    return zlib.crc32(label.lower().encode()) & 0xFFFFFFFF


def game_dir() -> Path:
    return Path(os.environ.get("SOULWARDEN_GAME_DIR") or Path.home() / "Games/soulwarden/game")


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


class Paks:
    """The pak files of the game copy, and the plain content of one entry."""

    def __init__(self, cache: Path | None = None) -> None:
        assets = game_dir() / "assets"
        if not assets.is_dir():
            raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
        paks = sorted(assets.rglob("*.pak"))
        if not paks:
            raise Failure(f"no game copy: {assets} has no pak file")
        self.assets = assets
        key = [(str(pak.relative_to(assets)), pak.stat().st_size, pak.stat().st_mtime_ns) for pak in paks]
        self.entries: dict[str, tuple[int, int, int, int, int]] = {}
        self.paks = [pak for pak in paks]
        loaded = False
        if cache is not None and cache.is_file():
            try:
                with open(cache, "rb") as handle:
                    stored = pickle.load(handle)
                if stored.get("key") == key:
                    self.entries = stored["entries"]
                    loaded = True
            except (OSError, pickle.UnpicklingError, EOFError, AttributeError):
                loaded = False
        if not loaded:
            for number, pak in enumerate(paks):
                try:
                    with zipfile.ZipFile(pak) as archive:
                        for info in archive.infolist():
                            name = info.filename.lower().replace("\\", "/")
                            self.entries.setdefault(name, (number, info.header_offset, info.compress_type,
                                                           info.compress_size, info.file_size))
                except (OSError, zipfile.BadZipFile) as error:
                    raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
            if cache is not None:
                cache.parent.mkdir(parents=True, exist_ok=True)
                with open(cache, "wb") as handle:
                    pickle.dump({"key": key, "entries": self.entries}, handle, protocol=pickle.HIGHEST_PROTOCOL)
        home = Path.home()
        self.extracted = Path(os.environ.get("SOULWARDEN_EXTRACT_DIR") or home / "Games/soulwarden/work/assets/extract")
        self.windows_library = os.environ.get("SOULWARDEN_OODLE_DLL", "")
        self.helper = Path(os.environ.get("SOULWARDEN_OODLE_DEC") or home / "Games/soulwarden/work/assets/bin/oodle_dec")
        self.native = None
        library = os.environ.get("SOULWARDEN_OODLE_LIB", "")
        if library:
            try:
                self.native = ctypes.CDLL(library)
                self.native.OodleLZ_Decompress.restype = ctypes.c_ssize_t
                self.native.OodleLZ_Decompress.argtypes = [
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int, ctypes.c_int,
                    ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_void_p,
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int]
            except (OSError, AttributeError) as error:
                raise Failure(f"SOULWARDEN_OODLE_LIB ({library}) is not an Oodle library: {error}") from error
        self.handles: dict[int, object] = {}
        self.files_read = 0
        self.bytes_read = 0

    def has(self, path: str) -> bool:
        return path in self.entries

    def paths(self, prefix: str, suffix: str = "") -> list[str]:
        return sorted(name for name in self.entries if name.startswith(prefix) and name.endswith(suffix))

    def _raw(self, number: int, offset: int, size: int) -> bytes:
        handle = self.handles.get(number)
        if handle is None:
            handle = open(self.paks[number], "rb")
            self.handles[number] = handle
        handle.seek(offset)
        header = handle.read(30)
        name_size, extra_size = struct.unpack_from("<HH", header, 26)
        handle.seek(offset + 30 + name_size + extra_size)
        return handle.read(size)

    def read(self, path: str) -> bytes:
        """@return The plain content of one entry. It raises `Failure` when the entry has no source."""
        entry = self.entries.get(path)
        if entry is None:
            raise Failure(f"{path}: not in the pak files of the game copy")
        number, offset, method, packed, size = entry
        pak = self.paks[number]
        try:
            self.files_read += 1
            self.bytes_read += packed
            if method == STORED:
                return plain_stream(self._raw(number, offset, packed))
            if method == DEFLATE:
                return plain_stream(zlib.decompress(self._raw(number, offset, packed), -15))
            if method != OODLE:
                raise Failure(f"compression method {method} is not supported")
            if self.native is not None:
                data = self._raw(number, offset, packed)
                out = ctypes.create_string_buffer(size)
                count = self.native.OodleLZ_Decompress(data, len(data), out, size, 1, 1, 0, None, 0, None, None,
                                                       None, 0, 3)
                if count != size:
                    raise Failure(f"the Oodle library gave {count} bytes, not {size}")
                return plain_stream(out.raw)
            if self.windows_library and self.helper.is_file():
                result = subprocess.run([str(self.helper), self.windows_library, str(size)],
                                        input=self._raw(number, offset, packed), capture_output=True, check=False)
                if result.returncode != 0:
                    raise Failure(f"the Oodle helper failed: {result.stderr.decode(errors='replace').strip()}")
                return plain_stream(result.stdout)
            file = self.extracted / path
            if file.is_file():
                return plain_stream(file.read_bytes())
        except (Failure, OSError, zlib.error, struct.error) as error:
            raise Failure(f"{pak.name}: {path}: {error}") from error
        raise Failure(f"{path}: the pak entry has Oodle data and no source: set SOULWARDEN_OODLE_LIB or "
                      f"SOULWARDEN_OODLE_DLL to an Oodle library")


class Element:
    __slots__ = ("name", "type_id", "value", "children")

    def __init__(self) -> None:
        self.name: int | None = None
        self.type_id = b""
        self.value: bytes | None = None
        self.children: list[Element] = []

    def child(self, wanted: int) -> Element | None:
        for node in self.children:
            if node.name == wanted:
                return node
        return None


def parse(data: bytes) -> list[Element]:
    """Reads a binary object stream (big-endian). @return Its root elements.

    stream  := tag:u8 = 0, version:u32, element*
    element := flags:u8 [name:u32 if 0x40] [classVersion:u8 if 0x80] typeId:16 bytes
               [value if 0x10] element* 0x00
    value   := size bytes. The size is the low 3 bits of the flags, or with flag 0x20 a number
               that has so many bytes.
    The type id of an element is kept as its 16 bytes.
    """
    if len(data) < 5 or data[0] != 0:
        raise Failure("not a binary object stream")
    position = 5
    end = len(data)
    roots: list[Element] = []
    stack: list[Element] = []
    unpack_name = struct.Struct(">I").unpack_from
    while position < end:
        flags = data[position]
        position += 1
        if flags == 0:
            if not stack:
                break
            stack.pop()
            continue
        element = Element()
        if flags & 0x40:
            element.name = unpack_name(data, position)[0]
            position += 4
        if flags & 0x80:
            position += 1
        element.type_id = data[position:position + 16]
        position += 16
        if flags & 0x10:
            size = flags & 7
            if flags & 0x20:
                size = int.from_bytes(data[position:position + size], "big")
                position += flags & 7
            element.value = data[position:position + size]
            position += size
            if position > end:
                raise Failure("the object stream ends in a value")
        (stack[-1].children if stack else roots).append(element)
        stack.append(element)
    if not roots:
        raise Failure("the object stream has no element")
    return roots


def walk(element: Element):
    yield element
    for child in element.children:
        yield from walk(child)


def type_bytes(text: str) -> bytes:
    return uuid.UUID(text).bytes


class Catalog:
    """The asset catalog of the game: the path of each asset id.

    header := "RASC" version:u32 size:u32 u32 guids:u32 types:u32 folders:u32 files:u32 end:u32 count:u32
    record := guid:u32 sub:u32 u32 u32 type:u32 u32 size:u64 folder:u32 file:u32     (40 bytes, from offset 40)
    `guid` and `type` are indexes into two tables of 16 bytes for each entry, and `folder` and
    `file` are offsets into two blocks of texts that end with a zero byte.
    Only the records whose file name has one of the wanted endings are kept.
    """

    def __init__(self, data: bytes, endings: tuple[str, ...]) -> None:
        if len(data) < 40 or data[:4] != b"RASC":
            raise Failure("the asset catalog has no RASC header")
        _, _, size, _, guids, _, folders, files, _, count = struct.unpack_from("<4s9I", data)
        if size != len(data) or 40 + 40 * count > len(data):
            raise Failure("the asset catalog does not have the size of its header")
        self.paths: dict[tuple[bytes, int], str] = {}
        self.by_guid: dict[bytes, list[tuple[int, str]]] = {}
        folder_names: dict[int, str] = {}
        file_names: dict[int, str | None] = {}
        view = memoryview(data)
        for guid, sub, _, _, _, _, _, folder, file in struct.iter_unpack("<6IQ2I", view[40:40 + 40 * count]):
            name = file_names.get(file, "")
            if name == "":
                start = files + file
                name = data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
                name = name if name.endswith(endings) else None
                file_names[file] = name
            if name is None:
                continue
            folder_name = folder_names.get(folder)
            if folder_name is None:
                start = folders + folder
                folder_name = data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
                folder_names[folder] = folder_name
            key = data[guids + 16 * guid:guids + 16 * guid + 16]
            self.paths.setdefault((key, sub), folder_name + name)
            self.by_guid.setdefault(key, []).append((sub, folder_name + name))

    def path(self, asset: tuple[bytes, int] | None) -> str | None:
        return self.paths.get(asset) if asset is not None else None

    def products(self, guid: bytes) -> list[tuple[int, str]]:
        """@return Each product of one source asset: (sub id, path)."""
        return self.by_guid.get(guid, [])


def asset_id(element: Element | None) -> tuple[bytes, int] | None:
    """@return The asset id of an asset field (16 bytes and the sub id), or no value for an empty field."""
    if element is None:
        return None
    value = element.value
    if value is None:
        # An asset id with two children: the 16 bytes and the sub id.
        if len(element.children) >= 2 and element.children[0].value is not None \
                and element.children[1].value is not None and len(element.children[0].value) == 16:
            guid = element.children[0].value
            sub = int.from_bytes(element.children[1].value, "big")
            return None if guid == bytes(16) else (guid, sub)
        return None
    if len(value) < 20:
        return None
    guid, sub = value[:16], int.from_bytes(value[16:20], "big")
    return None if guid == bytes(16) else (guid, sub)


def text(element: Element | None) -> str:
    return element.value.decode("utf-8", "replace") if element is not None and element.value is not None else ""


def number(element: Element | None) -> int:
    """@return The whole number of a field: its own value, or the value of its one child."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    return int.from_bytes(element.value, "big") if element is not None and element.value is not None else 0


def real(element: Element | None) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        return 0.0
    return struct.unpack(">f", element.value)[0]
