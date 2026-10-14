#!/usr/bin/env python3
"""Draw a part of the navigation data of the backend from above, with paths and marks on it.

The picture shows what a creature of the backend has: each cell of 0.25 m with a free floor
(on the terrain, on a floor of an object, with more than one floor), each cell with no place
for a creature, and the water. A path file gives a line on it: the path of a creature of a
live run, or the output of `tools/world path ... --tsv FILE`.

The tool needs Pillow (the license MIT-CMU). It reads the files of tools/gamedata/navigation.sh
with the module tools/gamedata/nav_file.py.

Usage: nav_draw.py NAV_DIR X0 Y0 X1 Y1 OUTPUT.png [--scale PIXELS] [--title TEXT]
                   [--path FILE.tsv[:COLOR[:LABEL]] ...] [--mark "X Y LABEL" ...] [--lines METERS]
  X0 Y0 X1 Y1   the rectangle in meters of the world (north is up)
  --scale       pixels for one cell (default 2)
  --path        a file with one place in each line: "x y z", or the lines "corner x y z" of
                `tools/world path --tsv`, or the lines of a backend log with "at X Y Z"
  --mark        a cross with a text at a place
  --lines       a white line each METERS (default 20)
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gamedata"))
import nav_file  # noqa: E402

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("nav_draw: this tool needs Pillow (python3 -m pip install pillow)", file=sys.stderr)
    sys.exit(1)

COLORS = {
    "terrain": (186, 214, 160), "floor": (84, 168, 120), "floors": (160, 84, 200), "blocked": (64, 40, 40),
    "none": (20, 20, 24), "shallow": (150, 205, 240), "deep": (60, 110, 200), "no data": (120, 120, 120),
}
LEGEND = (("terrain", "free floor: terrain"), ("floor", "free floor: on an object"), ("floors", "2 or more free floors"),
          ("blocked", "no place for a creature"), ("shallow", "water, a creature walks"), ("deep", "water, too deep"),
          ("no data", "no navigation data"))
PATH_COLORS = ((220, 30, 30), (255, 140, 0), (240, 30, 200), (30, 30, 220), (0, 0, 0))
NUMBER = r"-?\d+(?:\.\d+)?"


def kind_of(floors) -> str:
    if floors is None:
        return "no data"
    free = [floor for floor in floors if not floor[1] & nav_file.BLOCKED]
    walk = [floor for floor in free if not floor[1] & nav_file.DEEP]
    if len(walk) >= 2:
        return "floors"
    if walk:
        if walk[0][1] & nav_file.SHALLOW:
            return "shallow"
        return "terrain" if walk[0][1] & nav_file.TERRAIN else "floor"
    if free:
        return "deep"
    return "blocked" if floors else "none"


def read_path(file: Path) -> list[tuple[float, float]]:
    """@return The places of a path file: the first two numbers of each line that has two numbers or more."""
    points = []
    for line in file.read_text().splitlines():
        match = re.search(rf"\bat ({NUMBER}) ({NUMBER}) ({NUMBER})", line)
        numbers = match.groups() if match else re.findall(NUMBER, line.split("\t", 1)[1] if line.startswith(("corner", "cell", "place")) else line)
        if len(numbers) >= 2 and not line.startswith(("kind", "x\t", "#")):
            points.append((float(numbers[0]), float(numbers[1])))
    return points


def main() -> int:
    parser = argparse.ArgumentParser(description="Draw a part of the navigation data from above.")
    parser.add_argument("folder")
    parser.add_argument("x0", type=float)
    parser.add_argument("y0", type=float)
    parser.add_argument("x1", type=float)
    parser.add_argument("y1", type=float)
    parser.add_argument("output")
    parser.add_argument("--scale", type=int, default=2)
    parser.add_argument("--title", default="")
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--mark", action="append", default=[])
    parser.add_argument("--lines", type=float, default=20.0)
    arguments = parser.parse_args()
    try:
        navigation = nav_file.Navigation(Path(arguments.folder))
        # The first region that has a file gives the cell size.
        navigation.floors(arguments.x0, arguments.y0)
        navigation.floors(arguments.x1, arguments.y1)
        cell = navigation.cell()
        width = int(round((arguments.x1 - arguments.x0) / cell))
        height = int(round((arguments.y1 - arguments.y0) / cell))
        if width <= 0 or height <= 0 or width * height > 16_000_000:
            print("nav_draw: the rectangle is empty or too large (16 million cells at most)", file=sys.stderr)
            return 1
        image = Image.new("RGB", (width, height))
        pixels = image.load()
        for row in range(height):
            y = arguments.y0 + (row + 0.5) * cell
            for column in range(width):
                pixels[column, height - 1 - row] = COLORS[kind_of(navigation.floors(arguments.x0 + (column + 0.5) * cell, y))]
    except nav_file.Failure as error:
        print(f"nav_draw: {error}", file=sys.stderr)
        return 1
    scale = max(1, arguments.scale)
    image = image.resize((width * scale, height * scale), Image.NEAREST)
    legend_width = 300
    top = 34
    canvas = Image.new("RGB", (image.width + legend_width, image.height + top), (250, 250, 250))
    canvas.paste(image, (0, top))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 14)
        small = ImageFont.truetype("DejaVuSans.ttf", 12)
    except OSError:
        font = small = ImageFont.load_default()

    def place(x: float, y: float) -> tuple[float, float]:
        return (x - arguments.x0) / cell * scale, top + image.height - (y - arguments.y0) / cell * scale

    if arguments.lines > 0:
        first_x = int(arguments.x0 // arguments.lines + 1) * arguments.lines
        x = first_x
        while x < arguments.x1:
            px = place(x, arguments.y0)[0]
            draw.line([(px, top), (px, top + image.height)], fill=(255, 255, 255), width=1)
            draw.text((px + 3, top + 2), f"x {x:g}", fill=(255, 255, 255), font=small)
            x += arguments.lines
        y = int(arguments.y0 // arguments.lines + 1) * arguments.lines
        while y < arguments.y1:
            py = place(arguments.x0, y)[1]
            draw.line([(0, py), (image.width, py)], fill=(255, 255, 255), width=1)
            draw.text((3, py + 2), f"y {y:g}", fill=(255, 255, 255), font=small)
            y += arguments.lines
    draw.text((6, 8), arguments.title or f"Navigation data, x {arguments.x0:g} to {arguments.x1:g}, y {arguments.y0:g} to {arguments.y1:g}",
              fill=(0, 0, 0), font=font)
    line = top + 8
    left = image.width + 14
    for key, label in LEGEND:
        draw.rectangle([left, line, left + 20, line + 14], fill=COLORS[key], outline=(0, 0, 0))
        draw.text((left + 28, line), label, fill=(0, 0, 0), font=small)
        line += 22
    for index, entry in enumerate(arguments.path):
        parts = entry.split(":")
        color = PATH_COLORS[index % len(PATH_COLORS)]
        if len(parts) > 1 and parts[1]:
            try:
                color = tuple(int(parts[1][i:i + 2], 16) for i in (0, 2, 4))
            except ValueError:
                print(f"nav_draw: {parts[1]} is not a color (six hexadecimal digits)", file=sys.stderr)
                return 1
        label = parts[2] if len(parts) > 2 else Path(parts[0]).stem
        try:
            points = [place(x, y) for x, y in read_path(Path(parts[0]))]
        except OSError as error:
            print(f"nav_draw: {error}", file=sys.stderr)
            return 1
        if len(points) >= 2:
            draw.line(points, fill=color, width=3)
        for x, y in points:
            draw.rectangle([x - 3, y - 3, x + 3, y + 3], fill=color, outline=(0, 0, 0))
        draw.line([(left, line + 7), (left + 20, line + 7)], fill=color, width=3)
        draw.text((left + 28, line), f"{label} ({len(points)} places)"[:38], fill=(0, 0, 0), font=small)
        line += 22
    for entry in arguments.mark:
        parts = entry.split(None, 2)
        if len(parts) < 2:
            continue
        x, y = place(float(parts[0]), float(parts[1]))
        draw.line([(x - 7, y), (x + 7, y)], fill=(0, 0, 0), width=3)
        draw.line([(x, y - 7), (x, y + 7)], fill=(0, 0, 0), width=3)
        draw.line([(x - 6, y), (x + 6, y)], fill=(255, 255, 255), width=1)
        draw.line([(x, y - 6), (x, y + 6)], fill=(255, 255, 255), width=1)
        if len(parts) == 3:
            box = draw.textbbox((x + 9, y - 8), parts[2], font=small)
            draw.rectangle([box[0] - 2, box[1] - 1, box[2] + 2, box[3] + 1], fill=(255, 255, 255))
            draw.text((x + 9, y - 8), parts[2], fill=(0, 0, 0), font=small)
    line += 8
    draw.text((left, line), f"1 cell = {cell:g} m, north is up", fill=(0, 0, 0), font=small)
    draw.text((left, line + 18), f"white lines: each {arguments.lines:g} m", fill=(0, 0, 0), font=small)
    canvas.save(arguments.output)
    print(f"{arguments.output}: {canvas.width} x {canvas.height}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
