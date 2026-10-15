#!/usr/bin/env bash
# Make the ability data of the backend from the game copy of the user (stage 3.3a).
# The backend reads GAMEDATA_DIR/ability-data.json at its start. The file has what the abilities
# of a player need: the cooldown of each ability, the values of each status effect with its
# number on the wire, and the shape of each spell (protocol notes 4.22). The damage rows of the
# abilities are in the combat tables (tools/gamedata/combat.sh).
# Without the file the backend starts, and it refuses each ability press.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/abilities.sh [OUTPUT.json]
#        tools/gamedata/abilities.sh --find TEXT...   print the rows with a text
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a
# native Oodle library (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle
# library for Windows (SOULWARDEN_OODLE_DLL), or the files that tools/research/pakx.py extracted
# (SOULWARDEN_EXTRACT_DIR, default ~/Games/soulwarden/work/assets/extract). It stops with status 1 and one
# message when a file that it needs has no source, and then writes no file.
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB,
# SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "abilities: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/abilities.py" "$@"
