#!/usr/bin/env python3
"""Mute a development run: set the master volume (m_masterVolume) of each account to 0 in the settings files of a
development Proton prefix. tools/dev-mute.sh calls it (tools/dev-run.sh, before each start, while no game of the prefix
runs: the game would write the old value back).

Usage: dev_mute.py DEVELOPMENT_PREFIX USER_PREFIX

The settings of the user must not change. A check of a path and a later write by the same path is never safe: a
folder can be moved and a link put at its old path between the two (code review of stage 1.1b, finding F1; before it,
code review of stage 3.3b-S2b, finding F1). Thus the helper does not open a path a second time:

  1. It opens the development prefix and then each part of the settings folder below it from the folder before it
     (openat with O_NOFOLLOW: a link in a part is refused), and it keeps the descriptor of the settings folder.
  2. It opens each settings file one time from that descriptor (O_NOFOLLOW: a link is refused), and checks the open
     descriptor: the real path (/proc/self/fd/N) is in the resolved development prefix and not in the resolved prefix of
     the user, it is a regular file with one link, and its device and inode are not the device and inode of a settings
     file of the user (a hard link).
  3. It reads, changes, and writes the file through the same descriptor. A move of a folder or a new link after the
     open changes nothing: the descriptor names the file that the checks saw.

When the development prefix, a part of its settings folder, or a settings file is the prefix of the user, a path in
it, or a link, the helper writes nothing and says so on the error output (each file is checked before the first
write: one refused file stops all writes). Status 0 in each case: a start with no mute is not a fault.
SOULWARDEN_DEV_SOUND=1 keeps the volumes. tools/check_dev_mute.py checks this with folders of a test, never with the
prefix of the user.
"""

from __future__ import annotations

import os
import re
import stat
import sys
from typing import Callable

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

SETTINGS_PARTS = ("pfx", "drive_c", "users", "steamuser", "AppData", "Roaming", "AGS", "New World", "savedata")
SETTINGS_FILE = re.compile(r"usersettings_.*\.javsave")
VOLUME = re.compile(rb'(field="m_masterVolume" value=")[0-9]+(")')
FOLDER_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
FILE_FLAGS = os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC

Hook = Callable[[str], None]


class Refused(Exception):
    """A path is the prefix of the user, a path in it, or a link: no file changes."""


def say(text: str) -> None:
    print(f"dev-mute: {text}", file=sys.stderr)


def inside(path: str, folder: str) -> bool:
    """@return True when the resolved path is the folder or a path in it."""
    return path == folder or path.startswith(folder.rstrip("/") + "/")


def real_path(descriptor: int) -> str:
    """@return The path of the open file or folder now (the kernel link of the descriptor)."""
    return os.readlink(f"/proc/self/fd/{descriptor}")


def user_files(user_prefix: str) -> set[tuple[int, int]]:
    """@return The device and inode of each settings file of the user (read only, no write)."""
    found: set[tuple[int, int]] = set()
    folder = os.path.join(user_prefix, *SETTINGS_PARTS)
    try:
        names = os.listdir(folder)
    except OSError:
        return found
    for name in names:
        if SETTINGS_FILE.fullmatch(name):
            try:
                status = os.stat(os.path.join(folder, name))
            except OSError:
                continue
            found.add((status.st_dev, status.st_ino))
    return found


def open_settings_folder(development_prefix: str, development_resolved: str, user_resolved: str) -> int | None:
    """@return The descriptor of the settings folder of the development prefix, or no value when it is not there yet."""
    try:
        prefix = os.open(development_prefix, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    except FileNotFoundError:
        return None
    current = prefix
    try:
        if real_path(current) != development_resolved or inside(development_resolved, user_resolved):
            raise Refused(f"the development prefix {development_prefix}")
        for part in SETTINGS_PARTS:
            try:
                following = os.open(part, FOLDER_FLAGS, dir_fd=current)
            except FileNotFoundError:
                os.close(current)
                return None
            except OSError as error:
                # ELOOP or ENOTDIR: a link (or a file) in the place of a folder.
                raise Refused(f"the settings folder of the development prefix (the part {part!r} is a link or not a "
                              f"folder: {error.strerror})") from error
            os.close(current)
            current = following
            path = real_path(current)
            if inside(path, user_resolved) or not inside(path, development_resolved):
                raise Refused("the settings folder of the development prefix")
        return current
    except BaseException:
        os.close(current)
        raise


def mute(development_prefix: str, user_prefix: str, hook: Hook | None = None) -> int:
    """Sets the master volume of each settings file of the development prefix to 0. @return The number of changed files.
    @param hook A function of a test (tools/check_dev_mute.py): the helper calls it with "folder" after the open of the
           settings folder and with "write" after the checks of the files, before the first write. It never runs in a
           development start."""
    user_resolved = os.path.realpath(user_prefix)
    development_resolved = os.path.realpath(development_prefix)
    if inside(development_resolved, user_resolved):
        raise Refused(f"the development prefix {development_prefix}")
    folder = open_settings_folder(development_prefix, development_resolved, user_resolved)
    if folder is None:
        return 0  # No prefix or no settings yet: Proton makes them at the first start, with the volume of the game.
    opened: list[tuple[str, int]] = []
    try:
        if hook is not None:
            hook("folder")
        protected = user_files(user_resolved)
        for name in sorted(os.listdir(folder)):
            if not SETTINGS_FILE.fullmatch(name):
                continue
            try:
                descriptor = os.open(name, FILE_FLAGS, dir_fd=folder)
            except OSError as error:
                raise Refused(f"the settings file {name} (a link, or it cannot be opened: {error.strerror})") from error
            opened.append((name, descriptor))
            status = os.fstat(descriptor)
            path = real_path(descriptor)
            if (not stat.S_ISREG(status.st_mode) or status.st_nlink != 1 or (status.st_dev, status.st_ino) in protected
                    or inside(path, user_resolved) or not inside(path, development_resolved)):
                raise Refused(f"the settings file {name}")
        if hook is not None:
            hook("write")
        changed = 0
        for name, descriptor in opened:
            content = b""
            while chunk := os.pread(descriptor, 1 << 16, len(content)):
                content += chunk
            muted = VOLUME.sub(rb"\g<1>0\2", content)
            if muted != content:
                os.pwrite(descriptor, muted, 0)
                os.ftruncate(descriptor, len(muted))
                changed += 1
        return changed
    finally:
        for _, descriptor in opened:
            os.close(descriptor)
        os.close(folder)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX", file=sys.stderr)
        return 2
    if os.environ.get("SOULWARDEN_DEV_SOUND", "0") == "1":
        return 0
    try:
        mute(sys.argv[1], sys.argv[2])
    except Refused as refused:
        user_resolved = os.path.realpath(sys.argv[2])
        say(f"{refused} is the prefix of the user, a path in it, or a link ({user_resolved}): no settings file changed, "
            "the run has the volume of the game")
    except OSError as error:
        say(f"no settings file changed ({error.strerror}): the run has the volume of the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
