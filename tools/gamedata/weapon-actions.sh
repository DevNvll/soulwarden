#!/usr/bin/env bash
# Make the basic attacks of the melee weapons of the backend from the game copy of the user (stage 3.3b-M1).
# The backend reads GAMEDATA_DIR/weapon-actions.json at its start. The file has the attack graph of each melee weapon tag set:
# the fragments, their transitions, strikes (damage keys, windows, damage rows with their node conditions, shapes), payments, dodge
# windows, and lengths (protocol notes 4.32). The damage rows are in the combat tables (tools/gamedata/combat.sh).
# Without the file the backend starts, and each melee hit uses the light or the heavy row of its weapon (the rule of stage 3.2a).
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/weapon-actions.sh [OUTPUT.json]
#        tools/gamedata/weapon-actions.sh --find TEXT...   print the graphs of the weapons whose tags have a text
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message when a file that it
# needs has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "weapon-actions: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/weapon_actions.py" "$@"
