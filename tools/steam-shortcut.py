#!/usr/bin/env python3
"""Add the Soulwarden launcher to the Steam library as a non-Steam game.

Usage: steam-shortcut.py [show|add|remove] [--launcher auto|release|working-copy] [--steam-dir DIR]
                         [--name NAME] [--no-art]

  show     list the non-Steam games of the account (default)
  add      add the launcher, or correct its entry
  remove   remove the entry of the launcher and its library images

The launcher of the entry (--launcher):
  auto           the current release when it exists, else the working copy (default)
  release        the launcher of the current release of tools/release.sh:
                 ~/Games/soulwarden/releases/current/linux-compat/run-soulwarden.sh (SOULWARDEN_RELEASE_ROOT)
  working-copy   linux-compat/run-soulwarden.sh of this tree

The entry names the link "current", not a version: tools/release.sh activate and rollback change the
game of the entry with no change of the entry. An entry of the working copy starts the products of
apps/*/dist of this tree, which each build changes. The tool finds the Soulwarden entry by the two
launcher paths: `add` changes the entry from one launcher to the other.

Steam keeps the non-Steam games of an account in userdata/ACCOUNT/config/shortcuts.vdf, a
binary key-value file. Steam reads this file only at its start, and it writes the file again
from its memory when a non-Steam game changes. Thus do `add` with Steam closed, or start Steam
again directly after it. If the entry is not in the library after the start, do `add` again.

The account is the one that logged in to Steam last (config/loginusers.vdf: the same rule as
the launcher). The tool reads no other file of the Steam configuration. Before each change it
puts a copy of shortcuts.vdf in the backup folder of the Soulwarden data directory. It prints only
the last 4 digits of an account id, and it prints the home directory as "~".

The entry starts the launcher with the environment of a terminal: Steam adds its runtime
libraries (LD_LIBRARY_PATH) and its overlay (LD_PRELOAD) to a non-Steam game, and the launcher
and Proton do not need them. The library images come from the image cache of Steam for the
game (app 1063730), when the cache has them.

The entry and a development run (tools/dev-run.sh) have different Proton prefixes, and
tools/dev-stop.sh stops only development runs: one does not stop the game of the other.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import sys
import time
import zlib
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
WORKING_LAUNCHER = REPOSITORY / "linux-compat" / "run-soulwarden.sh"
DEFAULT_NAME = "New World: Aeternum (Soulwarden)"
GAME_APP_ID = 1063730
LAUNCH_OPTIONS = "env -u LD_PRELOAD -u LD_LIBRARY_PATH %command%"

# Value types of the binary key-value form.
TYPE_MAP = 0x00
TYPE_TEXT = 0x01
TYPE_INT32 = 0x02
TYPE_UINT64 = 0x07
TYPE_END = 0x08

# Library image of the cache (file name) and the suffix of its copy in config/grid.
ART_FILES = (
    ("library_capsule.jpg", "p.jpg"),
    ("library_header.jpg", ".jpg"),
    ("library_hero.jpg", "_hero.jpg"),
    ("logo.png", "_logo.png"),
)

Item = tuple[int, str, object]


def shown(path: object) -> str:
    """A path for a message: the home directory shows as "~", an account id as 4 digits."""
    text = str(path).replace(str(Path.home()), "~")
    return re.sub(r"/userdata/\d*?(\d{1,4})/", r"/userdata/...\1/", text)


def release_root() -> Path:
    """The release root of tools/release.sh. The link "current" in it is not resolved."""
    return Path(os.path.abspath(os.path.expanduser(os.environ.get("SOULWARDEN_RELEASE_ROOT") or "~/Games/soulwarden/releases")))


def launchers() -> dict[str, tuple[Path, Path]]:
    """The launcher and the start folder of each kind of entry."""
    current = release_root() / "current"
    return {
        "release": (current / "linux-compat" / "run-soulwarden.sh", current),
        "working-copy": (WORKING_LAUNCHER, REPOSITORY),
    }


def selected_launcher(choice: str) -> str:
    if choice != "auto":
        return choice
    return "release" if os.access(launchers()["release"][0], os.X_OK) else "working-copy"


def write_backup(data_dir: Path, content: bytes) -> Path:
    """Puts a copy of the file before a change in the backup folder. No copy is replaced."""
    folder = data_dir / "backup"
    folder.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    for number in range(1000):
        backup = folder / (f"steam-shortcuts-{stamp}.vdf" if number == 0 else f"steam-shortcuts-{stamp}-{number}.vdf")
        try:
            with open(backup, "xb") as output:
                output.write(content)
            return backup
        except FileExistsError:
            continue
    raise SystemExit("steam-shortcut: no free name for the copy of the file. No change was made.")


def parse_map(data: bytes, position: int) -> tuple[list[Item], int]:
    items: list[Item] = []
    while True:
        kind = data[position]
        position += 1
        if kind == TYPE_END:
            return items, position
        end = data.index(b"\x00", position)
        key = data[position:end].decode("utf-8")
        position = end + 1
        value: object
        if kind == TYPE_MAP:
            value, position = parse_map(data, position)
        elif kind == TYPE_TEXT:
            end = data.index(b"\x00", position)
            value = data[position:end].decode("utf-8")
            position = end + 1
        elif kind == TYPE_INT32:
            value = struct.unpack_from("<i", data, position)[0]
            position += 4
        elif kind == TYPE_UINT64:
            value = struct.unpack_from("<Q", data, position)[0]
            position += 8
        else:
            raise ValueError(f"value type {kind:#04x} at offset {position - 1} is not known")
        items.append((kind, key, value))


def dump_map(items: list[Item]) -> bytes:
    out = bytearray()
    for kind, key, value in items:
        out += bytes([kind]) + key.encode("utf-8") + b"\x00"
        if kind == TYPE_MAP:
            out += dump_map(value)  # type: ignore[arg-type]
        elif kind == TYPE_TEXT:
            out += str(value).encode("utf-8") + b"\x00"
        elif kind == TYPE_INT32:
            out += struct.pack("<i", value)
        else:
            out += struct.pack("<Q", value)
    return bytes(out) + bytes([TYPE_END])


def account_id(steam_dir: Path) -> int:
    """The account that logged in last: MostRecent = 1, else the largest Timestamp."""
    text = (steam_dir / "config" / "loginusers.vdf").read_text(encoding="utf-8", errors="replace")
    best_id, best_time = None, -1
    for steam_id, block in re.findall(r'"(\d{17})"\s*\{(.*?)\}', text, re.S):
        if re.search(r'"MostRecent"\s+"1"', block, re.I):
            best_id = steam_id
            break
        stamp = re.search(r'"Timestamp"\s+"(\d+)"', block, re.I)
        if stamp and int(stamp.group(1)) > best_time:
            best_id, best_time = steam_id, int(stamp.group(1))
    if best_id is None:
        raise SystemExit("steam-shortcut: no account was found in config/loginusers.vdf.")
    return int(best_id) & 0xFFFFFFFF


def steam_runs() -> bool:
    for comm in Path("/proc").glob("[0-9]*/comm"):
        try:
            if comm.read_text().strip() == "steam":
                return True
        except OSError:
            continue
    return False


def field(entry: list[Item], name: str) -> object:
    for _, key, value in entry:
        if key.lower() == name.lower():
            return value
    return None


def shortcut_id(exe: str, name: str) -> int:
    """The id that Steam gives a non-Steam game: CRC-32 of the two texts, with the top bit set."""
    return zlib.crc32((exe + name).encode("utf-8")) | 0x80000000


def make_entry(name: str, exe: str, start_dir: str, icon: str, last_play: int) -> list[Item]:
    app_id = shortcut_id(exe, name)
    return [
        (TYPE_INT32, "appid", struct.unpack("<i", struct.pack("<I", app_id))[0]),
        (TYPE_TEXT, "AppName", name),
        (TYPE_TEXT, "Exe", exe),
        (TYPE_TEXT, "StartDir", start_dir),
        (TYPE_TEXT, "icon", icon),
        (TYPE_TEXT, "ShortcutPath", ""),
        (TYPE_TEXT, "LaunchOptions", LAUNCH_OPTIONS),
        (TYPE_INT32, "IsHidden", 0),
        (TYPE_INT32, "AllowDesktopConfig", 1),
        (TYPE_INT32, "AllowOverlay", 0),
        (TYPE_INT32, "OpenVR", 0),
        (TYPE_INT32, "Devkit", 0),
        (TYPE_TEXT, "DevkitGameID", ""),
        (TYPE_INT32, "DevkitOverrideAppID", 0),
        (TYPE_INT32, "LastPlayTime", last_play),
        (TYPE_TEXT, "FlatpakAppID", ""),
        (TYPE_MAP, "tags", []),
    ]


def copy_art(steam_dir: Path, grid_dir: Path, app_id: int) -> str:
    """Copies the library images of the game to the grid folder. Returns the icon path or ""."""
    cache = steam_dir / "appcache" / "librarycache" / str(GAME_APP_ID)
    if not cache.is_dir():
        print("steam-shortcut: the image cache of Steam has no images of the game: no images.")
        return ""
    grid_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    for file_name, suffix in ART_FILES:
        source = next(iter(sorted(cache.rglob(file_name))), None)
        if source is not None:
            shutil.copyfile(source, grid_dir / f"{app_id}{suffix}")
            count += 1
    icon = ""
    icons = sorted(path for path in cache.glob("*.jpg") if path.is_file())
    if icons:
        target = grid_dir / f"{app_id}_icon.jpg"
        shutil.copyfile(icons[0], target)
        icon = str(target)
        count += 1
    print(f"steam-shortcut: {count} library images are in {shown(grid_dir)}.")
    return icon


def remove_art(grid_dir: Path, app_id: int) -> None:
    for suffix in [suffix for _, suffix in ART_FILES] + ["_icon.jpg"]:
        (grid_dir / f"{app_id}{suffix}").unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Soulwarden launcher as a non-Steam game of Steam.")
    parser.add_argument("command", nargs="?", default="show", choices=("show", "add", "remove"))
    parser.add_argument("--launcher", default="auto", choices=("auto", "release", "working-copy"),
                        help="add: the launcher of the entry (auto: the current release when it exists)")
    parser.add_argument("--steam-dir", default=os.environ.get("SOULWARDEN_STEAM_DIR", "~/.local/share/Steam"))
    parser.add_argument("--name", default=DEFAULT_NAME, help="name of the entry in the library")
    parser.add_argument("--no-art", action="store_true", help="copy no library images")
    arguments = parser.parse_args()

    steam_dir = Path(arguments.steam_dir).expanduser().resolve()
    account = account_id(steam_dir)
    config_dir = steam_dir / "userdata" / str(account) / "config"
    if not config_dir.is_dir():
        raise SystemExit(f"steam-shortcut: account ...{str(account)[-4:]} has no configuration folder in Steam.")
    shortcuts_file = config_dir / "shortcuts.vdf"
    original = shortcuts_file.read_bytes() if shortcuts_file.is_file() else b""

    top: list[Item] = [(TYPE_MAP, "shortcuts", [])]
    if original:
        top, end = parse_map(original, 0)
        if end != len(original) or dump_map(top) != original:
            raise SystemExit("steam-shortcut: shortcuts.vdf has a form that this tool does not write back "
                             "without a change. No change was made.")
    entries: list[Item] = next(value for kind, key, value in top if kind == TYPE_MAP and key.lower() == "shortcuts")

    kinds = {f'"{launcher}"': kind for kind, (launcher, _) in launchers().items()}
    present = [index for index, (_, _, entry) in enumerate(entries) if field(entry, "Exe") in kinds]
    choice = selected_launcher(arguments.launcher)

    if arguments.command == "show":
        print(f"steam-shortcut: account ...{str(account)[-4:]}, {len(entries)} non-Steam games.")
        for index, (_, key, entry) in enumerate(entries):
            mark = f"  <- Soulwarden launcher ({kinds[field(entry, 'Exe')]})" if index in present else ""
            print(f"  {key}: {field(entry, 'AppName')}  [{shown(field(entry, 'Exe'))}]{mark}")
        if not present:
            print("steam-shortcut: the Soulwarden launcher is not in the list.")
        print(f"steam-shortcut: add selects the launcher of the {choice.replace('-', ' ')}: "
              f"{shown(launchers()[choice][0])}")
        return 0

    grid_dir = config_dir / "grid"
    # The other Soulwarden entries go away: one entry starts the game.
    for index in reversed(present[1:]):
        old = entries[index][2]
        remove_art(grid_dir, shortcut_id(str(field(old, "Exe")), str(field(old, "AppName"))))
        del entries[index]
    present_index = present[0] if present else None
    if arguments.command == "remove":
        if present_index is None:
            print("steam-shortcut: the Soulwarden launcher is not in the list. No change was made.")
            return 0
        entry = entries[present_index][2]
        arguments.name = str(field(entry, "AppName"))
        remove_art(grid_dir, shortcut_id(str(field(entry, "Exe")), arguments.name))
        del entries[present_index]
    else:
        launcher, start_dir = launchers()[choice]
        if not os.access(launcher, os.X_OK):
            hint = " Make a release with tools/release.sh." if choice == "release" else ""
            raise SystemExit(f"steam-shortcut: the launcher was not found at {shown(launcher)}.{hint} No change was made.")
        exe = f'"{launcher}"'
        last_play = 0
        if present_index is not None:
            old = entries[present_index][2]
            last_play = int(field(old, "LastPlayTime") or 0)
            remove_art(grid_dir, shortcut_id(str(field(old, "Exe")), str(field(old, "AppName"))))
        app_id = shortcut_id(exe, arguments.name)
        icon = "" if arguments.no_art else copy_art(steam_dir, grid_dir, app_id)
        entry = make_entry(arguments.name, exe, f'"{start_dir}"', icon, last_play)
        print(f"steam-shortcut: the entry starts the launcher of the {choice.replace('-', ' ')}: {shown(launcher)}")
        if present_index is not None:
            entries[present_index] = (TYPE_MAP, entries[present_index][1], entry)
        else:
            used = {key for _, key, _ in entries}
            index = len(entries)
            while str(index) in used:
                index += 1
            entries.append((TYPE_MAP, str(index), entry))

    data = dump_map(top)
    check, end = parse_map(data, 0)
    if end != len(data) or dump_map(check) != data:
        raise SystemExit("steam-shortcut: the new file did not pass its check. No change was made.")
    action = "removed from" if arguments.command == "remove" else "in"
    status = (f"steam-shortcut: \"{arguments.name}\" is {action} the non-Steam games of account "
              f"...{str(account)[-4:]} ({len(entries)} entries, {len(data)} bytes).")
    if data == original:
        print(status)
        print("steam-shortcut: the file has this entry already. No change was made.")
        return 0
    if original:
        data_dir = Path(os.environ.get("SOULWARDEN_DATA_DIR") or
                        Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "soulwarden")
        print(f"steam-shortcut: copy of the file before the change: {shown(write_backup(data_dir, original))}")
    temporary = shortcuts_file.with_name(shortcuts_file.name + ".soulwarden-new")
    temporary.write_bytes(data)
    if original:
        shutil.copymode(shortcuts_file, temporary)
    os.replace(temporary, shortcuts_file)
    print(status)
    if steam_runs():
        print("steam-shortcut: Steam is running. It reads this file only at its start, and it can write "
              "the file again from its memory.\n"
              "  Close Steam and start it again now. If the entry is not in the library then, close Steam "
              "and do this command again.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
