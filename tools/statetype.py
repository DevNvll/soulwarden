#!/usr/bin/env python3
"""Print a replicated state layout from the client instructions.

Usage: statetype.py NAME_REGEX [--json] [--group-variable RVA=VALUE]

Read hubtypes.json and the game image. No Ghidra query is necessary. Report an
uncertain layout with the instruction address and return status 1. Never print
a partial layout as a complete layout. Reader pointers can remain unknown when
an opaque field constructor initializes them. That does not change field order.

Vitals uses two group variables whose values have live proof in protocol 4.5:
  --group-variable 0xA563290=0 --group-variable 0xA563298=0
The tool requires these values explicitly. It does not assume a live value from
the initial bytes of the image. Addresses apply to the supported client build.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nwbin import Image, game_executable, work_directory  # noqa: E402
from state_disassembly import (  # noqa: E402
    Instructions, Interpreter, OBJECT, UncertainLayout, constructor_of,
)


def group_variable(text):
    try:
        address, value = text.split('=', 1)
        address, value = int(address, 0), int(value, 0)
        if not 0 <= value < 10 or not 0 <= address < 0x100000000:
            raise ValueError
        return address, value
    except ValueError as exc:
        raise argparse.ArgumentTypeError('Use image-relative RVA=GROUP, with a group from 0 to 9.') from exc


def analyze(entry, image, instructions, values):
    row = {'name': entry['name'], 'type_index': entry.get('type_index')}
    try:
        constructor = constructor_of(image, int(entry['unmarshal']), instructions)
        machine = Interpreter(image, instructions, values)
        row.update(machine.result(constructor, wrapper=constructor == int(entry['unmarshal'])))
        readers = {}
        for group, fields in row['groups'].items():
            for index, (_, slot) in enumerate(fields):
                table = machine.read(OBJECT + slot * 8, 8)
                if table is None:
                    continue
                table = image.normalize(table)
                try:
                    reader = struct.unpack('<Q', image.read(table + 48, 8))[0] - image.image_base
                except (ValueError, struct.error):
                    continue
                if 0x1000 <= reader < 0x7EC8000:
                    readers[f'{group}:{index}'] = {'table': table, 'read': reader}
        row['readers'] = readers
    except (UncertainLayout, ValueError, KeyError) as exc:
        row['error'] = str(exc)
    return row


def print_layout(row):
    label = f"{row['name']}: type {row['type_index']}"
    if 'error' in row:
        print(f"{label} UNCERTAIN: {row['error']}")
        return
    print(f"{label} constructor 0x{row['constructor']:X} groups {row['count']}, {row['form']}")
    for group, fields in sorted(row['groups'].items()):
        print(f'  group {group}' + (' (empty)' if not fields else ''))
        for index, (name, slot) in enumerate(fields):
            reader = row['readers'].get(f'{group}:{index}')
            detail = f"table 0x{reader['table']:X} read 0x{reader['read']:X}" if reader else 'table ? (field initialization not proved)'
            print(f'    {index:2d} {name:40s} +0x{slot * 8:X} {detail}')
    if row['special']:
        print(f"  header fields: {[name for name, _ in row['special']]}")
    for note in row['notes']:
        print(f'  note: {note}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('pattern')
    parser.add_argument('--json', action='store_true', help='Print the recovered layouts and explicit errors as JSON.')
    parser.add_argument('--group-variable', action='append', default=[], type=group_variable)
    arguments = parser.parse_args()
    try:
        pattern = re.compile(arguments.pattern)
    except re.error as exc:
        parser.error(str(exc))
    entries = json.loads((work_directory() / 'hubtypes.json').read_text())
    image = Image(game_executable())
    instructions = Instructions(image)
    rows = [analyze(entry, image, instructions, dict(arguments.group_variable)) for entry in entries
            if str(entry.get('name', '')).endswith('ReplicatedState')
            and '::ClientMessages::' not in entry['name'] and pattern.search(entry['name'])]
    if arguments.json:
        print(json.dumps(rows, indent=2))
    else:
        for row in rows:
            print_layout(row)
    return int(any('error' in row for row in rows))


if __name__ == '__main__':
    sys.exit(main())
