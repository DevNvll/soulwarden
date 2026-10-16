#!/usr/bin/env python3
"""Check of the silence of a development run (code review of stage 3.3b-S6, finding S6-R1).

A development run makes no sound through a startup setting of the game, not through a change of a file: tools/dev-run.sh gives
the game the arguments `-sys_audio_disable 1` (the variable of the client: "1: use NullAudioSystem (disable all audio
functionality)"; the game log then has the line "<Audio>: Skipping Audio System Initialization - it was disabled by startup
settings!"; live check of 2026-10-15). The arguments are in tools/dev-env.sh (`dev_sound_arguments`); SOULWARDEN_DEV_SOUND=1
gives none. No tool of the tree edits a settings file of a Proton prefix: the file mute of the stages 1.1b to 3.3b-S6
(tools/dev_mute.py, tools/dev-mute.sh) is gone, because each method that edits a file in a prefix can be redirected by a swap
of folders to a file of the user.

The cases (each prints "ok NAME: ..." or "FAIL NAME: ..."):

  game argument        tools/dev-env.sh gives the arguments -sys_audio_disable 1
  sound override       with SOULWARDEN_DEV_SOUND=1 it gives none
  launcher line        tools/dev-run.sh gives the arguments to the launcher, before the arguments of its caller
  no settings write    no file mute in the tree: no tools/dev_mute.py, no tools/dev-mute.sh, and no tool that names the
                       volume field of a settings file (m_masterVolume) or a settings file (.javsave)

The check reads files and runs bash with tools/dev-env.sh only: it starts no game and opens no Proton prefix.
Usage: check_dev_sound.py [TREE] (default: the tree of this script). Status 0: each case passed.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository


def sound_arguments(tree: Path, override: str | None) -> list[str] | None:
    environment = {key: value for key, value in os.environ.items() if key != "SOULWARDEN_DEV_SOUND"}
    if override is not None:
        environment["SOULWARDEN_DEV_SOUND"] = override
    script = 'source "$1/tools/dev-env.sh" && declare -p dev_sound_arguments >/dev/null 2>&1 && printf "%s\\n" "${dev_sound_arguments[@]}"'
    result = subprocess.run(["bash", "-c", script, "check", str(tree)], capture_output=True, text=True, env=environment, check=False)
    if result.returncode != 0:
        return None
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    tree = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
    results: list[tuple[str, bool, str]] = []

    plain = sound_arguments(tree, None)
    results.append(("game argument", plain == ["-sys_audio_disable", "1"], f"arguments {plain}"))
    loud = sound_arguments(tree, "1")
    results.append(("sound override", loud == [], f"arguments with SOULWARDEN_DEV_SOUND=1: {loud}"))

    dev_run = (tree / "tools/dev-run.sh").read_text(errors="replace") if (tree / "tools/dev-run.sh").exists() else ""
    launcher = re.search(r'run-soulwarden\.sh"\s+"\$\{dev_sound_arguments\[@\]\}"\s+"\$@"', dev_run)
    results.append(("launcher line", launcher is not None, "tools/dev-run.sh gives \"${dev_sound_arguments[@]}\" \"$@\" to the launcher"))

    found = [name for name in ("tools/dev_mute.py", "tools/dev-mute.sh", "tools/check_dev_mute.py") if (tree / name).exists()]
    for path in sorted((tree / "tools").rglob("*")):
        if not path.is_file() or path.name == Path(__file__).name or "__pycache__" in path.parts:
            continue
        try:
            text = path.read_text(errors="ignore")
        except OSError:
            continue
        if "m_masterVolume" in text or ".javsave" in text or "dev-mute.sh" in text:
            found.append(str(path.relative_to(tree)))
    results.append(("no settings write", not found, f"files of a file mute: {sorted(set(found))}"))

    failures = 0
    for name, passed, detail in results:
        print(f"{'ok' if passed else 'FAIL'} {name}: {detail}")
        failures += 0 if passed else 1
    print(f"dev-sound check: {failures} failed check(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
