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
  swap at the write (sed shim)  its own folder; a command "sed" of the  its file muted, the user file kept
                                test first moves the development
                                savedata folder and puts a link to the
                                savedata of the user at its old path
                                (code review of stage 1.1b, finding F1:
                                the probe RDM1 of the reviewer)
  swap after the checks         the same move and link after the        the moved development file muted, the
                                checks of the files, before the write   user file kept
                                (the hook "write" of tools/dev_mute.py)
  swap after the folder open    the same move and link after the open   the moved development file muted, the
                                of the settings folder (hook "folder")  user file kept
  hard link                     its own folder; its settings file is a  the user file kept, a message
                                hard link of the settings file of the
                                user
  hard link, user path link     the same hard link; the user prefix     the user file kept, a message
                                path has a link in it
  prefix swap before the open   its own folder; before the open of the  the original settings file of the user
  (RCM1)                        development prefix, the development     kept, a message
                                prefix moves away, the prefix of the
                                user moves to the development path,
                                and the user path (a link) follows it
                                (code review of stage 3.3b-S5, finding
                                F1: the probe RCM1 of the reviewer; the
                                development path has spaces, a dollar
                                sign, and backticks)

The hook cases import tools/dev_mute.py from the folder of the script: a script with no helper fails them (the
proof on the tree of stage 1.1b). The case RCM1 makes its swap at the hook "prefix" of the helper, or (a helper with
no such hook: the tree of stage 3.3b-S5) at the call of its function open_settings_folder, as the probe of the
reviewer does.

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
         "savedata link", "settings file link", "user path through a link", "swap at the write (sed shim)",
         "swap after the checks", "swap after the folder open", "hard link", "hard link, user path link",
         "prefix swap before the open (RCM1)")
HOOK_CASES = {"swap after the checks": "write", "swap after the folder open": "folder",
              "prefix swap before the open (RCM1)": "prefix"}
PREFIX_SWAP = "prefix swap before the open (RCM1)"


def swap(development: Path, user: Path) -> None:
    """The race of finding F1: the development savedata folder moves, and a link to the savedata of the user takes its place."""
    folder = development / SETTINGS
    folder.rename(folder.with_name("savedata.before"))
    folder.symlink_to(user / SETTINGS, target_is_directory=True)


def swap_prefix(development: Path, user: Path, alias: Path) -> None:
    """The race of finding F1 of the review of stage 3.3b-S5 (RCM1): the development prefix moves away, the prefix of the
    user takes its path, and the user path (a link) follows the prefix of the user to its new place."""
    development.rename(development.with_name("old development prefix"))
    user.rename(development)
    alias.unlink()
    alias.symlink_to(development, target_is_directory=True)


def run_hook_case(script: Path, development: Path, user: Path, moment: str, alias: Path | None = None) -> subprocess.CompletedProcess:
    """Runs the helper of the script in this process, with the swap at one moment of its work."""
    helper = script.resolve().parent / "dev_mute.py"
    if not helper.is_file():
        return subprocess.CompletedProcess([], 1, "", "no helper tools/dev_mute.py beside the script")
    sys.path.insert(0, str(helper.parent))
    try:
        import dev_mute  # noqa: PLC0415 (the helper of the script under test)
    finally:
        sys.path.pop(0)
    done = [False]

    def make_swap() -> None:
        if done[0]:
            return
        done[0] = True
        if alias is not None:
            swap_prefix(development, user, alias)
        else:
            swap(development, user)

    def hook(at: str) -> None:
        if at == moment:
            make_swap()

    original_open = getattr(dev_mute, "open_settings_folder", None)
    if alias is not None and original_open is not None:
        # A helper with no hook "prefix" (the tree of stage 3.3b-S5): the swap at its folder open, as in the probe RCM1.
        def wrapped(*arguments):
            make_swap()
            return original_open(*arguments)
        dev_mute.open_settings_folder = wrapped
    try:
        dev_mute.mute(str(development), str(alias if alias is not None else user), hook)
    except dev_mute.Refused as refused:
        return subprocess.CompletedProcess([], 0, "", f"dev-mute: {refused}")
    finally:
        if original_open is not None:
            dev_mute.open_settings_folder = original_open
    return subprocess.CompletedProcess([], 0, "", "")


def run_case(script: Path, root: Path, case: str) -> list[tuple[bool, str]]:
    base = root / case
    user = base / "user prefix"
    development = base / ("development prefix $dollar `backtick`" if case == PREFIX_SWAP else "development prefix")
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
    elif case == "swap at the write (sed shim)":
        shim = base / "shim"
        shim.mkdir()
        (shim / "sed").write_text("#!/usr/bin/env bash\nset -eu\n"
                                  'mv -- "$CHECK_DEV_SETTINGS" "$CHECK_DEV_SETTINGS.before"\n'
                                  'ln -s -- "$CHECK_USER_SETTINGS" "$CHECK_DEV_SETTINGS"\n'
                                  'exec /usr/bin/sed "$@"\n')
        (shim / "sed").chmod(0o755)
        environment.update(PATH=f"{shim}:{environment['PATH']}", CHECK_DEV_SETTINGS=str(development / SETTINGS),
                           CHECK_USER_SETTINGS=str(user / SETTINGS))
    elif case == "hard link":
        development_file.unlink()
        os.link(user_file, development_file)
    elif case == "hard link, user path link":
        development_file.unlink()
        os.link(user_file, development_file)
        link = base / "user link"
        link.symlink_to(base, target_is_directory=True)
        user_argument = str(link / "user prefix")
    protected = (user_file.stat().st_dev, user_file.stat().st_ino)
    alias = None
    if case == PREFIX_SWAP:
        alias = base / "user alias"
        alias.symlink_to(user, target_is_directory=True)
    if case in HOOK_CASES:
        result = run_hook_case(script, development, user, HOOK_CASES[case], alias)
    else:
        result = subprocess.run(["bash", str(script), development_argument, user_argument], env=environment,
                                capture_output=True, text=True, check=False)
    if case == PREFIX_SWAP:
        # The original settings file of the user is now below the development path (the same device and inode).
        user_file = development / SETTINGS / "usersettings_test.javsave"
        same = user_file.is_file() and (user_file.stat().st_dev, user_file.stat().st_ino) == protected
        return [(result.returncode == 0, f"{case}: status 0 (status {result.returncode})"),
                (same, f"{case}: control: the swap moved the original settings file of the user to the development path"),
                (same and user_file.read_text() == LOUD, f"{case}: the original settings file of the user did not change"),
                ("dev-mute:" in result.stderr, f"{case}: a message says that no file changed ({result.stderr.strip()})")]
    checks = [(result.returncode == 0, f"{case}: status 0 (status {result.returncode})"),
              (user_file.read_text() == LOUD, f"{case}: the settings file of the user did not change")]
    if case in ("separate prefix with spaces", "user path through a link", "swap at the write (sed shim)"):
        checks.append((development_file.read_text() == MUTED, f"{case}: the development settings file has the volume 0"))
    elif case in HOOK_CASES:
        moved = development / SETTINGS.with_name("savedata.before") / development_file.name
        checks.append((moved.is_file() and moved.read_text() == MUTED,
                       f"{case}: the development settings file (now in the moved folder) has the volume 0 ({result.stderr.strip()})"))
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
