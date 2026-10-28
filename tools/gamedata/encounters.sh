#!/usr/bin/env bash
# Make the encounter recipes of the quests of the backend from the game copy of the user (stage QG5c2; docs/systems/encounters.md).
# The backend reads GAMEDATA_DIR/encounter-data.json at its start: the quest encounters of the placements of the main world, their stages,
# their spawns, their timers, their participants, and their parent spawners. Without the file the world has no encounter, and the rest of
# the backend works. The file is game data: it stays outside the repository. Its members also need a run of creatures.sh (the member
# kinds), interactions.sh (the start object templates), and gathering.sh (the start object rows) on the same game copy.
# Usage: tools/gamedata/encounters.sh [OUTPUT.json]
# A test call must give an OUTPUT path outside the common game data folder.
# The pak entries of the sources have Oodle data, which Python cannot read. The tool uses a native Oodle library
# (SOULWARDEN_OODLE_LIB), or the Oodle helper of tools/research with an Oodle library for Windows (SOULWARDEN_OODLE_DLL), or the
# files that tools/research/pakx.py extracted (SOULWARDEN_EXTRACT_DIR). It stops with status 1 and one message, and writes no
# file, when a source that it needs is not there or not complete (see encounters.py).
# Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game), SOULWARDEN_GAMEDATA_DIR (default
# ~/.local/share/soulwarden/gamedata), SOULWARDEN_PYTHON (default python3), SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
# SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR.
set -euo pipefail
repository_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
python_binary="${SOULWARDEN_PYTHON:-python3}"

if ! command -v "${python_binary}" >/dev/null 2>&1; then
    echo "encounters: Python 3 was not found (${python_binary}). Set SOULWARDEN_PYTHON. No file was written." >&2
    exit 1
fi
# -I: no module from the current directory or the user environment. -B: no bytecode cache.
exec "${python_binary}" -I -B "${repository_directory}/tools/gamedata/encounters.py" "$@"
