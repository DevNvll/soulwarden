#!/usr/bin/env python3
"""Picture tour of the creature slices: one creature of each slice in front of the camera, one picture.

The client shows some creature slices with no body or with no name plate when it gets the record of
the backend (protocol notes 4.14). No log line shows this: only a picture does. This tool makes one
creature of each slice of a creature file with a development command, takes a screenshot, and removes
the creature. Then a person (or an agent) looks at the sheets of pictures and writes each slice that
is not correct into tools/gamedata/creature_slices.json, which the creature data tool reads.

  creature_tour.py prepare GAMEDATA_DIR TOUR_DIR
      Makes a game data folder for the tour. GAMEDATA_DIR/creatures.json must come from
      `tools/gamedata/creatures.sh FILE --all-slices`. The tour file has one more kind for each
      slice (`Tour_N`, with only that slice): a kind of the game can have two slices, and a spawn
      command then selects one of them. The other game data files are links.
  creature_tour.py run TOUR_DIR OUTPUT_DIR [--character NAME] [--only TEXT] [--list FILE]
                   [--skip FOLDER]... [--wait SECONDS] [--distance METERS] [--again]
      Needs a development run that uses TOUR_DIR (SOULWARDEN_GAMEDATA_DIR=TOUR_DIR
      SOULWARDEN_TRACE=6769980 tools/scenarios/play.sh RUN) with the character in the world, at a
      place with light and free space in front of the camera (the street at the default spawn,
      after `tools/hx 'look -900 0'`). Writes OUTPUT_DIR/NNNN_SLICE.jpg (the full picture) and
      OUTPUT_DIR/tour.json. A slice that has a picture already is not done again (--again does it
      again). --skip: a slice whose name is a picture name of that folder (an earlier tour) is not
      done. --list: only the slices whose path is in the JSON list of that file.
  creature_tour.py check OUTPUT_DIR
      Removes each picture that is not a picture of the game (see below): `run` then does these
      slices again.
  creature_tour.py sheets OUTPUT_DIR [--columns N] [--rows N]
      Makes sheets of the pictures (OUTPUT_DIR/sheet-NN.jpg), with the name of the slice below each
      picture, and the table OUTPUT_DIR/tour.md.

A screenshot is a picture of the agent desktop. It is not a picture of the game while a person
shows that desktop on a monitor (`omarchy-agent-desktop enter`): the picture then has the size of
the monitor, or it shows the empty desktop. The picture of the game can also stay the same for some
time (one slice of the supported build made the client show no new picture for 1 minute). The tool
takes a picture again (up to 2 minutes) until it has the size of the agent desktop, colours in its
middle, and a difference from the picture of the slice before it. `check` finds such pictures of an
earlier tour.

The tool sends a small camera movement each 5 minutes: a client with no input leaves the world
after 20 minutes. It needs ImageMagick (`magick`).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import dev_env  # noqa: E402

FILE_NAME = "creatures.json"
# The size of a screenshot of the agent desktop with the game (tools/shot).
SHOT_SIZE = "1280 720"
# The middle of a screenshot of the game has colours. The empty agent desktop is grey.
COLOUR_CROP = "640x400+320+160"
LEAST_SATURATION = 0.04
# Two screenshots with a mean difference below this part of the value range are the same picture.
SAME_PICTURE = 0.002
SHOT_TRIES = 24
# The part of a screenshot of tools/shot (1280x720) that has the creature and its name plate, for a sheet.
SHEET_CROP = "420x328+430+150"
IDLE_INPUT_INTERVAL = 300.0


def run(*command: str) -> str:
    return subprocess.run(list(command), capture_output=True, text=True, check=False).stdout


def slice_name(path: str) -> str:
    return path.rsplit("/", 1)[-1].replace(".dynamicslice", "")


def game_picture(path: Path) -> bool:
    """@return True when a screenshot has the size of the agent desktop and colours in its middle: a picture of
    the game, not of a monitor of the user and not of the empty desktop."""
    text = run("magick", str(path), "-format", "%w %h ", "-write", "info:", "-crop", COLOUR_CROP, "+repage", "-colorspace",
               "HSL", "-channel", "G", "-separate", "-format", "%[fx:mean]", "info:")
    try:
        size, _, saturation = text.strip().rpartition(" ")
        return size == SHOT_SIZE and float(saturation) >= LEAST_SATURATION
    except ValueError:
        return False


def same_picture(first: Path, second: Path) -> bool:
    """@return True when two screenshots are the same picture: the game showed no new picture between them. Two
    pictures of the game are never equal: the grass and the trees of the scene move."""
    result = subprocess.run(["magick", "compare", "-metric", "MAE", str(first), str(second), "null:"], capture_output=True,
                            text=True, check=False)
    match = re.search(r"\(([0-9.eE+-]+)\)", result.stderr)
    return match is not None and float(match[1]) < SAME_PICTURE


def check_pictures(output: Path) -> int:
    removed = 0
    # In the sequence of the tour: a picture that is equal to the picture before it is not a picture of its slice.
    pictures = sorted(output.glob("[0-9][0-9][0-9][0-9]_*.jpg"), key=lambda picture: picture.stat().st_mtime)
    bad = [index for index, picture in enumerate(pictures)
           if not game_picture(picture) or (index > 0 and same_picture(pictures[index - 1], picture))]
    for index in bad:
        pictures[index].unlink()
        removed += 1
        print(f"removed {pictures[index].name}: not a picture of the game, or equal to the picture before it")
    print(f"tour: {removed} pictures removed")
    return 0


def prepare(source: Path, target: Path) -> int:
    document = json.loads((source / FILE_NAME).read_text(encoding="utf-8"))
    # For each slice: the kind with the most spawn points that has it.
    chosen: dict[int, dict] = {}
    points: dict[int, int] = {}
    for kind in sorted(document["kinds"], key=lambda entry: -entry["points"]):
        for body, _ in kind["bodies"]:
            chosen.setdefault(body, kind)
            points[body] = points.get(body, 0) + kind["points"]
    tour = []
    for body in sorted(chosen):
        kind = dict(chosen[body])
        tour.append({"kind": f"Tour_{body:04d}", "slice": body, "path": document["slices"][body]["path"],
                     "from": kind["id"], "name": kind["name"], "level": kind["level"],
                     "category": kind["category"], "categoryId": kind["categoryId"],
                     "pointsOfKindsWithSlice": points[body]})
        kind.update(id=f"Tour_{body:04d}", bodies=[[body, 100]], points=0)
        document["kinds"].append(kind)
    document["counts"]["kinds"] = len(document["kinds"])
    target.mkdir(parents=True, exist_ok=True)
    (target / FILE_NAME).write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
    (target / "tour-kinds.json").write_text(json.dumps(tour, indent=1) + "\n", encoding="utf-8")
    for other in source.iterdir():
        link = target / other.name
        if other.name != FILE_NAME and other.suffix == ".json" and not link.exists():
            link.symlink_to(other.absolute())
    print(f"{target}: {len(tour)} tour kinds, one for each slice")
    return 0


def camera() -> tuple[float, float, float, float]:
    text = run(sys.executable, "-B", str(TOOLS / "research/player_position.py"), "1")
    match = re.search(r"position (\S+) (\S+) (\S+) .*yaw (\S+) pitch", text)
    if not match:
        sys.exit("tour: no position of the player (run the game with SOULWARDEN_TRACE=6769980): " + text.strip())
    return float(match[1]), float(match[2]), float(match[3]), float(match[4])


def tour(arguments: argparse.Namespace) -> int:
    kinds = json.loads((Path(arguments.tour) / "tour-kinds.json").read_text(encoding="utf-8"))
    output = Path(arguments.output)
    output.mkdir(parents=True, exist_ok=True)
    table_path = output / "tour.json"
    table = {row["path"]: row for row in json.loads(table_path.read_text())} if table_path.is_file() else {}
    skipped = {picture.stem.split("_", 1)[-1] if re.match(r"\d{4}_", picture.name) else picture.stem
               for folder in arguments.skip for picture in Path(folder).iterdir()
               if picture.suffix in (".png", ".jpg")}
    listed = set(json.loads(Path(arguments.list).read_text(encoding="utf-8"))) if arguments.list else None
    world = str(TOOLS / "world")
    last_input = time.monotonic()
    done = 0
    before = output / "picture-before.png"
    before.unlink(missing_ok=True)
    for entry in kinds:
        name = slice_name(entry["path"])
        if arguments.only and arguments.only not in entry["path"] and arguments.only not in entry["from"]:
            continue
        if listed is not None and entry["path"] not in listed:
            continue
        picture = output / f"{entry['slice']:04d}_{name}.jpg"
        if name in skipped or (picture.is_file() and not arguments.again):
            continue
        if time.monotonic() - last_input > IDLE_INPUT_INTERVAL:
            run(str(TOOLS / "hx"), "look 30 0", "look -30 0")
            last_input = time.monotonic()
        _, _, _, yaw = camera()
        # In front of the camera, and some distance to the right of the character.
        dx = -math.sin(yaw) * arguments.distance + math.cos(yaw) * arguments.side
        dy = math.cos(yaw) * arguments.distance + math.sin(yaw) * arguments.side
        facing = (math.degrees(yaw) + 180.0) % 360.0
        reply = run(world, "creature", "spawn", entry["kind"], arguments.character, f"{dx:.2f}", f"{dy:.2f}", "0",
                    "--yaw", f"{facing:.1f}")
        match = re.search(r"creature ([0-9a-f]{16})", reply)
        row = dict(entry, picture="", creature="")
        if not match:
            row["result"] = "not made: " + (reply.strip() or "no reply of the backend")
            print(f"NOT MADE {name}: {reply.strip()}", flush=True)
            table[entry["path"]] = row
            if "no backend" in reply or not reply.strip():
                break
            continue
        time.sleep(arguments.wait)
        good = False
        for _ in range(SHOT_TRIES):
            shot = Path(run(str(TOOLS / "shot"), f"tour-{entry['slice']:04d}").strip())
            good = game_picture(shot) and not (before.is_file() and same_picture(before, shot))
            if good:
                subprocess.run(["magick", str(shot), "-quality", "88", str(picture)], check=False)
                shot.replace(before)
            shot.unlink(missing_ok=True)
            if good:
                break
            # A person shows the agent desktop on a monitor: wait, and keep the client in the world.
            time.sleep(5.0)
            if time.monotonic() - last_input > IDLE_INPUT_INTERVAL:
                run(str(TOOLS / "hx"), "look 30 0", "look -30 0")
                last_input = time.monotonic()
        run(world, "creature", "remove", match[1])
        time.sleep(0.6)
        if not good:
            row["result"] = "no picture of the game"
            table[entry["path"]] = row
            print(f"NO PICTURE {name}", flush=True)
            continue
        row.update(picture=picture.name, creature=match[1], result="")
        table[entry["path"]] = row
        done += 1
        print(f"{done} {name} ({entry['from']}, {entry['name']})", flush=True)
        table_path.write_text(json.dumps(list(table.values()), indent=1) + "\n")
    table_path.write_text(json.dumps(list(table.values()), indent=1) + "\n")
    before.unlink(missing_ok=True)
    print(f"tour: {done} pictures, {len(table)} slices in {table_path}")
    return 0


def sheets(arguments: argparse.Namespace) -> int:
    output = Path(arguments.output)
    rows = [row for row in json.loads((output / "tour.json").read_text())
            if row.get("picture") and (output / row["picture"]).is_file()]
    rows.sort(key=lambda row: row["slice"])
    per_sheet = arguments.columns * arguments.rows
    lines = ["| Sheet | Place | Slice | Kind of the tour | Name | Level | Result |", "| --- | --- | --- | --- | --- | ---: | --- |"]
    for number, start in enumerate(range(0, len(rows), per_sheet)):
        group = rows[start:start + per_sheet]
        command = ["magick", "montage"]
        for place, row in enumerate(group):
            command += ["-label", f"{place + 1} {slice_name(row['path'])}", str(output / row["picture"]) + f"[{SHEET_CROP}]"]
            lines.append(f"| {number:02d} | {place + 1} | `{slice_name(row['path'])}` | `{row['from']}` | {row['name']} | "
                         f"{row['level']} | {row.get('result', '')} |")
        command += ["-crop", SHEET_CROP, "+repage", "-tile", f"{arguments.columns}x{arguments.rows}", "-geometry",
                    "300x234+2+2", "-pointsize", "11", "-background", "white", "-quality", "86",
                    str(output / f"sheet-{number:02d}.jpg")]
        subprocess.run(command, check=True)
    (output / "tour.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"tour: {len(rows)} pictures in {math.ceil(len(rows) / per_sheet)} sheets, table {output / 'tour.md'}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    first = commands.add_parser("prepare")
    first.add_argument("gamedata")
    first.add_argument("tour")
    second = commands.add_parser("run")
    second.add_argument("tour")
    second.add_argument("output")
    second.add_argument("--character", default="Soulwarden")
    second.add_argument("--only", default="")
    second.add_argument("--list", default="")
    second.add_argument("--skip", action="append", default=[])
    second.add_argument("--wait", type=float, default=5.0)
    second.add_argument("--distance", type=float, default=5.5)
    second.add_argument("--side", type=float, default=0.9)
    second.add_argument("--again", action="store_true")
    fourth = commands.add_parser("check")
    fourth.add_argument("output")
    third = commands.add_parser("sheets")
    third.add_argument("output")
    third.add_argument("--columns", type=int, default=5)
    third.add_argument("--rows", type=int, default=5)
    arguments = parser.parse_args()
    if arguments.command == "prepare":
        return prepare(Path(arguments.gamedata), Path(arguments.tour))
    if arguments.command == "check":
        return check_pictures(Path(arguments.output))
    dev_env.data_dir()  # stops with a message when the development environment is not there
    if arguments.command == "run":
        return tour(arguments)
    return sheets(arguments)


if __name__ == "__main__":
    sys.exit(main())
