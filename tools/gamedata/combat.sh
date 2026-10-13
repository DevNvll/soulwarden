#!/usr/bin/env bash
# Make the combat tables of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/combat-tables.json at its start. The file has what the damage
# of a melee attack of a player needs: the weapon of each item (base damage, attribute factors,
# the damage rows of its light and of its heavy attack), the damage rows, the damage types, the
# damage that each attribute value and each level add, the rule of the gear score, and the
# defense values of each creature (protocol notes 4.15).
# Without the file the backend starts, and an attack of a player on a creature has no effect.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/combat.sh [OUTPUT.json]
#        tools/gamedata/combat.sh --find TEXT...   print the weapons and the creatures with a text
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
    echo "combat: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/combat_tables.py" "$@"
