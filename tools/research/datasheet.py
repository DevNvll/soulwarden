#!/usr/bin/env python3
"""Read a game data table (`*.datasheet`) of the client (analysis only).

Extract the file first with pakx.py. Layout (little-endian, format version 0x12):

  0x00 version:u32
  0x04 nameCrc:u32 nameOffset:u32        file name of the source table
  0x0C typeCrc:u32 typeOffset:u32        table type ("Objectives", "ObjectiveTasks", ...)
  0x38 stringsOffset:u32                 start of the text block, relative to 0x3C
  0x44 columns:u32 rows:u32
  0x5C columns * { crc:u32 nameOffset:u32 type:u32 }      type 1 text, 2 number (f32), 3 flag
       rows * columns * { text:u32 value:u32 }             text: offset of the cell text in the
                                                           text block; value: the same offset
                                                           (text), f32 (number), 0 or 1 (flag)
       text block (zero-terminated texts)

Usage:
  datasheet.py FILE...                     header and column list of each file
  datasheet.py FILE... --rows REGEX        rows where a cell matches (all non-empty cells)
  datasheet.py FILE... --rows REGEX --cols A,B,C     only these columns
  datasheet.py FILE... --all               all rows
  datasheet.py FILE... --tsv               all rows as tab-separated text with a header line
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

HEADER_SIZE = 0x5C
TYPE_TEXT, TYPE_NUMBER, TYPE_FLAG = 1, 2, 3


class Sheet:
    def __init__(self, path: Path):
        data = path.read_bytes()
        self.path = path
        self.version = struct.unpack_from("<I", data, 0)[0]
        strings = struct.unpack_from("<I", data, 0x38)[0] + 0x3C
        self._data = data
        self._strings = strings
        self.name = self._text(struct.unpack_from("<I", data, 0x08)[0])
        self.type = self._text(struct.unpack_from("<I", data, 0x10)[0])
        column_count, row_count = struct.unpack_from("<II", data, 0x44)
        self.columns = []
        self.column_types = []
        offset = HEADER_SIZE
        for _ in range(column_count):
            _, name_offset, kind = struct.unpack_from("<III", data, offset)
            self.columns.append(self._text(name_offset))
            self.column_types.append(kind)
            offset += 12
        self.rows = []
        for _ in range(row_count):
            row = []
            for kind in self.column_types:
                text, raw = struct.unpack_from("<II", data, offset)
                if kind == TYPE_TEXT:
                    row.append(self._text(text))
                elif kind == TYPE_NUMBER:
                    value = struct.unpack_from("<f", data, offset + 4)[0]
                    row.append(int(value) if value == int(value) else value)
                else:
                    row.append(bool(raw))
                offset += 8
            self.rows.append(row)

    def _text(self, offset: int) -> str:
        start = self._strings + offset
        end = self._data.index(b"\0", start)
        return self._data[start:end].decode("utf-8", "replace")

    def records(self):
        for row in self.rows:
            yield dict(zip(self.columns, row))


def shown(value) -> bool:
    return value not in ("", 0, False)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--rows", help="regular expression; print rows where a cell matches")
    parser.add_argument("--cols", help="comma-separated column names to print")
    parser.add_argument("--all", action="store_true", help="print all rows")
    parser.add_argument("--tsv", action="store_true", help="print all rows as tab-separated text")
    arguments = parser.parse_args()
    pattern = re.compile(arguments.rows) if arguments.rows else None
    wanted = arguments.cols.split(",") if arguments.cols else None
    for name in arguments.files:
        try:
            sheet = Sheet(Path(name))
        except (struct.error, ValueError) as error:
            print(f"FAILED {name}: {error}", file=sys.stderr)
            continue
        if arguments.tsv:
            columns = wanted or sheet.columns
            print("\t".join(columns))
            for record in sheet.records():
                print("\t".join(str(record.get(column, "")) for column in columns))
            continue
        if not (pattern or arguments.all):
            print(f"{name}: type {sheet.type}, name {sheet.name}, {len(sheet.rows)} rows, {len(sheet.columns)} columns")
            print("  " + ", ".join(sheet.columns))
            continue
        for index, record in enumerate(sheet.records()):
            if pattern and not any(pattern.search(str(value)) for value in record.values()):
                continue
            print(f"--- {Path(name).name} row {index}")
            for column, value in record.items():
                if wanted is not None and column not in wanted:
                    continue
                if wanted is not None or shown(value):
                    print(f"  {column} = {value}")


if __name__ == "__main__":
    main()
