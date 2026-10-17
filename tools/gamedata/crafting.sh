#!/usr/bin/env bash
# Make the crafting data of the backend from the game copy of the user (stage 3.7a).
# The backend reads GAMEDATA_DIR/crafting-data.json at its start: the recipes with their stations, ingredients, outputs, and options, the
# rows of the crafting station database, the ingredient categories, the events, and the resource items (docs/systems/crafting.md).
# Without the file each craft request is refused with no change. The file is game data: it stays outside the repository.
# Usage: tools/gamedata/crafting.sh [OUTPUT.json]
#        tools/gamedata/crafting.sh --recipe ID...     print recipes (with the causes of a recipe that the backend does not craft)
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a file that it needs has no source and when a part of the source is not complete or not consistent (see
# crafting.py; tools/gamedata/check_crafting_tool.py is the check of these rules).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "crafting: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/crafting.py" "$@"
