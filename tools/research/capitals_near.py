#!/usr/bin/env python3
"""List the slices that the world places near a position (analysis only).

A terrain project (`sharedassets/coatlicue/<world>`) has one folder for each region. Each
layer of a region has a `<layer>.capitals.json` file with a list of slice placements
("capitals": slice name, world position, rotation). Extract these files first with pakx.py.

Usage: capitals_near.py WORLD X Y RADIUS [NAME_REGEX]
Example: capitals_near.py newworld_vitaeeterna 9613 2329 150 'spawn|trigger|quest'
"""

from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

ROOT = Path.home() / "Games/soulwarden/work/assets/extract/sharedassets/coatlicue"


def main() -> None:
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    world = sys.argv[1]
    x, y, radius = (float(value) for value in sys.argv[2:5])
    pattern = re.compile(sys.argv[5], re.IGNORECASE) if len(sys.argv) > 5 else None
    rows = []
    for path in sorted((ROOT / world / "regions").glob("*/capitals/*/*.capitals.json")):
        try:
            capitals = json.loads(path.read_text(encoding="utf-8", errors="replace")).get("Capitals", [])
        except json.JSONDecodeError as error:
            print(f"FAILED {path}: {error}", file=sys.stderr)
            continue
        for capital in capitals:
            position = capital.get("worldPosition") or {}
            distance = math.hypot(position.get("x", 0.0) - x, position.get("y", 0.0) - y)
            name = capital.get("sliceName") or capital.get("variantName") or "?"
            if distance > radius or (pattern and not pattern.search(name)):
                continue
            rows.append((distance, name, position, path.parent.name, path.parent.parent.parent.name))
    for distance, name, position, layer, region in sorted(rows, key=lambda row: row[0]):
        print(
            f"{distance:7.1f} m  {position.get('x', 0):9.2f} {position.get('y', 0):9.2f} {position.get('z', 0):7.2f}"
            f"  {name}  [{region}/{layer}]"
        )


if __name__ == "__main__":
    main()
