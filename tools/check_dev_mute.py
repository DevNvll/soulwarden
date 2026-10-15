#!/usr/bin/env python3
"""Check of tools/dev-mute.sh, the mute of a development run (code review of stage 3.3b-S2b, finding F1).

The check makes two made-up Proton prefixes in a temporary folder, a "user" prefix and a
"development" prefix, each with a settings file of the game, and runs the mute script for each
case. It never reads or changes the real prefix of the user, and it starts no game.

  Case                          The development prefix                  Expected result
  separate prefix with spaces   its own folder (a path with spaces)     its file muted, the user file kept
  exact user prefix             the user prefix                         the user file kept, a message
  sound override                its own folder, SOULWARDEN_DEV_SOUND=1  both files kept
  prefix link                   a link to the user prefix               the user file kept, a message
  dot alias                     the user prefix with "/." at its end    the user file kept, a message
  savedata link                 its own folder; its savedata folder is  the user file kept, a message
                                a link to the savedata of the user
  settings file link            its own folder; its settings file is a  the user file kept, a message
                                link to the settings file of the user
  user path through a link      its own folder; the user prefix path    its file muted, the user file kept
                                has a link in it

Each run must give status 0. Usage: check_dev_mute.py [--script PATH]: PATH is a bash script
with the arguments DEVELOPMENT_PREFIX USER_PREFIX (default: tools/dev-mute.sh of this tree).
Status 0: each case passed. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository

SETTINGS = Path("pfx/drive_c/users/steamuser/AppData/Roaming/AGS/New World/savedata")
LOUD = '<field field="m_masterVolume" value="37"/>\n'
MUTED = '<field field="m_masterVolume" value="0"/>\n'
CASES = ("separate prefix with spaces", "exact user prefix", "sound override", "prefix link", "dot alias",
         "savedata link", "settings file link", "user path through a link")


def run_case(script: Path, root: Path, case: str) -> list[tuple[bool, str]]:
    base = root / case
    user = base / "user prefix"
    development = base / "development prefix"
    for prefix in (user, development):
        (prefix / SETTINGS).mkdir(parents=True)
        (prefix / SETTINGS / "usersettings_test.javsave").write_text(LOUD)
    user_file = user / SETTINGS / "usersettings_test.javsave"
    development_file = development / SETTINGS / "usersettings_test.javsave"
    environment = dict(os.environ, SOULWARDEN_DEV_SOUND="0")
    development_argument, user_argument = str(development), str(user)
    if case == "exact user prefix":
        development_argument = str(user)
    elif case == "sound override":
        environment["SOULWARDEN_DEV_SOUND"] = "1"
    elif case == "prefix link":
        link = base / "development link"
        link.symlink_to(user, target_is_directory=True)
        development_argument = str(link)
    elif case == "dot alias":
        development_argument = str(user) + "/."
    elif case == "savedata link":
        development_file.unlink()
        (development / SETTINGS).rmdir()
        (development / SETTINGS).symlink_to(user / SETTINGS, target_is_directory=True)
    elif case == "settings file link":
        development_file.unlink()
        development_file.symlink_to(user_file)
    elif case == "user path through a link":
        link = base / "user link"
        link.symlink_to(base, target_is_directory=True)
        user_argument = str(link / "user prefix")
    result = subprocess.run(["bash", str(script), development_argument, user_argument], env=environment,
                            capture_output=True, text=True, check=False)
    checks = [(result.returncode == 0, f"{case}: status 0 (status {result.returncode})"),
              (user_file.read_text() == LOUD, f"{case}: the settings file of the user did not change")]
    if case in ("separate prefix with spaces", "user path through a link"):
        checks.append((development_file.read_text() == MUTED, f"{case}: the development settings file has the volume 0"))
    elif case == "sound override":
        checks.append((development_file.read_text() == LOUD, f"{case}: the development settings file did not change"))
    else:
        checks.append(("dev-mute:" in result.stderr, f"{case}: a message says that no file changed"))
    if case == "settings file link":
        checks.append((development_file.is_symlink(), f"{case}: the link stays a link"))
    return checks


def main() -> int:
    arguments = sys.argv[1:]
    if arguments[:1] in (["-h"], ["--help"]):
        print(__doc__.strip())
        return 0
    script = Path(__file__).resolve().parent / "dev-mute.sh"
    if arguments[:1] == ["--script"] and len(arguments) == 2:
        script = Path(arguments[1])
    failures = 0
    with tempfile.TemporaryDirectory(prefix="soulwarden-mute-check-") as folder:
        for case in CASES:
            for passed, text in run_case(script, Path(folder), case):
                print(("ok    " if passed else "FAIL  ") + text)
                failures += 0 if passed else 1
    print(f"dev-mute check: {failures} failed check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
