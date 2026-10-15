#!/usr/bin/env bash
# Make the loot tables and the kill rewards of the backend from the game copy of the user.
# The backend reads GAMEDATA_DIR/loot-tables.json at its start. The file has what the rewards of a
# kill need: the experience and the coin of each creature level, the reward factors of each
# creature type, the level differences, the credit data of the game, the loot data of each vitals
# row, the loot tables and the loot buckets that a creature can reach, and the gear score ranges
# of each level (protocol notes 4.21).
# Without the file the backend starts, and a kill gives nothing.
# The file is game data: it stays outside the repository.
# Usage: tools/gamedata/loot.sh [OUTPUT.json]
#        tools/gamedata/loot.sh --find TEXT...   print the loot data of the vitals rows and of the tables with a text
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
    echo "loot: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/loot_tables.py" "$@"
