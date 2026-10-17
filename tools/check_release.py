#!/usr/bin/env python3
"""Check of the release command (tools/release.sh), of the release part of the launcher, and of the
launcher selection of the Steam library entry (tools/steam-shortcut.py).

The check copies the release tool, the Steam shortcut tool, and the launcher of this tree into a git
repository in a temporary folder, with made-up products and a made-up game data folder. It runs each
command against a temporary release root and a temporary data directory, and the Steam shortcut tool
against a made-up Steam directory. HOME of each command is a temporary folder. The check starts no
game, no launcher, and no Steam, and it writes no file of the user. It reads the folder of --gamedata
and the file of --backend only.

The cases (each prints "ok NAME: ..." or "FAIL NAME: ..."):

  install            a release of the commit: the layout, VERSION, release.json, read-only files, the link current
  copy               reflinks where the file system can do them (the extents are shared), else normal copies
  same state         an install with no change makes no new release
  second install     a new commit and a new product: a new release, and it is current
  refused            a changed tracked file, and a missing product: no release, no change of current
  allow dirty        --allow-dirty: the version ends with "dirty", and the launcher is the one of the working copy
  status             each release with its version, commit, time, and subject; the mark of the current one
  activate           activate VERSION; an unknown version changes nothing
  rollback           to the release before the current one, to the first, then a refusal
  lock               while a process holds run.lock with flock: install puts the files in place and changes no
                     link (status 3, one line with the activate command); activate and rollback change nothing;
                     no release is removed; after the game: activate
  prune              the 3 newest releases and the current one stay
  launcher files     each file of the repository that the launcher names is a part of a release
  launcher paths     the launcher of a release through the link current: the physical folder of the release, its
                     game data folder; SOULWARDEN_GAMEDATA_DIR; the working copy (no game data folder): the default
  shortcut ...       tools/steam-shortcut.py with a made-up Steam directory: the entry of the current release, the
                     change to the working copy, show, no release, remove, the backup of each change
  shortcut older ... an older entry of the tool (an older project name: "New World: Aeternum (NAME)" with the
                     launcher linux-compat/run-NAME.sh, here a made-up name; or a link to the launcher of the
                     working copy): add replaces it (one entry, the last play time, the images move to the new
                     id, the image cache replaces the images that it has), remove removes it, show marks it; an
                     entry of the same name form with a different launcher stays
  backend start      (with --backend) the backend of the current release starts with no game, with
                     SOULWARDEN_HARNESS not set, on a free loopback port, answers /healthz, and stops

Usage: check_release.py [--work DIR] [--backend FILE] [--gamedata DIR] [--keep]
  --work DIR      the parent of the temporary folder (default: the temporary folder of the system). A folder on
                  btrfs (~/Games/soulwarden/work) tests the reflinks.
  --backend FILE  a backend executable (apps/backend/dist/soulwarden-backend) as the backend product
  --gamedata DIR  copy this game data folder in place of the made-up one (read only; use a folder on btrfs for --work)
  --keep          keep the temporary folder
Status 0: each case passed.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

TREE = Path(__file__).resolve().parents[1]
FILES = ("tools/release.sh", "tools/release.py", "tools/steam-shortcut.py", "linux-compat/run-soulwarden.sh")
BACKEND = "apps/backend/dist/soulwarden-backend"
HOOK = "apps/game-hook/dist/steam_api64.dll"
LAUNCHER = "linux-compat/run-soulwarden.sh"
FICLONE = getattr(fcntl, "FICLONE", 0x40049409)


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module  # a dataclass reads its module
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    with open(path, "rb") as reader:
        return hashlib.file_digest(reader, "sha256").hexdigest()


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def reflink_possible(folder: Path) -> bool:
    source, target = folder / ".probe-source", folder / ".probe-target"
    source.write_bytes(os.urandom(65536))
    try:
        with open(source, "rb") as reader, open(target, "wb") as writer:
            fcntl.ioctl(writer.fileno(), FICLONE, reader.fileno())
        return True
    except OSError:
        return False
    finally:
        source.unlink()
        target.unlink(missing_ok=True)


def shared_extents(path: Path) -> bool | None:
    """True when filefrag shows a shared extent of the file. None: no filefrag."""
    if shutil.which("filefrag") is None:
        return None
    result = subprocess.run(["filefrag", "-v", str(path)], capture_output=True, text=True, check=False)
    return "shared" in result.stdout


class Fixture:
    def __init__(self, work: Path, gamedata: Path | None, backend: Path | None) -> None:
        self.work = work
        self.repo = work / "repo"
        self.root = work / "releases"
        self.data = work / "data"
        self.home = work / "home"
        self.gamedata = gamedata or work / "gamedata"
        self.backend = backend
        self.processes: list[subprocess.Popen] = []
        for folder in (self.repo, self.data, self.home):
            folder.mkdir(parents=True)
        self.env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "LANG": "C.UTF-8",
            "HOME": str(self.home),
            "XDG_DATA_HOME": str(self.home / ".local" / "share"),
            "SOULWARDEN_RELEASE_ROOT": str(self.root),
            "SOULWARDEN_DATA_DIR": str(self.data),
            "SOULWARDEN_GAMEDATA_DIR": str(self.gamedata),
            "GIT_CONFIG_GLOBAL": os.devnull,
            "GIT_CONFIG_NOSYSTEM": "1",
        }
        for name in FILES:
            (self.repo / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(TREE / name, self.repo / name)
        (self.repo / ".gitignore").write_text("dist/\n__pycache__/\n")
        (self.repo / "notes.txt").write_text("made-up state 0\n")
        self.git("init", "-q", "-b", "main")
        self.commit("Made-up state 0")
        if backend is not None:
            self.put(BACKEND, backend.read_bytes(), 0o755)
        else:
            self.put(BACKEND, b"#!/bin/sh\nexit 0\n" + b"#" * 65536 + b"\n", 0o755)
        self.put(HOOK, os.urandom(262144), 0o644)
        if gamedata is None:
            (self.gamedata / "folder-b").mkdir(parents=True)
            (self.gamedata / "table-a.json").write_bytes(os.urandom(98304))
            (self.gamedata / "table-c.json").write_text("{}\n")
            (self.gamedata / "folder-b" / "part-1.bin").write_bytes(os.urandom(262144))
            (self.gamedata / "folder-b" / "part-2.bin").write_bytes(os.urandom(131072))

    def git(self, *arguments: str) -> str:
        command = ["git", "-C", str(self.repo), "-c", "user.name=check", "-c", "user.email=check",
                   "-c", "commit.gpgsign=false", *arguments]
        return subprocess.run(command, env=self.env, capture_output=True, text=True, check=True).stdout.strip()

    def commit(self, subject: str) -> str:
        (self.repo / "notes.txt").write_text(subject + "\n")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", subject)
        return self.git("rev-parse", "--short", "HEAD")

    def put(self, name: str, content: bytes, mode: int) -> None:
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.unlink(missing_ok=True)
        path.write_bytes(content)
        path.chmod(mode)

    def release(self, *arguments: str, env: dict | None = None) -> subprocess.CompletedProcess:
        return subprocess.run([str(self.repo / "tools/release.sh"), *arguments], env=env or self.env,
                              capture_output=True, text=True, check=False)

    def shortcut(self, *arguments: str, env: dict | None = None, steam: str = "steam") -> subprocess.CompletedProcess:
        return subprocess.run(["python3", "-B", str(self.repo / "tools/steam-shortcut.py"), *arguments,
                               "--steam-dir", str(self.work / steam)], env=env or self.env,
                              capture_output=True, text=True, check=False)

    def current(self) -> str | None:
        link = self.root / "current"
        return os.readlink(link) if link.is_symlink() else None

    def releases(self) -> list[str]:
        if not self.root.is_dir():
            return []
        return sorted(entry.name for entry in self.root.iterdir()
                      if entry.is_dir() and not entry.is_symlink() and not entry.name.startswith("."))

    def manifest(self, name: str) -> dict:
        return json.loads((self.root / name / "release.json").read_text())

    def hold_lock(self) -> subprocess.Popen:
        """A process that holds run.lock of the data directory with flock, as the launcher of a game."""
        process = subprocess.Popen(["flock", str(self.data / "run.lock"), "-c", "echo held; exec sleep 600"],
                                   stdout=subprocess.PIPE, text=True, env=self.env, start_new_session=True)
        self.processes.append(process)
        if process.stdout.readline().strip() != "held":
            raise RuntimeError("flock did not take the lock")
        return process

    def stop(self, process: subprocess.Popen) -> None:
        """Stops a process and each process of its session (the child of flock holds the lock too)."""
        for signal_number in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.killpg(process.pid, signal_number)
            except ProcessLookupError:
                pass
            try:
                process.wait(10)
                break
            except subprocess.TimeoutExpired:
                continue
        if process.stdout is not None:
            process.stdout.close()

    def stop_all(self) -> None:
        for process in self.processes:
            self.stop(process)


class Results:
    def __init__(self) -> None:
        self.failures = 0

    def case(self, name: str, passed: bool, detail: str) -> bool:
        print(f"{'ok' if passed else 'FAIL'} {name}: {detail}", flush=True)
        self.failures += 0 if passed else 1
        return passed


def read_only(folder: Path) -> bool:
    return all(not (path.stat().st_mode & 0o222) for path in folder.rglob("*") if path.is_file())


def same_tree(left: Path, right: Path) -> bool:
    files = sorted(path.relative_to(left) for path in left.rglob("*") if path.is_file())
    other = sorted(path.relative_to(right) for path in right.rglob("*") if path.is_file())
    return files == other and all(sha256(left / name) == sha256(right / name) for name in files)


def check_release_cases(fixture: Fixture, results: Results) -> None:
    repo, root = fixture.repo, fixture.root
    short1 = fixture.commit("Made-up state 1")
    first = fixture.release("install")
    name1 = fixture.current() or ""
    release1 = root / name1
    manifest_text = (release1 / "release.json").read_text() if (release1 / "release.json").is_file() else ""
    manifest = json.loads(manifest_text) if manifest_text else {}
    parts = manifest.get("parts", {})
    layout = all((release1 / name).is_file() for name in (LAUNCHER, BACKEND, HOOK, "VERSION", "release.json"))
    results.case("install", first.returncode == 0 and re.fullmatch(rf"\d{{4}}-\d{{2}}-\d{{2}}-{short1}", name1) is not None
                 and layout and (release1 / "VERSION").read_text() == name1 + "\n"
                 and manifest.get("commit") == fixture.git("rev-parse", "HEAD")
                 and manifest.get("subject") == "Made-up state 1" and manifest.get("dirty") is False
                 and all(parts.get(name, {}).get("sha256") == sha256(repo / name) for name in (LAUNCHER, BACKEND, HOOK))
                 and sha256(release1 / LAUNCHER) == sha256(repo / LAUNCHER)
                 and same_tree(fixture.gamedata, release1 / "gamedata")
                 and manifest.get("gamedata", {}).get("files") == sum(1 for path in fixture.gamedata.rglob("*") if path.is_file())
                 and read_only(release1) and os.access(release1 / BACKEND, os.X_OK) and os.access(release1 / LAUNCHER, os.X_OK)
                 and "/home/" not in manifest_text and str(fixture.work) not in manifest_text,
                 f"status {first.returncode}, current -> {name1}, layout {layout}, parts {sorted(parts)}, "
                 f"read only {read_only(release1) if release1.is_dir() else None}, "
                 f"{manifest.get('gamedata', {}).get('files')} game data files")

    match = re.search(r"copies: (\d+) reflinks, (\d+) normal copies", first.stdout)
    reflinks, copies = (int(match.group(1)), int(match.group(2))) if match else (-1, -1)
    if reflink_possible(fixture.work):
        largest = max((path for path in (release1 / "gamedata").rglob("*") if path.is_file()), key=lambda path: path.stat().st_size)
        shared = shared_extents(largest)
        results.case("copy", reflinks >= 3 and shared is not False,
                     f"reflink file system: {reflinks} reflinks, {copies} normal copies; filefrag of the largest game data "
                     f"file ({largest.stat().st_size} bytes): shared extents {shared}")
    else:
        results.case("copy", reflinks == 0 and copies >= 3 and same_tree(fixture.gamedata, release1 / "gamedata"),
                     f"no reflink on this file system: {reflinks} reflinks, {copies} normal copies, equal bytes")

    again = fixture.release("install")
    results.case("same state", again.returncode == 0 and "No new release" in again.stdout
                 and fixture.releases() == [name1] and fixture.current() == name1,
                 f"status {again.returncode}, releases {fixture.releases()}")

    short2 = fixture.commit("Made-up state 2")
    fixture.put(HOOK, os.urandom(262144), 0o644)
    second = fixture.release("install")
    name2 = fixture.current() or ""
    results.case("second install", second.returncode == 0 and name2.endswith(short2) and name2 != name1
                 and fixture.releases() == sorted([name1, name2]) and fixture.manifest(name2).get("serial") == 2
                 and fixture.manifest(name2)["parts"][HOOK]["sha256"] == sha256(repo / HOOK)
                 and fixture.manifest(name1)["parts"][HOOK]["sha256"] != sha256(repo / HOOK),
                 f"status {second.returncode}, current -> {name2}, releases {fixture.releases()}")

    (repo / "notes.txt").write_text("a change that is not a commit\n")
    refused = fixture.release("install")
    hook = (repo / HOOK).read_bytes()
    fixture.git("checkout", "-q", "--", "notes.txt")
    (repo / HOOK).unlink()
    missing = fixture.release("install")
    fixture.put(HOOK, hook, 0o644)
    leftovers = [entry.name for entry in root.iterdir() if entry.name.startswith((".staging-", ".current-"))]
    results.case("refused", refused.returncode == 1 and "differ from the commit" in refused.stderr
                 and missing.returncode == 1 and "steam_api64.dll was not found" in missing.stderr
                 and fixture.releases() == sorted([name1, name2]) and fixture.current() == name2 and not leftovers,
                 f"changed file: status {refused.returncode} ({refused.stderr.strip()}); missing hook: status "
                 f"{missing.returncode}; releases {len(fixture.releases())}, leftovers {leftovers}")

    (repo / "notes.txt").write_text("a change for --allow-dirty\n")
    launcher = (repo / LAUNCHER).read_bytes()
    (repo / LAUNCHER).write_bytes(launcher + b"# a change of the working copy\n")
    dirty = fixture.release("install", "--allow-dirty")
    name3 = fixture.current() or ""
    dirty_launcher = (root / name3 / LAUNCHER).read_bytes() if (root / name3 / LAUNCHER).is_file() else b""
    fixture.git("checkout", "-q", "--", ".")
    results.case("allow dirty", dirty.returncode == 0 and name3.endswith(f"{short2}-dirty") and fixture.manifest(name3)["dirty"] is True
                 and dirty_launcher.endswith(b"# a change of the working copy\n"),
                 f"status {dirty.returncode}, current -> {name3}")

    shown = fixture.release("status")
    lines = shown.stdout.splitlines()
    marked = [line for line in lines if line.startswith("* ")]
    results.case("status", shown.returncode == 0 and len(marked) == 1 and name3 in marked[0]
                 and all(any(name in line and fixture.manifest(name)["short_commit"] in line
                             and fixture.manifest(name)["subject"] in line
                             and fixture.manifest(name)["installed_at"][:10] in line for line in lines)
                         for name in (name1, name2, name3)),
                 f"{len(lines)} lines, current line: {marked[0].split()[1] if marked else None}")

    activated = fixture.release("activate", name1)
    after_activate = fixture.current()
    unknown = fixture.release("activate", "1999-01-01-0000000")
    results.case("activate", activated.returncode == 0 and after_activate == name1 and unknown.returncode == 1
                 and fixture.current() == name1, f"activate: status {activated.returncode}, current -> {after_activate}; "
                 f"unknown version: status {unknown.returncode}")

    fixture.release("activate", name3)
    back1 = fixture.release("rollback")
    after1 = fixture.current()
    back2 = fixture.release("rollback")
    after2 = fixture.current()
    back3 = fixture.release("rollback")
    results.case("rollback", back1.returncode == 0 and after1 == name2 and back2.returncode == 0 and after2 == name1
                 and back3.returncode == 1 and "no release before" in back3.stderr and fixture.current() == name1,
                 f"{name3} -> {after1} -> {after2}, then status {back3.returncode}")

    fixture.release("activate", name3)
    holder = fixture.hold_lock()
    short4 = fixture.commit("Made-up state 4")
    locked = fixture.release("install")
    name4 = next((name for name in fixture.releases() if name.endswith(short4)), "")
    locked_lines = [line for line in locked.stdout.splitlines() if "tools/release.sh activate" in line]
    locked_activate = fixture.release("activate", name4)
    locked_rollback = fixture.release("rollback")
    locked_status = fixture.release("status")
    unchanged = fixture.current() == name3
    count_held = len(fixture.releases())
    fixture.stop(holder)
    free_activate = fixture.release("activate", name4)
    results.case("lock", locked.returncode == 3 and name4 != "" and len(locked_lines) == 1 and name4 in locked_lines[0]
                 and locked_activate.returncode == 3 and locked_rollback.returncode == 3 and unchanged
                 and count_held == 4 and "runs" in locked_status.stdout
                 and free_activate.returncode == 0 and fixture.current() == name4,
                 f"lock held: install status {locked.returncode}, activate {locked_activate.returncode}, rollback "
                 f"{locked_rollback.returncode}, current unchanged {unchanged}, {count_held} releases; lock free: "
                 f"activate {free_activate.returncode}, current -> {fixture.current()}")

    after_free = fixture.releases()
    holder = fixture.hold_lock()
    short5 = fixture.commit("Made-up state 5")
    fixture.release("install")
    short6 = fixture.commit("Made-up state 6")
    fixture.release("install")
    while_held = fixture.releases()
    fixture.stop(holder)
    fixture.release("activate", name2)
    kept = fixture.releases()
    name5 = next((name for name in kept if name.endswith(short5)), "")
    name6 = next((name for name in kept if name.endswith(short6)), "")
    results.case("prune", name1 not in after_free and len(after_free) == 3 and len(while_held) == 5
                 and kept == sorted([name2, name4, name5, name6]) and fixture.current() == name2,
                 f"after the free activate: {len(after_free)} releases (the first removed: {name1 not in after_free}); "
                 f"with the lock held: {len(while_held)}; after activate {name2}: {kept}")


def check_launcher_cases(fixture: Fixture, results: Results, release_tool) -> None:
    text = (fixture.repo / LAUNCHER).read_text()
    named = set()
    for base, rest in re.findall(r"\$\{(repository_dir|launcher_dir)\}/([A-Za-z0-9_./-]+)", text):
        name = os.path.normpath(("linux-compat/" if base == "launcher_dir" else "") + rest)
        if name != ".":  # the folder of the repository itself
            named.add(name)
    allowed = {*release_tool.LAUNCHER_FILES, *release_tool.PRODUCTS, release_tool.GAMEDATA}
    results.case("launcher files", bool(named) and named <= allowed,
                 f"the launcher names {sorted(named)}; not in a release: {sorted(named - allowed)}")

    marker = 'if [[ ! -x "${proton_dir}/proton" ]]; then'
    head = text.split(marker)[0] if marker in text else ""
    probe = head + ('printf "%s\\n" "gamedata=${gamedata_dir}" "hook=${hook_dll}" "backend=${backend_executable}" '
                    '"script=${launcher_script}"\n')
    base = fixture.work / "launcher-probe"
    version = base / "releases" / "2026-01-01-0000000"
    (version / "linux-compat").mkdir(parents=True)
    (version / "gamedata").mkdir()
    (version / "linux-compat" / "run-soulwarden.sh").write_text(probe)
    (base / "releases" / "current").symlink_to(version.name)
    working = base / "working-copy"
    (working / "linux-compat").mkdir(parents=True)
    (working / "linux-compat" / "run-soulwarden.sh").write_text(probe)

    def paths(script: Path, gamedata: str | None) -> dict[str, str]:
        env = {key: value for key, value in fixture.env.items() if not key.startswith("SOULWARDEN_")}
        if gamedata is not None:
            env["SOULWARDEN_GAMEDATA_DIR"] = gamedata
        result = subprocess.run(["bash", str(script)], env=env, capture_output=True, text=True, check=False)
        return dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)

    physical = version.resolve()

    def masked(text: str) -> str:
        return text.replace(str(fixture.work.resolve()), "WORK").replace(str(fixture.work), "WORK")
    release = paths(base / "releases" / "current" / "linux-compat" / "run-soulwarden.sh", None)
    chosen = paths(base / "releases" / "current" / "linux-compat" / "run-soulwarden.sh", "/made-up/gamedata")
    plain = paths(working / "linux-compat" / "run-soulwarden.sh", None)
    default = str(fixture.home / ".local" / "share" / "soulwarden" / "gamedata")
    results.case("launcher paths", bool(head) and release.get("gamedata") == str(physical / "gamedata")
                 and release.get("hook") == str(physical / HOOK) and release.get("backend") == str(physical / BACKEND)
                 and release.get("script") == str(physical / LAUNCHER)
                 and chosen.get("gamedata") == "/made-up/gamedata" and plain.get("gamedata") == default,
                 f"through current: {masked(release.get('gamedata', ''))}; SOULWARDEN_GAMEDATA_DIR: "
                 f"{chosen.get('gamedata')}; working copy: {masked(plain.get('gamedata', ''))}")


def made_up_steam(steam: Path, tool, entries: list) -> Path:
    """A made-up Steam directory: an account, its shortcuts.vdf with these entries, and an image cache with
    the capsule and an icon of the game. Returns the configuration folder of the account."""
    steam_id = (0x01100001 << 32) | 4242  # made up: the account 4242
    (steam / "config").mkdir(parents=True)
    (steam / "config" / "loginusers.vdf").write_text(
        f'"users"\n{{\n\t"{steam_id}"\n\t{{\n\t\t"AccountName"\t\t"made-up"\n\t\t"PersonaName"\t\t"Made-up"\n'
        f'\t\t"MostRecent"\t\t"1"\n\t\t"Timestamp"\t\t"1700000000"\n\t}}\n}}\n')
    config = steam / "userdata" / str(steam_id & 0xFFFFFFFF) / "config"
    config.mkdir(parents=True)
    items = [(tool.TYPE_MAP, str(index), entry) for index, entry in enumerate(entries)]
    (config / "shortcuts.vdf").write_bytes(tool.dump_map([(tool.TYPE_MAP, "shortcuts", items)]))
    cache = steam / "appcache" / "librarycache" / str(tool.GAME_APP_ID)
    cache.mkdir(parents=True)
    (cache / "library_capsule.jpg").write_bytes(b"made-up capsule")
    (cache / "icon.jpg").write_bytes(b"made-up icon")
    return config


def shortcut_entries(tool, config: Path) -> list:
    top, _ = tool.parse_map((config / "shortcuts.vdf").read_bytes(), 0)
    return [entry for _, _, entry in next(value for kind, key, value in top if key == "shortcuts")]


def check_shortcut_cases(fixture: Fixture, results: Results) -> None:
    tool = load("steam_shortcut_check", fixture.repo / "tools/steam-shortcut.py")
    steam = fixture.work / "steam"
    other = tool.make_entry("Made-up other game", '"/opt/made-up/run"', '"/opt/made-up"', "", 7)
    config = made_up_steam(steam, tool, [other])
    shortcuts = config / "shortcuts.vdf"
    backups = fixture.data / "backup"

    def entries() -> list:
        top, _ = tool.parse_map(shortcuts.read_bytes(), 0)
        return next(value for kind, key, value in top if key == "shortcuts")

    def soulwarden() -> list:
        return [entry for _, _, entry in entries() if str(tool.field(entry, "Exe")).endswith('/linux-compat/run-soulwarden.sh"')]

    release_exe = f'"{fixture.root}/current/{LAUNCHER}"'
    working_exe = f'"{fixture.repo.resolve()}/{LAUNCHER}"'
    original = shortcuts.read_bytes()
    added = fixture.shortcut("add")
    found = soulwarden()
    entry = found[0] if found else []
    app_id = tool.shortcut_id(release_exe, tool.DEFAULT_NAME)
    copies = sorted(backups.iterdir()) if backups.is_dir() else []
    results.case("shortcut release", added.returncode == 0 and len(entries()) == 2 and len(found) == 1
                 and tool.field(entry, "Exe") == release_exe and tool.field(entry, "StartDir") == f'"{fixture.root}/current"'
                 and entries()[0][2] == other and len(copies) == 1 and copies[0].read_bytes() == original
                 and (config / "grid" / f"{app_id}p.jpg").is_file(),
                 f"status {added.returncode}, {len(entries())} entries, Exe {str(tool.field(entry, 'Exe')).replace(str(fixture.work), 'WORK')}, "
                 f"{len(copies)} backup")

    top, _ = tool.parse_map(shortcuts.read_bytes(), 0)
    for _, _, item in next(value for kind, key, value in top if key == "shortcuts"):
        if tool.field(item, "Exe") == release_exe:
            item[:] = [(kind, key, 1234 if key == "LastPlayTime" else value) for kind, key, value in item]
    shortcuts.write_bytes(tool.dump_map(top))
    switched = fixture.shortcut("add", "--launcher", "working-copy")
    found = soulwarden()
    entry = found[0] if found else []
    new_id = tool.shortcut_id(working_exe, tool.DEFAULT_NAME)
    copies = sorted(backups.iterdir()) if backups.is_dir() else []
    results.case("shortcut working copy", switched.returncode == 0 and len(entries()) == 2 and len(found) == 1
                 and tool.field(entry, "Exe") == working_exe and tool.field(entry, "StartDir") == f'"{fixture.repo.resolve()}"'
                 and tool.field(entry, "LastPlayTime") == 1234 and not (config / "grid" / f"{app_id}p.jpg").exists()
                 and (config / "grid" / f"{new_id}p.jpg").is_file() and len(copies) == 2,
                 f"status {switched.returncode}, {len(found)} Soulwarden entry, LastPlayTime {tool.field(entry, 'LastPlayTime')}, "
                 f"{len(copies)} backups")

    listed = fixture.shortcut("show")
    results.case("shortcut show", listed.returncode == 0 and "(working-copy)" in listed.stdout
                 and "add selects the launcher of the release" in listed.stdout,
                 f"status {listed.returncode}, {len(listed.stdout.splitlines())} lines")

    no_release = dict(fixture.env, SOULWARDEN_RELEASE_ROOT=str(fixture.work / "no-releases"))
    before = shortcuts.read_bytes()
    refused = fixture.shortcut("add", "--launcher", "release", env=no_release)
    automatic = fixture.shortcut("add", env=no_release)
    results.case("shortcut no release", refused.returncode != 0 and "tools/release.sh" in refused.stderr
                 and automatic.returncode == 0 and "No change was made" in automatic.stdout and shortcuts.read_bytes() == before,
                 f"--launcher release: status {refused.returncode}; auto: status {automatic.returncode}, the working copy, no change")

    removed = fixture.shortcut("remove")
    copies = sorted(backups.iterdir()) if backups.is_dir() else []
    results.case("shortcut remove", removed.returncode == 0 and len(entries()) == 1 and entries()[0][2] == other
                 and not soulwarden() and not (config / "grid" / f"{new_id}p.jpg").exists() and len(copies) == 3,
                 f"status {removed.returncode}, {len(entries())} entry, {len(copies)} backups")


def check_older_entry_cases(fixture: Fixture, results: Results) -> None:
    tool = load("steam_shortcut_check", fixture.repo / "tools/steam-shortcut.py")
    backups = fixture.data / "backup"
    release_exe = f'"{fixture.root}/current/{LAUNCHER}"'
    other = tool.make_entry("Made-up other game", '"/opt/made-up/run"', '"/opt/made-up"', "", 7)
    # The same name form, with a launcher that is not a launcher of the tool: it stays.
    decoy = tool.make_entry("New World: Aeternum (Decoy)", '"/opt/made-up/decoy/run.sh"', '"/opt/made-up/decoy"', "", 9)
    # An older launcher name in the working copy: a link to the launcher (the form of the main checkout).
    link = fixture.repo / "linux-compat" / "run-oldname.sh"
    link.symlink_to("run-soulwarden.sh")
    old_exe = f'"{link}"'
    old = tool.make_entry("New World: Aeternum (Oldname)", old_exe, f'"{fixture.repo}"', "", 4321)
    config = made_up_steam(fixture.work / "steam-older", tool, [other, old, decoy])
    grid = config / "grid"
    grid.mkdir()
    old_id = tool.shortcut_id(old_exe, "New World: Aeternum (Oldname)")
    for suffix, content in (("p.jpg", b"old capsule"), ("_hero.jpg", b"old hero"), ("_logo.png", b"old logo"),
                            ("_icon.jpg", b"old icon")):
        (grid / f"{old_id}{suffix}").write_bytes(content)
    count = len(list(backups.iterdir())) if backups.is_dir() else 0
    added = fixture.shortcut("add", steam="steam-older")
    entries = shortcut_entries(tool, config)
    found = [entry for entry in entries if tool.field(entry, "Exe") in (release_exe, old_exe)]
    entry = found[0] if found else []
    new_id = tool.shortcut_id(release_exe, tool.DEFAULT_NAME)

    def image(app_id: int, suffix: str) -> bytes | None:
        path = grid / f"{app_id}{suffix}"
        return path.read_bytes() if path.is_file() else None

    left = sorted(path.name for path in grid.iterdir() if path.name.startswith(str(old_id)))
    results.case("shortcut older entry", added.returncode == 0 and len(entries) == 3 and len(found) == 1
                 and tool.field(entry, "Exe") == release_exe and tool.field(entry, "AppName") == tool.DEFAULT_NAME
                 and tool.field(entry, "LastPlayTime") == 4321 and entries[0] == other and decoy in entries
                 and image(new_id, "_hero.jpg") == b"old hero" and image(new_id, "_logo.png") == b"old logo"
                 and image(new_id, "p.jpg") == b"made-up capsule" and image(new_id, "_icon.jpg") == b"made-up icon"
                 and tool.field(entry, "icon") == str(grid / f"{new_id}_icon.jpg") and not left
                 and len(list(backups.iterdir())) == count + 1,
                 f"status {added.returncode}, {len(entries)} entries, {len(found)} Soulwarden entry, LastPlayTime "
                 f"{tool.field(entry, 'LastPlayTime')}, moved hero {image(new_id, '_hero.jpg') == b'old hero'}, "
                 f"images of the old id left {left}, decoy kept {decoy in entries}")

    # The older entry of a different checkout: the name form and run-NAME.sh, with no file.
    elsewhere = '"/made-up/checkout/linux-compat/run-oldname.sh"'
    old = tool.make_entry("New World: Aeternum (Oldname)", elsewhere, '"/made-up/checkout"', "", 4321)
    config = made_up_steam(fixture.work / "steam-elsewhere", tool, [other, old, decoy])
    grid = config / "grid"
    grid.mkdir()
    old_id = tool.shortcut_id(elsewhere, "New World: Aeternum (Oldname)")
    (grid / f"{old_id}_hero.jpg").write_bytes(b"old hero")
    removed = fixture.shortcut("remove", steam="steam-elsewhere")
    entries = shortcut_entries(tool, config)
    results.case("shortcut older entry remove", removed.returncode == 0 and entries == [other, decoy]
                 and not (grid / f"{old_id}_hero.jpg").exists(),
                 f"status {removed.returncode}, {len(entries)} entries left (the other game and the decoy), "
                 f"images of the old id left {(grid / f'{old_id}_hero.jpg').exists()}")

    # A link to the launcher of the working copy with a name of a different form.
    linked = tool.make_entry("Made-up old name", old_exe, f'"{fixture.repo}"', "", 5)
    made_up_steam(fixture.work / "steam-link", tool, [other, linked, decoy])
    listed = fixture.shortcut("show", steam="steam-link")
    marks = [line for line in listed.stdout.splitlines() if "<- Soulwarden launcher" in line]
    results.case("shortcut older entry show", listed.returncode == 0 and len(marks) == 1 and "Made-up old name" in marks[0]
                 and "(older entry)" in marks[0], f"status {listed.returncode}, {len(marks)} marked entry")


def check_backend_case(fixture: Fixture, results: Results) -> None:
    release = (fixture.root / "current").resolve()
    executable = fixture.root / "current" / BACKEND
    port, rep_port = free_port(), free_port()
    data = fixture.work / "backend-data"
    env = {key: value for key, value in fixture.env.items() if not key.startswith("SOULWARDEN_")}
    log = open(fixture.work / "backend-start.log", "wb")
    process = subprocess.Popen([str(executable), "--data-dir", str(data), "--gamedata-dir", str(fixture.root / "current" / "gamedata"),
                                "--http-host", "127.0.0.1", "--http-port", str(port), "--rep-address", f"127.0.0.1:{rep_port}",
                                "--no-rep-listener"], env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    fixture.processes.append(process)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    health, started = None, time.monotonic()
    while time.monotonic() - started < 120 and process.poll() is None:
        try:
            with opener.open(f"http://127.0.0.1:{port}/healthz", timeout=1) as reply:
                health = reply.status
                break
        except OSError:
            time.sleep(0.2)
    ready = time.monotonic() - started
    try:
        opener.open(urllib.request.Request(f"http://127.0.0.1:{port}/v1/shutdown", method="POST", data=b""), timeout=3).close()
    except OSError:
        pass
    try:
        status = process.wait(20)
        stopped = "shutdown route"
    except subprocess.TimeoutExpired:
        fixture.stop(process)
        status, stopped = process.returncode, "signal"
    log.close()
    results.case("backend start", health == 200 and process.poll() is not None and release.name == fixture.current(),
                 f"the backend of release {release.name} ({sha256(executable)[:12]}): /healthz status {health} after "
                 f"{ready:.1f} s, stopped by the {stopped} (status {status})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--work", help="the parent of the temporary folder")
    parser.add_argument("--backend", help="a backend executable as the backend product (case backend start)")
    parser.add_argument("--gamedata", help="a game data folder to copy (read only) in place of the made-up one")
    parser.add_argument("--keep", action="store_true", help="keep the temporary folder")
    arguments = parser.parse_args()
    parent = Path(arguments.work).expanduser() if arguments.work else None
    if parent is not None:
        parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="soulwarden-release-check-", dir=parent))
    backend = Path(arguments.backend).expanduser().resolve() if arguments.backend else None
    gamedata = Path(arguments.gamedata).expanduser().resolve() if arguments.gamedata else None
    fixture = Fixture(work, gamedata, backend)
    results = Results()
    try:
        release_tool = load("release_check", fixture.repo / "tools/release.py")
        check_release_cases(fixture, results)
        check_launcher_cases(fixture, results, release_tool)
        check_shortcut_cases(fixture, results)
        check_older_entry_cases(fixture, results)
        if backend is not None:
            check_backend_case(fixture, results)
    finally:
        fixture.stop_all()
        if arguments.keep:
            print(f"release check: the temporary folder stays: {work}")
        else:
            for path in work.rglob("*"):
                if not path.is_symlink() and path.is_dir():
                    path.chmod(path.stat().st_mode | stat.S_IWUSR)
            shutil.rmtree(work)
    print(f"release check: {results.failures} failed check(s)")
    return 1 if results.failures else 0


if __name__ == "__main__":
    sys.exit(main())
