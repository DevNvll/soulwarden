#!/usr/bin/env python3
"""Releases of Soulwarden: a fixed copy of a tested state for the game of the user.

Usage: tools/release.sh [install [--allow-dirty] | status | activate VERSION | rollback]

  install           (default) make a release from the commit of this working copy, and make it current
  status            the current release and the other releases
  activate VERSION  make a release current
  rollback          make the release before the current one current

The Steam library entry (tools/steam-shortcut.py add) starts the launcher of the current release, not
the launcher of the working copy. Thus a stage that changes the working copy does not change the game
of the user: only a release does.

A release has the launcher of the commit (linux-compat/run-soulwarden.sh: it needs no other file of
the repository), a copy of the two products of apps/*/dist, and a copy of the game data folder
(~/.local/share/soulwarden/gamedata, SOULWARDEN_GAMEDATA_DIR). The products and the game data are the
ones of the last live run: the command copies them, and it builds nothing. The layout keeps the
paths of the repository, so the launcher finds its products with no change:

  ROOT/VERSION/VERSION                               the version name
  ROOT/VERSION/release.json                          the commit, its subject, the install time, the SHA-256 of each part
  ROOT/VERSION/linux-compat/run-soulwarden.sh        the launcher of the commit
  ROOT/VERSION/apps/backend/dist/soulwarden-backend  the backend
  ROOT/VERSION/apps/game-hook/dist/steam_api64.dll   the hook
  ROOT/VERSION/gamedata/                             the game data (the launcher uses this folder)
  ROOT/current -> VERSION                            the current release

ROOT is ~/Games/soulwarden/releases (SOULWARDEN_RELEASE_ROOT). VERSION is the date and the short
commit (2026-10-16-8b4abb8). The files of a release are read only.

Rules:
- install refuses a working copy in which a tracked file differs from the commit. --allow-dirty is
  only for a test: the version name then ends with "dirty", and the launcher comes from the working
  copy, not from the commit.
- A release with the commit, the products, and the game data of an earlier release is that release:
  install makes no copy, and it makes that release current.
- The copies are reflinks where the file system can do them (btrfs): an unchanged file uses no new
  space. Else a normal copy, which must leave 1 GiB free on the disk.
- The link "current" changes atomically (a new link, then a rename). It does not change while the
  game of the user runs: the launcher holds the lock run.lock in the data directory of the user
  (~/.local/share/soulwarden, SOULWARDEN_DATA_DIR). Then install puts the files in place, and it
  prints the command that activates the release later. The command opens the lock file read only.
- After a change of "current", the command removes each release that is not one of the 3 newest
  releases and that is not current. While the game runs, it removes nothing.

Status: 0 done; 1 error; 2 a wrong argument; 3 the game of the user runs, and "current" did not change.
The output shows the home directory as "~".
"""

from __future__ import annotations

import argparse
import contextlib
import datetime
import fcntl
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

REPOSITORY = Path(__file__).resolve().parent.parent
LAUNCHER = "linux-compat/run-soulwarden.sh"
# The files of the repository that a release takes from the commit: the launcher and each file that
# it needs. tools/check_release.py checks that the launcher names no other file of the repository.
LAUNCHER_FILES = (LAUNCHER,)
# The products of the last live run.
PRODUCTS = ("apps/backend/dist/soulwarden-backend", "apps/game-hook/dist/steam_api64.dll")
GAMEDATA = "gamedata"
MANIFEST = "release.json"
VERSION_FILE = "VERSION"
CURRENT = "current"
COMMAND_LOCK = ".release.lock"
STAGING_PREFIX = ".staging-"
LINK_PREFIX = ".current-"
KEEP = 3
FORMAT = 1
STATUS_ERROR = 1
STATUS_GAME_RUNS = 3
FREE_SPACE_RESERVE = 1 << 30
CHUNK = 1 << 20
FICLONE = getattr(fcntl, "FICLONE", 0x40049409)


class ReleaseError(Exception):
    pass


def shown(text: object) -> str:
    """A path or a message for the output: the home directory shows as "~"."""
    return str(text).replace(str(Path.home()), "~")


def size_text(count: int) -> str:
    value, unit = float(count), "B"
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            break
        value /= 1024
    return f"{count} B" if unit == "B" else f"{value:.1f} {unit}"


def absolute(text: str) -> Path:
    return Path(os.path.abspath(os.path.expanduser(text)))


def data_home() -> str:
    return os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")


def release_root() -> Path:
    return absolute(os.environ.get("SOULWARDEN_RELEASE_ROOT") or "~/Games/soulwarden/releases")


def user_data_dir() -> Path:
    """The data directory of the launcher (the same rule as linux-compat/run-soulwarden.sh)."""
    return absolute(os.environ.get("SOULWARDEN_DATA_DIR") or os.path.join(data_home(), "soulwarden"))


def gamedata_source() -> Path:
    """The game data folder of the tools (the same rule as the launcher of the working copy)."""
    return absolute(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or os.path.join(data_home(), "soulwarden", "gamedata"))


def run_git(*arguments: str, binary: bool = False) -> bytes | str:
    # No optional locks: a status in the main checkout must not take the index lock of a stage.
    result = subprocess.run(["git", "--no-optional-locks", "-C", str(REPOSITORY), *arguments],
                            capture_output=True, check=False)
    if result.returncode != 0:
        message = result.stderr.decode("utf-8", "replace").strip().splitlines()
        raise ReleaseError(f"git {arguments[0]} failed: {shown(message[-1]) if message else result.returncode}")
    return result.stdout if binary else result.stdout.decode("utf-8", "replace")


@dataclass
class Commit:
    full: str
    short: str
    subject: str
    time: str
    changed: list[str]


def read_commit() -> Commit:
    full = str(run_git("rev-parse", "--verify", "HEAD^{commit}")).strip()
    short = str(run_git("rev-parse", "--short", full)).strip()
    subject, time = str(run_git("log", "-1", "--format=%s%x00%cI", full)).rstrip("\n").split("\x00")
    status = str(run_git("status", "--porcelain=v1", "--untracked-files=no"))
    changed = [line[3:] for line in status.splitlines() if line.strip()]
    return Commit(full, short, subject, time, changed)


@contextlib.contextmanager
def command_lock(root: Path) -> Iterator[None]:
    """One release command at a time for a release root."""
    root.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(root / COMMAND_LOCK, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o644)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ReleaseError(f"a different release command uses {shown(root)}. Do this command again after it.") from None
        yield
    finally:
        os.close(descriptor)


def game_lock_file() -> Path:
    return user_data_dir() / "run.lock"


@contextlib.contextmanager
def game_lock() -> Iterator[bool]:
    """Gives True when no game of the user runs. The command then holds the lock of the launcher
    until the end of the block, so no game starts in it. The file is opened read only, and a file
    that is not there is a free lock: the command writes no file in the data directory."""
    try:
        descriptor = os.open(game_lock_file(), os.O_RDONLY | os.O_CLOEXEC)
    except FileNotFoundError:
        yield True
        return
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            yield False
            return
        yield True
    finally:
        os.close(descriptor)


def game_runs() -> bool:
    with game_lock() as free:
        return not free


def file_digest(path: Path) -> str:
    with open(path, "rb") as reader:
        return hashlib.file_digest(reader, "sha256").hexdigest()


def tree_files(folder: Path) -> list[Path]:
    """Each file below a folder, sorted. A folder of a release has only folders and regular files."""
    found: list[Path] = []
    for directory, folders, files in os.walk(folder):
        folders.sort()
        base = Path(directory)
        for name in folders + files:
            mode = os.lstat(base / name).st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISDIR(mode) or stat.S_ISREG(mode)):
                raise ReleaseError(f"{shown(base / name)} is not a folder or a regular file. A release copies no link.")
        found.extend(base / name for name in sorted(files))
    return found


def summarize(files: list[tuple[str, int, str]]) -> dict[str, object]:
    """The files, the bytes, and one SHA-256 of a list of files: of the lines "PATH NUL SIZE NUL SHA-256"."""
    digest = hashlib.sha256()
    for name, size, sha256 in files:
        digest.update(f"{name}\0{size}\0{sha256}\n".encode())
    return {"files": len(files), "bytes": sum(size for _, size, _ in files), "sha256": digest.hexdigest()}


def gamedata_digest(folder: Path) -> dict[str, object]:
    """The whole folder, and each entry of its top level (a table file or a data folder)."""
    files = [(path.relative_to(folder).as_posix(), path.stat().st_size, file_digest(path)) for path in tree_files(folder)]
    entries: dict[str, object] = {}
    grouped: dict[str, list[tuple[str, int, str]]] = {}
    for name, size, sha256 in files:
        top, separator, rest = name.partition("/")
        if separator:
            grouped.setdefault(top, []).append((rest, size, sha256))
        else:
            entries[top] = {"bytes": size, "sha256": sha256}
    entries.update({top: summarize(items) for top, items in grouped.items()})
    return {**summarize(files), "entries": dict(sorted(entries.items()))}


class Copier:
    """Copies files into a new release. A reflink where the file system can do it, else a normal copy."""

    def __init__(self) -> None:
        self.reflinks = 0
        self.copies = 0
        self.copied_bytes = 0

    def file(self, source: Path, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        status = os.stat(source)
        with open(source, "rb") as reader, open(target, "xb") as writer:
            try:
                fcntl.ioctl(writer.fileno(), FICLONE, reader.fileno())
                self.reflinks += 1
            except OSError:
                free = shutil.disk_usage(target.parent).free
                if free < status.st_size + FREE_SPACE_RESERVE:
                    raise ReleaseError(f"the file system of {shown(target.parent)} makes no reflink, and a copy of "
                                       f"{shown(source)} ({size_text(status.st_size)}) leaves less than "
                                       f"{size_text(FREE_SPACE_RESERVE)} free ({size_text(free)} free).") from None
                writer.truncate(0)
                shutil.copyfileobj(reader, writer, CHUNK)
                self.copies += 1
                self.copied_bytes += status.st_size
        os.utime(target, ns=(status.st_atime_ns, status.st_mtime_ns))
        os.chmod(target, stat.S_IMODE(status.st_mode) & ~0o222)

    def tree(self, source: Path, target: Path) -> None:
        target.mkdir(parents=True)
        for path in tree_files(source):
            self.file(path, target / path.relative_to(source))

    def summary(self) -> str:
        return f"{self.reflinks} reflinks, {self.copies} normal copies ({size_text(self.copied_bytes)})"


@dataclass
class Release:
    name: str
    path: Path
    manifest: dict

    @property
    def serial(self) -> int:
        return int(self.manifest.get("serial", 0))

    def fingerprint(self) -> tuple:
        parts = {name: value["sha256"] for name, value in self.manifest.get("parts", {}).items()}
        return (self.manifest.get("commit"), bool(self.manifest.get("dirty")), sorted(parts.items()),
                self.manifest.get("gamedata", {}).get("sha256"))

    def missing(self) -> list[str]:
        names = [*LAUNCHER_FILES, *PRODUCTS]
        lost = [name for name in names if not (self.path / name).is_file()]
        return lost + ([GAMEDATA] if not (self.path / GAMEDATA).is_dir() else [])


def read_release(path: Path) -> Release | None:
    if path.name.startswith(".") or path.name == CURRENT or path.is_symlink() or not path.is_dir():
        return None
    try:
        manifest = json.loads((path / MANIFEST).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(manifest, dict) or manifest.get("version") != path.name:
        return None
    return Release(path.name, path, manifest)


def all_releases(root: Path) -> list[Release]:
    """The releases of a root, the newest first."""
    if not root.is_dir():
        return []
    found = [release for release in map(read_release, root.iterdir()) if release is not None]
    return sorted(found, key=lambda release: (release.serial, release.name), reverse=True)


def current_name(root: Path) -> str | None:
    link = root / CURRENT
    if not link.is_symlink():
        if link.exists():
            raise ReleaseError(f"{shown(link)} is not a symbolic link. Remove it, then do this command again.")
        return None
    return Path(os.readlink(link)).name


def set_current(root: Path, name: str) -> bool:
    """Makes a release current: a new link, then a rename over the old link. False: the game of the
    user runs, and nothing changed."""
    current_name(root)  # refuses a "current" that is not a link
    with game_lock() as free:
        if not free:
            return False
        temporary = root / f"{LINK_PREFIX}{os.getpid()}"
        with contextlib.suppress(FileNotFoundError):
            temporary.unlink()
        os.symlink(name, temporary)
        os.replace(temporary, root / CURRENT)
    return True


def prune(root: Path) -> None:
    """Removes each release that is not one of the newest releases and that is not current."""
    releases = all_releases(root)
    keep = {release.name for release in releases[:KEEP]} | {current_name(root)}
    for release in releases:
        if release.name not in keep:
            shutil.rmtree(release.path)
            print(f"release: removed the old release {release.name}.")


def remove_leftovers(root: Path) -> None:
    """Removes the folders and links of an install that stopped (the command lock is held)."""
    for entry in root.iterdir():
        if entry.name.startswith(STAGING_PREFIX) and not entry.is_symlink() and entry.is_dir():
            shutil.rmtree(entry)
        elif entry.name.startswith(LINK_PREFIX) and entry.is_symlink():
            entry.unlink()


def locked_message(name: str) -> str:
    return (f"release: the game of the user runs (the lock {shown(game_lock_file())} is held): {name} is installed, "
            f"but it is not current. After the game stops: tools/release.sh activate {name}")


def activate_and_prune(root: Path, name: str) -> int:
    if not set_current(root, name):
        print(locked_message(name))
        return STATUS_GAME_RUNS
    print(f"release: {name} is the current release. The Steam library entry starts it.")
    prune(root)
    return 0


def unique_name(root: Path, base: str) -> str:
    name, number = base, 1
    while (root / name).exists() or (root / name).is_symlink():
        number += 1
        name = f"{base}-{number}"
    return name


def install(allow_dirty: bool) -> int:
    root = release_root()
    commit = read_commit()
    if commit.changed and not allow_dirty:
        listed = ", ".join(commit.changed[:5]) + (", ..." if len(commit.changed) > 5 else "")
        raise ReleaseError(f"{len(commit.changed)} tracked files differ from the commit {commit.short} ({listed}). "
                           "A release must be a commit. No release was made.")
    dirty = bool(commit.changed)
    for name in PRODUCTS:
        if not (REPOSITORY / name).is_file():
            raise ReleaseError(f"{name} was not found. Put the product of the live run into dist first. No release was made.")
    if not os.access(REPOSITORY / PRODUCTS[0], os.X_OK):
        raise ReleaseError(f"{PRODUCTS[0]} is not executable. No release was made.")
    source_gamedata = gamedata_source()
    if not source_gamedata.is_dir():
        raise ReleaseError(f"the game data folder {shown(source_gamedata)} was not found (SOULWARDEN_GAMEDATA_DIR). "
                           "No release was made.")

    with command_lock(root):
        remove_leftovers(root)
        staging = root / f"{STAGING_PREFIX}{os.getpid()}"
        try:
            copier = Copier()
            for name in LAUNCHER_FILES:
                target = staging / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if dirty:
                    copier.file(REPOSITORY / name, target)
                else:
                    # The bytes of the commit, not of the working copy: a stage can change the file now.
                    target.write_bytes(run_git("cat-file", "blob", f"{commit.full}:{name}", binary=True))
                    target.chmod(0o555)
            for name in PRODUCTS:
                copier.file(REPOSITORY / name, staging / name)
            copier.tree(source_gamedata, staging / GAMEDATA)

            parts = {name: {"bytes": (staging / name).stat().st_size, "sha256": file_digest(staging / name)}
                     for name in (*LAUNCHER_FILES, *PRODUCTS)}
            gamedata = gamedata_digest(staging / GAMEDATA)
            releases = all_releases(root)
            manifest = {
                "format": FORMAT,
                "version": "",
                "serial": max((release.serial for release in releases), default=0) + 1,
                "commit": commit.full,
                "short_commit": commit.short,
                "subject": commit.subject,
                "commit_time": commit.time,
                "dirty": dirty,
                "installed_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                "parts": parts,
                "gamedata": gamedata,
            }
            print(f"release: commit {commit.short} \"{commit.subject}\"{' with changed tracked files (--allow-dirty)' if dirty else ''}")
            print(f"release: game data {shown(source_gamedata)}: {gamedata['files']} files, {size_text(int(gamedata['bytes']))}")
            same = Release("", staging, manifest).fingerprint()
            twin = next((release for release in releases if release.fingerprint() == same and not release.missing()), None)
            if twin is not None:
                print(f"release: {twin.name} has this commit, these products, and this game data. No new release.")
                return activate_and_prune(root, twin.name)

            print(f"release: copies: {copier.summary()}")
            base = f"{datetime.date.today().isoformat()}-{commit.short}" + ("-dirty" if dirty else "")
            name = unique_name(root, base)
            manifest["version"] = name
            (staging / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            (staging / VERSION_FILE).write_text(name + "\n", encoding="utf-8")
            for written in (MANIFEST, VERSION_FILE):
                (staging / written).chmod(0o444)
            os.rename(staging, root / name)
            print(f"release: {name} is in {shown(root / name)}.")
        finally:
            if staging.exists():
                shutil.rmtree(staging)
        return activate_and_prune(root, name)


def find_release(root: Path, name: str) -> Release:
    release = read_release(root / name) if (root / name).exists() else None
    if release is None:
        names = ", ".join(release.name for release in all_releases(root)) or "none"
        raise ReleaseError(f"no release {name} in {shown(root)} (releases: {names}).")
    if release.missing():
        raise ReleaseError(f"the release {name} is not complete (missing: {', '.join(release.missing())}).")
    return release


def activate(name: str) -> int:
    root = release_root()
    with command_lock(root):
        find_release(root, name)
        if current_name(root) == name:
            print(f"release: {name} is the current release already.")
            if not game_runs():
                prune(root)
            return 0
        return activate_and_prune(root, name)


def rollback() -> int:
    root = release_root()
    with command_lock(root):
        current = current_name(root)
        releases = all_releases(root)
        now = next((release for release in releases if release.name == current), None)
        if now is None:
            raise ReleaseError(f"there is no current release in {shown(root)}.")
        earlier = [release for release in releases if release.serial < now.serial]
        if not earlier:
            raise ReleaseError(f"there is no release before {now.name}.")
        find_release(root, earlier[0].name)
        print(f"release: back from {now.name} to {earlier[0].name}.")
        return activate_and_prune(root, earlier[0].name)


def status() -> int:
    root = release_root()
    releases = all_releases(root)
    current = current_name(root)
    print(f"release: root {shown(root)}, {len(releases)} releases.")
    if game_runs():
        print(f"release: the game of the user runs (the lock {shown(game_lock_file())} is held).")
    if current is None:
        print("release: there is no current release. The Steam library entry starts the working copy, or nothing.")
    elif current not in {release.name for release in releases}:
        print(f"release: the link current names {current}, which is not a release. Do tools/release.sh activate VERSION.")
    for release in releases:
        manifest = release.manifest
        mark = "*" if release.name == current else " "
        installed = str(manifest.get("installed_at", ""))[:16].replace("T", " ")
        dirty = " (dirty)" if manifest.get("dirty") else ""
        missing = f" (not complete: {', '.join(release.missing())})" if release.missing() else ""
        print(f"{mark} {release.name:<28} {manifest.get('short_commit', ''):<10} {installed:<16}  "
              f"{manifest.get('subject', '')}{dirty}{missing}")
    if releases and current is not None and releases[0].name != current:
        print(f"release: the newest release {releases[0].name} is not current: tools/release.sh activate {releases[0].name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(prog="tools/release.sh", description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog="\n\n".join(__doc__.split("\n\n")[1:]))
    parser.add_argument("command", nargs="?", default="install", choices=("install", "status", "activate", "rollback"))
    parser.add_argument("version", nargs="?", help="the release of activate")
    parser.add_argument("--allow-dirty", action="store_true", help="install: only for a test (the version ends with dirty)")
    arguments = parser.parse_args()
    if (arguments.command == "activate") != (arguments.version is not None):
        parser.error("activate needs one VERSION, and the other commands take none")
    if arguments.allow_dirty and arguments.command != "install":
        parser.error("--allow-dirty is an option of install")
    try:
        if arguments.command == "install":
            return install(arguments.allow_dirty)
        if arguments.command == "activate":
            return activate(arguments.version)
        if arguments.command == "rollback":
            return rollback()
        return status()
    except ReleaseError as error:
        print(f"release: {error}", file=sys.stderr)
        return STATUS_ERROR


if __name__ == "__main__":
    sys.exit(main())
