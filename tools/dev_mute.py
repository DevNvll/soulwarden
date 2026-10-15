#!/usr/bin/env python3
"""Mute a development run: set the master volume (m_masterVolume) of each account to 0 in the settings files of a
development Proton prefix. tools/dev-mute.sh calls it (tools/dev-run.sh, before each start, while no game of the prefix
runs: the game would write the old value back).

Usage: dev_mute.py DEVELOPMENT_PREFIX USER_PREFIX

The settings of the user must not change. A check of a path and a later use of a path is never safe: a folder can be
moved and a link put at its old path between the two (code reviews of stage 3.3b-S2b, 1.1b, and 3.3b-S5, finding F1 of
each). Thus the helper uses no path text after its first open:

  1. It opens the development prefix one time (O_DIRECTORY) and keeps the descriptor. It compares the device and the
     inode of that descriptor (fstat) with the device and the inode (stat) of the prefix of the user and of each folder
     from it to the settings folder of the user. When one is equal, it writes nothing.
  2. It opens each next folder of the settings folder (pfx/drive_c/users/steamuser/AppData/Roaming/AGS/New World/savedata)
     from the descriptor before it (openat, O_DIRECTORY and O_NOFOLLOW: a link is refused). A folder whose device and
     inode are a folder of step 1 is refused too.
  3. It opens each settings file from the descriptor of the settings folder (O_RDWR and O_NOFOLLOW). It refuses a file
     that is not a regular file, a file with more than one link (st_nlink), and a file whose device and inode are those
     of a settings file of the user (stat of the files of the user, no open).
  4. It reads, changes, and writes each file through its descriptor (pread, pwrite, ftruncate). A move of a folder or a
     new link after the open changes nothing: each descriptor names the object that the checks saw.

The check protects against a path or a link that leads to the prefix of the user by mistake. It is not a protection
against a process of the same user: such a process can change the files of the user anyway.

When a check refuses, the helper writes nothing and says so on the error output (each file is checked before the first
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
PREFIX_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC
FOLDER_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
FILE_FLAGS = os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC

Hook = Callable[[str], None]
Identity = tuple[int, int]


class Refused(Exception):
    """An object of the development prefix is an object of the user, a link, or a file with more than one link: no file changes."""


def say(text: str) -> None:
    print(f"dev-mute: {text}", file=sys.stderr)


def identity_of(status: os.stat_result) -> Identity:
    return status.st_dev, status.st_ino


def user_folders(user_prefix: str) -> set[Identity]:
    """@return The device and inode of the prefix of the user and of each folder from it to its settings folder (stat, no open)."""
    found: set[Identity] = set()
    path = user_prefix
    for part in ("",) + SETTINGS_PARTS:
        path = os.path.join(path, part) if part else path
        try:
            found.add(identity_of(os.stat(path)))
        except FileNotFoundError:
            break  # The user has no such folder (yet): the folders below it are not there either.
    return found


def user_files(user_prefix: str) -> set[Identity]:
    """@return The device and inode of each settings file of the user (stat, no open, no write)."""
    found: set[Identity] = set()
    folder = os.path.join(user_prefix, *SETTINGS_PARTS)
    try:
        names = os.listdir(folder)
    except OSError:
        return found
    for name in names:
        if SETTINGS_FILE.fullmatch(name):
            try:
                found.add(identity_of(os.stat(os.path.join(folder, name))))
            except OSError:
                continue
    return found


def open_settings_folder(prefix: int, protected: set[Identity]) -> int | None:
    """@return The descriptor of the settings folder below the open development prefix, or no value when it is not there yet.
    The descriptor of the prefix stays open (the caller closes it)."""
    current = prefix
    try:
        for part in SETTINGS_PARTS:
            try:
                following = os.open(part, FOLDER_FLAGS, dir_fd=current)
            except FileNotFoundError:
                if current != prefix:
                    os.close(current)
                return None
            except OSError as error:
                # ELOOP or ENOTDIR: a link (or a file) in the place of a folder.
                raise Refused(f"the settings folder of the development prefix (the part {part!r} is a link or not a "
                              f"folder: {error.strerror})") from error
            if current != prefix:
                os.close(current)
            current = following
            if identity_of(os.fstat(current)) in protected:
                raise Refused(f"the settings folder of the development prefix (the part {part!r} is a folder of the user)")
        return current
    except BaseException:
        if current != prefix:
            os.close(current)
        raise


def mute(development_prefix: str, user_prefix: str, hook: Hook | None = None) -> int:
    """Sets the master volume of each settings file of the development prefix to 0. @return The number of changed files.
    @param hook A function of a test (tools/check_dev_mute.py): the helper calls it with "prefix" before the open of the
           development prefix, with "folder" after the open of the settings folder, and with "write" after the checks of the
           files, before the first write. It never runs in a development start."""
    if hook is not None:
        hook("prefix")
    try:
        prefix = os.open(development_prefix, PREFIX_FLAGS)
    except FileNotFoundError:
        return 0  # No prefix yet: Proton makes it at the first start, with the volume of the game.
    folder: int | None = None
    opened: list[int] = []
    try:
        # The objects of the user now, after the open: the descriptor of the development prefix cannot change any more.
        protected = user_folders(user_prefix)
        if identity_of(os.fstat(prefix)) in protected:
            raise Refused(f"the development prefix {development_prefix} (it is a folder of the user)")
        folder = open_settings_folder(prefix, protected)
        if folder is None:
            return 0  # No settings yet: Proton makes them at the first start, with the volume of the game.
        if hook is not None:
            hook("folder")
        protected_files = user_files(user_prefix)
        for name in sorted(os.listdir(folder)):
            if not SETTINGS_FILE.fullmatch(name):
                continue
            try:
                descriptor = os.open(name, FILE_FLAGS, dir_fd=folder)
            except OSError as error:
                raise Refused(f"the settings file {name} (a link, or it cannot be opened: {error.strerror})") from error
            opened.append(descriptor)
            status = os.fstat(descriptor)
            if not stat.S_ISREG(status.st_mode):
                raise Refused(f"the settings file {name} (not a regular file)")
            if status.st_nlink != 1:
                raise Refused(f"the settings file {name} (it has {status.st_nlink} links)")
            if identity_of(status) in protected_files:
                raise Refused(f"the settings file {name} (it is a settings file of the user)")
        if hook is not None:
            hook("write")
        changed = 0
        for descriptor in opened:
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
        for descriptor in opened:
            os.close(descriptor)
        if folder is not None:
            os.close(folder)
        os.close(prefix)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: tools/dev-mute.sh DEVELOPMENT_PREFIX USER_PREFIX", file=sys.stderr)
        return 2
    if os.environ.get("SOULWARDEN_DEV_SOUND", "0") == "1":
        return 0
    try:
        mute(sys.argv[1], sys.argv[2])
    except Refused as refused:
        say(f"{refused}: no settings file changed, the run has the volume of the game")
    except OSError as error:
        say(f"no settings file changed ({error.strerror}): the run has the volume of the game")
    return 0


if __name__ == "__main__":
    sys.exit(main())
